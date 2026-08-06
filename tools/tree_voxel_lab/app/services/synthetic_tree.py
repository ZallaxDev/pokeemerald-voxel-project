import numpy as np
from PIL import Image
from pathlib import Path
from app.config import EXAMPLES_DIR

def generate_synthetic_tree_image() -> Image.Image:
    """
    Generates a 64x64 pixel art tree image with grass background,
    multiple foliage lobes, a central hole, a trunk, a projected shadow,
    and multi-segment rows (separated foliage parts).
    Uses synthetic non-pokemon palette.
    """
    w, h = 64, 64
    img_data = np.zeros((h, w, 4), dtype=np.uint8)

    # 1. Background Grass (R: 120, G: 190, B: 80) with subtle pattern
    for y in range(h):
        for x in range(w):
            var = (x * 7 + y * 13) % 15
            img_data[y, x] = [115 + var, 185 + var, 75 + var, 255]

    # Colors
    LEAF_DARK = np.array([30, 95, 45, 255], dtype=np.uint8)
    LEAF_MID = np.array([45, 140, 60, 255], dtype=np.uint8)
    LEAF_LIGHT = np.array([80, 190, 85, 255], dtype=np.uint8)
    TRUNK_DARK = np.array([75, 45, 25, 255], dtype=np.uint8)
    TRUNK_LIGHT = np.array([115, 70, 35, 255], dtype=np.uint8)
    SHADOW_COLOR = np.array([40, 105, 50, 255], dtype=np.uint8)

    # 2. Projected shadow on grass (bottom right of trunk/crown)
    for y in range(48, 58):
        for x in range(16, 48):
            dx = (x - 32) / 14.0
            dy = (y - 53) / 5.0
            if dx*dx + dy*dy <= 1.0:
                img_data[y, x] = SHADOW_COLOR

    # 3. Trunk (y: 36..54, x: 28..35)
    for y in range(36, 55):
        for x in range(28, 36):
            if x in (28, 35):
                img_data[y, x] = TRUNK_DARK
            else:
                img_data[y, x] = TRUNK_LIGHT

    # Helper for filled circles with shading
    def draw_lobe(cx, cy, rx, ry, is_separate=False):
        for y in range(cy - ry, cy + ry + 1):
            for x in range(cx - rx, cx + rx + 1):
                if 0 <= x < w and 0 <= y < h:
                    dx = (x - cx) / float(rx)
                    dy = (y - cy) / float(ry)
                    if dx * dx + dy * dy <= 1.0:
                        if dy < -0.3 and dx < 0.0:
                            col = LEAF_LIGHT
                        elif dy > 0.4 or dx > 0.4:
                            col = LEAF_DARK
                        else:
                            col = LEAF_MID
                        img_data[y, x] = col

    # 4. Lobe 1 (Top Left): cx=24, cy=18, rx=12, ry=10
    draw_lobe(24, 18, 12, 10)

    # Lobe 2 (Top Right): cx=40, cy=18, rx=11, ry=9
    draw_lobe(40, 18, 11, 9)

    # Lobe 3 (Bottom Left main): cx=20, cy=30, rx=13, ry=11
    draw_lobe(20, 30, 13, 11)

    # Lobe 4 (Bottom Right main): cx=44, cy=30, rx=12, ry=10
    draw_lobe(44, 30, 12, 10)

    # Lobe 5 (Center Crown): cx=32, cy=24, rx=14, ry=12
    draw_lobe(32, 24, 14, 12)

    # 5. Multi-segment row / Separate foliage branch on right (cx=54, cy=22, rx=5, ry=5)
    draw_lobe(54, 22, 5, 5, is_separate=True)

    # 6. Central Hole in crown (y: 23..26, x: 30..33)
    for y in range(23, 27):
        for x in range(30, 34):
            var = (x * 7 + y * 13) % 15
            img_data[y, x] = [115 + var, 185 + var, 75 + var, 255]

    img = Image.fromarray(img_data, mode="RGBA")
    output_path = EXAMPLES_DIR / "synthetic-tree.png"
    img.save(output_path, "PNG")
    return img
