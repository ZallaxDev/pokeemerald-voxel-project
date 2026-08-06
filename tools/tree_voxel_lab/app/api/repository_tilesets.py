from fastapi import APIRouter, HTTPException, Query, Response
from typing import List, Dict, Any
from app.services.repository_tilesets import list_repository_tilesets, render_repo_tileset

router = APIRouter(prefix="/api/repository", tags=["repository"])

@router.get("/tilesets")
async def get_repository_tilesets():
    """
    Returns list of all available GBA tilesets in the game repository.
    """
    try:
        tilesets = list_repository_tilesets()
        return {"tilesets": tilesets, "count": len(tilesets)}
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))

@router.get("/tileset-image")
async def get_repository_tileset_image(
    tileset_id: str = Query("primary/general"),
    palette: str = Query("02.pal")
):
    """
    Returns recolored RGBA tileset PNG with specified palette applied.
    """
    try:
        png_bytes, _ = render_repo_tileset(tileset_id, palette)
        return Response(content=png_bytes, media_type="image/png")
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))

@router.get("/palette-colors")
async def get_repository_palette_colors(
    tileset_id: str = Query("primary/general"),
    palette: str = Query("02.pal")
):
    """
    Returns the 16 RGB colors of specified palette for UI swatches.
    """
    try:
        _, colors = render_repo_tileset(tileset_id, palette)
        return {"palette": palette, "colors": colors}
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))
