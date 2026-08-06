from app.models.project import ProjectData, TileReference
from app.models.seeds import SeedItem

def test_project_serialization_roundtrip():
    project = ProjectData(
        grid_width=4,
        grid_height=4,
        tile_size=16,
        tiles=[TileReference(tile_x=0, tile_y=0, comp_x=1, comp_y=1)],
        seeds=[SeedItem(id=1, x=5, y=5, radius=3.0, locked=True)]
    )

    data_dict = project.model_dump()
    reconstructed = ProjectData(**data_dict)

    assert reconstructed.grid_width == 4
    assert len(reconstructed.tiles) == 1
    assert reconstructed.tiles[0].comp_x == 1
    assert reconstructed.seeds[0].locked is True
