import numpy as np
import time
from typing import Dict, Any, List, Tuple, Optional
from app.models.volume import GenerateVolumeRequest, VolumeStats, RegionParam
from app.services.distance_transform import compute_distance_transform
from app.services.seed_detector import detect_seed_points
from app.services.watershed_service import run_watershed_segmentation

def generate_voxel_volume(req: GenerateVolumeRequest) -> Tuple[np.ndarray, VolumeStats, List[Dict[str, Any]]]:
    """
    Generates 3D Voxel Volume from composite RGBA, foliage mask, regions map, and distance map.
    Supports adjustable crown vertical max width center (crown_center_y), center voxel duplication control (center_dup),
    and flat margin width (flat_margin).
    """
    start_time = time.time()

    rgba = np.array(req.composition_rgba, dtype=np.uint8)
    h, w, _ = rgba.shape

    # 1. Resolve Foliage Mask
    if req.foliage_mask is not None and len(req.foliage_mask) == h:
        foliage = np.array(req.foliage_mask, dtype=bool)
    else:
        foliage = (rgba[:, :, 3] > 10)

    # 2. Resolve Trunk Mask
    if req.trunk_mask is not None and len(req.trunk_mask) == h and np.any(req.trunk_mask):
        trunk = np.array(req.trunk_mask, dtype=bool)
    else:
        non_transparent = (rgba[:, :, 3] > 10)
        trunk = non_transparent & (~foliage)

        if not np.any(trunk) and np.any(foliage):
            ys, xs = np.where(foliage)
            max_y = np.max(ys)
            cx = int(np.mean(xs))
            trunk_w = max(4, (np.max(xs) - np.min(xs)) // 4)
            trunk = np.zeros((h, w), dtype=bool)
            x_start = max(0, cx - trunk_w // 2)
            x_end = min(w, cx + trunk_w // 2)
            trunk[max_y:h, x_start:x_end] = True

    # 3. Resolve Shadow Mask
    shadow = np.array(req.shadow_mask, dtype=bool) if (req.shadow_mask and len(req.shadow_mask) == h) else np.zeros((h, w), dtype=bool)

    # 4. Resolve Distance Map
    if req.distance_map is not None and len(req.distance_map) == h:
        dist_map = np.array(req.distance_map, dtype=np.float32)
    else:
        _, dist_map, _ = compute_distance_transform(foliage.astype(np.uint8))

    # 5. Resolve Regions Map
    if req.regions_map is not None and len(req.regions_map) == h:
        regions = np.array(req.regions_map, dtype=int)
    else:
        seeds = detect_seed_points(foliage.astype(np.uint8), dist_map, min_distance=3, max_seeds=16)
        regions, _ = run_watershed_segmentation(foliage.astype(np.uint8), seeds, dist_map)

    # Determine max depth limit
    max_d = max(4, req.max_depth)
    center_z = max_d // 2

    # Global bounding box of foliage for crown vertical envelope calculations
    if np.any(foliage):
        f_ys, f_xs = np.where(foliage)
        min_fy, max_fy = np.min(f_ys), np.max(f_ys)
        foliage_h = max(1.0, float(max_fy - min_fy))
    else:
        min_fy, max_fy, foliage_h = 0, h - 1, float(h)

    # Crown vertical max width center (0.1 to 0.9, default 0.5)
    c_y = max(0.1, min(0.9, req.crown_center_y))
    span_y = max(c_y, 1.0 - c_y)

    # Central duplication count (0 = unified single center slice, >0 = duplicated central slices)
    c_dup = max(0, min(10, req.center_dup))

    # Flat margin pixel width offset (0 to 8px)
    f_margin = float(max(0, req.flat_margin))

    # Helper function for Z range calculation with center unification / duplication
    def get_z_bounds(half_depth, z_offset=0):
        if half_depth <= 1:
            z_mid = center_z + z_offset
            return max(0, min(max_d - 1, z_mid)), max(0, min(max_d - 1, z_mid))

        half_span = half_depth - 1
        extra_back = c_dup // 2
        extra_front = (c_dup + 1) // 2 if c_dup > 0 else 0

        z_s = max(0, center_z + z_offset - half_span - extra_back)
        z_e = min(max_d - 1, center_z + z_offset + half_span + extra_front)
        return z_s, z_e

    # Grid array: (Depth, Height, Width, 8) -> [active, r, g, b, mat_id, region_id, generated, locked]
    grid = np.zeros((max_d, h, w, 8), dtype=np.uint16)

    # Track region statistics for depth calculations
    unique_regions = [r for r in np.unique(regions) if r > 0]
    region_stats = {}
    for r_id in unique_regions:
        rmask = (regions == r_id) & foliage
        if not np.any(rmask):
            continue
        ys, xs = np.where(rmask)
        min_y, max_y = np.min(ys), np.max(ys)
        min_x, max_x = np.min(xs), np.max(xs)
        rx = max(1.0, (max_x - min_x) / 2.0)
        ry = max(1.0, (max_y - min_y) / 2.0)
        cx = np.mean(xs)
        cy = np.mean(ys)
        max_dist = np.max(dist_map[rmask]) if np.any(dist_map[rmask]) else 1.0
        region_stats[r_id] = {
            "min_x": min_x, "max_x": max_x,
            "min_y": min_y, "max_y": max_y,
            "rx": rx, "ry": ry,
            "cx": cx, "cy": cy,
            "max_dist": max_dist
        }

    # 1. Process Foliage Voxels
    for y in range(h):
        for x in range(w):
            if not foliage[y, x]:
                continue  # Strictly enforce foliage mask

            r_id = int(regions[y, x])
            r_param = req.region_params.get(r_id, RegionParam(region_id=r_id))
            r_stat = region_stats.get(r_id, {"rx": 10.0, "ry": 10.0, "cx": x, "cy": y, "max_dist": 1.0})

            color = rgba[y, x, :3]
            if r_param.color_override:
                color = np.array(r_param.color_override[:3], dtype=np.uint8)

            d_raw = float(dist_map[y, x])

            # Check if pixel is within the flat margin width
            if f_margin > 0 and d_raw <= f_margin:
                half_depth = 1
                z_start, z_end = get_z_bounds(half_depth)
            else:
                d_val = max(0.0, d_raw - f_margin)
                effective_max = max(1.0, float(r_stat["max_dist"]) - f_margin)
                norm_d = d_val / effective_max if effective_max > 0 else 0.5
                norm_d = np.clip(norm_d, 0.0, 1.0)

                # Vertical envelope multiplier based on crown_center_y slider (peaks at c_y)
                ty = (y - min_fy) / foliage_h
                wy = 1.0 - ((ty - c_y) / span_y) ** 2
                vertical_weight = max(0.25, min(1.0, wy))

                # Compute depth span based on selected volumetric crown shape
                if req.mode == "distance":
                    half_depth = int(round((norm_d ** req.depth_gamma) * (max_d / 2.0) * vertical_weight * r_param.depth_scale))
                    half_depth = max(1, min(max_d // 2, half_depth))
                    z_start, z_end = get_z_bounds(half_depth)

                elif req.mode == "conical":
                    cone_scale = 0.2 + 0.8 * ty
                    half_depth = int(round((max_d / 2.0) * cone_scale * (norm_d ** 0.5) * vertical_weight * r_param.depth_scale))
                    half_depth = max(1, min(max_d // 2, half_depth))
                    z_start, z_end = get_z_bounds(half_depth)

                elif req.mode == "tiered":
                    tier_norm = (int(norm_d * 4.0) / 4.0) + 0.1
                    half_depth = int(round((max_d / 2.0) * tier_norm * vertical_weight * r_param.depth_scale))
                    half_depth = max(1, min(max_d // 2, half_depth))
                    z_start, z_end = get_z_bounds(half_depth)

                elif req.mode == "cylindrical":
                    half_depth = int(round((max_d / 2.0) * (0.6 + 0.4 * norm_d) * vertical_weight * r_param.depth_scale))
                    half_depth = max(1, min(max_d // 2, half_depth))
                    z_start, z_end = get_z_bounds(half_depth)

                elif req.mode == "ellipsoidal":
                    dx = (x - r_stat["cx"]) / (r_stat["rx"] * r_param.z_radius_scale)
                    dy = (y - r_stat["cy"]) / (r_stat["ry"] * r_param.z_radius_scale)
                    rad_sq = dx*dx + dy*dy
                    if rad_sq > 1.0:
                        half_depth = 1
                    else:
                        rz = (max_d / 2.0) * r_param.depth_scale * r_param.flattening * vertical_weight
                        half_depth = int(round(rz * np.sqrt(max(0.0, 1.0 - rad_sq))))
                        half_depth = max(1, min(max_d // 2, half_depth))

                    z_offset = int(round(r_param.z_center_offset))
                    z_start, z_end = get_z_bounds(half_depth, z_offset)

                else:  # "hybrid" default mode
                    dx = (x - r_stat["cx"]) / max(1.0, r_stat["rx"])
                    dy = (y - r_stat["cy"]) / max(1.0, r_stat["ry"])
                    rad_sq = dx*dx + dy*dy
                    base_depth = (max_d / 2.0) * r_param.depth_scale * (norm_d ** (0.7 * req.depth_gamma)) * vertical_weight
                    if rad_sq <= 1.0:
                        base_depth *= (1.0 + 0.3 * np.sqrt(1.0 - rad_sq))
                    half_depth = int(round(base_depth))
                    half_depth = max(1, min(max_d // 2, half_depth))

                    z_offset = int(round(r_param.z_center_offset))
                    z_start, z_end = get_z_bounds(half_depth, z_offset)

            # Fill voxels in computed Z range
            for z in range(z_start, z_end + 1):
                z_rel = abs(z - center_z) / max(1.0, float(half_depth))
                darken = 1.0 - 0.25 * z_rel
                r_col = int(color[0] * darken)
                g_col = int(color[1] * darken)
                b_col = int(color[2] * darken)

                grid[z, y, x] = [1, r_col, g_col, b_col, 1, r_id, 1, 0]

    # 2. Process Trunk Voxels
    if req.trunk_mode != "none" and np.any(trunk):
        trunk_depth = max(2, min(max_d, req.trunk_depth))

        if req.trunk_mode == "rounded":
            t_ys, t_xs = np.where(trunk)
            min_tx, max_tx = np.min(t_xs), np.max(t_xs)
            cx_t = (min_tx + max_tx) / 2.0
            rx_t = max(1.0, (max_tx - min_tx) / 2.0)

        for y in range(h):
            for x in range(w):
                if trunk[y, x]:
                    color = rgba[y, x, :3]

                    if req.trunk_mode == "prismatic":
                        half_d = trunk_depth // 2
                    elif req.trunk_mode == "rounded":
                        dx = (x - cx_t) / rx_t
                        rad_sq = dx*dx
                        half_d = int(round((trunk_depth / 2.0) * np.sqrt(max(0.1, 1.0 - rad_sq))))
                        half_d = max(1, half_d)
                    else:  # "extruded" default
                        half_d = trunk_depth // 2

                    z_t_start, z_t_end = get_z_bounds(half_d)

                    for z in range(z_t_start, z_t_end + 1):
                        if grid[z, y, x, 0] == 1 and grid[z, y, x, 4] == 1:
                            continue

                        darken = req.trunk_color_darken if z != center_z else 1.0
                        r_col = int(color[0] * darken)
                        g_col = int(color[1] * darken)
                        b_col = int(color[2] * darken)

                        grid[z, y, x] = [1, r_col, g_col, b_col, 2, 0, 1, 0]

    # 3. Apply Manual Voxel Edits
    manual_locked_count = 0
    if req.manual_edits:
        for v in req.manual_edits.get("added", []):
            x, y, z = v.get("x"), v.get("y"), v.get("z")
            if 0 <= x < w and 0 <= y < h and 0 <= z < max_d:
                r = v.get("r", 100)
                g = v.get("g", 150)
                b = v.get("b", 80)
                mat_id = 1 if v.get("material") == "foliage" else 2
                grid[z, y, x] = [1, r, g, b, mat_id, v.get("region_id", 0), 0, 1]
                manual_locked_count += 1

        for v in req.manual_edits.get("removed", []):
            x, y, z = v.get("x"), v.get("y"), v.get("z")
            if 0 <= x < w and 0 <= y < h and 0 <= z < max_d:
                grid[z, y, x] = [0, 0, 0, 0, 0, 0, 0, 1]

    # 4. Assemble output voxel list
    voxel_list = []
    foliage_count = 0
    trunk_count = 0

    zs, ys, xs = np.where(grid[:, :, :, 0] == 1)
    for z, y, x in zip(zs, ys, xs):
        vdata = grid[z, y, x]
        mat_str = "foliage" if vdata[4] == 1 else ("trunk" if vdata[4] == 2 else "shadow")
        if vdata[4] == 1:
            foliage_count += 1
        elif vdata[4] == 2:
            trunk_count += 1

        voxel_list.append({
            "x": int(x),
            "y": int(y),
            "z": int(z),
            "r": int(vdata[1]),
            "g": int(vdata[2]),
            "b": int(vdata[3]),
            "material": mat_str,
            "region_id": int(vdata[5]),
            "generated": bool(vdata[6] == 1),
            "manually_locked": bool(vdata[7] == 1)
        })

    exec_time = (time.time() - start_time) * 1000.0
    stats = VolumeStats(
        width=w,
        height=h,
        depth=max_d,
        total_voxels=len(voxel_list),
        foliage_voxels=foliage_count,
        trunk_voxels=trunk_count,
        manually_locked_count=manual_locked_count,
        generation_time_ms=round(exec_time, 2)
    )

    return grid, stats, voxel_list
