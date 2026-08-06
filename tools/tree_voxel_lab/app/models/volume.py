from typing import List, Optional, Dict, Any
from pydantic import BaseModel

class RegionParam(BaseModel):
    region_id: int
    depth_scale: float = 1.0
    z_center_offset: float = 0.0
    z_radius_scale: float = 1.0
    flattening: float = 1.0
    hardness: float = 1.0
    overlap: float = 1.0
    priority: int = 1
    front_crop: float = 0.0
    back_crop: float = 0.0
    color_override: Optional[List[int]] = None

class GenerateVolumeRequest(BaseModel):
    composition_rgba: List[List[List[int]]]  # Height x Width x 4
    foliage_mask: Optional[List[List[int]]] = None
    trunk_mask: Optional[List[List[int]]] = None
    shadow_mask: Optional[List[List[int]]] = None
    regions_map: Optional[List[List[int]]] = None
    distance_map: Optional[List[List[float]]] = None
    mode: str = "hybrid"  # "distance", "conical", "ellipsoidal", "tiered", "cylindrical", "hybrid"
    max_depth: int = 24
    min_depth: int = 4
    depth_gamma: float = 1.0
    crown_center_y: float = 0.5  # Vertical position of crown max width (0.1 to 0.9)
    center_dup: int = 0  # Number of duplicated central Z voxels (0 = unified single center slice)
    flat_margin: int = 0  # Width in pixels of flat silhouette margin before Z depth expands (0 to 8px)
    asymmetry: float = 0.0
    trunk_mode: str = "extruded"  # "none", "extruded", "prismatic", "rounded"
    trunk_depth: int = 12
    trunk_color_darken: float = 0.85
    color_mode: str = "nearest_front"
    region_params: Dict[int, RegionParam] = {}
    manual_edits: Optional[Dict[str, Any]] = None

class VoxelItem(BaseModel):
    x: int
    y: int
    z: int
    r: int
    g: int
    b: int
    material: str
    region_id: int
    generated: bool = True
    manually_locked: bool = False

class VolumeStats(BaseModel):
    width: int
    height: int
    depth: int
    total_voxels: int
    foliage_voxels: int
    trunk_voxels: int
    manually_locked_count: int
    generation_time_ms: float
