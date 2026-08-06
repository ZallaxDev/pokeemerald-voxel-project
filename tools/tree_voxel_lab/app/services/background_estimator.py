import numpy as np
from scipy.ndimage import label, binary_fill_holes
from typing import List, Tuple, Optional, Dict, Any

def color_distance_rgb(c1: np.ndarray, c2: np.ndarray, perceptual: bool = True) -> np.ndarray:
    """
    Computes Euclidean or weighted perceptual color distance between RGB colors.
    """
    diff = c1.astype(np.float32) - c2.astype(np.float32)
    if perceptual:
        # Perceptual RGB weighting (weighted Euclidean distance)
        r_mean = (c1[..., 0].astype(np.float32) + c2[..., 0].astype(np.float32)) / 2.0
        weight_r = 2.0 + r_mean / 256.0
        weight_g = 4.0
        weight_b = 2.0 + (255.0 - r_mean) / 256.0
        dist_sq = weight_r * (diff[..., 0]**2) + weight_g * (diff[..., 1]**2) + weight_b * (diff[..., 2]**2)
        return np.sqrt(dist_sq)
    else:
        return np.sqrt(np.sum(diff**2, axis=-1))

def estimate_background_mask(
    rgba_img: np.ndarray,
    method: str = "color",
    bg_colors: List[List[int]] = [],
    bg_tile: Optional[np.ndarray] = None,
    tile_offset_x: int = 0,
    tile_offset_y: int = 0,
    tolerance: float = 30.0,
    perceptual: bool = True,
    connectivity: int = 4,
    include_alpha: bool = True,
    remove_small: int = 5
) -> Tuple[np.ndarray, np.ndarray]:
    """
    Estimates background mask and difference map from RGBA image.
    Returns (bg_mask_bool_2d, diff_map_float_2d).
    bg_mask: True for background, False for foreground.
    """
    h, w, c = rgba_img.shape
    rgb = rgba_img[:, :, :3]
    alpha = rgba_img[:, :, 3]

    bg_mask = np.zeros((h, w), dtype=bool)
    diff_map = np.zeros((h, w), dtype=np.float32)

    # 1. Alpha channel check if include_alpha
    if include_alpha:
        bg_mask |= (alpha < 10)

    if method in ("color", "samples"):
        if not bg_colors:
            # Default fallback: pick top-left pixel color as background sample
            bg_colors = [rgb[0, 0].tolist()]

        min_dists = np.full((h, w), 999999.0, dtype=np.float32)
        for target_color in bg_colors:
            target_arr = np.array(target_color[:3], dtype=np.uint8)
            dists = color_distance_rgb(rgb, target_arr, perceptual=perceptual)
            min_dists = np.minimum(min_dists, dists)

        diff_map = min_dists
        bg_mask |= (min_dists <= tolerance)

    elif method == "bg_tile":
        if bg_tile is None or bg_tile.size == 0:
            # Fallback to color 0,0
            bg_colors = [rgb[0, 0].tolist()]
            return estimate_background_mask(rgba_img, method="color", bg_colors=bg_colors, tolerance=tolerance)

        th, tw = bg_tile.shape[:2]
        t_rgb = bg_tile[:, :, :3]

        # Tile background across canvas with phase offset
        tiled_bg = np.zeros((h, w, 3), dtype=np.uint8)
        for y in range(h):
            for x in range(w):
                ty = (y - tile_offset_y) % th
                tx = (x - tile_offset_x) % tw
                tiled_bg[y, x] = t_rgb[ty, tx]

        dists = color_distance_rgb(rgb, tiled_bg, perceptual=perceptual)
        diff_map = dists
        bg_mask |= (dists <= tolerance)

    elif method == "floodfill":
        # Flood fill from image borders
        visited = np.zeros((h, w), dtype=bool)
        start_color = rgb[0, 0]

        # Seed border pixels
        seeds = []
        for x in range(w):
            seeds.append((0, x))
            seeds.append((h - 1, x))
        for y in range(h):
            seeds.append((y, 0))
            seeds.append((y, w - 1))

        stack = [p for p in seeds if not visited[p]]
        for p in stack:
            visited[p] = True

        neighbors = [(-1, 0), (1, 0), (0, -1), (0, 1)]
        if connectivity == 8:
            neighbors.extend([(-1, -1), (-1, 1), (1, -1), (1, 1)])

        while stack:
            cy, cx = stack.pop()
            curr_c = rgb[cy, cx]

            for dy, dx in neighbors:
                ny, nx = cy + dy, cx + dx
                if 0 <= ny < h and 0 <= nx < w and not visited[ny, nx]:
                    d = color_distance_rgb(rgb[ny:ny+1, nx:nx+1], curr_c, perceptual=perceptual)[0, 0]
                    if d <= tolerance:
                        visited[ny, nx] = True
                        stack.append((ny, nx))

        bg_mask |= visited
        diff_map = bg_mask.astype(np.float32) * 255.0

    # Morphological cleaning of noise
    if remove_small > 0:
        struct = np.ones((3, 3), dtype=int) if connectivity == 8 else None
        labeled, num_features = label(bg_mask, structure=struct)
        for i in range(1, num_features + 1):
            comp = (labeled == i)
            if np.sum(comp) < remove_small:
                bg_mask[comp] = False

    return bg_mask, diff_map
