import numpy as np
from scipy.ndimage import distance_transform_edt
from typing import Dict, Any, Tuple

def compute_distance_transform(
    foliage_mask: np.ndarray,
    pre_opening: bool = False,
    pre_closing: bool = False,
    kernel_size: int = 3
) -> Tuple[np.ndarray, np.ndarray, Dict[str, Any]]:
    """
    Computes exact Euclidean distance transform on foliage mask.
    Returns (raw_dist_2d_float, norm_dist_2d_float, stats_dict).
    """
    mask = foliage_mask.astype(bool)
    
    if pre_opening:
        from scipy.ndimage import binary_opening
        mask = binary_opening(mask, structure=np.ones((kernel_size, kernel_size)))
    if pre_closing:
        from scipy.ndimage import binary_closing
        mask = binary_closing(mask, structure=np.ones((kernel_size, kernel_size)))

    raw_dist = distance_transform_edt(mask).astype(np.float32)
    max_val = float(np.max(raw_dist)) if np.any(raw_dist) else 1.0
    norm_dist = (raw_dist / max_val) if max_val > 0 else raw_dist

    stats = {
        "max_distance": max_val,
        "mean_distance": float(np.mean(raw_dist[mask])) if np.any(mask) else 0.0,
        "foliage_pixels": int(np.sum(mask))
    }

    return raw_dist, norm_dist, stats
