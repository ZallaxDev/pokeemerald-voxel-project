import numpy as np
from skimage.feature import peak_local_max
from typing import List
from app.models.seeds import SeedItem

def detect_seed_points(
    foliage_mask: np.ndarray,
    distance_map: np.ndarray,
    min_distance: int = 3,
    min_prominence: float = 0.1,
    max_seeds: int = 16,
    min_radius: float = 0.01,
    locked_seeds: List[SeedItem] = []
) -> List[SeedItem]:
    """
    Detects local maxima in distance map within foliage mask as initial region seeds.
    Auto-normalizes distance map if raw distance values (> 1.0) are passed.
    Preserves locked seeds.
    """
    mask = np.nan_to_num(np.array(foliage_mask, dtype=float), nan=0.0).astype(bool)
    if not np.any(mask):
        return locked_seeds

    dist_filtered = np.nan_to_num(np.array(distance_map, dtype=float), nan=0.0)
    dist_filtered[~mask] = 0.0

    # Auto-normalize if raw distance values are provided
    max_d = float(np.max(dist_filtered))
    if max_d > 1.0:
        dist_filtered = dist_filtered / max_d

    thresh = float(min_prominence)
    peaks = peak_local_max(
        dist_filtered,
        min_distance=max(1, min_distance),
        threshold_abs=thresh,
        num_peaks=max_seeds
    )

    # Fallback: if no peaks found with threshold, pick centroid/maximum of mask
    if len(peaks) == 0 and np.any(mask):
        max_pos = np.unravel_index(np.argmax(dist_filtered), dist_filtered.shape)
        peaks = np.array([[max_pos[0], max_pos[1]]])

    result_seeds: List[SeedItem] = list(locked_seeds)
    locked_coords = {(s.x, s.y) for s in locked_seeds}

    seed_id_counter = max([s.id for s in locked_seeds], default=0) + 1

    for row, col in peaks:
        x, y = int(col), int(row)
        if (x, y) in locked_coords:
            continue

        val = float(dist_filtered[row, col])

        result_seeds.append(
            SeedItem(
                id=seed_id_counter,
                x=x,
                y=y,
                weight=1.0,
                radius=round(max(1.0, val * 10.0), 2),
                locked=False,
                region_id=seed_id_counter
            )
        )
        seed_id_counter += 1

    return result_seeds
