import struct
from typing import List, Dict, Any, Tuple

def export_to_vox_bytes(voxels: List[Dict[str, Any]], width: int, height: int, depth: int) -> bytes:
    """
    Generates binary MagicaVoxel .vox file data.
    """
    if not voxels:
        raise ValueError("Cannot export empty voxel list to .vox")

    # Map colors to 256 palette indices (Index 1-255, Index 255 default black)
    palette = []
    color_to_idx = {}
    
    # Pre-populate palette from voxel colors (up to 255 unique colors)
    for v in voxels:
        col = (v["r"], v["g"], v["b"], 255)
        if col not in color_to_idx:
            if len(palette) < 255:
                idx = len(palette) + 1  # 1-indexed in .vox palette
                color_to_idx[col] = idx
                palette.append(col)
            else:
                # Map to closest existing palette color
                closest_idx = 1
                min_d = 999999
                for p_idx, p_col in enumerate(palette, start=1):
                    d = (col[0]-p_col[0])**2 + (col[1]-p_col[1])**2 + (col[2]-p_col[2])**2
                    if d < min_d:
                        min_d = d
                        closest_idx = p_idx
                color_to_idx[col] = closest_idx

    # Build SIZE chunk data
    # MagicaVoxel coordinates: X, Y (depth), Z (height)
    size_content = struct.pack("<iii", width, depth, height)
    size_chunk = b"SIZE" + struct.pack("<ii", len(size_content), 0) + size_content

    # Build XYZI chunk data
    xyzi_body = bytearray()
    xyzi_body.extend(struct.pack("<i", len(voxels)))
    for v in voxels:
        # Convert coords to MagicaVoxel axis orientation
        vx = max(0, min(255, v["x"]))
        vy = max(0, min(255, v["z"]))  # Depth -> MagicaVoxel Y
        vz = max(0, min(255, height - 1 - v["y"]))  # Invert Y height -> MagicaVoxel Z
        c_idx = color_to_idx.get((v["r"], v["g"], v["b"], 255), 1)
        xyzi_body.extend(struct.pack("BBBB", vx, vy, vz, c_idx))

    xyzi_chunk = b"XYZI" + struct.pack("<ii", len(xyzi_body), 0) + xyzi_body

    # Build RGBA palette chunk
    rgba_body = bytearray()
    for i in range(256):
        if i < len(palette):
            r, g, b, a = palette[i]
            rgba_body.extend(struct.pack("BBBB", r, g, b, a))
        else:
            rgba_body.extend(struct.pack("BBBB", 0, 0, 0, 255))

    rgba_chunk = b"RGBA" + struct.pack("<ii", len(rgba_body), 0) + rgba_body

    # Build MAIN chunk containing SIZE, XYZI, and RGBA
    main_children = size_chunk + xyzi_chunk + rgba_chunk
    main_chunk = b"MAIN" + struct.pack("<ii", 0, len(main_children)) + main_children

    # File Header: 'VOX ' + version 150
    header = b"VOX " + struct.pack("<i", 150)

    return header + main_chunk
