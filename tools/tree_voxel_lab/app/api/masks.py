from fastapi import APIRouter, HTTPException
import numpy as np
import time
from app.models.masks import MaskOpRequest, MaskOpResponse
from app.services.morphology import apply_morphology

router = APIRouter(prefix="/api", tags=["masks"])

@router.post("/clean-mask", response_model=MaskOpResponse)
async def clean_mask(req: MaskOpRequest):
    try:
        t0 = time.time()
        mask_arr = np.array(req.mask, dtype=np.uint8)

        cleaned = apply_morphology(
            mask_2d=mask_arr,
            op=req.op,
            kernel_size=req.kernel_size,
            iterations=req.iterations,
            min_size=req.min_size,
            max_hole_size=req.max_hole_size,
            connectivity=req.connectivity
        )

        exec_time = (time.time() - t0) * 1000.0
        return MaskOpResponse(
            mask=cleaned.tolist(),
            pixel_count=int(np.sum(cleaned)),
            execution_time_ms=round(exec_time, 2),
            message=f"Applied morphological {req.op} successfully"
        )
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))
