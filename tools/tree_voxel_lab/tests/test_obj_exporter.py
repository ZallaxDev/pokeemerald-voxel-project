from app.services.obj_exporter import export_to_obj_mtl

def test_obj_exporter():
    voxels = [
        {"x": 0, "y": 0, "z": 0, "r": 255, "g": 0, "b": 0, "material": "foliage", "region_id": 1}
    ]

    obj_str, mtl_str = export_to_obj_mtl(voxels, 4, 4, 4)
    assert "mtllib tree_model.mtl" in obj_str
    assert "v " in obj_str
    assert "f " in obj_str
    assert "newmtl " in mtl_str
