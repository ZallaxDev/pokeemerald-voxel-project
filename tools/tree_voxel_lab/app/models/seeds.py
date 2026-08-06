from typing import List, Optional
from pydantic import BaseModel

class SeedItem(BaseModel):
    id: int
    x: int
    y: int
    weight: float = 1.0
    radius: float = 5.0
    locked: bool = False
    region_id: Optional[int] = None

class DetectSeedsRequest(BaseModel):
    foliage_mask: List[List[int]]
    distance_map: Optional[List[List[float]]] = None
    min_distance: int = 4
    min_prominence: float = 0.5
    max_seeds: int = 16
    min_radius: float = 2.0
    pre_smooth_sigma: float = 0.0
    locked_seeds: List[SeedItem] = []

class WatershedRequest(BaseModel):
    foliage_mask: List[List[int]]
    seeds: List[SeedItem]
    distance_map: Optional[List[List[float]]] = None
    connectivity: int = 4
    manual_overrides: Optional[List[List[int]]] = None

class RegionMeta(BaseModel):
    id: int
    centroid: List[float]
    area: int
    bbox: List[int]  # [min_y, min_x, max_y, max_x]
    color: str
