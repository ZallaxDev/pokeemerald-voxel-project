from fastapi import APIRouter, HTTPException, Response, Body
from typing import Dict, Any, List
import json
import numpy as np

from app.services.vox_exporter import export_to_vox_bytes
from app.services.obj_exporter import export_to_obj_mtl
from app.services.diagnostics import create_diagnostic_preview, build_diagnostic_zip

router = APIRouter(prefix="/api/export", tags=["export"])

@router.post("/vox")
async def export_vox(payload: Dict[str, Any] = Body(...)):
    try:
        voxels = payload.get("voxels", [])
        w = payload.get("width", 32)
        h = payload.get("height", 32)
        d = payload.get("depth", 32)

        vox_data = export_to_vox_bytes(voxels, w, h, d)
        return Response(
            content=vox_data,
            media_type="application/octet-stream",
            headers={"Content-Disposition": "attachment; filename=tree_model.vox"}
        )
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))

@router.post("/obj")
async def export_obj(payload: Dict[str, Any] = Body(...)):
    try:
        voxels = payload.get("voxels", [])
        w = payload.get("width", 32)
        h = payload.get("height", 32)
        d = payload.get("depth", 32)

        obj_str, mtl_str = export_to_obj_mtl(voxels, w, h, d)
        return {
            "obj": obj_str,
            "mtl": mtl_str
        }
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))

@router.post("/diagnostics-zip")
async def export_diagnostics_zip(payload: Dict[str, Any] = Body(...)):
    try:
        rgba = np.array(payload.get("composition_rgba", []), dtype=np.uint8)
        foliage = np.array(payload.get("foliage_mask", []), dtype=bool)
        trunk = np.array(payload.get("trunk_mask", []), dtype=bool) if payload.get("trunk_mask") else None
        shadow = np.array(payload.get("shadow_mask", []), dtype=bool) if payload.get("shadow_mask") else None
        dist_map = np.array(payload.get("distance_map", []), dtype=np.float32)
        regions_map = np.array(payload.get("regions_map", []), dtype=int)
        stats = json.dumps(payload.get("stats", {}), indent=2)

        zip_bytes = build_diagnostic_zip(
            rgba_comp=rgba,
            foliage_mask=foliage,
            trunk_mask=trunk,
            shadow_mask=shadow,
            distance_map=dist_map,
            regions_map=regions_map,
            stats_json=stats
        )

        return Response(
            content=zip_bytes,
            media_type="application/zip",
            headers={"Content-Disposition": "attachment; filename=tree_diagnostics.zip"}
        )
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))
