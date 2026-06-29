import math
import os
import random
from mathutils import Vector

import bpy


ROOT = os.path.dirname(os.path.abspath(__file__))
BLEND_PATH = os.path.join(ROOT, "retro_urban_male_ps1.blend")
FBX_PATH = os.path.join(ROOT, "retro_urban_male_ps1.fbx")
GLB_PATH = os.path.join(ROOT, "retro_urban_male_ps1.glb")
ATLAS_PATH = os.path.join(ROOT, "retro_urban_male_atlas.png")


ATLAS_SIZE = 512


RECTS = {
    "skin": (0, 0, 128, 192),
    "face": (128, 0, 128, 128),
    "hair": (256, 0, 96, 96),
    "shirt": (0, 192, 192, 160),
    "pants": (192, 192, 160, 192),
    "shoes": (352, 192, 96, 96),
    "hands": (352, 0, 96, 96),
    "sole": (448, 192, 64, 64),
}


def uv_rect(name):
    x, y, w, h = RECTS[name]
    pad = 2
    u0 = (x + pad) / ATLAS_SIZE
    v0 = (y + pad) / ATLAS_SIZE
    u1 = (x + w - pad) / ATLAS_SIZE
    v1 = (y + h - pad) / ATLAS_SIZE
    return (u0, v0, u1, v1)


def set_pixel(pixels, x, y, color):
    if 0 <= x < ATLAS_SIZE and 0 <= y < ATLAS_SIZE:
        i = (y * ATLAS_SIZE + x) * 4
        pixels[i:i + 4] = color


def mix(a, b, t):
    return tuple(a[i] * (1.0 - t) + b[i] * t for i in range(4))


def fill_rect(pixels, rect, base, shadow=(0, 0, 0, 1), highlight=(1, 1, 1, 1), noise=0.08):
    x0, y0, w, h = rect
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            gx = (x - x0) / max(1, w - 1)
            gy = (y - y0) / max(1, h - 1)
            shade = -0.17 * gx + 0.15 * (1.0 - gy)
            checker = 0.045 if ((x // 3 + y // 3) % 2 == 0) else -0.025
            grain = random.uniform(-noise, noise)
            c = base
            if shade + checker + grain > 0:
                c = mix(c, highlight, min(0.35, shade + checker + grain))
            else:
                c = mix(c, shadow, min(0.35, -(shade + checker + grain)))
            set_pixel(pixels, x, y, (c[0], c[1], c[2], 1))


def line(pixels, x0, y0, x1, y1, color, width=1):
    dx = abs(x1 - x0)
    dy = -abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    while True:
        for yy in range(y0 - width, y0 + width + 1):
            for xx in range(x0 - width, x0 + width + 1):
                set_pixel(pixels, xx, yy, color)
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def ellipse(pixels, cx, cy, rx, ry, color, fill=True):
    for y in range(cy - ry, cy + ry + 1):
        for x in range(cx - rx, cx + rx + 1):
            d = ((x - cx) / max(1, rx)) ** 2 + ((y - cy) / max(1, ry)) ** 2
            if (fill and d <= 1.0) or (not fill and 0.82 <= d <= 1.18):
                set_pixel(pixels, x, y, color)


def save_atlas():
    random.seed(24)
    image = bpy.data.images.new("retro_urban_male_atlas", ATLAS_SIZE, ATLAS_SIZE, alpha=True)
    pixels = [0.0] * (ATLAS_SIZE * ATLAS_SIZE * 4)

    fill_rect(pixels, RECTS["skin"], (0.64, 0.38, 0.27, 1), (0.22, 0.12, 0.10, 1), (0.93, 0.68, 0.48, 1), 0.07)
    fill_rect(pixels, RECTS["face"], (0.62, 0.37, 0.27, 1), (0.19, 0.10, 0.08, 1), (0.95, 0.70, 0.50, 1), 0.075)
    fill_rect(pixels, RECTS["hair"], (0.10, 0.07, 0.045, 1), (0.03, 0.02, 0.015, 1), (0.24, 0.16, 0.10, 1), 0.06)
    fill_rect(pixels, RECTS["shirt"], (0.12, 0.30, 0.27, 1), (0.04, 0.08, 0.08, 1), (0.30, 0.55, 0.47, 1), 0.07)
    fill_rect(pixels, RECTS["pants"], (0.11, 0.14, 0.18, 1), (0.025, 0.025, 0.035, 1), (0.26, 0.34, 0.43, 1), 0.09)
    fill_rect(pixels, RECTS["shoes"], (0.08, 0.07, 0.055, 1), (0.025, 0.02, 0.015, 1), (0.30, 0.25, 0.18, 1), 0.05)
    fill_rect(pixels, RECTS["hands"], (0.64, 0.39, 0.28, 1), (0.20, 0.10, 0.08, 1), (0.95, 0.70, 0.50, 1), 0.08)
    fill_rect(pixels, RECTS["sole"], (0.025, 0.025, 0.025, 1), (0.0, 0.0, 0.0, 1), (0.12, 0.11, 0.09, 1), 0.03)

    dark = (0.035, 0.025, 0.02, 1)
    skin_shadow = (0.18, 0.09, 0.07, 1)
    light = (0.90, 0.66, 0.46, 1)
    # Face plate: eyebrows, eyes, nose, mouth, moustache and jaw shading.
    fx, fy, _, _ = RECTS["face"]
    line(pixels, fx + 28, fy + 48, fx + 52, fy + 45, dark, 2)
    line(pixels, fx + 76, fy + 45, fx + 100, fy + 48, dark, 2)
    ellipse(pixels, fx + 42, fy + 58, 5, 3, (0.04, 0.05, 0.055, 1))
    ellipse(pixels, fx + 86, fy + 58, 5, 3, (0.04, 0.05, 0.055, 1))
    line(pixels, fx + 64, fy + 57, fx + 58, fy + 85, skin_shadow, 2)
    line(pixels, fx + 58, fy + 85, fx + 71, fy + 88, light, 1)
    line(pixels, fx + 39, fy + 96, fx + 90, fy + 97, dark, 2)
    line(pixels, fx + 42, fy + 87, fx + 88, fy + 88, (0.16, 0.08, 0.055, 1), 3)
    line(pixels, fx + 49, fy + 112, fx + 79, fy + 116, skin_shadow, 1)
    line(pixels, fx + 14, fy + 76, fx + 28, fy + 112, (0.30, 0.16, 0.12, 1), 3)
    line(pixels, fx + 104, fy + 76, fx + 91, fy + 112, (0.30, 0.16, 0.12, 1), 3)

    # Shirt folds, collar, seams and small wear marks.
    sx, sy, _, _ = RECTS["shirt"]
    line(pixels, sx + 20, sy + 26, sx + 172, sy + 25, (0.05, 0.13, 0.12, 1), 2)
    line(pixels, sx + 72, sy + 22, sx + 96, sy + 39, (0.24, 0.49, 0.42, 1), 1)
    line(pixels, sx + 120, sy + 22, sx + 96, sy + 39, (0.04, 0.12, 0.10, 1), 1)
    for x in (42, 86, 138, 166):
        line(pixels, sx + x, sy + 44, sx + x - random.randint(10, 18), sy + 135, (0.04, 0.11, 0.10, 1), 2)
        line(pixels, sx + x + 4, sy + 42, sx + x + random.randint(5, 14), sy + 128, (0.27, 0.52, 0.45, 1), 1)
    for _ in range(18):
        px = sx + random.randint(15, 178)
        py = sy + random.randint(55, 145)
        line(pixels, px, py, px + random.randint(4, 12), py + random.randint(-2, 4), (0.05, 0.13, 0.11, 1), 1)

    # Jeans: fly, pockets, seams, knee dirt and highlights.
    px, py, _, _ = RECTS["pants"]
    line(pixels, px + 78, py + 12, px + 78, py + 82, (0.025, 0.035, 0.05, 1), 2)
    line(pixels, px + 56, py + 34, px + 25, py + 57, (0.20, 0.28, 0.34, 1), 2)
    line(pixels, px + 102, py + 34, px + 137, py + 57, (0.20, 0.28, 0.34, 1), 2)
    line(pixels, px + 28, py + 15, px + 134, py + 15, (0.025, 0.035, 0.05, 1), 2)
    for x in (36, 118):
        line(pixels, px + x, py + 85, px + x + 15, py + 168, (0.04, 0.055, 0.075, 1), 2)
        line(pixels, px + x + 14, py + 92, px + x + 28, py + 160, (0.24, 0.31, 0.40, 1), 1)
    for _ in range(22):
        ellipse(pixels, px + random.randint(14, 146), py + random.randint(50, 180), random.randint(2, 8), random.randint(1, 5), (0.06, 0.08, 0.09, 1))

    # Hands: blocky finger lines and knuckles.
    hx, hy, _, _ = RECTS["hands"]
    for x in (25, 39, 53, 67):
        line(pixels, hx + x, hy + 36, hx + x - 6, hy + 86, skin_shadow, 1)
        ellipse(pixels, hx + x - 2, hy + 34, 4, 2, light)

    # Shoes: laces and scuffed toes.
    bx, by, _, _ = RECTS["shoes"]
    line(pixels, bx + 20, by + 34, bx + 75, by + 30, (0.02, 0.018, 0.015, 1), 2)
    for y in (42, 52, 62):
        line(pixels, bx + 24, by + y, bx + 70, by + y - 4, (0.18, 0.15, 0.11, 1), 1)
    ellipse(pixels, bx + 70, by + 72, 15, 6, (0.28, 0.23, 0.17, 1))

    image.pixels[:] = pixels
    image.filepath_raw = ATLAS_PATH
    image.file_format = "PNG"
    image.save()
    return image


verts = []
faces = []
face_uvs = []
face_groups = []


def add_face(indices, rect_name, group):
    faces.append(indices)
    u0, v0, u1, v1 = uv_rect(rect_name)
    face_uvs.append([(u0, v0), (u1, v0), (u1, v1), (u0, v1)])
    face_groups.append(group)


def add_box(center, size, rect_name, group, name="", skew=None):
    cx, cy, cz = center
    sx, sy, sz = (s / 2.0 for s in size)
    base = [
        (-sx, -sy, -sz), (sx, -sy, -sz), (sx, sy, -sz), (-sx, sy, -sz),
        (-sx, -sy, sz), (sx, -sy, sz), (sx, sy, sz), (-sx, sy, sz),
    ]
    start = len(verts)
    for x, y, z in base:
        if skew:
            x += skew[0] * z
            y += skew[1] * z
        verts.append((cx + x, cy + y, cz + z))
    quads = [
        (0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1),
        (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0),
    ]
    for q in quads:
        add_face(tuple(start + i for i in q), rect_name, group)


def add_tapered_prism(center, height, radius_bottom, radius_top, sides, rect_name, group, axis="z", bend=0.0):
    cx, cy, cz = center
    start = len(verts)
    for ring, z in enumerate((-height / 2.0, height / 2.0)):
        r = radius_bottom if ring == 0 else radius_top
        for i in range(sides):
            a = (i / sides) * math.tau + math.pi / sides
            x = math.cos(a) * r[0]
            y = math.sin(a) * r[1]
            if axis == "z":
                verts.append((cx + x + bend * z, cy + y, cz + z))
            elif axis == "x":
                verts.append((cx + z, cy + x, cz + y + bend * z))
            else:
                verts.append((cx + x, cy + z, cz + y + bend * z))
    for i in range(sides):
        j = (i + 1) % sides
        add_face((start + i, start + j, start + sides + j, start + sides + i), rect_name, group)
    add_face(tuple(start + i for i in reversed(range(sides))), rect_name, group)
    add_face(tuple(start + sides + i for i in range(sides)), rect_name, group)


def build_mesh():
    # Body core: original urban character in a slight A-pose.
    add_box((0, 0, 2.78), (0.82, 0.46, 1.18), "shirt", "spine", "torso", skew=(0.03, 0.0))
    add_box((0, 0, 3.48), (0.34, 0.30, 0.24), "skin", "neck", "neck")
    add_box((0, 0, 4.05), (0.66, 0.54, 0.82), "skin", "head", "head", skew=(0.04, 0.0))
    add_box((0, -0.02, 4.52), (0.72, 0.58, 0.26), "hair", "head", "flat cropped hair")
    add_box((0, -0.31, 4.08), (0.18, 0.11, 0.16), "skin", "head", "nose")
    add_box((-0.15, -0.286, 4.18), (0.11, 0.018, 0.035), "hair", "head", "left eye")
    add_box((0.15, -0.286, 4.18), (0.11, 0.018, 0.035), "hair", "head", "right eye")
    add_box((-0.15, -0.292, 4.28), (0.18, 0.018, 0.035), "hair", "head", "left eyebrow")
    add_box((0.15, -0.292, 4.28), (0.18, 0.018, 0.035), "hair", "head", "right eyebrow")
    add_box((0, -0.294, 3.90), (0.26, 0.018, 0.035), "hair", "head", "mouth line")
    add_box((0, -0.296, 3.98), (0.30, 0.018, 0.045), "hair", "head", "short moustache")
    add_box((-0.43, 0.0, 4.08), (0.10, 0.12, 0.23), "skin", "head", "ear L")
    add_box((0.43, 0.0, 4.08), (0.10, 0.12, 0.23), "skin", "head", "ear R")

    # Arms and chunky hands.
    add_tapered_prism((-0.72, 0, 3.05), 0.82, (0.18, 0.16), (0.20, 0.17), 6, "skin", "upper_arm.L", axis="z", bend=-0.14)
    add_tapered_prism((-0.95, 0, 2.34), 0.82, (0.15, 0.13), (0.18, 0.15), 6, "skin", "forearm.L", axis="z", bend=-0.10)
    add_box((-1.08, -0.01, 1.74), (0.22, 0.14, 0.42), "hands", "hand.L", "hand L", skew=(-0.06, 0))
    add_box((-1.16, -0.02, 1.58), (0.10, 0.10, 0.25), "hands", "hand.L", "thumb L", skew=(0.08, 0))
    add_box((-1.16, -0.08, 1.79), (0.045, 0.055, 0.30), "hands", "hand.L", "finger L 1")
    add_box((-1.10, -0.08, 1.79), (0.045, 0.055, 0.34), "hands", "hand.L", "finger L 2")
    add_box((-1.04, -0.08, 1.79), (0.045, 0.055, 0.32), "hands", "hand.L", "finger L 3")
    add_box((-0.98, -0.08, 1.79), (0.045, 0.055, 0.27), "hands", "hand.L", "finger L 4")
    add_tapered_prism((0.72, 0, 3.05), 0.82, (0.18, 0.16), (0.20, 0.17), 6, "skin", "upper_arm.R", axis="z", bend=0.14)
    add_tapered_prism((0.95, 0, 2.34), 0.82, (0.15, 0.13), (0.18, 0.15), 6, "skin", "forearm.R", axis="z", bend=0.10)
    add_box((1.08, -0.01, 1.74), (0.22, 0.14, 0.42), "hands", "hand.R", "hand R", skew=(0.06, 0))
    add_box((1.16, -0.02, 1.58), (0.10, 0.10, 0.25), "hands", "hand.R", "thumb R", skew=(-0.08, 0))
    add_box((0.98, -0.08, 1.79), (0.045, 0.055, 0.27), "hands", "hand.R", "finger R 1")
    add_box((1.04, -0.08, 1.79), (0.045, 0.055, 0.32), "hands", "hand.R", "finger R 2")
    add_box((1.10, -0.08, 1.79), (0.045, 0.055, 0.34), "hands", "hand.R", "finger R 3")
    add_box((1.16, -0.08, 1.79), (0.045, 0.055, 0.30), "hands", "hand.R", "finger R 4")
    add_box((-0.91, -0.005, 1.93), (0.25, 0.16, 0.10), "shirt", "forearm.L", "left wrist cuff")
    add_box((0.91, -0.005, 1.93), (0.25, 0.16, 0.10), "shirt", "forearm.R", "right wrist cuff")

    # Pants, legs and oversized shoes.
    add_box((0, -0.245, 2.18), (0.78, 0.055, 0.11), "pants", "spine", "low belt")
    add_box((0, -0.29, 2.19), (0.13, 0.05, 0.12), "shoes", "spine", "dull belt buckle")
    add_box((-0.32, -0.205, 1.96), (0.20, 0.045, 0.24), "pants", "thigh.L", "left pocket patch")
    add_box((0.32, -0.205, 1.96), (0.20, 0.045, 0.24), "pants", "thigh.R", "right pocket patch")
    add_box((-0.24, 0.0, 1.72), (0.36, 0.32, 1.28), "pants", "thigh.L", "left upper leg", skew=(-0.02, 0))
    add_box((0.24, 0.0, 1.72), (0.36, 0.32, 1.28), "pants", "thigh.R", "right upper leg", skew=(0.02, 0))
    add_box((-0.28, 0.0, 0.80), (0.31, 0.28, 0.98), "pants", "shin.L", "left lower leg", skew=(-0.02, 0))
    add_box((0.28, 0.0, 0.80), (0.31, 0.28, 0.98), "pants", "shin.R", "right lower leg", skew=(0.02, 0))
    add_box((-0.31, -0.10, 0.13), (0.44, 0.68, 0.24), "shoes", "foot.L", "shoe L", skew=(-0.03, 0))
    add_box((0.31, -0.10, 0.13), (0.44, 0.68, 0.24), "shoes", "foot.R", "shoe R", skew=(0.03, 0))
    add_box((-0.31, -0.11, 0.005), (0.48, 0.72, 0.07), "sole", "foot.L", "shoe sole L")
    add_box((0.31, -0.11, 0.005), (0.48, 0.72, 0.07), "sole", "foot.R", "shoe sole R")

    mesh = bpy.data.meshes.new("Retro_Urban_Male_MeshData")
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new("Retro_Urban_Male_Mesh", mesh)
    bpy.context.collection.objects.link(obj)

    uv_layer = mesh.uv_layers.new(name="retro_atlas_uv")
    for poly, uvs in zip(mesh.polygons, face_uvs):
        for loop_index, uv in zip(poly.loop_indices, uvs):
            uv_layer.data[loop_index].uv = uv

    atlas = save_atlas()
    mat = bpy.data.materials.new("Retro_Single_Diffuse_Atlas_Nearest")
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    tex = mat.node_tree.nodes.new("ShaderNodeTexImage")
    tex.image = atlas
    tex.interpolation = "Closest"
    mat.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 1.0
    obj.data.materials.append(mat)

    group_names = sorted(set(face_groups))
    for group_name in group_names:
        obj.vertex_groups.new(name=group_name)
    for poly, group_name in zip(mesh.polygons, face_groups):
        obj.vertex_groups[group_name].add(poly.vertices, 1.0, "ADD")

    # Split quads/ngons into triangles for a faithful PS1-era budget.
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.quads_convert_to_tris(quad_method="BEAUTY", ngon_method="BEAUTY")
    bpy.ops.object.mode_set(mode="OBJECT")
    mesh.update()
    return obj


def add_armature(mesh_obj):
    bpy.ops.object.armature_add(enter_editmode=True, location=(0, 0, 0))
    arm = bpy.context.object
    arm.name = "Retro_Urban_Male_Rig"
    arm.data.name = "Retro_Urban_Male_Armature"
    arm.show_in_front = True

    bones = arm.data.edit_bones
    root = bones[0]
    root.name = "root"
    root.head = (0, 0, 0.05)
    root.tail = (0, 0, 2.10)

    def bone(name, head, tail, parent):
        b = bones.new(name)
        b.head = head
        b.tail = tail
        b.parent = parent
        b.use_connect = False
        return b

    spine = bone("spine", (0, 0, 2.05), (0, 0, 3.42), root)
    head = bone("head", (0, 0, 3.42), (0, 0, 4.58), spine)
    ual = bone("upper_arm.L", (-0.40, 0, 3.25), (-0.80, 0, 2.72), spine)
    fal = bone("forearm.L", (-0.80, 0, 2.72), (-1.03, 0, 1.92), ual)
    bone("hand.L", (-1.03, 0, 1.92), (-1.12, 0, 1.42), fal)
    uar = bone("upper_arm.R", (0.40, 0, 3.25), (0.80, 0, 2.72), spine)
    far = bone("forearm.R", (0.80, 0, 2.72), (1.03, 0, 1.92), uar)
    bone("hand.R", (1.03, 0, 1.92), (1.12, 0, 1.42), far)
    tl = bone("thigh.L", (-0.20, 0, 2.08), (-0.25, 0, 1.18), root)
    sl = bone("shin.L", (-0.25, 0, 1.18), (-0.31, 0, 0.34), tl)
    bone("foot.L", (-0.31, 0, 0.34), (-0.31, -0.42, 0.08), sl)
    tr = bone("thigh.R", (0.20, 0, 2.08), (0.25, 0, 1.18), root)
    sr = bone("shin.R", (0.25, 0, 1.18), (0.31, 0, 0.34), tr)
    bone("foot.R", (0.31, 0, 0.34), (0.31, -0.42, 0.08), sr)

    bpy.ops.object.mode_set(mode="OBJECT")
    mesh_obj.parent = arm
    modifier = mesh_obj.modifiers.new("Basic_Retro_Armature_Deform", "ARMATURE")
    modifier.object = arm
    return arm


def add_preview_camera_and_light():
    bpy.context.scene.world.color = (0.54, 0.54, 0.54)
    bpy.ops.object.light_add(type="AREA", location=(0, -3.4, 5.0), rotation=(math.radians(62), 0, 0))
    key = bpy.context.object
    key.name = "Preview_Key_Area"
    key.data.energy = 650
    key.data.size = 4.0
    bpy.ops.object.light_add(type="SUN", location=(2.5, -4.0, 5.0), rotation=(math.radians(45), 0, math.radians(24)))
    rim = bpy.context.object
    rim.name = "Preview_Soft_Sun"
    rim.data.energy = 1.0
    bpy.ops.object.camera_add(location=(0, -7.5, 2.32), rotation=(math.radians(90), 0, 0))
    camera = bpy.context.object
    camera.name = "Preview_Camera"
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = 5.35
    bpy.context.scene.camera = camera


def main():
    os.makedirs(ROOT, exist_ok=True)
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()

    mesh_obj = build_mesh()
    arm = add_armature(mesh_obj)
    add_preview_camera_and_light()

    tri_count = sum(len(poly.vertices) - 2 for poly in mesh_obj.data.polygons)
    mesh_obj["asset_notes"] = (
        "Original low-poly PS1-inspired male character. "
        f"{tri_count} triangles, single 512x512 diffuse atlas, nearest filtering, simple armature."
    )
    arm["rig_notes"] = "Basic FK armature with vertex groups for head, torso, arms, hands, legs, and feet."

    bpy.ops.wm.save_as_mainfile(filepath=BLEND_PATH)

    bpy.ops.object.select_all(action="DESELECT")
    mesh_obj.select_set(True)
    arm.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.export_scene.fbx(
        filepath=FBX_PATH,
        use_selection=True,
        add_leaf_bones=False,
        bake_anim=False,
        path_mode="COPY",
        embed_textures=True,
    )
    bpy.ops.export_scene.gltf(
        filepath=GLB_PATH,
        export_format="GLB",
        use_selection=True,
        export_apply=False,
    )
    print(f"Created {BLEND_PATH}")
    print(f"Exported {FBX_PATH}")
    print(f"Exported {GLB_PATH}")
    print(f"Atlas {ATLAS_PATH}")
    print(f"Triangles {tri_count}")


if __name__ == "__main__":
    main()
