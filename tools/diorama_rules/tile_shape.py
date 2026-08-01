#!/usr/bin/env python3
"""Pure deterministic R2 classification over complete Emerald layouts."""

from __future__ import annotations

import hashlib
import json
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from typing import Any

from profiles import TileShape, shape_from_action


CARDINAL_OFFSETS = {"n": (0, -1), "e": (1, 0), "s": (0, 1), "w": (-1, 0)}

WATER_BEHAVIORS = frozenset({
    "MB_POND_WATER", "MB_INTERIOR_DEEP_WATER", "MB_DEEP_WATER",
    "MB_SOOTOPOLIS_DEEP_WATER", "MB_OCEAN_WATER", "MB_SHALLOW_WATER",
    "MB_NO_SURFACING", "MB_SEAWEED", "MB_SEAWEED_NO_SURFACING",
    "MB_WATERFALL", "MB_EASTWARD_CURRENT", "MB_WESTWARD_CURRENT",
    "MB_NORTHWARD_CURRENT", "MB_SOUTHWARD_CURRENT", "MB_HOT_SPRINGS",
})
GRASS_BEHAVIORS = frozenset({
    "MB_TALL_GRASS", "MB_LONG_GRASS", "MB_SHORT_GRASS",
    "MB_LONG_GRASS_SOUTH_EDGE", "MB_ASHGRASS",
})
FLOWER_BEHAVIORS = frozenset({"MB_FLOWERS", "MB_ANIMATED_FLOWERS"})
LEDGE_BEHAVIORS = frozenset({
    "MB_JUMP_EAST", "MB_JUMP_WEST", "MB_JUMP_NORTH", "MB_JUMP_SOUTH",
    "MB_JUMP_NORTHEAST", "MB_JUMP_NORTHWEST",
    "MB_JUMP_SOUTHEAST", "MB_JUMP_SOUTHWEST",
})
STAIR_BEHAVIORS = frozenset({"MB_STAIRS_OUTSIDE_ABANDONED_SHIP"})


class ClassificationError(ValueError):
    pass


@dataclass(frozen=True)
class _Candidate:
    rule_id: str
    priority: int
    action: Mapping[str, object]
    source: str


def _value(cell: Mapping[str, object], *names: str) -> object:
    for name in names:
        if name in cell:
            return cell[name]
    return None


def _matches_neighbor(selector: Mapping[str, object], cell: Mapping[str, object]) -> bool:
    aliases = {
        "metatile": ("metatile",), "behavior": ("behavior",),
        "behaviorId": ("behaviorId",), "layerType": ("layerType", "layer"),
        "elevation": ("elevation",),
    }
    return all(key == "behaviorId" and "behaviorId" not in cell
               or _value(cell, *aliases[key]) == expected
               for key, expected in selector.items() if key in aliases)


def _matches_selector(selector: Mapping[str, object], cell: Mapping[str, object],
                      neighbors: Mapping[str, Mapping[str, object]],
                      map_type: str | None, events: Sequence[Mapping[str, object]]) -> bool:
    dimensions = (
        ("tilesets", _value(cell, "tileset")),
        ("metatiles", _value(cell, "metatile")),
        ("behaviors", _value(cell, "behavior")),
        ("behaviorIds", _value(cell, "behaviorId")),
        ("layerTypes", _value(cell, "layerType", "layer")),
        ("elevations", _value(cell, "elevation")),
    )
    if any(field in selector and value not in selector[field] for field, value in dimensions):
        return False
    if "mapTypes" in selector and map_type not in selector["mapTypes"]:
        return False
    required = dict(selector.get("neighbors", {}))
    when_above = selector.get("when_above", selector.get("whenAbove"))
    if when_above is not None:
        required["n"] = when_above
    for direction, wanted in required.items():
        if direction not in CARDINAL_OFFSETS:
            raise ClassificationError(f"non-cardinal neighbor selector {direction!r}")
        neighbor = neighbors.get(direction)
        if neighbor is None or not _matches_neighbor(wanted, neighbor):
            return False
    wanted_event = selector.get("event")
    if wanted_event is not None and not any(
            event.get("kind") == wanted_event.get("kind") and
            ("class" not in wanted_event or event.get("class") == wanted_event["class"])
            for event in events):
        return False
    return True


def _select_authored(candidates: Sequence[_Candidate], tier: str) -> _Candidate | None:
    if not candidates:
        return None
    priority = max(candidate.priority for candidate in candidates)
    winners = sorted((candidate for candidate in candidates if candidate.priority == priority),
                     key=lambda candidate: candidate.rule_id)
    outcomes = {json.dumps(candidate.action, sort_keys=True, separators=(",", ":"),
                           ensure_ascii=True) for candidate in winners}
    if len(outcomes) != 1:
        ids = ", ".join(candidate.rule_id for candidate in winners)
        raise ClassificationError(f"conflicting equal-tier {tier} matches: {ids}")
    return winners[0]


def _stable_id(prefix: str, value: Mapping[str, object]) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":"),
                         ensure_ascii=True).encode("ascii")
    return f"{prefix}-{hashlib.sha256(encoded).hexdigest()[:16]}"


def _behavior_action(behavior: object, cell: Mapping[str, object]) -> tuple[dict[str, object], bool] | None:
    if behavior in WATER_BEHAVIORS:
        class_name = ("shallow-water" if behavior == "MB_SHALLOW_WATER" else
                      "waterfall" if behavior == "MB_WATERFALL" else
                      "current" if str(behavior).endswith("WARD_CURRENT") else
                      "hot-spring" if behavior == "MB_HOT_SPRINGS" else "water")
        return {"class": class_name, "pool": "water"}, False
    if behavior in GRASS_BEHAVIORS:
        return {"class": "grass", "pool": "vegetation"}, True
    if behavior in FLOWER_BEHAVIORS:
        return {"class": "flower", "pool": "vegetation"}, True
    if behavior in LEDGE_BEHAVIORS:
        return {"class": "ledge", "pool": "terrain"}, False
    if behavior in STAIR_BEHAVIORS:
        return {"class": "stairs", "pool": "terrain"}, False
    animation = _value(cell, "animationClass", "animationType", "animation")
    if animation in ("flower", "flowers", "animated-flowers"):
        return {"class": "flower", "pool": "vegetation"}, True
    return None


def _with_cell_evidence(shape: TileShape, cell: Mapping[str, object]) -> TileShape:
    evidence = list(shape.evidence)
    collision = _value(cell, "collision")
    elevation = _value(cell, "elevation")
    layer = _value(cell, "layerType", "layer")
    if collision not in (None, 0):
        evidence.append(f"collision:{collision}")
    if elevation not in (None, 0):
        evidence.append(f"elevation:{elevation}")
    if layer not in (None, "normal", 0):
        evidence.append(f"layerType:{layer}")
    return TileShape(shape.class_name, shape.height, shape.art_mode, shape.pool,
                     shape.authored, shape.source, shape.confidence, tuple(evidence),
                     shape.ambiguity, shape.prop_ground)


class TileShapeClassifier:
    """Classifier configured from compile_rules.py's normalized IR collections."""

    def __init__(self, *, contextual_rules: Sequence[Mapping[str, object]] = (),
                 tileset_pins: Sequence[Mapping[str, object]] = (),
                 behavior_rules: Sequence[Mapping[str, object]] = (),
                 default_action: Mapping[str, object] | None = None) -> None:
        self.contextual_rules = tuple(contextual_rules)
        self.tileset_pins = tuple(tileset_pins)
        self.behavior_rules = tuple(behavior_rules)
        self.default_action = default_action or {"class": "ground", "pool": "terrain"}

    @classmethod
    def from_compiled(cls, compiled: Mapping[str, object], map_symbol: str | None = None):
        local: Sequence[Mapping[str, object]] = ()
        if map_symbol is not None:
            local = next((row.get("contextualRules", ()) for row in compiled.get("maps", ())
                          if row.get("symbol") == map_symbol), ())
        return cls(contextual_rules=tuple(local) + tuple(compiled.get("contextualRules", ())),
                   tileset_pins=compiled.get("tilesetPins", ()),
                   behavior_rules=compiled.get("behaviorRules", ()),
                   default_action=compiled.get("default"))

    def classify_cell(self, cell: Mapping[str, object], *,
                      neighbors: Mapping[str, Mapping[str, object]] | None = None,
                      map_type: str | None = None,
                      events: Sequence[Mapping[str, object]] = ()) -> TileShape:
        neighbors = neighbors or {}
        context = []
        for rule in self.contextual_rules:
            selector = rule.get("selector", {})
            if _matches_selector(selector, cell, neighbors, map_type, events):
                rule_id = str(rule.get("id", _stable_id("context", rule)))
                context.append(_Candidate(rule_id, int(rule.get("priority", 0)),
                                          rule["action"], f"context:{rule_id}"))
        selected = _select_authored(context, "contextual authored")
        if selected is not None:
            return _with_cell_evidence(shape_from_action(
                selected.action, source=selected.source, authored=True, confidence=1.0,
                evidence=(f"rule:{selected.rule_id}", "selector:matched")), cell)

        pins = []
        tileset = _value(cell, "tileset")
        local_metatile = _value(cell, "localMetatile", "local_metatile", "metatile")
        for pin in self.tileset_pins:
            if pin.get("tileset") == tileset and pin.get("metatile") == local_metatile:
                rule_id = str(pin.get("id", _stable_id("pin", pin)))
                pins.append(_Candidate(rule_id, int(pin.get("priority", 0)), pin["action"],
                                       f"pin:{tileset}:{local_metatile}"))
        selected = _select_authored(pins, "tileset/metatile pin")
        if selected is not None:
            return _with_cell_evidence(shape_from_action(
                selected.action, source=selected.source, authored=True, confidence=1.0,
                evidence=(f"tileset:{tileset}", f"metatile:{local_metatile}")), cell)

        behavior = _value(cell, "behavior")
        explicit = []
        for rule in self.behavior_rules:
            if rule.get("behavior") == behavior:
                rule_id = str(rule.get("id", _stable_id("behavior", rule)))
                explicit.append(_Candidate(rule_id, int(rule.get("priority", 0)), rule["action"],
                                           f"behavior:{behavior}"))
        selected = _select_authored(explicit, "behavior rule")
        if selected is not None:
            return _with_cell_evidence(shape_from_action(
                selected.action, source=selected.source, authored=False,
                confidence=0.95, evidence=(f"behavior:{behavior}",)), cell)
        derived = _behavior_action(behavior, cell)
        if derived is not None:
            action, authored = derived
            return _with_cell_evidence(shape_from_action(
                action, source=f"behavior:{behavior or 'animation'}", authored=authored,
                confidence=0.95, evidence=(f"behavior:{behavior}",)), cell)

        evidence = [f"behavior:{behavior}"] if behavior is not None else []
        if cell.get("missingTileset"):
            evidence.append(f"tileset:missing:{tileset}")
        ambiguity = (f"no reliable classification for behavior {behavior!r}",)
        return _with_cell_evidence(shape_from_action(
            self.default_action, source="fallback:flat", authored=False,
            confidence=0.0, evidence=tuple(evidence), ambiguity=ambiguity), cell)

    def classify_layout(self, cells: Sequence[Mapping[str, object]], width: int, height: int,
                        *, map_type: str | None = None,
                        events: Mapping[tuple[int, int], Sequence[Mapping[str, object]]] | None = None
                        ) -> tuple[TileShape, ...]:
        if width <= 0 or height <= 0 or len(cells) != width * height:
            raise ClassificationError("layout dimensions do not match cell count")
        events = events or {}
        output = []
        for index, cell in enumerate(cells):
            x, y = index % width, index // width
            neighbors = {}
            for direction, (dx, dy) in CARDINAL_OFFSETS.items():
                nx, ny = x + dx, y + dy
                if 0 <= nx < width and 0 <= ny < height:
                    neighbors[direction] = cells[ny * width + nx]
            output.append(self.classify_cell(cell, neighbors=neighbors, map_type=map_type,
                                             events=events.get((x, y), ())))
        return tuple(output)


def classify_cell(cell: Mapping[str, object], **kwargs: Any) -> TileShape:
    classifier_keys = {"contextual_rules", "tileset_pins", "behavior_rules", "default_action"}
    classifier_args = {key: kwargs.pop(key) for key in tuple(kwargs) if key in classifier_keys}
    return TileShapeClassifier(**classifier_args).classify_cell(cell, **kwargs)


def classify_layout(cells: Sequence[Mapping[str, object]], width: int, height: int,
                    **kwargs: Any) -> tuple[TileShape, ...]:
    classifier_keys = {"contextual_rules", "tileset_pins", "behavior_rules", "default_action"}
    classifier_args = {key: kwargs.pop(key) for key in tuple(kwargs) if key in classifier_keys}
    return TileShapeClassifier(**classifier_args).classify_layout(cells, width, height, **kwargs)
