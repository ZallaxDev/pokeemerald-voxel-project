from fastapi import APIRouter, HTTPException
import numpy as np
from typing import List, Dict, Any
from app.models.seeds import DetectSeedsRequest, WatershedRequest
from app.services.distance_transform import compute_distance_transform
from app.services.seed_detector import detect_seed_points
from app.services.watershed_service import run_watershed_segmentation

router = APIRouter(prefix="/api", tags=["segmentation"])

@router.post("/distance-transform")
async def get_distance_transform(payload: Dict[str, Any]):
    try:
        mask_arr = np.array(payload.get("foliage_mask", []), dtype=np.uint8)
        pre_open = payload.get("pre_opening", False)
        pre_close = payload.get("pre_closing", False)

        raw_dist, norm_dist, stats = compute_distance_transform(
            foliage_mask=mask_arr,
            pre_opening=pre_open,
            pre_closing=pre_close
        )

        return {
            "raw_distance": raw_dist.tolist(),
            "norm_distance": norm_dist.tolist(),
            "stats": stats
        }
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))

@router.post("/detect-seeds")
async def detect_seeds_endpoint(req: DetectSeedsRequest):
    try:
        mask_arr = np.array(req.foliage_mask, dtype=np.uint8)
        if req.distance_map:
            dist_arr = np.array(req.distance_map, dtype=np.float32)
        else:
            dist_arr, _, _ = compute_distance_transform(mask_arr)

        seeds = detect_seed_points(
            foliage_mask=mask_arr,
            distance_map=dist_arr,
            min_distance=req.min_distance,
            min_prominence=req.min_prominence,
            max_seeds=req.max_seeds,
            min_radius=req.min_radius,
            locked_seeds=req.locked_seeds
        )

        return {
            "seeds": [s.model_dump() for s in seeds],
            "count": len(seeds)
        }
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))

@router.post("/watershed")
async def watershed_endpoint(req: WatershedRequest):
    try:
        mask_arr = np.array(req.foliage_mask, dtype=np.uint8)
        if req.distance_map:
            dist_arr = np.array(req.distance_map, dtype=np.float32)
        else:
            dist_arr, _, _ = compute_distance_transform(mask_arr)

        manual_arr = np.array(req.manual_overrides, dtype=int) if req.manual_overrides else None

        labels, region_metas = run_watershed_segmentation(
            foliage_mask=mask_arr,
            seeds=req.seeds,
            distance_map=dist_arr,
            connectivity=req.connectivity,
            manual_overrides=manual_arr
        )

        return {
            "regions_map": labels.tolist(),
            "region_count": len(region_metas),
            "regions": [r.model_dump() for r in region_metas]
        }
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))
