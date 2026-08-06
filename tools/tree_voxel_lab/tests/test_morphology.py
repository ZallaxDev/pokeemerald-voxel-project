import numpy as np
from app.services.morphology import apply_morphology

def test_dilation_and_erosion():
    # 5x5 mask with single pixel center
    mask = np.zeros((5, 5), dtype=np.uint8)
    mask[2, 2] = 1

    dilated = apply_morphology(mask, op="dilate", kernel_size=3, iterations=1)
    assert np.sum(dilated) > 1
    assert dilated[2, 2] == 1
    assert dilated[1, 2] == 1

    eroded = apply_morphology(dilated, op="erode", kernel_size=3, iterations=1)
    assert eroded[2, 2] == 1
    assert np.sum(eroded) < np.sum(dilated)

def test_fill_holes():
    # 5x5 box with hollow center
    mask = np.ones((5, 5), dtype=np.uint8)
    mask[0, :] = 0
    mask[4, :] = 0
    mask[:, 0] = 0
    mask[:, 4] = 0
    mask[2, 2] = 0  # Hole

    filled = apply_morphology(mask, op="fill_holes", max_hole_size=10)
    assert filled[2, 2] == 1

def test_remove_small():
    mask = np.zeros((10, 10), dtype=np.uint8)
    mask[1, 1] = 1  # Small isolated noise (size 1)
    mask[5:8, 5:8] = 1  # Larger component (size 9)

    cleaned = apply_morphology(mask, op="remove_small", min_size=5)
    assert cleaned[1, 1] == 0
    assert cleaned[6, 6] == 1
