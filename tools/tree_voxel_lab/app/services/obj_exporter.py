from typing import List, Dict, Any, Tuple

def export_to_obj_mtl(voxels: List[Dict[str, Any]], width: int, height: int, depth: int) -> Tuple[str, str]:
    """
    Generates Wavefront OBJ and MTL text files from voxel list.
    Removes hidden interior faces.
    Returns (obj_text_content, mtl_text_content).
    """
    if not voxels:
        return "# Empty model\n", "# Empty material\n"

    # Map colors to material names
    materials = {}
    mat_count = 0
    
    # 3D occupancy map for culling internal faces
    grid = {}
    for v in voxels:
        key = (v["x"], v["y"], v["z"])
        col = (v["r"], v["g"], v["b"])
        grid[key] = col

        if col not in materials:
            mat_count += 1
            materials[col] = f"mat_{mat_count}"

    obj_lines = ["# Emerald Tree Voxel Lab Exported OBJ", "mtllib tree_model.mtl\n"]
    mtl_lines = ["# Emerald Tree Voxel Lab Exported MTL\n"]

    for col, m_name in materials.items():
        r_f = col[0] / 255.0
        g_f = col[1] / 255.0
        b_f = col[2] / 255.0
        mtl_lines.append(f"newmtl {m_name}")
        mtl_lines.append(f"Kd {r_f:.4f} {g_f:.4f} {b_f:.4f}")
        mtl_lines.append("Ka 0.0000 0.0000 0.0000")
        mtl_lines.append("Ks 0.0000 0.0000 0.0000\n")

    # Cube face definitions (6 directions)
    # Normals: +X, -X, +Y, -Y, +Z, -Z
    faces = [
        # +X face (right)
        {"dir": (1, 0, 0), "quad": [(1, 0, 0), (1, 1, 0), (1, 1, 1), (1, 0, 1)]},
        # -X face (left)
        {"dir": (-1, 0, 0), "quad": [(0, 0, 1), (0, 1, 1), (0, 1, 0), (0, 0, 0)]},
        # +Y face (top)
        {"dir": (0, 1, 0), "quad": [(0, 1, 0), (0, 1, 1), (1, 1, 1), (1, 1, 0)]},
        # -Y face (bottom)
        {"dir": (0, -1, 0), "quad": [(0, 0, 1), (0, 0, 0), (1, 0, 0), (1, 0, 1)]},
        # +Z face (front)
        {"dir": (0, 0, 1), "quad": [(0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)]},
        # -Z face (back)
        {"dir": (0, 0, -1), "quad": [(1, 0, 0), (0, 0, 0), (0, 1, 0), (1, 1, 0)]},
    ]

    vert_count = 1
    current_mat = None

    for (vx, vy, vz), col in grid.items():
        # Invert Y so OBJ standard coordinate system matches expectation (Y up)
        obj_y = height - 1 - vy
        
        m_name = materials[col]

        for f in faces:
            dx, dy, dz = f["dir"]
            neighbor = (vx + dx, vy + dy, vz + dz)
            # Cull internal face if neighbor exists!
            if neighbor in grid:
                continue

            if current_mat != m_name:
                obj_lines.append(f"usemtl {m_name}")
                current_mat = m_name

            quad_v_indices = []
            for qx, qy, qz in f["quad"]:
                px = vx + qx
                py = obj_y + qy
                pz = vz + qz
                obj_lines.append(f"v {px} {py} {pz}")
                quad_v_indices.append(vert_count)
                vert_count += 1

            i1, i2, i3, i4 = quad_v_indices
            obj_lines.append(f"f {i1} {i2} {i3} {i4}")

    return "\n".join(obj_lines), "\n".join(mtl_lines)
