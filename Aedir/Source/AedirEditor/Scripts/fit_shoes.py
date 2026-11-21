# blender -b --python fit_shoes.py -- shoes.fbx body.json out.fbx preview_prefix [skeleton.json]
# Fits the pair of medieval shoes (one unskinned model of both) onto the feet of the body the creatures wear (exported with
# AedirImportModels -ExportBody=) and skins them. With skeleton.json only that skeleton's bones are used, the body's extra
# ones given to the nearest of them above.
# - the pair is split into its two shoes, each given to the foot on its side;
# - each shoe is turned to point the way its foot does (heel to toe) and laid on it heel to heel, sole under sole: its
#   length scaled to the foot's and LENGTH_ROOM more, its width to the foot's and WIDTH_ROOM more, its height with the
#   larger of the two, so it keeps its own shape;
# - wherever the skin still comes through, the shoe is pushed out off it to SKIN_ROOM, the push spread over its
#   neighbours so the leather does not dent;
# - weights come from the skin right under it (the foot, the ball of the foot, the shin above the ankle), the toes'
#   given to the ball of the foot, where a shoe bends.
import bpy, sys, json
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
argv = sys.argv[sys.argv.index("--") + 1:]
SRC, BODY, OUT, PREVIEW = argv[0], argv[1], argv[2], argv[3]
SKELETON = argv[4] if len(argv) > 4 else None

LENGTH_ROOM = 2.4   # cm the shoe is longer than the foot inside it
WIDTH_ROOM = 1.15   # times the foot's width the shoe is across
SOLE_UNDER = 1.0    # cm the sole lies under the bottom of the foot
HEEL_BEHIND = 1.6   # cm the back of the shoe lies behind the heel
SKIN_ROOM = 0.7    # cm the leather keeps off the skin at the least
PUSH_ROUNDS = 12

d = json.load(open(BODY))
names = [b["name"] for b in d["bones"]]
idx = {n: i for i, n in enumerate(names)}
FLIP = lambda v: Vector((v[0], -v[1], v[2]))
def bone_pos(n):
    return FLIP(d["bones"][idx[n]]["m"][12:15])
allowed = {b["name"] for b in json.load(open(SKELETON))["bones"]} if SKELETON else set(names)
def kept_of(i):
    while i >= 0 and names[i] not in allowed:
        i = d["bones"][i]["parent"]
    return i
KEPT = [kept_of(i) for i in range(len(names))]
verts = [FLIP(v[:3]) for v in d["verts"]]
weights = []
for v in d["verts"]:
    w = {}
    rest = v[3:]
    for i in range(0, len(rest), 2):
        b = KEPT[rest[i]]
        w[b] = w.get(b, 0) + rest[i + 1]
    weights.append(w)
top = [max(w, key=w.get) for w in weights]
tris = [tuple(d["tris"][i:i + 3]) for i in range(0, len(d["tris"]), 3)]
def under(i, root):
    while i >= 0:
        if names[i] == root:
            return True
        i = d["bones"][i]["parent"]
    return False

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=SRC)
src = [o for o in bpy.data.objects if o.type == "MESH"][0]
src_world = src.matrix_world.copy()
pts = [src_world @ v.co for v in src.data.vertices]
# The model comes in metres; the body is in centimetres.
extent = max(max(p[k] for p in pts) - min(p[k] for p in pts) for k in range(3))
unit = 100.0 if extent < 5.0 else 1.0
pts = [p * unit for p in pts]
polys = [list(p.vertices) for p in src.data.polygons]

# The two shoes: the model's points on either side of its middle across (its X).
mid_x = (min(p.x for p in pts) + max(p.x for p in pts)) * 0.5
side_of = [0 if p.x < mid_x else 1 for p in pts]

out = list(pts)
skin_of = {}
for side in (0, 1):
    mine = [i for i in range(len(pts)) if side_of[i] == side]
    sx = sum(pts[i].x for i in mine) / len(mine)
    # The foot on that side of the body (its X the same way).
    lx, rx = bone_pos("foot_l").x, bone_pos("foot_r").x
    foot = "foot_l" if (sx - mid_x) * (lx - rx) > 0 else "foot_r"
    suffix = foot[-2:]
    foot_bones = {i for i in range(len(names)) if under(i, foot)}
    shin = idx["calf" + suffix]
    skin = [k for k in range(len(verts)) if top[k] in foot_bones]
    # The foot's own frame: heel to toe along the ground, across, up.
    heel_to_toe = (bone_pos("ball" + suffix) - bone_pos(foot))
    heel_to_toe.z = 0
    heel_to_toe.normalize()
    up = Vector((0, 0, 1))
    across = heel_to_toe.cross(up).normalized()
    along_f = [verts[k].dot(heel_to_toe) for k in skin]
    across_f = [verts[k].dot(across) for k in skin]
    heel_f, toe_f = min(along_f), max(along_f)
    sole_f = min(verts[k].z for k in skin)
    width_f = max(across_f) - min(across_f)
    centre_across_f = (max(across_f) + min(across_f)) * 0.5
    # The shoe's own frame: the long way across the ground is its length; its heel is where its opening is, the higher end.
    xs = [pts[i].x for i in mine]
    ys = [pts[i].y for i in mine]
    long_axis = Vector((0, 1, 0)) if (max(ys) - min(ys)) >= (max(xs) - min(xs)) else Vector((1, 0, 0))
    along_s = [pts[i].dot(long_axis) for i in mine]
    lo, hi = min(along_s), max(along_s)
    high = sorted(mine, key=lambda i: -pts[i].z)[: max(len(mine) // 5, 1)]
    opening = sum(pts[i].dot(long_axis) for i in high) / len(high)
    toe_way = long_axis if opening < (lo + hi) * 0.5 else -long_axis
    across_s_axis = toe_way.cross(up).normalized()
    a_s = [pts[i].dot(toe_way) for i in mine]
    c_s = [pts[i].dot(across_s_axis) for i in mine]
    heel_s = min(a_s)
    length_s = max(a_s) - heel_s
    width_s = max(c_s) - min(c_s)
    centre_across_s = (max(c_s) + min(c_s)) * 0.5
    sole_s = min(pts[i].z for i in mine)
    k_len = (toe_f - heel_f + LENGTH_ROOM + HEEL_BEHIND) / length_s
    k_wid = width_f * WIDTH_ROOM / width_s
    k_up = max(k_len, k_wid)
    origin = heel_to_toe * (heel_f - HEEL_BEHIND) + across * centre_across_f + up * (sole_f - SOLE_UNDER)
    for i in mine:
        p = pts[i]
        a = (p.dot(toe_way) - heel_s) * k_len
        c = (p.dot(across_s_axis) - centre_across_s) * k_wid
        h = (p.z - sole_s) * k_up
        out[i] = origin + heel_to_toe * a + across * c + up * h
    print("[fit] %s: foot %.1f cm long, %.1f across; shoe %.1f by %.1f, scaled %.2f long %.2f across %.2f up" % (foot, toe_f - heel_f, width_f, length_s, width_s, k_len, k_wid, k_up))
    skin_of[side] = (foot_bones | {shin}, foot)

# The skin of the feet and the shins, to keep the shoes off and to weigh them by.
FEET = set()
for side in (0, 1):
    FEET |= skin_of[side][0]
keep = [t for t in tris if any(top[k] in FEET for k in t)]
tree = BVHTree.FromPolygons(verts, keep, all_triangles=True)
nbrs = [set() for _ in out]
for p in polys:
    for a in p:
        for b in p:
            if a != b:
                nbrs[a].add(b)
def through():
    worst, count = 0.0, 0
    push = [Vector() for _ in out]
    for i, v in enumerate(out):
        loc, normal, face, dist = tree.find_nearest(v)
        if loc is None:
            continue
        gap = (v - loc).dot(normal)
        if gap < SKIN_ROOM:
            push[i] = normal * (SKIN_ROOM - gap)
            worst = max(worst, SKIN_ROOM - gap)
            count += 1
    return push, worst, count
for r in range(PUSH_ROUNDS):
    push, worst, count = through()
    if count == 0:
        break
    spread = [Vector(p) for p in push]
    for i in range(len(out)):
        for j in nbrs[i]:
            spread[j] += push[i] * 0.5 / max(len(nbrs[i]), 1)
    for i in range(len(out)):
        out[i] = out[i] + spread[i]
push, worst, count = through()
print("[fit] through the skin after pushing: %d points, deepest %.2f cm" % (count, worst))

def weigh(v):
    loc, _, face, _ = tree.find_nearest(v)
    a, b, c = (verts[k] for k in keep[face])
    v0, v1, v2 = b - a, c - a, loc - a
    d00, d01, d11, d20, d21 = v0.dot(v0), v0.dot(v1), v1.dot(v1), v2.dot(v0), v2.dot(v1)
    den = d00 * d11 - d01 * d01
    wb = (d11 * d20 - d01 * d21) / den if den > 1e-9 else 1.0 / 3.0
    wc = (d00 * d21 - d01 * d20) / den if den > 1e-9 else 1.0 / 3.0
    shares = (max(1.0 - wb - wc, 0.0), max(wb, 0.0), max(wc, 0.0))
    total = {}
    for k, f in zip(keep[face], shares):
        for bone, w in weights[k].items():
            total[bone] = total.get(bone, 0) + w * f
    # A leather shoe bends at the ball of the foot, not at each toe inside it.
    merged = {}
    for bone, w in total.items():
        n = names[bone]
        if "toe" in n:
            bone = idx["ball" + n[-2:]]
        merged[bone] = merged.get(bone, 0) + w
    s = sum(merged.values())
    return {bone: w / s for bone, w in merged.items()}

for o in [o for o in bpy.data.objects if o.type != "MESH"]:
    bpy.data.objects.remove(o)
arm_data = bpy.data.armatures.new("Armature")
arm_obj = bpy.data.objects.new("Armature", arm_data)
bpy.context.scene.collection.objects.link(arm_obj)
bpy.context.view_layer.objects.active = arm_obj
bpy.ops.object.mode_set(mode="EDIT")
ebs = {}
for bi, b in enumerate(d["bones"]):
    if KEPT[bi] != bi:
        continue
    m = b["m"]
    pos = FLIP(m[12:15])
    x, y, z = FLIP(m[0:3]), -FLIP(m[4:7]), FLIP(m[8:11])
    eb = arm_data.edit_bones.new(b["name"])
    eb.head = pos
    eb.tail = pos + y * 5.0
    ebs[bi] = (eb, x, y, z, pos)
    if b["parent"] >= 0:
        eb.parent = ebs[KEPT[b["parent"]]][0]
for eb, x, y, z, pos in ebs.values():
    eb.matrix = Matrix(((x[0], y[0], z[0], pos[0]), (x[1], y[1], z[1], pos[1]), (x[2], y[2], z[2], pos[2]), (0, 0, 0, 1)))
    eb.length = 5.0
bpy.ops.object.mode_set(mode="OBJECT")

shoes = src
me = shoes.data
shoes.parent = None
shoes.matrix_world = Matrix.Identity(4)
for i, v in enumerate(out):
    me.vertices[i].co = v
if me.has_custom_normals:
    bpy.context.view_layer.objects.active = shoes
    shoes.select_set(True)
    bpy.ops.mesh.customdata_custom_splitnormals_clear()
me.update()
shoes.name = "MedievalShoes"
if me.materials:
    me.materials[0].name = "MedievalShoes"
groups = {}
for i, v in enumerate(out):
    best = sorted(weigh(v).items(), key=lambda e: -e[1])[:4]
    tot = sum(w for _, w in best)
    for b, w in best:
        if w / tot > 0.02:
            g = groups.get(b)
            if g is None:
                g = groups[b] = shoes.vertex_groups.new(name=names[b])
            g.add([i], w / tot, "REPLACE")
print("[fit] weighted to", sorted(names[b] for b in groups))
shoes.parent = arm_obj
mod = shoes.modifiers.new("Armature", "ARMATURE")
mod.object = arm_obj

bpy.ops.object.select_all(action="DESELECT")
arm_obj.select_set(True)
shoes.select_set(True)
bpy.context.view_layer.objects.active = arm_obj
# The scene's units are centimetres, as the body's are: in metres every point would come out a hundred times too far.
bpy.context.scene.unit_settings.system = "METRIC"
bpy.context.scene.unit_settings.scale_length = 0.01
bpy.ops.export_scene.fbx(filepath=OUT, use_selection=True, object_types={"ARMATURE", "MESH"}, global_scale=1.0, apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE", axis_forward="-Y", axis_up="Z", bake_space_transform=False, use_mesh_modifiers=False, mesh_smooth_type="FACE", add_leaf_bones=False, primary_bone_axis="Y", secondary_bone_axis="X", use_armature_deform_only=False, armature_nodetype="NULL", bake_anim=False)

# ---- preview: the shoes on the feet, from the side, the front and above.
bme = bpy.data.meshes.new("Body")
bme.from_pydata([tuple(v) for v in verts], [], tris)
bme.update()
bo = bpy.data.objects.new("Body", bme)
bpy.context.scene.collection.objects.link(bo)
scn = bpy.context.scene
scn.render.engine = "BLENDER_WORKBENCH"
scn.render.resolution_x, scn.render.resolution_y = 900, 600
feet = (bone_pos("foot_l") + bone_pos("foot_r")) * 0.5
for name, direction, scale in (("side", (1, 0, 0.15), 45), ("front", (0, -1, 0.2), 45), ("top", (0.2, -0.3, 1), 45), ("back", (0.3, 1, 0.3), 45)):
    cam = bpy.data.cameras.new(name)
    cam.type = "ORTHO"
    cam.ortho_scale = scale
    co = bpy.data.objects.new(name, cam)
    scn.collection.objects.link(co)
    dd = Vector(direction).normalized()
    co.location = feet + dd * 300
    co.rotation_euler = (-dd).to_track_quat("-Z", "Y").to_euler()
    cam.clip_end = 2000
    scn.camera = co
    scn.render.filepath = PREVIEW + "_" + name + ".png"
    bpy.ops.render.render(write_still=True)
print("[fit] done")
