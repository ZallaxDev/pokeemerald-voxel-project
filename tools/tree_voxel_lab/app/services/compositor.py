import numpy as np
from typing import List, Dict, Any, Tuple

def assemble_tile_composition(
    tileset_rgba: np.ndarray,
    tile_size: int,
    grid_cols: int,
    grid_rows: int,
    tiles_data: List[Dict[str, Any]],
    offset_x: int = 0,
    offset_y: int = 0,
    spacing: int = 0
) -> np.ndarray:
    """
    Composes a single full tree composite RGBA image from placed tiles.
    Nearest-neighbor pixel exact assembly. No interpolation.
    """
    comp_w = grid_cols * tile_size
    comp_h = grid_rows * tile_size
    composite = np.zeros((comp_h, comp_w, 4), dtype=np.uint8)

    ts_h, ts_w, _ = tileset_rgba.shape

    for tile in tiles_data:
        tx = tile.get("tile_x", 0)
        ty = tile.get("tile_y", 0)
        cx = tile.get("comp_x", 0)
        cy = tile.get("comp_y", 0)
        flip_h = tile.get("flip_h", False)
        flip_v = tile.get("flip_v", False)
        rot = tile.get("rot", 0)  # 0, 90, 180, 270 degrees

        # Extract source tile from tileset
        src_x = offset_x + tx * (tile_size + spacing)
        src_y = offset_y + ty * (tile_size + spacing)

        if src_x < 0 or src_y < 0 or src_x + tile_size > ts_w or src_y + tile_size > ts_h:
            continue

        tile_patch = tileset_rgba[src_y : src_y + tile_size, src_x : src_x + tile_size].copy()

        # Apply transformations
        if rot == 90:
            tile_patch = np.rot90(tile_patch, k=-1, axes=(0, 1))
        elif rot == 180:
            tile_patch = np.rot90(tile_patch, k=2, axes=(0, 1))
        elif rot == 270:
            tile_patch = np.rot90(tile_patch, k=1, axes=(0, 1))

        if flip_h:
            tile_patch = np.fliplr(tile_patch)
        if flip_v:
            tile_patch = np.flipud(tile_patch)

        # Place onto composite canvas
        dst_x = cx * tile_size
        dst_y = cy * tile_size

        if dst_x >= 0 and dst_y >= 0 and dst_x + tile_size <= comp_w and dst_y + tile_size <= comp_h:
            # Layer tile patch preserving alpha if source tile has transparent pixels
            alpha_src = tile_patch[:, :, 3] / 255.0
            for c in range(3):
                composite[dst_y : dst_y + tile_size, dst_x : dst_x + tile_size, c] = (
                    composite[dst_y : dst_y + tile_size, dst_x : dst_x + tile_size, c] * (1 - alpha_src)
                    + tile_patch[:, :, c] * alpha_src
                ).astype(np.uint8)
            composite[dst_y : dst_y + tile_size, dst_x : dst_x + tile_size, 3] = np.maximum(
                composite[dst_y : dst_y + tile_size, dst_x : dst_x + tile_size, 3],
                tile_patch[:, :, 3]
            )

    return composite
