#!/usr/bin/env python3
"""Repeat-aware generic volume measurement over complete Emerald layouts."""

from __future__ import annotations

from collections import Counter, deque
from typing import Any


MAX_ART_BANDS = 6
_EXCLUDED_BEHAVIOR_FAMILIES = frozenset((
    "water", "ledge", "stairs", "bridge", "hole", "movement", "warp",
))


def _shape_value(shape: Any, attribute: str, key: str) -> Any:
    return getattr(shape, attribute) if hasattr(shape, attribute) else shape[key]


def _source_signature(pixel: Any, provenance: str) -> tuple:
    source = pixel.source
    if source is None or source.color_index == 0:
        return (provenance, None)
    return (provenance, source.layer, source.subtile, source.tile, source.palette,
            source.hflip, source.vflip, source.u, source.v, source.color_index)


def source_band(summary: dict, provenance: str, axis: str, half: int) -> dict | None:
    """Return one authoritative 8-pixel art band and its render source."""
    layers = summary.get("layers")
    if not summary.get("resolved") or not layers or half not in (0, 1):
        return None
    pixels = layers[2]
    if axis == "z":
        indices = tuple(y * 16 + x for y in range(half * 8, half * 8 + 8)
                        for x in range(16))
    elif axis == "x":
        indices = tuple(y * 16 + x for y in range(16)
                        for x in range(half * 8, half * 8 + 8))
    else:
        raise ValueError("volume axis must be x or z")
    visible = [pixels[index] for index in indices if pixels[index].visible]
    if not visible:
        return None
    layers_used = {pixel.source.layer for pixel in visible}
    render_layer = "foreground" if layers_used == {1} else "full"
    return {
        "signature": tuple(_source_signature(pixels[index], provenance) for index in indices),
        "pixelSignature": tuple(pixels[index].rgba for index in indices),
        "layer": render_layer,
        "sourceHalf": half,
    }


def measure_signatures(signatures: list[tuple]) -> tuple[int, bool, bool]:
    """Measure a front-anchored sequence; return period, repeat, contradiction."""
    extent = len(signatures)
    if extent == 0:
        return 0, False, True
    period = min(extent, MAX_ART_BANDS)
    repeated = False
    if extent > 1:
        for distance in range(1, extent):
            if signatures[distance] != signatures[0]:
                continue
            candidate = max(distance, 2)
            if all(signature == signatures[index % candidate]
                   for index, signature in enumerate(signatures)):
                period = min(candidate, MAX_ART_BANDS)
                repeated = True
                break
        if not repeated and extent > 2 and signatures[1] == signatures[2]:
            period = 2
            repeated = True
    return period, repeated, False


def _vegetation_like(summary: dict) -> bool:
    layers = summary.get("layers")
    if not layers:
        return False
    foreground = layers[1]
    visible = [pixel for pixel in foreground if pixel.visible]
    corners = (foreground[0], foreground[15], foreground[240], foreground[255])
    if 40 <= len(visible) <= 230 and sum(not pixel.visible for pixel in corners) >= 2:
        crown = visible
    elif len(visible) == 256:
        full = layers[2]
        corner_colors = Counter(pixel.rgba for pixel in
                                (full[0], full[15], full[240], full[255]))
        background, count = min(corner_colors.items(),
                                key=lambda item: (-item[1], item[0]))
        if count < 2:
            return False
        pending = deque(index for index in range(256)
                        if (index % 16 in (0, 15) or index // 16 in (0, 15))
                        and full[index].rgba == background)
        outside = set(pending)
        while pending:
            index = pending.popleft()
            x, y = index % 16, index // 16
            for neighbor in ((index - 1 if x else -1),
                             (index + 1 if x < 15 else -1),
                             (index - 16 if y else -1),
                             (index + 16 if y < 15 else -1)):
                if neighbor >= 0 and neighbor not in outside \
                        and full[neighbor].rgba == background:
                    outside.add(neighbor)
                    pending.append(neighbor)
        crown = [pixel for index, pixel in enumerate(full) if index not in outside]
        if not 40 <= len(crown) <= 230:
            return False
    else:
        return False
    red = sum(pixel.rgba[0] for pixel in crown)
    green = sum(pixel.rgba[1] for pixel in crown)
    blue = sum(pixel.rgba[2] for pixel in crown)
    return green * 10 > red * 11 and green * 10 > blue * 11


def _runs(offsets: set[int], width: int, axis: str) -> list[list[int]]:
    lines: dict[int, list[int]] = {}
    for offset in offsets:
        x, y = offset % width, offset // width
        fixed, varying = (x, y) if axis == "z" else (y, x)
        lines.setdefault(fixed, []).append(varying)
    output = []
    for fixed, values in sorted(lines.items()):
        values.sort()
        start = 0
        while start < len(values):
            end = start + 1
            while end < len(values) and values[end] == values[end - 1] + 1:
                end += 1
            segment = values[start:end]
            output.append([
                (value * width + fixed) if axis == "z" else (fixed * width + value)
                for value in segment
            ])
            start = end
    return output


def _measure_axis(component: set[int], width: int, cells: tuple[dict, ...],
                  summaries: tuple[dict, ...], axis: str, outdoor: bool) -> dict | None:
    measured = []
    for offsets in _runs(component, width, axis):
        bands = []
        # South/east is the front. Within a cell, bottom/right is nearest first.
        for offset in reversed(offsets):
            provenance = str(cells[offset].get("composition", {}).get(
                "provenance", {}).get("pair", cells[offset].get("tileset", "")))
            for half in (1, 0):
                band = source_band(summaries[offset], provenance, axis, half)
                if band is None:
                    bands = []
                    break
                bands.append({**band, "sourceCellOffset": offset,
                              "expectedMetatile": cells[offset]["metatile"]})
            if not bands:
                break
        if not bands:
            continue
        period, repeated, contradiction = measure_signatures(
            [band["signature"] for band in bands])
        if contradiction or period == 0:
            continue
        measured.append({"offsets": offsets, "extentBands": len(bands),
                         "periodBands": period, "fromRepeat": repeated,
                         "bands": bands})
    if not measured:
        return None
    repeat_count = sum(run["fromRepeat"] for run in measured)
    repeated_votes = Counter(run["periodBands"] for run in measured if run["fromRepeat"])
    votes = repeated_votes or Counter(run["periodBands"] for run in measured)
    mode_bands, mode_count = min(votes.items(), key=lambda item: (-item[1], item[0]))
    # A visual repeat is evidence by itself. Non-repeating drawings need a strict
    # regional consensus from at least two independent runs.
    if repeat_count == 0 and (len(measured) < 2 or mode_count * 2 <= len(measured)):
        return None
    conflicts = sum(run["periodBands"] != mode_bands and not run["fromRepeat"]
                    for run in measured)
    if conflicts * 2 >= len(measured):
        return None
    mode_repeat = sum(run["fromRepeat"] and run["periodBands"] == mode_bands
                      for run in measured) * 2 > mode_count
    silhouette = sum(_vegetation_like(summaries[offset]) for offset in component) * 2 \
               >= len(component)
    for run in measured:
        height_bands = run["periodBands"]
        adopted = run["fromRepeat"] and mode_bands > height_bands
        if silhouette or mode_repeat:
            height_bands = mode_bands
            adopted = run["periodBands"] != mode_bands
        elif adopted:
            height_bands = mode_bands
        roof_candidate_bands = 0
        if outdoor and (not run["fromRepeat"] or adopted) and height_bands >= 2:
            roof_candidate_bands = min(2, height_bands - 1)
            if len(run["bands"]) < 2 \
                    or run["bands"][-1]["signature"] == run["bands"][-2]["signature"]:
                roof_candidate_bands = 0
        source = run["bands"][:height_bands]
        while len(source) < height_bands:
            source.append(source[len(source) % len(run["bands"])] )
        run.update({"heightBands": height_bands, "roofBands": 0,
                    "roofCandidateBands": roof_candidate_bands,
                    "adoptedConsensus": adopted, "sourceBands": source})
    score = (repeat_count, mode_count, len(measured), -conflicts)
    confidence = min(0.99, 0.55 + 0.08 * repeat_count
                     + 0.05 * mode_count - 0.1 * conflicts)
    return {"axis": axis, "runs": measured, "modeBands": mode_bands,
            "conflicts": conflicts, "confidence": confidence, "score": score,
            "silhouette": silhouette, "modeRepeat": mode_repeat}


def _eligible(offset: int, analysis: dict, cells: tuple[dict, ...],
              shapes: tuple[Any, ...], summaries: tuple[dict, ...],
              excluded_owners: set[int]) -> bool:
    structural = analysis["cells"][offset]
    shape = shapes[offset]
    cell = cells[offset]
    if structural["owner"] is not None or structural["voidKind"] != "none":
        return False
    if bool(_shape_value(shape, "authored", "authored")):
        return False
    if _shape_value(shape, "art_mode", "artMode") != "upright":
        return False
    if structural.get("region") in excluded_owners:
        return False
    blocked = cell.get("movementEvidence", {}).get(
        "candidateBlocked", int(cell.get("collision", 0)) != 0)
    if not blocked:
        return False
    families = set(cell.get("behaviorFamilies", ()))
    behavior = str(cell.get("behavior") or "")
    if any(token in behavior for token in ("WATER", "OCEAN", "CURRENT")):
        families.add("water")
    if behavior.startswith("MB_JUMP_"):
        families.add("ledge")
    if any(token in behavior for token in ("STAIRS", "LADDER", "ESCALATOR")):
        families.add("stairs")
    if any(token in behavior for token in ("BRIDGE", "WARP", "DOOR", "HOLE")):
        families.add("bridge" if "BRIDGE" in behavior else "warp")
    if families & _EXCLUDED_BEHAVIOR_FAMILIES:
        return False
    return bool(summaries[offset].get("resolved") and summaries[offset].get("visible"))


def detect_layout(layout: dict, cells: tuple[dict, ...], shapes: tuple[Any, ...],
                  summaries: tuple[dict, ...], analysis: dict,
                  excluded_structure_ids: set[int] | frozenset[int] = frozenset(),
                  *, outdoor: bool, allow_volumes: bool = True) -> dict:
    """Detect and claim conservative generic volumes in one complete layout."""
    width, height = int(layout["width"]), int(layout["height"])
    if not allow_volumes:
        return {"accepted": [], "rejected": []}
    eligible = {offset for offset in range(width * height)
                if _eligible(offset, analysis, cells, shapes, summaries,
                             set(excluded_structure_ids))}
    components = []
    while eligible:
        start = min(eligible)
        eligible.remove(start)
        queue = deque((start,))
        component = {start}
        while queue:
            offset = queue.popleft()
            x, y = offset % width, offset // width
            for neighbor in ((offset - 1 if x else -1),
                             (offset + 1 if x + 1 < width else -1),
                             (offset - width if y else -1),
                             (offset + width if y + 1 < height else -1)):
                if neighbor in eligible:
                    eligible.remove(neighbor)
                    component.add(neighbor)
                    queue.append(neighbor)
        components.append(component)

    accepted = []
    rejected = []
    for component in components:
        choices = [choice for axis in ("z", "x")
                   if (choice := _measure_axis(component, width, cells, summaries,
                                               axis, outdoor)) is not None]
        if not choices:
            rejected.append({"cells": sorted(component), "reason": "no-coherent-runs"})
            continue
        # Red reads north-south first. X is only the fallback for Emerald art
        # without a coherent north-south run, not a competing majority vote.
        choice = next((candidate for candidate in choices if candidate["axis"] == "z"),
                      choices[0])
        measured_cells = {}
        for run in choice["runs"]:
            for local, offset in enumerate(run["offsets"]):
                measured_cells[offset] = {
                    "axis": choice["axis"], "extentBands": run["extentBands"],
                    "periodBands": run["periodBands"],
                    "heightBands": run["heightBands"], "roofBands": run["roofBands"],
                    "roofCandidateBands": run["roofCandidateBands"],
                    "runLocal": local, "runLength": len(run["offsets"]),
                    "fromRepeat": run["fromRepeat"],
                    "adoptedConsensus": run["adoptedConsensus"],
                    "silhouette": choice["silhouette"],
                    "modeRepeat": choice["modeRepeat"],
                    "confidence": choice["confidence"],
                    "sourceBands": [{key: band[key] for key in (
                        "sourceCellOffset", "expectedMetatile", "layer", "sourceHalf")}
                        for band in run["sourceBands"]],
                }
        accepted.append({"cells": sorted(measured_cells), "measurements": measured_cells,
                         "axis": choice["axis"], "modeBands": choice["modeBands"],
                         "confidence": choice["confidence"],
                         "conflicts": choice["conflicts"]})
    return {"accepted": accepted, "rejected": rejected}


def apply_to_analysis(analysis: dict, detection: dict, width: int,
                      summaries: tuple[dict, ...]) -> dict[int, dict]:
    """Append accepted generic owners and return measurements by cell offset."""
    measurements = {}
    for volume_index, volume in enumerate(detection["accepted"], 1):
        offsets = volume["cells"]
        xs = [offset % width for offset in offsets]
        ys = [offset // width for offset in offsets]
        candidate_id = len(analysis["candidates"]) + 1
        visible = sum(int(summaries[offset].get("visible", 0)) for offset in offsets)
        transparent = sum(int(summaries[offset].get("transparent", 0)) for offset in offsets)
        black = sum(int(summaries[offset].get("black", 0)) for offset in offsets)
        components = sum(int(summaries[offset].get("components", 0)) for offset in offsets)
        analysis["candidates"].append({
            "id": candidate_id, "owner": f"measured-volume:{offsets[0]}",
            "kind": "generic-volume", "priority": 1, "cells": offsets,
            "pixels": {"visible": visible, "transparent": transparent,
                       "black": black, "components": components},
            "bbox": {"x": min(xs), "y": min(ys),
                     "width": max(xs) - min(xs) + 1, "height": max(ys) - min(ys) + 1},
            "class": "wall-volume", "pool": "structure",
            "evidence": ["pixels:source-band-repeat", "collision:candidate-blocked",
                         f"volume:axis-{volume['axis']}"],
            "source": "heuristic:measured-volume", "claimOnly": False,
            "propGround": None, "objectOffsetQ16": None, "objectDepthOffsetQ32": 0,
            "objectThicknessQ32": None, "objectPixelRows": None,
            "objectGroundMode": None, "spriteDepthBiasMillionths": 0,
        })
        for offset in offsets:
            analysis["cells"][offset]["owner"] = candidate_id
            analysis["cells"][offset]["ownerKind"] = "generic-volume"
            analysis["cells"][offset]["source"] = "heuristic:measured-volume"
            measurements[offset] = {**volume["measurements"][offset],
                                    "localStructureId": candidate_id,
                                    "conflicts": volume["conflicts"]}
    return measurements
