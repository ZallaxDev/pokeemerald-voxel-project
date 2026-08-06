import numpy as np
from app.models.volume import GenerateVolumeRequest
from app.services.voxel_generator import generate_voxel_volume

def test_geometry_rules():
    # 16x16 composite image with two separate foliage parts and a central hole
    rgba = np.full((16, 16, 4), 255, dtype=np.uint8)
    rgba[:, :, :3] = [40, 140, 50]

    foliage = np.zeros((16, 16), dtype=np.uint8)
    foliage[2:6, 2:6] = 1   # Left lobe
    foliage[2:6, 10:14] = 1  # Right lobe (Multi-segment row!)
    # Middle (x: 6..9) is empty space / hole!

    trunk = np.zeros((16, 16), dtype=np.uint8)
    trunk[10:14, 7:9] = 1

    regions = foliage.copy().astype(int)
    regions[2:6, 10:14] = 2

    dist = np.ones((16, 16), dtype=np.float32)

    req = GenerateVolumeRequest(
        composition_rgba=rgba.tolist(),
        foliage_mask=foliage.tolist(),
        trunk_mask=trunk.tolist(),
        regions_map=regions.tolist(),
        distance_map=dist.tolist(),
        mode="hybrid",
        max_depth=12
    )

    grid, stats, voxels = generate_voxel_volume(req)

    # 1. Check silhouette fidelity: no voxels placed at (y=4, x=8) in hole!
    assert grid[:, 4, 8, 0].sum() == 0

    # 2. Left and right lobes stay separate
    assert grid[:, 4, 4, 0].sum() > 0
    assert grid[:, 4, 12, 0].sum() > 0

    # 3. Trunk present
    assert stats.trunk_voxels > 0

def test_manual_voxels_preservation():
    rgba = np.full((8, 8, 4), 255, dtype=np.uint8)
    foliage = np.ones((8, 8), dtype=np.uint8)
    regions = np.ones((8, 8), dtype=int)
    dist = np.ones((8, 8), dtype=np.float32)

    req = GenerateVolumeRequest(
        composition_rgba=rgba.tolist(),
        foliage_mask=foliage.tolist(),
        regions_map=regions.tolist(),
        distance_map=dist.tolist(),
        max_depth=8,
        manual_edits={"added": [{"x": 1, "y": 1, "z": 1, "r": 255, "g": 0, "b": 0, "material": "foliage"}]}
    )

    grid, stats, voxels = generate_voxel_volume(req)
    assert stats.manually_locked_count == 1
    # Check red color on added voxel
    v_data = grid[1, 1, 1]
    assert v_data[1] == 255 and v_data[2] == 0  # Red color
