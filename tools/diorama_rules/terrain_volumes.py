#!/usr/bin/env python3
"""Local blocked-art volume inference shared by offline diorama tools."""

from __future__ import annotations

from collections import Counter, deque
from dataclasses import dataclass


# The reference caps at six 8px rows; Emerald metatiles are 16px tall.
MAX_ROWS = 3


@dataclass
class Run:
    component: int
    x: int
    north: int
    south: int
    extent: int
    height: int
    from_repeat: bool


def _candidate(cell: dict, resolution: dict) -> bool:
    source = resolution["source"]
    return (cell["collision"] != 0
            and resolution["rule"]["shape"] == "flat"
            and source not in {"map", "tileset pin", "building", "exact pattern",
                               "contextual rule", "event"})


def resolve_volumes(cells: list[dict], resolved: list[dict],
                    width: int, height: int) -> None:
    """Refine blocked fallback cells in place using local north-south art runs."""
    candidates = [_candidate(cell, resolution)
                  for cell, resolution in zip(cells, resolved)]
    components = [0] * len(cells)
    component_count = 0
    for start, candidate in enumerate(candidates):
        if not candidate or components[start]:
            continue
        component_count += 1
        components[start] = component_count
        queue = deque([start])
        while queue:
            current = queue.popleft()
            x, y = current % width, current // width
            for nx, ny in ((x, y - 1), (x + 1, y), (x, y + 1), (x - 1, y)):
                if not (0 <= nx < width and 0 <= ny < height):
                    continue
                neighbor = ny * width + nx
                if candidates[neighbor] and not components[neighbor]:
                    components[neighbor] = component_count
                    queue.append(neighbor)

    runs: list[Run] = []
    for index, component in enumerate(components):
        if not component:
            continue
        x, north = index % width, index // width
        if north > 0 and components[index - width] == component:
            continue
        south = north
        while south + 1 < height and components[(south + 1) * width + x] == component:
            south += 1
        extent = south - north + 1
        unit = min(extent, MAX_ROWS)
        front = cells[south * width + x]["metatile"]
        from_repeat = False
        for distance in range(1, extent):
            if cells[(south - distance) * width + x]["metatile"] == front:
                unit = min(MAX_ROWS, distance)
                from_repeat = True
                break
        if not from_repeat and extent >= 3:
            if (cells[(south - 1) * width + x]["metatile"]
                    == cells[(south - 2) * width + x]["metatile"]):
                unit = 1
                from_repeat = True
        runs.append(Run(component, x, north, south, extent, unit, from_repeat))

    for component in range(1, component_count + 1):
        votes = Counter(run.height for run in runs if run.component == component)
        if not votes:
            continue
        mode = max(votes, key=lambda value: (votes[value], value))
        for run in runs:
            if run.component == component and run.from_repeat and mode > run.height:
                run.height = mode

    for run in runs:
        for y in range(run.north, run.south + 1):
            resolution = resolved[y * width + run.x]
            rule = dict(resolution["rule"])
            rule.update({
                "shape": "cliff",
                "archetype": "wall-volume",
                "groundHeight": 0,
                "height": run.height,
                "volumeNorthY": run.north,
                "volumeSouthY": run.south,
            })
            resolution["source"] = "automatic blocked volume"
            resolution["rule"] = rule
