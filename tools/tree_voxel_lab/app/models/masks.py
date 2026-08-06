from typing import List, Optional
from pydantic import BaseModel

class MaskOpRequest(BaseModel):
    mask: List[List[int]]  # 2D array of 0/1 or category values
    op: str  # dilate, erode, open, close, fill_holes, remove_small
    kernel_size: int = 3
    iterations: int = 1
    min_size: int = 10
    max_hole_size: int = 10
    connectivity: int = 4

class MaskOpResponse(BaseModel):
    mask: List[List[int]]
    pixel_count: int
    execution_time_ms: float
    message: str = "Success"
