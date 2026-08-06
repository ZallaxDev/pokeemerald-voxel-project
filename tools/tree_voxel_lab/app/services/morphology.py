import numpy as np
from scipy.ndimage import (
    binary_dilation,
    binary_erosion,
    binary_opening,
    binary_closing,
    binary_fill_holes,
    label
)

def apply_morphology(
    mask_2d: np.ndarray,
    op: str,
    kernel_size: int = 3,
    iterations: int = 1,
    min_size: int = 10,
    max_hole_size: int = 10,
    connectivity: int = 4
) -> np.ndarray:
    """
    Applies morphological operation on binary 2D mask.
    mask_2d: boolean or 0/1 array.
    Returns uint8 2D mask (0 or 1).
    """
    bool_mask = mask_2d.astype(bool)
    struct = np.ones((kernel_size, kernel_size), dtype=bool) if connectivity == 8 else None

    if op == "dilate":
        res = binary_dilation(bool_mask, structure=struct, iterations=iterations)
    elif op == "erode":
        res = binary_erosion(bool_mask, structure=struct, iterations=iterations)
    elif op == "open":
        res = binary_opening(bool_mask, structure=struct)
    elif op == "close":
        res = binary_closing(bool_mask, structure=struct)
    elif op == "fill_holes":
        # Fill holes with max size constraint if needed
        filled = binary_fill_holes(bool_mask)
        holes = filled & (~bool_mask)
        if max_hole_size > 0:
            labeled_holes, num_h = label(holes)
            res = bool_mask.copy()
            for i in range(1, num_h + 1):
                comp = (labeled_holes == i)
                if np.sum(comp) <= max_hole_size:
                    res |= comp
        else:
            res = filled
    elif op == "remove_small":
        labeled, num_f = label(bool_mask)
        res = bool_mask.copy()
        for i in range(1, num_f + 1):
            comp = (labeled == i)
            if np.sum(comp) < min_size:
                res[comp] = False
    else:
        res = bool_mask

    return res.astype(np.uint8)
