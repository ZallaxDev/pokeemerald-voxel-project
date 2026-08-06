from fastapi import FastAPI
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse, Response
from pathlib import Path
import io

from app.config import STATIC_DIR, HOST, PORT
from app.api.background import router as bg_router
from app.api.masks import router as masks_router
from app.api.segmentation import router as seg_router
from app.api.volume import router as volume_router
from app.api.export import router as export_router
from app.api.project import router as project_router
from app.api.tileset import router as tileset_router
from app.api.repository_tilesets import router as repo_tilesets_router
from app.services.synthetic_tree import generate_synthetic_tree_image

app = FastAPI(
    title="Emerald Tree Voxel Lab",
    version="1.0.0",
    docs_url="/docs",
    redoc_url=None
)

# Include API Routers
app.include_router(bg_router)
app.include_router(masks_router)
app.include_router(seg_router)
app.include_router(volume_router)
app.include_router(export_router)
app.include_router(project_router)
app.include_router(tileset_router)
app.include_router(repo_tilesets_router)

# Mount static files
app.mount("/static", StaticFiles(directory=str(STATIC_DIR)), name="static")

@app.get("/api/health")
async def health_check():
    return {
        "status": "ok",
        "service": "Emerald Tree Voxel Lab",
        "version": "1.0.0",
        "host": HOST,
        "port": PORT
    }

@app.get("/api/synthetic-example")
async def get_synthetic_example():
    """
    Returns synthetic test tree PNG as image/png response.
    """
    img = generate_synthetic_tree_image()
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return Response(content=buf.getvalue(), media_type="image/png")

@app.get("/")
async def serve_index():
    index_path = STATIC_DIR / "index.html"
    return FileResponse(index_path)
