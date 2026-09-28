"""Generate test STL geometries for the CNC feature test loop."""
import trimesh
import numpy as np
import os

OUT_DIR = os.path.join(os.path.dirname(__file__))

def save(mesh, name):
    path = os.path.join(OUT_DIR, f"{name}.stl")
    mesh.export(path, file_type="stl")
    print(f"  {name}.stl  ({len(mesh.faces)} faces, bounds {mesh.bounds[0]} -> {mesh.bounds[1]})")
    return path

# G1: cube 20x20x20 mm, vertical walls (baseline)
def make_cube():
    mesh = trimesh.creation.box(extents=[20, 20, 20])
    mesh.apply_translation([0, 0, 10])  # base on Z=0
    return save(mesh, "cube")

# G2: bowl - revolved open bowl, outer flares outward 5 deg to z=20, then 30 deg lip to z=25
def make_bowl():
    # Build revolution profile (r, z) pairs
    pts = []
    wall = 3.0
    # Base: flat bottom at z=0
    r_base = 25.0
    # 5-degree outward flare from z=0 to z=20
    for z in np.linspace(0, 20, 40):
        r = r_base + z * np.tan(np.radians(5))
        pts.append([r, z])
    # 30-degree lip from z=20 to z=25
    r_at_20 = r_base + 20 * np.tan(np.radians(5))
    for z in np.linspace(20, 25, 20):
        dz = z - 20
        r = r_at_20 + dz * np.tan(np.radians(30))
        pts.append([r, z])
    # Inner wall (offset inward by wall thickness)
    inner_pts = []
    for r, z in reversed(pts):
        inner_pts.append([max(r - wall, 0.5), z])
    # Bottom disk
    profile = np.array(pts + inner_pts)
    # Create by revolution
    angles = np.linspace(0, 2*np.pi, 64, endpoint=False)
    vertices = []
    faces = []
    n_profile = len(profile)
    n_angles = len(angles)
    for i, angle in enumerate(angles):
        for r, z in profile:
            x = r * np.cos(angle)
            y = r * np.sin(angle)
            vertices.append([x, y, z])
    vertices = np.array(vertices)
    # Create faces connecting adjacent profile rings
    for i in range(n_angles):
        i_next = (i + 1) % n_angles
        for j in range(n_profile - 1):
            v0 = i * n_profile + j
            v1 = i * n_profile + j + 1
            v2 = i_next * n_profile + j + 1
            v3 = i_next * n_profile + j
            faces.append([v0, v1, v2])
            faces.append([v0, v2, v3])
    mesh = trimesh.Trimesh(vertices=vertices, faces=np.array(faces))
    mesh.fix_normals()
    return save(mesh, "bowl")

# G3: truncated cone r=20 at z=0 -> r=15 at z=25 (~11 deg inward)
def make_cone():
    # Approximate with cylinder sections
    sections = 50
    angles = np.linspace(0, 2*np.pi, 64, endpoint=False)
    vertices = []
    faces = []
    n_angles = len(angles)
    for i, z in enumerate(np.linspace(0, 25, sections)):
        r = 20 - (5/25) * z  # linear from 20 to 15
        for angle in angles:
            vertices.append([r*np.cos(angle), r*np.sin(angle), z])
    vertices = np.array(vertices)
    for i in range(sections - 1):
        for j in range(n_angles):
            j_next = (j + 1) % n_angles
            v0 = i * n_angles + j
            v1 = (i+1) * n_angles + j
            v2 = (i+1) * n_angles + j_next
            v3 = i * n_angles + j_next
            faces.append([v0, v1, v2])
            faces.append([v0, v2, v3])
    # Cap bottom and top
    bot_center = len(vertices)
    vertices = np.vstack([vertices, [[0, 0, 0]]])
    for j in range(n_angles):
        j_next = (j + 1) % n_angles
        faces.append([bot_center, j_next, j])
    top_center = len(vertices)
    vertices = np.vstack([vertices, [[0, 0, 25]]])
    top_start = (sections-1) * n_angles
    for j in range(n_angles):
        j_next = (j + 1) % n_angles
        faces.append([top_center, top_start + j, top_start + j_next])
    mesh = trimesh.Trimesh(vertices=vertices, faces=np.array(faces))
    mesh.fix_normals()
    return save(mesh, "cone")

# G4: stepped - 30x30x10 base + 20x20x10 block centered on top
def make_stepped():
    base = trimesh.creation.box(extents=[30, 30, 10])
    base.apply_translation([0, 0, 5])
    top = trimesh.creation.box(extents=[20, 20, 10])
    top.apply_translation([0, 0, 15])
    mesh = trimesh.util.concatenate([base, top])
    return save(mesh, "stepped")

# G5: slot_corner - L-shape with inside corner r=0.8 and 2mm slot
def make_slot_corner():
    # L-shape: 30x30 with 15x15 cutout from one corner, extruded to 15mm
    # Approximate with boxes
    arm1 = trimesh.creation.box(extents=[30, 15, 15])
    arm1.apply_translation([0, -7.5, 7.5])
    arm2 = trimesh.creation.box(extents=[15, 30, 15])
    arm2.apply_translation([-7.5, 0, 7.5])
    mesh = trimesh.util.concatenate([arm1, arm2])
    # Add a thin slot (2mm wide, 10mm deep) on one face
    slot = trimesh.creation.box(extents=[2, 5, 15])
    slot.apply_translation([10, 10, 7.5])
    mesh = trimesh.util.concatenate([mesh, slot])
    return save(mesh, "slot_corner")

if __name__ == "__main__":
    print("Generating test geometries...")
    make_cube()
    make_bowl()
    make_cone()
    make_stepped()
    make_slot_corner()
    print("Done.")
