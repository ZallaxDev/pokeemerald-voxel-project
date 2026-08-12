#!/usr/bin/env python3
"""Compile General terrace artwork into deterministic relative terrain levels."""

from __future__ import annotations

from collections import defaultdict, deque
from dataclasses import dataclass


@dataclass(frozen=True)
class TerraceRole:
    profile: str
    quadrants: tuple[int, int, int, int]  # north-west, north-east, south-east, south-west
    solid: bool
    ports: tuple[int, int]


TERRACE_ROLES = {
    121: TerraceRole("horizontal", (1, 1, 0, 0), True, (1, 3)),
    136: TerraceRole("vertical", (0, 1, 1, 0), True, (0, 2)),
    137: TerraceRole("outer", (1, 1, 1, 0), True, (2, 3)),
    144: TerraceRole("inner", (0, 1, 0, 0), True, (0, 1)),
    135: TerraceRole("horizontal", (1, 1, 0, 0), False, (1, 3)),
    133: TerraceRole("vertical", (0, 1, 1, 0), False, (0, 2)),
    111: TerraceRole("outer", (1, 1, 1, 0), False, (2, 3)),
    141: TerraceRole("inner", (0, 1, 0, 0), False, (0, 1)),
    175: TerraceRole("transition-horizontal", (1, 1, 0, 0), False, (1, 3)),
    207: TerraceRole("transition-horizontal", (1, 1, 0, 0), False, (1, 3)),
}


def role_for_cell(cell: dict) -> TerraceRole | None:
    if cell.get("tileset") != "gTileset_General":
        return None
    return TERRACE_ROLES.get(cell.get("localMetatile", cell.get("metatile")))


def solve_layout(cells: tuple[dict, ...], width: int, height: int,
                 contradictions: list[tuple[int, ...]] | None = None,
                 conflict_edges: list[tuple[object, object, int, int]] | None = None
                 ) -> dict[int, dict]:
    """Return solved cells; contradictory connected topology components are omitted."""
    if len(cells) != width * height:
        raise ValueError("terrace topology layout dimensions do not match its cells")
    cardinal = ((0, -1), (1, 0), (0, 1), (-1, 0))
    all_candidates = {offset: role for offset, cell in enumerate(cells)
                      if (role := role_for_cell(cell)) is not None}
    candidates = {offset: role for offset, role in all_candidates.items() if role.solid}
    for offset, role in all_candidates.items():
        if not role.profile.startswith("transition-"):
            continue
        x, y = offset % width, offset // width
        if any((neighbor := (y + cardinal[d][1]) * width + x + cardinal[d][0])
               in candidates and (d + 2) % 4 in candidates[neighbor].ports
               for d in role.ports
               if 0 <= x + cardinal[d][0] < width
               and 0 <= y + cardinal[d][1] < height):
            candidates[offset] = role
    roles = {}
    for offset, role in candidates.items():
        x, y = offset % width, offset // width
        for direction in role.ports:
            dx, dy = cardinal[direction]
            nx, ny = x + dx, y + dy
            if not (0 <= nx < width and 0 <= ny < height):
                continue
            neighbor = ny * width + nx
            neighbor_role = candidates.get(neighbor)
            if neighbor_role is not None and (direction + 2) % 4 in neighbor_role.ports:
                roles[offset] = role
                break
    if not roles:
        return {}

    # Collapse ordinary terrain into regions. Terrace course cells remain explicit nodes.
    region_by_cell: dict[int, int] = {}
    region_cells: dict[int, list[int]] = {}
    next_node = len(cells)
    for start in range(len(cells)):
        if start in roles or start in region_by_cell:
            continue
        if cells[start].get("collision", 0) != 0:
            continue
        node = next_node
        next_node += 1
        pending = deque((start,))
        region_by_cell[start] = node
        region_cells[node] = []
        while pending:
            offset = pending.popleft()
            region_cells[node].append(offset)
            x, y = offset % width, offset // width
            for neighbor in (offset - width if y else -1,
                             offset + 1 if x + 1 < width else -1,
                             offset + width if y + 1 < height else -1,
                             offset - 1 if x else -1):
                if (neighbor >= 0 and neighbor not in roles and neighbor not in region_by_cell
                        and cells[neighbor].get("collision", 0) == 0
                        and cells[neighbor].get("elevation") == cells[offset].get("elevation")):
                    region_by_cell[neighbor] = node
                    pending.append(neighbor)

    def node_for(offset: int, quadrant: int):
        role = roles.get(offset)
        return ((offset, role.quadrants[quadrant]) if role is not None
                else region_by_cell.get(offset))

    # Each role has one low and one high surface node. Cell boundaries connect in
    # two halves so diagonal corners join straight courses without scalar side guesses.
    graph: dict[object, list[tuple[object, int]]] = defaultdict(list)
    for offset, role in sorted(roles.items()):
        low = (offset, 0)
        high = (offset, 1)
        graph[low].append((high, 1))
        graph[high].append((low, -1))
        x, y = offset % width, offset // width
        for direction, neighbor in enumerate((
                offset - width if y else -1,
                offset + 1 if x + 1 < width else -1,
                offset + width if y + 1 < height else -1,
                offset - 1 if x else -1)):
            if neighbor < 0:
                continue
            source_quadrants = ((0, 1), (1, 2), (3, 2), (0, 3))[direction]
            target_quadrants = ((3, 2), (0, 3), (0, 1), (1, 2))[direction]
            for source_quadrant, target_quadrant in zip(source_quadrants, target_quadrants):
                source_node = node_for(offset, source_quadrant)
                target_node = node_for(neighbor, target_quadrant)
                if source_node is None or target_node is None:
                    continue
                graph[source_node].append((target_node, 0))
                graph[target_node].append((source_node, 0))

    solved: dict[int, dict] = {}
    visited: set[object] = set()
    for seed in sorted(graph, key=str):
        if seed in visited:
            continue
        values = {seed: 0}
        component = []
        pending = deque((seed,))
        contradictory = False
        while pending:
            node = pending.popleft()
            if node in visited:
                continue
            visited.add(node)
            component.append(node)
            for neighbor, delta in graph[node]:
                expected = values[node] + delta
                if neighbor in values:
                    if values[neighbor] != expected:
                        contradictory = True
                        if conflict_edges is not None:
                            conflict_edges.append((node, neighbor, values[neighbor], expected))
                else:
                    values[neighbor] = expected
                    pending.append(neighbor)
        if contradictory:
            if contradictions is not None:
                contradictions.append(tuple(sorted(node[0] for node in component
                                                   if isinstance(node, tuple))))
            continue
        anchor = max(values[node] for node in component)
        for node in component:
            ground = values[node] - anchor
            if isinstance(node, tuple):
                offset, level = node
                if level != 0:
                    continue
                role = roles[offset]
                solved[offset] = {"groundQ16": ground * 16,
                                  "topQ16": (ground + (1 if role.solid else 0)) * 16,
                                  "profile": role.profile, "solid": role.solid}
            else:
                for offset in region_cells[node]:
                    solved[offset] = {"groundQ16": ground * 16,
                                      "topQ16": ground * 16,
                                      "profile": "none", "solid": False}
    return solved
