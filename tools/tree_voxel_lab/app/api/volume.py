from fastapi import APIRouter, HTTPException
from app.models.volume import GenerateVolumeRequest
from app.services.voxel_generator import generate_voxel_volume

router = APIRouter(prefix="/api", tags=["volume"])

@router.post("/generate-volume")
async def generate_volume_endpoint(req: GenerateVolumeRequest):
    try:
        grid, stats, voxel_list = generate_voxel_volume(req)
        return {
            "stats": stats.model_dump(),
            "voxels": voxel_list
        }
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))
