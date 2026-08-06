from pathlib import Path
from typing import List, Dict, Any, Tuple, Optional
from PIL import Image
import numpy as np
import io

from app.config import BASE_DIR
from app.services.image_loader import parse_jasc_pal, validate_safe_path

# Root path for data/tilesets in the repository
DATA_TILESETS_DIR = BASE_DIR.parent.parent / "data" / "tilesets"

def list_repository_tilesets() -> List[Dict[str, Any]]:
    """
    Scans the repository data/tilesets directory for all available primary and secondary tilesets.
    Returns list of dicts: [{ id, name, category, palettes: [...] }]
    """
    if not DATA_TILESETS_DIR.exists():
        return []

    tilesets = []
    for category_dir in [DATA_TILESETS_DIR / "primary", DATA_TILESETS_DIR / "secondary"]:
        if not category_dir.exists():
            continue
        for ts_dir in sorted(category_dir.iterdir()):
            if ts_dir.is_dir() and (ts_dir / "tiles.png").exists():
                pal_dir = ts_dir / "palettes"
                palettes = []
                if pal_dir.exists():
                    palettes = sorted([f.name for f in pal_dir.glob("*.pal")])
                
                rel_id = f"{category_dir.name}/{ts_dir.name}"
                tilesets.append({
                    "id": rel_id,
                    "name": ts_dir.name,
                    "category": category_dir.name,
                    "palettes": palettes
                })
    return tilesets

def render_repo_tileset(tileset_id: str, palette_name: str = "02.pal") -> Tuple[bytes, List[Tuple[int, int, int]]]:
    """
    Loads tiles.png and palette_name for tileset_id from repo, remaps 4bpp indices,
    and returns (png_bytes, palette_colors_list).
    """
    parts = tileset_id.replace("\\", "/").split("/")
    if len(parts) != 2 or parts[0] not in ("primary", "secondary"):
        raise ValueError(f"Invalid tileset_id: {tileset_id}")

    ts_dir = DATA_TILESETS_DIR / parts[0] / parts[1]
    ts_png_path = validate_safe_path(str(ts_dir / "tiles.png"))
    
    pal_path = ts_dir / "palettes" / palette_name
    if not pal_path.exists():
        # Fallback to 00.pal if requested palette not found
        pal_path = ts_dir / "palettes" / "00.pal"
    
    if not pal_path.exists():
        raise FileNotFoundError(f"Palette file not found for tileset {tileset_id}")

    pal_path = validate_safe_path(str(pal_path))
    pal_colors = parse_jasc_pal(pal_path.read_text(encoding="utf-8"))

    img = Image.open(ts_png_path)
    indices = np.array(img, dtype=np.uint8)

    if indices.ndim == 3:
        indices = (indices[:, :, 0] // 16).astype(np.uint8)

    h, w = indices.shape
    rgba = np.zeros((h, w, 4), dtype=np.uint8)

    for i, col in enumerate(pal_colors[:16]):
        mask = (indices == i)
        rgba[mask, 0] = col[0]
        rgba[mask, 1] = col[1]
        rgba[mask, 2] = col[2]
        rgba[mask, 3] = 0 if i == 0 else 255

    out_img = Image.fromarray(rgba, mode="RGBA")
    buf = io.BytesIO()
    out_img.save(buf, format="PNG")

    return buf.getvalue(), pal_colors[:16]
