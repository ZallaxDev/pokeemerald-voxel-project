#!/usr/bin/env python3
"""Local blocked-art volume inference shared by offline diorama tools."""

from __future__ import annotations

from collections import Counter, deque
from dataclasses import dataclass


# The reference caps at six 8px rows; Emerald metatiles are 16px tall.
MAX_ROWS = 3


def normalize_rule(rule: dict) -> dict:
    archetype = rule.get("archetype")
    shape = rule.get("shape") or {
        "ground": "flat", "void": "hidden", "ledge": "ledge",
        "water": "water", "shallow-water": "water", "waterfall": "water",
        "current": "water", "hot-spring": "water",
        "cliff": "cliff", "mound": "cliff", "wall-volume": "cliff",
        "bridge": "bridge", "deck": "bridge", "rail": "bridge", "support": "bridge",
        "stairs-n": "stairs", "stairs-s": "stairs", "stairs-e": "stairs",
        "stairs-w": "stairs", "stairs-down-n": "stairs",
        "stairs-down-s": "stairs", "stairs-down-e": "stairs",
        "stairs-down-w": "stairs",
    }.get(archetype, "cutout" if archetype else "flat")
    faces = {
        face: {"metatile": "self", "layer": "foreground" if face == "plane" else "full"}
        for face in ("top", "north", "east", "south", "west", "plane")
    }
    faces.update(rule.get("faces", {}))
    return {
        "shape": shape,
        "archetype": archetype,
        "profile": rule.get("profile", "none"),
        "semanticProfile": rule.get("profile"),
        "axis": rule.get("axis", "x"),
        "baseMetatile": rule.get("baseMetatile", "self"),
        "groundHeight": rule.get("groundOffset", rule.get("groundHeight", 0)),
        "height": rule.get("height", 0),
        "terrainClass": rule.get("terrainClass", "ground"),
        "faces": faces,
    }


def resolve_layout_terrain(cells: list[dict], width: int, height: int,
                           default_action: dict, terrain_defaults: dict[str, str],
                           behavior_rules: dict[str, dict],
                           pins: dict[tuple[str, int], dict],
                           automatic_cells: list[bool] | None = None,
                           terrain_anchors: dict[int, dict | float] | None = None) -> list[dict]:
    """Resolve immutable layout art using the same rules as the editor and compiler."""
    resolved = []
    for cell in cells:
        source = "fallback"
        action = {**default_action, "terrainClass": terrain_defaults[cell["tileset"]]}
        pin = pins.get((cell["tileset"], cell["localMetatile"]))
        if pin is not None:
            source, action = "tileset pin", pin
        elif cell.get("behaviorName", cell.get("behavior")) in behavior_rules:
            behavior = cell.get("behaviorName", cell.get("behavior"))
            source = f"behavior:{behavior}"
            action = behavior_rules[behavior]
        elif cell["collision"] or cell["elevation"] not in (0, 3, 15):
            source = "collision/elevation"
        elif cell.get("layerType") not in (None, "normal", 0):
            source = "visual heuristic"
        resolved.append({"source": source, "rule": normalize_rule(action)})
    for index, raw_anchor in (terrain_anchors or {}).items():
        anchor = raw_anchor if isinstance(raw_anchor, dict) else {"level": raw_anchor}
        if not 0 <= index < len(resolved) or "archetype" not in anchor:
            continue
        rule = dict(resolved[index]["rule"])
        authored = normalize_rule(anchor)
        rule.update({key: authored[key] for key in
                     ("shape", "archetype", "axis", "terrainClass")})
        if "shape" in anchor:
            rule["shape"] = anchor["shape"]
        if "height" in anchor:
            rule["height"] = anchor["height"]
        rule["anchorGroup"] = anchor.get("group", index + 1)
        resolved[index] = {"source": "map", "rule": rule}
    resolve_volumes(cells, resolved, width, height, automatic_cells, terrain_anchors)
    return resolved


@dataclass
class Run:
    component: int
    x: int
    north: int
    south: int
    extent: int
    height: int
    from_repeat: bool
    terrain_measured: bool


def _candidate(cell: dict, resolution: dict) -> bool:
    source = resolution["source"]
    return (cell["collision"] != 0
            and resolution["rule"]["shape"] == "flat"
            and source not in {"map", "tileset pin", "building", "exact pattern",
                               "contextual rule", "event"}
            and resolution["rule"].get("terrainClass") == "rock"
            and source in {"behavior:MB_CAVE", "behavior:MB_MOUNTAIN_TOP"})


def resolve_volumes(cells: list[dict], resolved: list[dict],
                     width: int, height: int,
                     automatic_cells: list[bool] | None = None,
                     terrain_anchors: dict[int, dict | float] | None = None) -> None:
    """Refine blocked fallback cells in place using local north-south art runs."""
    if automatic_cells is None:
        automatic_cells = [True] * len(cells)
    if terrain_anchors is None:
        terrain_anchors = {}
    terrain_anchors = {
        index: value if isinstance(value, dict) else {"level": value}
        for index, value in terrain_anchors.items()
    }
    if len(automatic_cells) != len(cells):
        raise ValueError("automatic cell mask must match the layout cell count")
    candidates = [automatic and _candidate(cell, resolution)
                  for cell, resolution, automatic in
                  zip(cells, resolved, automatic_cells)]
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
        terrain_measured = (resolved[index]["rule"].get("terrainClass") == "rock"
                            and resolved[index]["source"] in
                            {"behavior:MB_CAVE", "behavior:MB_MOUNTAIN_TOP"})
        unit = min(extent, MAX_ROWS)
        front = cells[south * width + x]["metatile"]
        from_repeat = False
        if terrain_measured:
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
        else:
            unit = 1
        runs.append(Run(component, x, north, south, extent, unit, from_repeat,
                        terrain_measured))

    for component in range(1, component_count + 1):
        votes = Counter(run.height for run in runs if run.component == component)
        if not votes:
            continue
        mode = max(votes, key=lambda value: (votes[value], value))
        for run in runs:
            if (run.component == component and run.terrain_measured
                    and run.from_repeat and mode > run.height):
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
                "volumeTerrainMeasured": run.terrain_measured,
                "volumeNorthY": run.north,
                "volumeSouthY": run.south,
            })
            resolution["source"] = "automatic blocked volume"
            resolution["rule"] = rule

    def plateau_candidate(index: int) -> bool:
        rule = resolved[index]["rule"]
        visual_ground = (cells[index]["collision"] == 0
                         or (resolved[index]["source"] == "tileset pin"
                             and rule.get("archetype") == "ground"))
        return (visual_ground and rule["shape"] == "flat"
                and rule.get("archetype") != "void"
                and rule.get("terrainClass") != "water")

    effective_elevations = [cell.get("elevation", 0)
                            if 0 < cell.get("elevation", 0) < 15 else 0
                            for cell in cells]
    regions = [0] * len(cells)
    region_count = 0
    for start in range(len(cells)):
        if regions[start] or not plateau_candidate(start):
            continue
        region_count += 1
        regions[start] = region_count
        queue = deque([start])
        while queue:
            current = queue.popleft()
            x, y = current % width, current // width
            for nx, ny in ((x, y - 1), (x + 1, y), (x, y + 1), (x - 1, y)):
                if not (0 <= nx < width and 0 <= ny < height):
                    continue
                neighbor = ny * width + nx
                if (regions[neighbor] or not plateau_candidate(neighbor)
                        or effective_elevations[current]
                        != effective_elevations[neighbor]):
                    continue
                regions[neighbor] = region_count
                queue.append(neighbor)

    visual_regions = list(regions)
    visual_region_count = region_count

    def visual_key(index: int) -> tuple:
        rule = resolved[index]["rule"]
        return (rule["shape"], rule.get("archetype"),
                rule.get("terrainClass"), effective_elevations[index],
                rule.get("anchorGroup"))

    for start in range(len(cells)):
        if visual_regions[start] or resolved[start]["rule"]["shape"] == "hidden":
            continue
        visual_region_count += 1
        key = visual_key(start)
        visual_regions[start] = visual_region_count
        queue = deque([start])
        while queue:
            current = queue.popleft()
            x, y = current % width, current // width
            for nx, ny in ((x, y - 1), (x + 1, y), (x, y + 1), (x - 1, y)):
                if not (0 <= nx < width and 0 <= ny < height):
                    continue
                neighbor = ny * width + nx
                if visual_regions[neighbor] or visual_key(neighbor) != key:
                    continue
                visual_regions[neighbor] = visual_region_count
                queue.append(neighbor)

    for index, region in enumerate(visual_regions):
        if region:
            resolved[index]["rule"]["visualRegion"] = region
            if regions[index]:
                resolved[index]["rule"]["plateauRegion"] = regions[index]

    def transition_directions(index: int) -> tuple[tuple[int, int], tuple[int, int]] | None:
        rule = resolved[index]["rule"]
        if rule["shape"] == "ledge":
            low = {0x3A: (0, -1), 0x38: (1, 0), 0x3B: (0, 1), 0x39: (-1, 0),
                   "MB_JUMP_NORTH": (0, -1), "MB_JUMP_EAST": (1, 0),
                   "MB_JUMP_SOUTH": (0, 1), "MB_JUMP_WEST": (-1, 0)}.get(
                       cells[index].get("behaviorName", cells[index].get("behavior")))
            return ((-low[0], -low[1]), low) if low else None
        if rule["shape"] != "stairs":
            return None
        high = {"stairs-n": (0, -1), "stairs-down-s": (0, -1),
                "stairs-s": (0, 1), "stairs-down-n": (0, 1),
                "stairs-e": (1, 0), "stairs-down-w": (1, 0),
                "stairs-w": (-1, 0), "stairs-down-e": (-1, 0)}.get(
                    rule.get("archetype"))
        return (high, (-high[0], -high[1])) if high else None

    def find_region(index: int, direction: tuple[int, int]) -> int:
        x, y = index % width, index // width
        for step in range(1, 9):
            nx, ny = x + direction[0] * step, y + direction[1] * step
            if not (0 <= nx < width and 0 <= ny < height):
                return 0
            neighbor = ny * width + nx
            if regions[neighbor]:
                return regions[neighbor]
            if resolved[neighbor]["rule"]["shape"] not in {"ledge", "stairs"}:
                return 0
        return 0

    def find_region_across_cliffs(index: int, dy: int) -> int:
        x, y = index % width, index // width
        for step in range(1, 9):
            ny = y + dy * step
            if not 0 <= ny < height:
                return 0
            neighbor = ny * width + x
            if regions[neighbor]:
                return regions[neighbor]
            if resolved[neighbor]["rule"]["shape"] != "cliff":
                return 0
        return 0

    plateau_edges = []
    graph: dict[int, list[tuple[int, int]]] = {}
    for index in range(len(cells)):
        directions = transition_directions(index)
        if not directions:
            continue
        high, low = find_region(index, directions[0]), find_region(index, directions[1])
        if not high or not low or high == low:
            continue
        plateau_edges.append((high, low, index, 1))
        graph.setdefault(high, []).append((low, -1))
        graph.setdefault(low, []).append((high, 1))

    for index, resolution in enumerate(resolved):
        rule = resolution["rule"]
        if (resolution["source"] != "automatic blocked volume"
                or rule["shape"] != "cliff"
                or not rule.get("volumeTerrainMeasured")
                or index // width != rule.get("volumeNorthY")
                or not rule.get("height")):
            continue
        south = rule.get("volumeSouthY", index // width) * width + index % width
        if not 0 <= south < len(cells):
            continue
        high = find_region(index, (0, -1))
        low = find_region(south, (0, 1))
        if not high or not low or high == low:
            continue
        delta = min(int(rule["height"]), MAX_ROWS)
        plateau_edges.append((high, low, None, delta))
        graph.setdefault(high, []).append((low, -delta))
        graph.setdefault(low, []).append((high, delta))

    for index, resolution in enumerate(resolved):
        if (resolution["source"] != "tileset pin"
                or resolution["rule"]["shape"] != "cliff"
                or resolution["rule"].get("axis", "x") != "x"):
            continue
        high = find_region_across_cliffs(index, -1)
        low = find_region_across_cliffs(index, 1)
        if not high or not low or high == low:
            continue
        delta = min(max(1, int(resolution["rule"].get("height", 1))), MAX_ROWS)
        plateau_edges.append((high, low, index, delta))
        graph.setdefault(high, []).append((low, -delta))
        graph.setdefault(low, []).append((high, delta))

    anchors: dict[int, float] = {}
    anchor_conflicts = set()
    for index, anchor in terrain_anchors.items():
        if not 0 <= index < len(regions) or not visual_regions[index]:
            raise ValueError(f"terrain anchor cell {index} has no visible connected region")
        if not regions[index]:
            continue
        anchor_height = anchor["level"]
        region = regions[index]
        if region in anchors and anchors[region] != anchor_height:
            anchor_conflicts.add(region)
        else:
            anchors[region] = anchor_height
    for index, resolution in enumerate(resolved):
        rule = resolution["rule"]
        elevation = effective_elevations[index]
        if rule["shape"] != "bridge" or not elevation:
            continue
        x, y = index % width, index // width
        for nx, ny in ((x, y - 1), (x + 1, y), (x, y + 1), (x - 1, y)):
            if not (0 <= nx < width and 0 <= ny < height):
                continue
            neighbor = ny * width + nx
            region = regions[neighbor]
            if not region or effective_elevations[neighbor] != elevation:
                continue
            height = rule.get("groundHeight", 0)
            if region in anchors and anchors[region] != height:
                anchor_conflicts.add(region)
            else:
                anchors[region] = height

    levels: dict[int, int] = {}
    visited_regions = set()
    for start in graph:
        if start in visited_regions:
            continue
        levels[start] = 0
        component = []
        queue = deque([start])
        conflict = False
        while queue:
            current = queue.popleft()
            if current in visited_regions:
                continue
            visited_regions.add(current)
            component.append(current)
            for neighbor, delta in graph[current]:
                expected = levels[current] + delta
                if neighbor in levels and levels[neighbor] != expected:
                    conflict = True
                elif neighbor not in levels:
                    levels[neighbor] = expected
                    queue.append(neighbor)
        minimum = min(levels[region] for region in component)
        maximum = max(levels[region] for region in component)
        for region in component:
            levels[region] = 0 if conflict or maximum - minimum > MAX_ROWS \
                else levels[region] - minimum

    for region in range(1, region_count + 1):
        levels.setdefault(region, 0)

    level_offsets: dict[int, float] = {}
    offset_visited = set()
    for start in range(1, region_count + 1):
        if start in offset_visited:
            continue
        component = []
        queue = deque([start])
        while queue:
            current = queue.popleft()
            if current in offset_visited:
                continue
            offset_visited.add(current)
            component.append(current)
            queue.extend(neighbor for neighbor, _delta in graph.get(current, [])
                         if neighbor not in offset_visited)
        requested = {anchors[region] - levels[region] for region in component
                     if region in anchors and region not in anchor_conflicts}
        if len(requested) == 1:
            offset = requested.pop()
            for region in component:
                level_offsets[region] = offset

    for index, region in enumerate(regions):
        if region not in levels:
            continue
        rule = resolved[index]["rule"]
        rule["groundHeight"] = (rule.get("groundHeight", 0) + levels[region]
                                + level_offsets.get(region, 0))
    for high_region, low_region, index, _delta in plateau_edges:
        high, low = levels.get(high_region, 0), levels.get(low_region, 0)
        if index is None or high <= low:
            continue
        rule = resolved[index]["rule"]
        if rule["shape"] == "ledge":
            rule["groundHeight"] = high
        else:
            rule["groundHeight"] = low
            rule["height"] = high - low

    cliff_seen = set()
    for start, resolution in enumerate(resolved):
        rule = resolution["rule"]
        if (start in cliff_seen or rule["shape"] != "cliff"
                or not rule.get("volumeTerrainMeasured", False)):
            continue
        component, adjacent = [], set()
        cliff_seen.add(start)
        queue = deque([start])
        while queue:
            current = queue.popleft()
            component.append(current)
            x, y = current % width, current // width
            for nx, ny in ((x, y - 1), (x + 1, y), (x, y + 1), (x - 1, y)):
                if not (0 <= nx < width and 0 <= ny < height):
                    continue
                neighbor = ny * width + nx
                if regions[neighbor] in levels:
                    adjacent.add(levels[regions[neighbor]])
                elif (neighbor not in cliff_seen
                      and resolved[neighbor]["rule"]["shape"] == "cliff"
                       and resolved[neighbor]["rule"].get("volumeTerrainMeasured", False)):
                    cliff_seen.add(neighbor)
                    queue.append(neighbor)
        if len(adjacent) >= 2:
            low, high = min(adjacent), max(adjacent)
            for index in component:
                resolved[index]["rule"].update({"groundHeight": low,
                                                  "height": high - low})

    anchored_visual_regions = {}
    for index, anchor in terrain_anchors.items():
        if regions[index]:
            continue
        region = visual_regions[index]
        previous = anchored_visual_regions.get(region)
        value = (anchor["level"], anchor.get("height"))
        if previous is not None and previous != value:
            raise ValueError(f"conflicting terrain anchors for visual region {region}")
        anchored_visual_regions[region] = value
    for index, region in enumerate(visual_regions):
        if region not in anchored_visual_regions:
            continue
        level, feature_height = anchored_visual_regions[region]
        resolved[index]["rule"]["groundHeight"] = level
        if feature_height is not None:
            resolved[index]["rule"]["height"] = feature_height

    edges = ((0, -1, 1), (1, 0, 2), (0, 1, 4), (-1, 0, 8))
    for index, resolution in enumerate(resolved):
        rule = resolution["rule"]
        if rule["shape"] != "cliff":
            continue
        x, y = index % width, index // width
        top = rule.get("groundHeight", 0) + rule.get("height", 0)
        ground = rule.get("groundHeight", 0)
        edge_mask = base_mask = transition_mask = 0
        for dx, dy, edge in edges:
            nx, ny = x + dx, y + dy
            neighbor = resolved[ny * width + nx]["rule"] \
                if 0 <= nx < width and 0 <= ny < height else None
            neighbor_top = (neighbor.get("groundHeight", 0) + neighbor.get("height", 0)) \
                if neighbor and neighbor["shape"] != "hidden" else ground
            if neighbor is None or neighbor["shape"] != "cliff" or neighbor_top < top:
                edge_mask |= edge
            if neighbor is None or neighbor_top <= ground:
                base_mask |= edge
            if neighbor and neighbor["shape"] == "cliff" and neighbor_top != top:
                transition_mask |= edge
        corner_mask = 0
        if edge_mask & 9 == 9:
            corner_mask |= 1
        if edge_mask & 3 == 3:
            corner_mask |= 2
        if edge_mask & 6 == 6:
            corner_mask |= 4
        if edge_mask & 12 == 12:
            corner_mask |= 8
        rule.update({"cliffEdgeMask": edge_mask, "cliffBaseMask": base_mask,
                     "cliffTransitionMask": transition_mask,
                     "cliffCornerMask": corner_mask})
