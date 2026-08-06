from app.services.vox_exporter import export_to_vox_bytes

def test_vox_exporter_format():
    voxels = [
        {"x": 0, "y": 0, "z": 0, "r": 255, "g": 0, "b": 0, "material": "foliage", "region_id": 1},
        {"x": 1, "y": 1, "z": 1, "r": 0, "g": 255, "b": 0, "material": "foliage", "region_id": 1}
    ]

    vox_bytes = export_to_vox_bytes(voxels, 8, 8, 8)
    assert vox_bytes.startswith(b"VOX ")
    assert b"SIZE" in vox_bytes
    assert b"XYZI" in vox_bytes
    assert b"RGBA" in vox_bytes
