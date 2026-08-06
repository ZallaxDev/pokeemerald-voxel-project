from fastapi import APIRouter, HTTPException, UploadFile, File, Form
from fastapi.responses import Response
import numpy as np
import io
from PIL import Image
from typing import List, Optional
import json

from app.services.image_loader import parse_jasc_pal

router = APIRouter(prefix="/api/tileset", tags=["tileset"])

@router.post("/apply-palette")
async def apply_palette(
    tileset_file: UploadFile = File(...),
    palette_file: Optional[UploadFile] = File(None),
    palette_colors_json: Optional[str] = Form(None)
):
    """
    Applies a JASC-PAL palette to an indexed GBA 4bpp tileset PNG.
    Returns the recolored RGBA PNG.
    """
    try:
        ts_content = await tileset_file.read()
        img = Image.open(io.BytesIO(ts_content))

        # Read palette colors
        colors = []
        if palette_file:
            pal_content = (await palette_file.read()).decode("utf-8", errors="ignore")
            colors = parse_jasc_pal(pal_content)
        elif palette_colors_json:
            colors = json.loads(palette_colors_json)
        else:
            raise ValueError("No palette provided")

        if len(colors) < 16:
            raise ValueError("Palette must contain at least 16 RGB colors")

        # Extract 4bpp index array
        indices = np.array(img, dtype=np.uint8)
        if indices.ndim == 3:
            # If image was converted to RGB, map intensity to index 0..15
            indices = (indices[:, :, 0] // 16).astype(np.uint8)

        h, w = indices.shape
        rgba = np.zeros((h, w, 4), dtype=np.uint8)

        for i, col in enumerate(colors[:16]):
            mask = (indices == i)
            rgba[mask, 0] = col[0]
            rgba[mask, 1] = col[1]
            rgba[mask, 2] = col[2]
            # Index 0 in GBA tilesets is transparent background
            rgba[mask, 3] = 0 if i == 0 else 255

        out_img = Image.fromarray(rgba, mode="RGBA")
        buf = io.BytesIO()
        out_img.save(buf, format="PNG")

        return Response(content=buf.getvalue(), media_type="image/png")
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))
