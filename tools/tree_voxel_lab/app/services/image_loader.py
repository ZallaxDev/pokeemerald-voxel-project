from pathlib import Path
from typing import Union, Tuple, List
from PIL import Image
import numpy as np
from app.config import BASE_DIR, MAX_IMAGE_DIMENSION

def validate_safe_path(path_str: str) -> Path:
    """
    Validates that a path stays within allowable directories (no path traversal).
    Only allows reading from product repo paths or temp paths.
    """
    path = Path(path_str).resolve()
    # Path traversal check
    root_repo = BASE_DIR.parent.parent.resolve()
    if not str(path).startswith(str(root_repo)):
        raise ValueError(f"Path traversal access denied: {path_str}")
    if not path.exists():
        raise FileNotFoundError(f"File not found: {path_str}")
    return path

def load_image_as_rgba(source: Union[str, Path, bytes, Image.Image]) -> Tuple[np.ndarray, Tuple[int, int]]:
    """
    Loads any image source (path, bytes, or PIL Image) safely into an RGBA numpy array.
    Returns (rgba_array, (width, height)).
    """
    if isinstance(source, (str, Path)):
        safe_p = validate_safe_path(str(source))
        img = Image.open(safe_p)
    elif isinstance(source, bytes):
        import io
        img = Image.open(io.BytesIO(source))
    elif isinstance(source, Image.Image):
        img = source
    else:
        raise ValueError("Invalid image source type")

    img = img.convert("RGBA")
    w, h = img.size
    if w > MAX_IMAGE_DIMENSION or h > MAX_IMAGE_DIMENSION:
        raise ValueError(f"Image dimensions {w}x{h} exceed maximum allowed {MAX_IMAGE_DIMENSION}x{MAX_IMAGE_DIMENSION}")

    arr = np.array(img, dtype=np.uint8)
    return arr, (w, h)

def parse_jasc_pal(content: str) -> List[Tuple[int, int, int]]:
    """
    Parses a JASC-PAL palette file text content into a list of RGB tuples.
    """
    lines = [l.strip() for l in content.strip().splitlines() if l.strip()]
    if len(lines) < 3 or lines[0] != "JASC-PAL":
        raise ValueError("Invalid JASC-PAL header")
    try:
        count = int(lines[2])
    except ValueError:
        raise ValueError("Invalid JASC-PAL color count")
    
    colors = []
    for line in lines[3:3+count]:
        parts = line.split()
        if len(parts) >= 3:
            r, g, b = int(parts[0]), int(parts[1]), int(parts[2])
            colors.append((r, g, b))
    return colors
