import numpy as np
from app.services.distance_transform import compute_distance_transform

def test_distance_transform_basic():
    # 10x10 square mask
    mask = np.zeros((10, 10), dtype=np.uint8)
    mask[2:8, 2:8] = 1

    raw, norm, stats = compute_distance_transform(mask)

    assert stats["foliage_pixels"] == 36
    assert stats["max_distance"] >= 2.0
    # Center pixel should have maximum distance
    assert raw[4, 4] > raw[2, 2]
    assert norm[4, 4] == 1.0
