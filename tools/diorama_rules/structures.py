#!/usr/bin/env python3
"""Deterministic whole-layout structure claims and connectivity analysis."""

from __future__ import annotations

from collections import deque
from collections.abc import Callable
from typing import Any

from profiles import TileShape


# These ranks are part of the generated/runtime ordering contract. Pattern rule
# priority is used only to order templates within the highest-ranked stage.
STAGE_PRIORITY_TEMPLATE = 4
STAGE_PRIORITY_AUTHORED_SPECIAL = 3
STAGE_PRIORITY_PROP_CANDIDATE = 2
STAGE_PRIORITY_GENERIC_VOLUME = 1
STAGE_PRIORITY_REGION = 0

_PROP_POOLS = frozenset(("vegetation", "furniture", "prop"))
_SPECIAL_CLASSES = frozenset((
    "awning", "bridge", "building", "cliff", "deck", "ledge", "mound",
    "rail", "roof", "stairs", "support", "top-slab", "wall", "wall-volume",
))


def _shape_value(shape: TileShape, attribute: str, key: str) -> Any:
    return getattr(shape, attribute, shape[key])


def _summary_value(summary: dict, name: str) -> int:
    aliases = {
        "visible": ("visible", "visiblePixels"),
        "transparent": ("transparent", "transparentPixels"),
        "black": ("black", "blackPixels"),
        "components": ("components", "componentCount", "component_count"),
    }
    value: Any = 0
    for key in aliases[name]:
        if key in summary:
            value = summary[key]
            break
    if name == "components" and isinstance(value, (list, tuple, set, frozenset)):
        return len(value)
    return int(value)


def _pixels(offsets: list[int], summaries: tuple[dict, ...]) -> dict[str, int]:
    return {
        name: sum(_summary_value(summaries[offset], name) for offset in offsets)
        for name in ("visible", "transparent", "black", "components")
    }


def _bbox(offsets: list[int], width: int) -> dict[str, int]:
    xs = [offset % width for offset in offsets]
    ys = [offset // width for offset in offsets]
    return {"x": min(xs), "y": min(ys), "width": max(xs) - min(xs) + 1,
            "height": max(ys) - min(ys) + 1}


def _pattern_matches(pattern: dict, layout: dict, cells: tuple[dict, ...],
                     origin_x: int, origin_y: int) -> bool:
    pair = pattern.get("tilesets")
    if pair and (layout.get("primary_tileset") != pair.get("primary")
                 or layout.get("secondary_tileset") != pair.get("secondary")):
        return False
    width = layout["width"]
    return all(cells[(origin_y + row) * width + origin_x + column].get("metatile") == value
               for row, values in enumerate(pattern["cells"])
               for column, value in enumerate(values))


def _action_class(action: dict) -> str:
    return str(action.get("class", action.get("shape", action.get("archetype", "building"))))


def analyze_layout(layout: dict, cells: tuple[dict, ...], shapes: tuple[TileShape, ...],
                   patterns: list[dict], pixel_summaries: tuple[dict, ...],
                   door_offsets: frozenset[int] = frozenset(),
                   event_candidates: dict[int, dict] | None = None) -> dict:
    """Build candidate ownership for one complete layout without changing shapes."""
    width, height = int(layout["width"]), int(layout["height"])
    count = width * height
    if len(cells) != count or len(shapes) != count or len(pixel_summaries) != count:
        raise ValueError("cells, shapes, and pixel summaries must cover the full layout")

    effective_classes: list[str] = []
    void_kinds: list[str] = []
    for shape, summary in zip(shapes, pixel_summaries):
        class_name = str(_shape_value(shape, "class_name", "class"))
        authored = bool(_shape_value(shape, "authored", "authored"))
        resolved = bool(summary.get("resolved", summary.get("available", bool(summary))))
        visible = _summary_value(summary, "visible")
        black = _summary_value(summary, "black")
        void_kind = "none"
        animated = bool(cells[len(effective_classes)].get("animationSources"))
        if not authored and not animated and resolved and visible == 0:
            class_name, void_kind = "void", "transparent"
        elif not authored and not animated and resolved and visible == black and black > 0:
            class_name, void_kind = "void", "black"
        effective_classes.append(class_name)
        void_kinds.append(void_kind)

    candidates: list[dict] = []
    owners: list[int | None] = [None] * count
    regions: list[int | None] = [None] * count
    cell_sources = [str(_shape_value(shape, "source", "source")) for shape in shapes]

    def add_candidate(owner: str, kind: str, priority: int, offsets: list[int],
                      class_name: str, pool: str, evidence: set[str], source: str,
                      claim_only: bool = False, *, region: bool = False) -> int:
        ordered = sorted(offsets)
        candidate_id = len(candidates) + 1
        candidates.append({
            "id": candidate_id, "owner": owner, "kind": kind, "priority": priority,
            "cells": ordered, "pixels": _pixels(ordered, pixel_summaries),
            "bbox": _bbox(ordered, width), "class": class_name, "pool": pool,
            "evidence": sorted(evidence), "source": source, "claimOnly": claim_only,
        })
        target = regions if region else owners
        for offset in ordered:
            target[offset] = candidate_id
            if not region:
                cell_sources[offset] = source
        return candidate_id

    ordered_patterns = sorted(patterns, key=lambda item: (-int(item["priority"]), str(item["id"])))
    for pattern in ordered_patterns:
        pattern_height = len(pattern["cells"])
        pattern_width = len(pattern["cells"][0]) if pattern_height else 0
        if pattern_width > width or pattern_height > height:
            continue
        mask = pattern["claimMask"]
        for origin_y in range(height - pattern_height + 1):
            for origin_x in range(width - pattern_width + 1):
                if not _pattern_matches(pattern, layout, cells, origin_x, origin_y):
                    continue
                claimed = [(origin_y + row) * width + origin_x + column
                           for row, mask_row in enumerate(mask)
                           for column, value in enumerate(mask_row) if value == "1"]
                if any(owners[offset] is not None for offset in claimed):
                    continue
                action = pattern.get("action", {})
                source = f"pattern:{pattern['id']}"
                class_name = _action_class(action)
                add_candidate(str(pattern["id"]), "template", STAGE_PRIORITY_TEMPLATE,
                              claimed, class_name, str(action.get("pool", "structure")),
                              {source}, source,
                              bool(action.get("claimOnly", False) or class_name == "claim-only"))

    def flood_available(start: int, predicate: Callable[[int], bool]) -> list[int]:
        queue, found = deque((start,)), []
        owners[start] = -1
        while queue:
            offset = queue.popleft()
            found.append(offset)
            x, y = offset % width, offset // width
            for neighbor in ((offset - 1 if x else -1),
                             (offset + 1 if x + 1 < width else -1),
                             (offset - width if y else -1),
                             (offset + width if y + 1 < height else -1)):
                if neighbor >= 0 and owners[neighbor] is None and predicate(neighbor):
                    owners[neighbor] = -1
                    queue.append(neighbor)
        for offset in found:
            owners[offset] = None
        return found

    def shape_group(offset: int, kind: str, priority: int,
                    predicate: Callable[[int], bool]) -> None:
        shape = shapes[offset]
        class_name = effective_classes[offset]
        pool = str(_shape_value(shape, "pool", "pool"))
        group = flood_available(offset, lambda other: predicate(other)
                                and effective_classes[other] == class_name
                                and str(_shape_value(shapes[other], "pool", "pool")) == pool)
        sources = {str(_shape_value(shapes[item], "source", "source")) for item in group}
        evidence = {value for item in group for value in _shape_value(
            shapes[item], "evidence", "evidence")}
        source = next(iter(sources)) if len(sources) == 1 else kind
        add_candidate(f"{kind}:{group[0]}", kind, priority, group, class_name, pool,
                      evidence, source, class_name == "claim-only")

    def is_special(offset: int) -> bool:
        shape = shapes[offset]
        class_name = effective_classes[offset]
        return (bool(_shape_value(shape, "authored", "authored"))
                and str(_shape_value(shape, "pool", "pool")) not in _PROP_POOLS
                and (str(_shape_value(shape, "art_mode", "artMode")) == "stair"
                     or class_name in _SPECIAL_CLASSES or class_name.startswith("stairs")))

    for offset in range(count):
        if owners[offset] is None and is_special(offset):
            shape_group(offset, "authored-special", STAGE_PRIORITY_AUTHORED_SPECIAL,
                        is_special)

    for offset, event in sorted((event_candidates or {}).items()):
        if not 0 <= offset < count or owners[offset] is not None:
            continue
        class_name = str(event.get("class", "signpost"))
        pool = str(event.get("pool", "prop"))
        source = str(event.get("source", "event:background"))
        evidence = {str(value) for value in event.get("evidence", (source,))}
        add_candidate(source, "prop-candidate", STAGE_PRIORITY_PROP_CANDIDATE,
                      [offset], class_name, pool, evidence, source)

    def is_prop(offset: int) -> bool:
        shape = shapes[offset]
        return (bool(_shape_value(shape, "authored", "authored"))
                and str(_shape_value(shape, "pool", "pool")) in _PROP_POOLS)

    for offset in range(count):
        if owners[offset] is None and is_prop(offset):
            shape_group(offset, "prop-candidate", STAGE_PRIORITY_PROP_CANDIDATE, is_prop)

    def is_generic(offset: int) -> bool:
        shape = shapes[offset]
        return (not bool(_shape_value(shape, "authored", "authored"))
                and str(_shape_value(shape, "art_mode", "artMode")) == "upright"
                and effective_classes[offset] != "void")

    for offset in range(count):
        if owners[offset] is None and is_generic(offset):
            shape_group(offset, "generic-volume", STAGE_PRIORITY_GENERIC_VOLUME, is_generic)

    for offset in range(count):
        if owners[offset] is not None or regions[offset] is not None:
            continue
        class_name = effective_classes[offset]
        pool = str(_shape_value(shapes[offset], "pool", "pool"))
        group = flood_available(offset, lambda other: effective_classes[other] == class_name
                                and str(_shape_value(shapes[other], "pool", "pool")) == pool)
        evidence = {value for item in group for value in _shape_value(
            shapes[item], "evidence", "evidence")}
        add_candidate(f"region:{group[0]}", "region", STAGE_PRIORITY_REGION, group,
                      class_name, pool, evidence, "region", class_name == "claim-only",
                      region=True)

    door_fold = [False] * count
    for offset in sorted(door_offsets):
        if not 0 <= offset < count or owners[offset] is not None or offset < width:
            continue
        north = shapes[offset - width]
        if (not bool(_shape_value(north, "authored", "authored"))
                and str(_shape_value(north, "art_mode", "artMode")) == "upright"
                and str(_shape_value(north, "pool", "pool"))
                == str(_shape_value(shapes[offset], "pool", "pool"))):
            door_fold[offset] = True

    return {
        "candidates": candidates,
        "cells": [{"owner": owners[offset], "region": regions[offset],
                   "ownerKind": (candidates[owners[offset] - 1]["kind"]
                                 if owners[offset] is not None else None),
                   "doorFold": door_fold[offset], "voidKind": void_kinds[offset],
                   "source": cell_sources[offset]}
                  for offset in range(count)],
    }


def overlay_rows(analysis: dict, width: int, field: str) -> tuple[tuple[object, ...], ...]:
    """Emit a deterministic headless claim, region, or source overlay."""
    keys = {"claim": "owner", "region": "region", "source": "source"}
    if field not in keys or width <= 0 or len(analysis["cells"]) % width:
        raise ValueError("overlay field and width must describe a complete grid")
    key = keys[field]
    values = [cell[key] if cell[key] is not None else 0 for cell in analysis["cells"]]
    return tuple(tuple(values[offset:offset + width])
                 for offset in range(0, len(values), width))
