from typing import List, Optional, Dict, Any
from pydantic import BaseModel
from app.models.seeds import SeedItem
from app.models.volume import RegionParam

class ProjectMeta(BaseModel):
    version: str = "1.0.0"
    tool_version: str = "1.0.0"
    created_at: str = ""
    name: str = "Untitled Tree Voxel Project"
    notes: str = ""

class TileReference(BaseModel):
    tileset_name: str = ""
    tile_x: int = 0
    tile_y: int = 0
    comp_x: int = 0
    comp_y: int = 0
    flip_h: bool = False
    flip_v: bool = False
    rot: int = 0

class ProjectData(BaseModel):
    meta: ProjectMeta = ProjectMeta()
    grid_width: int = 4
    grid_height: int = 4
    tile_size: int = 16
    tiles: List[TileReference] = []
    foliage_mask: Optional[List[List[int]]] = None
    trunk_mask: Optional[List[List[int]]] = None
    shadow_mask: Optional[List[List[int]]] = None
    ignore_mask: Optional[List[List[int]]] = None
    background_method: str = "color"
    bg_colors: List[List[int]] = []
    seeds: List[SeedItem] = []
    regions_map: Optional[List[List[int]]] = None
    region_params: Dict[int, RegionParam] = {}
    preset: str = "diorama_balanced"
    max_depth: int = 24
    generation_mode: str = "hybrid"
    manual_voxels: Dict[str, Any] = {}
