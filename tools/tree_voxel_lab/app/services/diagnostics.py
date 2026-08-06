import numpy as np
from PIL import Image
import io
import zipfile
from typing import Dict, Any, Tuple

def create_diagnostic_preview(
    rgba_comp: np.ndarray,
    foliage_mask: np.ndarray,
    regions_map: np.ndarray,
    distance_map: np.ndarray
) -> bytes:
    """
    Renders a 4-quadrant diagnostic PNG image showing original composition,
    foliage mask, distance map, and region segmentation.
    """
    h, w, _ = rgba_comp.shape
    canvas = np.zeros((h * 2, w * 2, 4), dtype=np.uint8)

    # Q1 (Top-Left): Original composition
    canvas[0:h, 0:w] = rgba_comp

    # Q2 (Top-Right): Foliage mask (white on black)
    f_vis = np.zeros((h, w, 4), dtype=np.uint8)
    f_vis[foliage_mask.astype(bool)] = [255, 255, 255, 255]
    f_vis[~foliage_mask.astype(bool)] = [0, 0, 0, 255]
    canvas[0:h, w:w*2] = f_vis

    # Q3 (Bottom-Left): Distance map (heat gradient)
    d_vis = np.zeros((h, w, 4), dtype=np.uint8)
    max_d = np.max(distance_map) if np.max(distance_map) > 0 else 1.0
    norm_d = (distance_map / max_d * 255.0).astype(np.uint8)
    d_vis[:, :, 0] = norm_d  # Red channel heat
    d_vis[:, :, 1] = (norm_d * 0.5).astype(np.uint8)
    d_vis[:, :, 2] = 255 - norm_d
    d_vis[:, :, 3] = 255
    canvas[h:h*2, 0:w] = d_vis

    # Q4 (Bottom-Right): Region Map
    r_vis = np.zeros((h, w, 4), dtype=np.uint8)
    r_vis[:, :, 3] = 255
    for r_id in np.unique(regions_map):
        if r_id == 0:
            continue
        mask_r = (regions_map == r_id)
        # Assign distinct color per region
        r_vis[mask_r, 0] = (r_id * 70) % 255
        r_vis[mask_r, 1] = (r_id * 130) % 255
        r_vis[mask_r, 2] = (r_id * 200) % 255
    canvas[h:h*2, w:w*2] = r_vis

    img = Image.fromarray(canvas, mode="RGBA")
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return buf.getvalue()

def build_diagnostic_zip(
    rgba_comp: np.ndarray,
    foliage_mask: np.ndarray,
    trunk_mask: np.ndarray,
    shadow_mask: np.ndarray,
    distance_map: np.ndarray,
    regions_map: np.ndarray,
    stats_json: str
) -> bytes:
    """
    Bundles all diagnostic PNG masks and metrics JSON into a single downloadable ZIP file.
    """
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        def add_png(name: str, arr: np.ndarray):
            img = Image.fromarray(arr)
            b = io.BytesIO()
            img.save(b, format="PNG")
            zf.writestr(name, b.getvalue())

        add_png("source_composition.png", rgba_comp)
        
        f_img = (foliage_mask * 255).astype(np.uint8)
        add_png("foliage_mask.png", f_img)
        
        t_img = (trunk_mask * 255).astype(np.uint8) if trunk_mask is not None else np.zeros_like(f_img)
        add_png("trunk_mask.png", t_img)

        s_img = (shadow_mask * 255).astype(np.uint8) if shadow_mask is not None else np.zeros_like(f_img)
        add_png("shadow_mask.png", s_img)

        max_d = np.max(distance_map) if np.max(distance_map) > 0 else 1.0
        d_img = (distance_map / max_d * 255.0).astype(np.uint8)
        add_png("distance_map.png", d_img)

        zf.writestr("diagnostics_summary.json", stats_json)

    return buf.getvalue()
