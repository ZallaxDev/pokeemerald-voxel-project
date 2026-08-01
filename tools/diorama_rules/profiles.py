#!/usr/bin/env python3
"""Closed profile vocabulary and immutable TileShape output records."""

from __future__ import annotations

from collections.abc import Iterator, Mapping
from dataclasses import dataclass
from types import MappingProxyType
from typing import Any


SEMANTIC_POOLS = frozenset({
    "terrain", "water", "structure", "vegetation", "furniture", "prop",
})
ART_MODES = frozenset({
    "flat", "top", "upright", "grass", "flower", "stair", "cutout",
})

# Heights are measured in map-cell units. One Emerald metatile is one cell high.
NOMINAL_HEIGHTS = MappingProxyType({
    "ground": 0.0,
    "void": 0.0,
    "water": -0.125,
    "shallow-water": -0.0625,
    "waterfall": -0.125,
    "current": -0.125,
    "hot-spring": -0.125,
    "grass": 0.0,
    "flower": 0.0,
    "animated-cutout": 0.0,
    "ledge": 0.375,
    "mound": 0.5,
    "stairs": 1.0,
    "stairs-n": 1.0,
    "stairs-s": 1.0,
    "stairs-e": 1.0,
    "stairs-w": 1.0,
    "stairs-down-n": 1.0,
    "stairs-down-s": 1.0,
    "stairs-down-e": 1.0,
    "stairs-down-w": 1.0,
    "cliff": 1.0,
    "wall": 1.0,
    "wall-volume": 1.0,
    "roof": 1.75,
    "top-slab": 0.375,
    "bridge": 0.25,
    "deck": 0.25,
    "rail": 0.625,
    "support": 1.0,
    "tree": 1.0,
    "forest-wall": 1.0,
    "shrub": 0.75,
    "hedge": 0.75,
    "rock": 0.75,
    "boulder": 1.0,
    "building": 1.0,
    "awning": 0.5,
    "claim-only": 0.0,
    "counter": 0.5,
    "table": 0.75,
    "desk": 1.5,
    "bed": 0.4375,
    "bookcase": 2.0,
    "billboard": 1.0,
    "cutout": 1.0,
    "console": 1.0,
    "signpost": 1.0,
    "post": 1.0,
    "stump": 1.0,
    "round-hull": 1.0,
    "grouped-hull": 1.0,
    "relief": 0.1875,
})

CLASS_ART_MODES = MappingProxyType({
    "ground": "flat", "void": "flat", "water": "flat", "claim-only": "flat",
    "shallow-water": "flat", "waterfall": "flat", "current": "flat",
    "hot-spring": "flat",
    "grass": "grass", "flower": "flower", "animated-cutout": "cutout",
    "ledge": "top", "roof": "top", "top-slab": "top", "bed": "top",
    "bridge": "top", "deck": "top", "awning": "top",
    "stairs": "stair", "stairs-n": "stair", "stairs-s": "stair",
    "stairs-e": "stair", "stairs-w": "stair", "stairs-down-n": "stair",
    "stairs-down-s": "stair", "stairs-down-e": "stair",
    "stairs-down-w": "stair",
})

CLASS_POOLS = MappingProxyType({
    "water": "water", "shallow-water": "water", "waterfall": "water",
    "current": "water",
    "hot-spring": "water", "grass": "vegetation", "flower": "vegetation",
    "tree": "vegetation", "forest-wall": "vegetation", "shrub": "vegetation",
    "hedge": "vegetation", "building": "structure", "roof": "structure",
    "bridge": "structure", "deck": "structure", "wall-volume": "structure",
    "counter": "furniture", "table": "furniture", "desk": "furniture",
    "bed": "furniture", "bookcase": "furniture", "billboard": "prop",
    "cutout": "prop", "animated-cutout": "prop", "console": "prop",
    "signpost": "prop", "post": "prop", "rock": "prop", "boulder": "prop",
})


class ProfileError(ValueError):
    pass


@dataclass(frozen=True, order=True)
class PropGround:
    """Ground under additive prop art; automatic inference is deliberately pending."""

    mode: str
    metatile: int | None = None

    def __post_init__(self) -> None:
        if self.mode not in ("manual", "automatic-pending"):
            raise ProfileError(f"unknown propGround mode {self.mode!r}")
        if self.mode == "manual":
            if not isinstance(self.metatile, int) or isinstance(self.metatile, bool) \
                    or not 0 <= self.metatile <= 1023:
                raise ProfileError("manual propGround requires metatile 0 through 1023")
        elif self.metatile is not None:
            raise ProfileError("automatic-pending propGround cannot name a metatile")

    def as_dict(self) -> dict[str, object]:
        result: dict[str, object] = {"mode": self.mode}
        if self.metatile is not None:
            result["metatile"] = self.metatile
        return result


AUTOMATIC_PROP_GROUND = PropGround("automatic-pending")


@dataclass(frozen=True)
class TileShape(Mapping[str, Any]):
    """Immutable value-only classifier result with stable serialization."""

    class_name: str
    height: float
    art_mode: str
    pool: str
    authored: bool
    source: str
    confidence: float
    evidence: tuple[str, ...]
    ambiguity: tuple[str, ...]
    prop_ground: PropGround

    def __post_init__(self) -> None:
        if self.art_mode not in ART_MODES:
            raise ProfileError(f"unknown artMode {self.art_mode!r}")
        if self.pool not in SEMANTIC_POOLS:
            raise ProfileError(f"unknown semantic pool {self.pool!r}")
        if not 0.0 <= self.confidence <= 1.0:
            raise ProfileError("confidence must be between zero and one")
        if self.class_name not in NOMINAL_HEIGHTS:
            raise ProfileError(f"unknown TileShape class {self.class_name!r}")
        object.__setattr__(self, "height", float(self.height))
        object.__setattr__(self, "evidence", tuple(sorted(set(self.evidence))))
        object.__setattr__(self, "ambiguity", tuple(sorted(set(self.ambiguity))))

    def as_dict(self) -> dict[str, object]:
        return {
            "class": self.class_name,
            "height": self.height,
            "artMode": self.art_mode,
            "pool": self.pool,
            "authored": self.authored,
            "source": self.source,
            "confidence": self.confidence,
            "evidence": list(self.evidence),
            "ambiguity": list(self.ambiguity),
            "propGround": self.prop_ground.as_dict(),
        }

    def __getitem__(self, key: str) -> Any:
        try:
            return self.as_dict()[key]
        except KeyError as error:
            raise KeyError(key) from error

    def __iter__(self) -> Iterator[str]:
        return iter(self.as_dict())

    def __len__(self) -> int:
        return 10


def prop_ground_from_action(action: Mapping[str, object]) -> PropGround:
    raw = action.get("propGround", action.get("groundPolicy"))
    if raw is None:
        return AUTOMATIC_PROP_GROUND
    if isinstance(raw, int) and not isinstance(raw, bool):
        return PropGround("manual", raw)
    if not isinstance(raw, Mapping):
        raise ProfileError("propGround must be an object")
    mode = raw.get("mode")
    if mode == "automatic":
        mode = "automatic-pending"
    return PropGround(str(mode), raw.get("metatile"))


def shape_from_action(action: Mapping[str, object], *, source: str, authored: bool,
                      confidence: float, evidence: tuple[str, ...] = (),
                      ambiguity: tuple[str, ...] = ()) -> TileShape:
    archetype = str(action.get("archetype", "ground"))
    class_name = str(action.get("class", action.get("shape", archetype)))
    if class_name in ("flat", "extruded", "building-part"):
        class_name = archetype
    elif class_name == "hidden":
        class_name = "void"
    art_mode = str(action.get("artMode", CLASS_ART_MODES.get(class_name, "upright")))
    pool = str(action.get("pool", CLASS_POOLS.get(class_name, "terrain")))
    height = action.get("height", NOMINAL_HEIGHTS.get(class_name))
    if class_name not in NOMINAL_HEIGHTS or height is None:
        raise ProfileError(f"unknown TileShape class {class_name!r}")
    return TileShape(class_name, float(height), art_mode, pool, authored, source,
                     confidence, evidence, ambiguity, prop_ground_from_action(action))
