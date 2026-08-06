from fastapi import APIRouter, HTTPException, UploadFile, File, Form
from fastapi.responses import Response
import numpy as np
import io
from PIL import Image
from typing import List, Optional
import json

from app.services.background_estimator import estimate_background_mask
from app.services.image_loader import load_image_as_rgba

router = APIRouter(prefix="/api", tags=["background"])

@router.post("/analyze-background")
async def analyze_background(
    file: UploadFile = File(...),
    method: str = Form("color"),
    bg_colors_json: str = Form("[]"),
    tolerance: float = Form(30.0),
    perceptual: bool = Form(True),
    connectivity: int = Form(4),
    remove_small: int = Form(5)
):
    try:
        content = await file.read()
        rgba, (w, h) = load_image_as_rgba(content)
        bg_colors = json.loads(bg_colors_json)

        bg_mask, diff_map = estimate_background_mask(
            rgba_img=rgba,
            method=method,
            bg_colors=bg_colors,
            tolerance=tolerance,
            perceptual=perceptual,
            connectivity=connectivity,
            remove_small=remove_small
        )

        # Foliage mask is inverse of background mask
        foliage_mask = (~bg_mask).astype(np.uint8)

        max_d = np.max(diff_map) if np.max(diff_map) > 0 else 1.0
        norm_diff = (diff_map / max_d * 255.0).astype(np.uint8)

        return {
            "width": w,
            "height": h,
            "foliage_mask": foliage_mask.tolist(),
            "bg_mask": bg_mask.astype(np.uint8).tolist(),
            "diff_map": norm_diff.tolist(),
            "bg_pixel_count": int(np.sum(bg_mask)),
            "foliage_pixel_count": int(np.sum(foliage_mask))
        }
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))
