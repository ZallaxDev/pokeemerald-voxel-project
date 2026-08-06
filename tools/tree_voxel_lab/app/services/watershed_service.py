import numpy as np
from skimage.segmentation import watershed
from typing import List, Tuple, Dict, Any
from app.models.seeds import SeedItem, RegionMeta

# Distinct color palette for region visualization
REGION_PALETTE = [
    "#FF5722", "#2196F3", "#4CAF50", "#FFEB3B", "#9C27B0",
    "#00BCD4", "#E91E63", "#8BC34A", "#FF9800", "#673AB7",
    "#009688", "#3F51B5", "#CDDC39", "#FFC107", "#795548"
]

def run_watershed_segmentation(
    foliage_mask: np.ndarray,
    seeds: List[SeedItem],
    distance_map: np.ndarray,
    connectivity: int = 4,
    manual_overrides: np.ndarray = None
) -> Tuple[np.ndarray, List[RegionMeta]]:
    """
    Runs marker-based watershed segmentation strictly inside the foliage mask.
    Returns (region_map_2d_int, list_of_region_metadata).
    Region IDs start at 1. 0 is background / non-foliage.
    """
    mask = np.nan_to_num(np.array(foliage_mask, dtype=float), nan=0.0).astype(bool)
    h, w = mask.shape

    if not np.any(mask) or not seeds:
        return np.zeros((h, w), dtype=int), []

    # Prepare seed markers array
    markers = np.zeros((h, w), dtype=int)
    for seed in seeds:
        if 0 <= seed.x < w and 0 <= seed.y < h:
            rid = seed.region_id if seed.region_id is not None else seed.id
            markers[seed.y, seed.x] = rid

    # Invert distance map so watershed treats peaks as basins
    dist = np.nan_to_num(np.array(distance_map, dtype=float), nan=0.0)
    dist_inv = -dist.astype(np.float32)

    # Watershed constrained by foliage mask
    labels = watershed(dist_inv, markers, mask=mask)

    # Apply manual overrides if provided
    if manual_overrides is not None and manual_overrides.shape == (h, w):
        override_mask = (manual_overrides > 0) & mask
        labels[override_mask] = manual_overrides[override_mask]

    # Calculate metadata per region
    unique_ids = [r for r in np.unique(labels) if r > 0]
    region_metas: List[RegionMeta] = []

    for idx, rid in enumerate(unique_ids):
        region_mask = (labels == rid)
        area = int(np.sum(region_mask))
        if area == 0:
            continue

        ys, xs = np.where(region_mask)
        min_y, max_y = int(np.min(ys)), int(np.max(ys))
        min_x, max_x = int(np.min(xs)), int(np.max(xs))

        cy = float(np.mean(ys))
        cx = float(np.mean(xs))

        color = REGION_PALETTE[idx % len(REGION_PALETTE)]

        region_metas.append(
            RegionMeta(
                id=int(rid),
                centroid=[round(cx, 2), round(cy, 2)],
                area=area,
                bbox=[min_y, min_x, max_y, max_x],
                color=color
            )
        )

    return labels.astype(int), region_metas
