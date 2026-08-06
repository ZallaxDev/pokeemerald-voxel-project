from app.services.repository_tilesets import list_repository_tilesets, render_repo_tileset

def test_list_repository_tilesets():
    tilesets = list_repository_tilesets()
    assert len(tilesets) > 0
    # primary/general should exist
    general = next((t for t in tilesets if t["id"] == "primary/general"), None)
    assert general is not None
    assert "02.pal" in general["palettes"]

def test_render_repo_tileset():
    png_bytes, colors = render_repo_tileset("primary/general", "02.pal")
    assert len(png_bytes) > 0
    assert len(colors) == 16
    # 02.pal should contain green leaf colors (e.g. RGB(180, 255, 131))
    assert (180, 255, 131) in colors
