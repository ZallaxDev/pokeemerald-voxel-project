import numpy as np
from app.models.seeds import SeedItem
from app.services.distance_transform import compute_distance_transform
from app.services.watershed_service import run_watershed_segmentation

def test_watershed_segmentation():
    # Two separated circular blobs in a 20x20 mask
    mask = np.zeros((20, 20), dtype=np.uint8)
    mask[2:8, 2:8] = 1   # Blob 1
    mask[12:18, 12:18] = 1  # Blob 2

    raw_dist, norm_dist, _ = compute_distance_transform(mask)

    seeds = [
        SeedItem(id=1, x=5, y=5, radius=3.0, locked=True, region_id=1),
        SeedItem(id=2, x=15, y=15, radius=3.0, locked=True, region_id=2)
    ]

    labels, metas = run_watershed_segmentation(mask, seeds, norm_dist)

    assert len(metas) == 2
    assert labels[5, 5] == 1
    assert labels[15, 15] == 2
    assert labels[0, 0] == 0  # Background stays 0
