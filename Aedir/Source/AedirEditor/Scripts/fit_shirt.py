# blender -b --python fit_shirt.py -- shirt.fbx manny.json out.fbx preview_prefix
# Fits the A-posed linen shirt onto the UE5 mannequin's bind pose and skins it from the nearest skin: the torso keeps its
# shape, raised a little and widened to the body; each sleeve is swept along the arm's bones; then everything is pushed out
# of the body and weighted from the skin under it (torso skin for the torso, arm skin for the sleeves, mixed at the shoulder).
import bpy, sys, json, math
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree
argv = sys.argv[sys.argv.index("--") + 1:]
SRC, BODY, OUT, PREVIEW = argv[0], argv[1], argv[2], argv[3]

d = json.load(open(BODY))
names = [b["name"] for b in d["bones"]]
idx = {n: i for i, n in enumerate(names)}
FLIP = lambda v: Vector((v[0], -v[1], v[2]))
def bone_pos(n):
    return FLIP(d["bones"][idx[n]]["m"][12:15])
verts = [FLIP(v[:3]) for v in d["verts"]]
weights = []
for v in d["verts"]:
    w = {}
    rest = v[3:]
    for i in range(0, len(rest), 2):
        w[rest[i]] = w.get(rest[i], 0) + rest[i + 1]
    weights.append(w)
top = [max(w, key=w.get) for w in weights]
tris = [tuple(d["tris"][i:i + 3]) for i in range(0, len(d["tris"]), 3)]
HANDS = {i for i, n in enumerate(names) if n.startswith(("hand", "thumb", "index", "middle", "ring", "pinky"))}
bvh = BVHTree.FromPolygons(verts, [t for t in tris if not any(top[k] in HANDS for k in t)], all_triangles=True)

ARMSKIN = {i for i, n in enumerate(names) if n.startswith(("upperarm", "lowerarm", "hand"))}
TORSO = {i for i, n in enumerate(names) if n.startswith(("pelvis", "spine", "neck", "clavicle"))}

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.context.scene.unit_settings.scale_length = 0.01
bpy.ops.import_scene.fbx(filepath=SRC)
shirt = [o for o in bpy.data.objects if o.type == "MESH"][0]
# In cm, Blender axes (front -Y, up Z), as the body is.
src = [(shirt.matrix_world @ v.co) for v in shirt.data.vertices]
zs = [v.z for v in src]
unit = 100.0 if max(zs) - min(zs) < 10 else 1.0
src = [v * unit for v in src]
s_lo, s_hi = min(v.z for v in src), max(v.z for v in src)
print("[fit] shirt z %.1f..%.1f" % (s_lo, s_hi))

# ---- torso: the collar's back at the base of the neck, the rest as it is, widened to the body.
neck = bone_pos("neck_01")
dz = (neck.z + 2.0) - s_hi
def body_band(z0, z1):
    return [verts[i] for i in range(len(verts)) if z0 < verts[i].z < z1 and top[i] in TORSO]
band = body_band(105, 135)
bw = max(abs(v.x) for v in band)
by_lo, by_hi = min(v.y for v in band), max(v.y for v in band)
sband = [v for v in src if 105 - dz < v.z < 135 - dz and abs(v.x) < 18.5]
sw = max(abs(v.x) for v in sband)
sy_lo, sy_hi = min(v.y for v in sband), max(v.y for v in sband)
SX = max(1.0, bw * 1.06 / sw)
SY = max(1.0, (by_hi - by_lo) * 1.08 / (sy_hi - sy_lo))
ymid_s, ymid_b = (sy_lo + sy_hi) / 2, (by_lo + by_hi) / 2
print("[fit] body half width %.1f depth %.1f; shirt %.1f %.1f; scale %.2f %.2f, down %.1f" % (bw, by_hi - by_lo, sw, sy_hi - sy_lo, SX, SY, -dz))
def torso_map(v):
    return Vector((v.x * SX, (v.y - ymid_s) * SY + ymid_b, v.z + dz))

# ---- sleeves: the sleeve's own axis laid along upper arm and forearm.
def sleeve_mapper(side):
    sv = [v for v in src if v.x * side > 0]
    near = [v for v in sv if 21 < abs(v.x) < 25 and v.z > 120]
    tipx = max(abs(v.x) for v in sv)
    far = [v for v in sv if abs(v.x) > tipx - 5]
    a = sum(near, Vector()) / len(near)
    b = sum(far, Vector()) / len(far)
    axis = (b - a).normalized()
    length = (b - a).length
    suf = "_l" if side > 0 else "_r"
    S, E, H = bone_pos("upperarm" + suf), bone_pos("lowerarm" + suf), bone_pos("hand" + suf)
    W = H + (E - H).normalized() * 1.0
    L1, L2 = (E - S).length, (W - E).length
    d1, d2 = (E - S).normalized(), (W - E).normalized()
    rot1, rot2 = axis.rotation_difference(d1), axis.rotation_difference(d2)
    # The sleeve starts where the arm leaves the shoulder, a share of its way in from the bone's head.
    start = S + d1 * 2.0
    total = L1 + L2 - 2.0
    print("[fit] sleeve", side, "shirt length %.1f -> arm %.1f, axis %s" % (length, total, tuple(round(c, 2) for c in axis)))
    def mapper(v):
        rel = v - a
        t = rel.dot(axis) / length
        off = rel - axis * rel.dot(axis)
        along = t * total
        share = max(0.0, min(1.0, (along - (L1 - 2.0) + 6.0) / 12.0))
        rot = rot1.slerp(rot2, share * share * (3 - 2 * share))
        p = start + d1 * along if along < L1 - 2.0 else E + d2 * (along - (L1 - 2.0))
        return p + rot @ off, t
    return mapper
maps = {1: sleeve_mapper(1.0), -1: sleeve_mapper(-1.0)}

def smooth(x, a, b):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)
out, sleeve_share = [], []
for v in src:
    side = 1 if v.x > 0 else -1
    pt, t = maps[side](v)
    pt_t = torso_map(v)
    # Out past the side of the torso, and above the waist, it is sleeve.
    s = smooth(abs(v.x), 17.0, 24.0) * (smooth(v.z, 98.0, 104.0) if abs(v.x) < 30.0 else 1.0)
    out.append(pt_t.lerp(pt, s))
    sleeve_share.append(s)

# ---- push out of the body, the clearance growing towards the loose hem and the sleeve ends
probe = bvh.find_nearest(Vector((0, ymid_b - 40, bone_pos("spine_03").z)), 60.0)
NSIGN = 1.0 if probe[1].y < 0 else -1.0
def push(points, margin):
    n = 0
    for i, v in enumerate(points):
        hit = bvh.find_nearest(v, 30.0)
        if hit[0] is not None and (v - hit[0]).dot(hit[1] * NSIGN) < margin:
            points[i] = hit[0] + hit[1] * NSIGN * margin
            n += 1
    return n
# Pushed out once, the shift each point got is spread to its neighbours so nothing is left flattened, and pushed again.
before = [v.copy() for v in out]
pushed = push(out, 2.2)
nbrs = [[] for _ in out]
for e in shirt.data.edges:
    a, b = e.vertices
    nbrs[a].append(b); nbrs[b].append(a)
shift = [out[i] - before[i] for i in range(len(out))]
for _ in range(6):
    shift = [shift[i] * 0.4 + (sum((shift[j] for j in nbrs[i]), Vector()) / max(len(nbrs[i]), 1)) * 0.6 for i in range(len(out))]
out = [before[i] + shift[i] for i in range(len(out))]
pushed += push(out, 1.8)
print("[fit] pushed out", pushed, "of", len(out))

# ---- weights
def tree_of(allowed):
    ids = [i for i in range(len(verts)) if top[i] in allowed]
    t = KDTree(len(ids))
    for n, i in enumerate(ids):
        t.insert(verts[i], n)
    t.balance()
    return t, ids
tree_torso, ids_torso = tree_of(TORSO)
tree_arm, ids_arm = tree_of(ARMSKIN)
def weigh(v, tree, ids):
    total = {}
    for co, n, dist in tree.find_n(v, 8):
        f = 1.0 / (dist * dist + 1.0)
        for b, w in weights[ids[n]].items():
            total[b] = total.get(b, 0) + w * f
    s = sum(total.values())
    return {b: w / s for b, w in total.items()}

for o in [o for o in bpy.data.objects if o.type != "MESH"]:
    bpy.data.objects.remove(o)
arm_data = bpy.data.armatures.new("Armature")
arm_obj = bpy.data.objects.new("Armature", arm_data)
bpy.context.scene.collection.objects.link(arm_obj)
bpy.context.view_layer.objects.active = arm_obj
bpy.ops.object.mode_set(mode="EDIT")
ebs = []
for b in d["bones"]:
    m = b["m"]
    pos = FLIP(m[12:15])
    x, y, z = FLIP(m[0:3]), -FLIP(m[4:7]), FLIP(m[8:11])
    eb = arm_data.edit_bones.new(b["name"])
    eb.head = pos
    eb.tail = pos + y * 5.0
    ebs.append((eb, x, y, z, pos))
    if b["parent"] >= 0:
        eb.parent = ebs[b["parent"]][0]
for eb, x, y, z, pos in ebs:
    eb.matrix = Matrix(((x[0], y[0], z[0], pos[0]), (x[1], y[1], z[1], pos[1]), (x[2], y[2], z[2], pos[2]), (0, 0, 0, 1)))
    eb.length = 5.0
bpy.ops.object.mode_set(mode="OBJECT")

me = shirt.data
shirt.parent = None
shirt.matrix_world = Matrix.Identity(4)
for i, v in enumerate(out):
    me.vertices[i].co = v
# The model's own normals were for where its points were: smooth ones are made anew for where they are now.
bpy.context.view_layer.objects.active = shirt
shirt.select_set(True)
if me.has_custom_normals:
    bpy.ops.mesh.customdata_custom_splitnormals_clear()
for poly in me.polygons:
    poly.use_smooth = True
me.update()
shirt.name = "MedievalShirt"
me.materials[0].name = "MedievalShirt"
groups = {}
for i, v in enumerate(out):
    s = sleeve_share[i]
    wt = weigh(v, tree_torso, ids_torso) if s < 0.999 else {}
    wa = weigh(v, tree_arm, ids_arm) if s > 0.001 else {}
    mix = {}
    for b, w in wt.items():
        mix[b] = mix.get(b, 0) + w * (1 - s)
    for b, w in wa.items():
        mix[b] = mix.get(b, 0) + w * s
    best = sorted(mix.items(), key=lambda e: -e[1])[:4]
    tot = sum(w for _, w in best)
    for b, w in best:
        if w / tot > 0.02:
            g = groups.get(b)
            if g is None:
                g = groups[b] = shirt.vertex_groups.new(name=names[b])
            g.add([i], w / tot, "REPLACE")
shirt.parent = arm_obj
mod = shirt.modifiers.new("Armature", "ARMATURE")
mod.object = arm_obj
print("[fit] bones", sorted(names[b] for b in groups))

# Points that ended up on top of each other are one, and triangles with no area are gone: cloth cannot be made of them.
bpy.ops.object.select_all(action="DESELECT")
shirt.select_set(True)
bpy.context.view_layer.objects.active = shirt
nv = len(shirt.data.vertices)
bpy.ops.object.mode_set(mode="EDIT")
bpy.ops.mesh.select_all(action="SELECT")
bpy.ops.mesh.remove_doubles(threshold=0.01)
bpy.ops.mesh.select_all(action="SELECT")
bpy.ops.mesh.delete_loose(use_verts=True, use_edges=True, use_faces=False)
bpy.ops.mesh.select_all(action="SELECT")
bpy.ops.mesh.dissolve_degenerate(threshold=0.01)
bpy.ops.mesh.select_all(action="SELECT")
bpy.ops.mesh.quads_convert_to_tris()
bpy.ops.mesh.dissolve_degenerate(threshold=0.01)
bpy.ops.object.mode_set(mode="OBJECT")
print("[fit] merged %d points into %d" % (nv, len(shirt.data.vertices)))
bpy.ops.object.select_all(action="DESELECT")
arm_obj.select_set(True)
shirt.select_set(True)
bpy.context.view_layer.objects.active = arm_obj
bpy.ops.export_scene.fbx(filepath=OUT, use_selection=True, object_types={"ARMATURE", "MESH"}, global_scale=1.0, apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE", axis_forward="-Y", axis_up="Z", bake_space_transform=False, use_mesh_modifiers=False, mesh_smooth_type="FACE", add_leaf_bones=False, primary_bone_axis="Y", secondary_bone_axis="X", use_armature_deform_only=False, armature_nodetype="NULL", bake_anim=False)

# ---- preview with the body
bme = bpy.data.meshes.new("Body")
bme.from_pydata([tuple(v) for v in verts], [], tris)
bme.update()
bo = bpy.data.objects.new("Body", bme)
bpy.context.scene.collection.objects.link(bo)
c = Vector((0, ymid_b, 115))
scn = bpy.context.scene
scn.render.engine = "BLENDER_WORKBENCH"
scn.display.shading.color_type = "RANDOM"
scn.render.resolution_x = scn.render.resolution_y = 600
for name, direction in (("front", (0, -1, 0)), ("side", (1, 0, 0)), ("back", (0, 1, 0))):
    cam = bpy.data.cameras.new(name)
    cam.type = "ORTHO"
    cam.ortho_scale = 140
    co = bpy.data.objects.new(name, cam)
    scn.collection.objects.link(co)
    dd = Vector(direction)
    co.location = c + dd * 300
    co.rotation_euler = (-dd).to_track_quat("-Z", "Y").to_euler()
    cam.clip_end = 1000
    scn.camera = co
    scn.render.filepath = PREVIEW + "_" + name + ".png"
    bpy.ops.render.render(write_still=True)
print("[fit] done")
