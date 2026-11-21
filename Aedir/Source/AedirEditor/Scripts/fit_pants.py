# blender -b --python fit_pants.py -- pants.glb body.json out.fbx preview_prefix [skeleton.json]
# Fits the medieval trousers with bandaged shins (Medieval Pants With Bandage Legs by Paradoox, CC-BY; the converted glb
# of it, whose mannequin is left out) onto the body the creatures wear (exported with AedirImportModels -ExportBody=) and
# skins them, keeping their own cut. With skeleton.json only that skeleton's bones are used, the body's extra ones given
# to the nearest of them above.
# - the trousers are thinned to KEEP_SHARE of their points, enough for a garment that is not simulated;
# - they are measured at their waist, crotch and ankles, and the body at its waist (WAIST_ABOVE_PELVIS), crotch and
#   ankles (ANKLE_ABOVE_FOOT above the ankle joint, into the shoes' cuffs);
# - below the crotch each leg is carried along the body's leg, the hip to the knee to the ankle, every height of it to
#   the same share of the body's leg, and widened or narrowed round the leg by how thick the body's leg is there against
#   how thick the trouser's is (with LEG_ROOM), so their bagginess keeps its shape; above it the seat and waist are
#   carried onto the hips the same way, by their width and depth; the two meet over BLEND_CM round the crotch;
# - wherever the skin still comes through, the cloth is pushed out off it to SKIN_ROOM, the push spread over its
#   neighbours;
# - weights come from the skin right under it (the legs, the hips and the bottom of the back).
import bpy, sys, json, math
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
argv = sys.argv[sys.argv.index("--") + 1:]
SRC, BODY, OUT, PREVIEW = argv[0], argv[1], argv[2], argv[3]
SKELETON = argv[4] if len(argv) > 4 else None

KEEP_SHARE = 0.35
WAIST_ABOVE_PELVIS = 10.0 # cm the waistband sits above the pelvis bone, over the top of the body's own underwear
ANKLE_ABOVE_FOOT = 2.0    # cm the bottom of the bandages lies above the ankle joint
LEG_ROOM = 0.8            # cm the thinnest of a trouser leg keeps off the skin
LEG_MOST_WIDENED = 1.25   # a trouser leg is never widened more than this round the body's leg
HIP_ROOM = 1.4            # cm round the hips and the seat
BLEND_CM = 6.0
SKIN_ROOM = 0.6
PUSH_ROUNDS = 16
BAND = 2.0                # cm half the height of a band measured across

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
LEGS = {i for i in range(len(names)) if under(i, "thigh_l") or under(i, "thigh_r")}
FEET = {i for i in range(len(names)) if under(i, "foot_l") or under(i, "foot_r")}
HIPS = {idx["pelvis"], idx["spine_01"]}
COVER = (LEGS - FEET) | HIPS

# ---- the trousers, without the mannequin they were modelled on, in centimetres.
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SRC)
meshes = [o for o in bpy.data.objects if o.type == "MESH"]
pants = max(meshes, key=lambda o: len(o.data.vertices))
for o in meshes:
    if o is not pants:
        bpy.data.objects.remove(o)
world = pants.matrix_world.copy()
pants.parent = None
pants.matrix_world = Matrix.Identity(4)
for v in pants.data.vertices:
    v.co = (world @ v.co) * 100.0
bpy.context.view_layer.objects.active = pants
pants.select_set(True)
dec = pants.modifiers.new("Fewer", "DECIMATE")
dec.ratio = KEEP_SHARE
bpy.ops.object.modifier_apply(modifier="Fewer")
me = pants.data
pts = [v.co.copy() for v in me.vertices]
polys = [list(p.vertices) for p in me.polygons]
print("[fit] trousers: %d points after thinning" % len(pts))

# Which way the trousers face: the knees bend forward, so the front of the leg below the knee is the side the cloth
# stands out at the knee. Face them to -Y, as the body does.
zs = [p.z for p in pts]
z_bot, z_top = min(zs), max(zs)
knee_band = [p for p in pts if abs(p.z - (z_bot + (z_top - z_bot) * 0.33)) < BAND]
mean_y = sum(p.y for p in knee_band) / max(len(knee_band), 1)
front_y = sum(1 for p in knee_band if p.y < mean_y - 4) - sum(1 for p in knee_band if p.y > mean_y + 4)
if front_y < 0:
    pts = [Vector((-p.x, -p.y, p.z)) for p in pts]
    print("[fit] trousers turned round to face the body's way")

def band_of(points, z, sel=None):
    return [p for p in points if abs(p.z - z) < BAND and (sel is None or sel(p))]

# The crotch: the highest height at which no cloth lies in the middle across.
def crotch_of(points, lo, hi, half_gap):
    z = lo
    step = 0.5
    best = lo
    while z < hi:
        b = band_of(points, z)
        if b and not any(abs(p.x) < half_gap for p in b):
            best = z
        z += step
    return best
s_crotch = crotch_of(pts, z_bot + 20, z_top - 5, 1.5)
s_top, s_bot = z_top, z_bot
print("[fit] trousers: waist %.1f, crotch %.1f, ankles %.1f cm" % (s_top, s_crotch, s_bot))

# The body: its waist, crotch and ankles, and the line of each leg through the hip, knee and ankle joints.
skin_legs = [verts[k] for k in range(len(verts)) if top[k] in COVER]
b_top = bone_pos("pelvis").z + WAIST_ABOVE_PELVIS
b_crotch = crotch_of(skin_legs, bone_pos("calf_l").z, bone_pos("pelvis").z, 1.0)
b_bot = bone_pos("foot_l").z + ANKLE_ABOVE_FOOT
print("[fit] body: waist %.1f, crotch %.1f, ankles %.1f cm" % (b_top, b_crotch, b_bot))

def body_leg_point(side, share):
    # share 0 at the ankle, 1 at the crotch, along the hip, knee and ankle joints.
    sx = "_l" if side > 0 else "_r"
    foot, knee, hip = bone_pos("foot" + sx), bone_pos("calf" + sx), bone_pos("thigh" + sx)
    zc = b_bot + (b_crotch - b_bot) * share
    a, b = (foot, knee) if zc < knee.z else (knee, hip)
    t = (zc - a.z) / max(b.z - a.z, 1e-3)
    p = a.lerp(b, max(min(t, 1.0), 0.0))
    return Vector((p.x, p.y, zc))

def pct(values, share):
    values = sorted(values)
    return values[min(int(len(values) * share), len(values) - 1)] if values else 1.0

# Each leg is measured at a few heights only, over a wide band, and carried between them smoothly: measured at every
# height its centre and thickness wander with each fold and bandage, and the cloth came out in spikes.
LEG_KEYS = [0.0, 0.12, 0.25, 0.4, 0.55, 0.7, 0.85, 1.0]
HIP_KEYS = [0.0, 0.35, 0.7, 1.0]
WIDE = 3.0
def wide_band(points, z, sel):
    return [p for p in points if abs(p.z - z) < WIDE and sel(p)]
def centre_of(b, fallback):
    return Vector((sum(p.x for p in b) / len(b), sum(p.y for p in b) / len(b), fallback.z)) if b else fallback
legs = {}
for side in (-1.0, 1.0):
    rows = []
    for share in LEG_KEYS:
        zs_ = s_bot + (s_crotch - s_bot) * share
        sb = wide_band(pts, zs_, lambda p: p.x * side > 0)
        sc = centre_of(sb, Vector((side * 10.0, 0, zs_)))
        s_in = pct([Vector((p.x - sc.x, p.y - sc.y, 0)).length for p in sb], 0.1)
        bc0 = body_leg_point(side, share)
        bb = wide_band(skin_legs, bc0.z, lambda p: p.x * side > 0 and Vector((p.x - bc0.x, p.y - bc0.y, 0)).length < 14)
        bc = centre_of(bb, bc0)
        b_mid = pct([Vector((p.x - bc.x, p.y - bc.y, 0)).length for p in bb], 0.5)
        k = max(min((b_mid + LEG_ROOM) / max(s_in, 0.5), LEG_MOST_WIDENED), 0.7)
        rows.append((share, sc, bc, k))
        print("[fit] leg %+d at %.2f: trouser %.1f cm in, leg %.1f cm thick, widened %.2f" % (side, share, s_in, b_mid, k))
    legs[side] = rows
def along(rows, share):
    share = max(min(share, 1.0), 0.0)
    for (s0, sc0, bc0, k0), (s1, sc1, bc1, k1) in zip(rows, rows[1:]):
        if share <= s1:
            t = (share - s0) / max(s1 - s0, 1e-6)
            t = t * t * (3 - 2 * t)
            return sc0.lerp(sc1, t), bc0.lerp(bc1, t), k0 + (k1 - k0) * t
    return rows[-1][1], rows[-1][2], rows[-1][3]

def map_leg(p):
    side = 1.0 if p.x > 0 else -1.0
    share = (p.z - s_bot) / max(s_crotch - s_bot, 1e-3)
    sc, bc, k = along(legs[side], share)
    zb = b_bot + (b_crotch - b_bot) * share
    return Vector((bc.x + (p.x - sc.x) * k, bc.y + (p.y - sc.y) * k, zb))

def extent(points, z):
    b = [p for p in points if abs(p.z - z) < WIDE]
    if not b:
        return Vector((0, 0, z)), 1.0, 1.0
    xs, ys = [p.x for p in b], [p.y for p in b]
    return Vector(((pct(xs, 0.95) + pct(xs, 0.05)) * 0.5, (pct(ys, 0.95) + pct(ys, 0.05)) * 0.5, z)), (pct(xs, 0.95) - pct(xs, 0.05)) * 0.5, (pct(ys, 0.95) - pct(ys, 0.05)) * 0.5
hip_skin = [v for v in skin_legs if v.z <= b_top + 4]
hips = []
for share in HIP_KEYS:
    zs_ = s_crotch + (s_top - s_crotch) * share
    zb = b_crotch + (b_top - b_crotch) * share
    sc, sx, sy = extent(pts, zs_)
    bc, bx, by = extent(hip_skin, zb)
    kx = max(min((bx + HIP_ROOM) / max(sx, 1.0), 1.4), 0.7)
    ky = max(min((by + HIP_ROOM) / max(sy, 1.0), 1.4), 0.7)
    hips.append((share, sc, bc, kx, ky))
    print("[fit] hips at %.2f: trousers %.1f by %.1f, body %.1f by %.1f" % (share, sx, sy, bx, by))
def map_hips(p):
    share = max(min((p.z - s_crotch) / max(s_top - s_crotch, 1e-3), 1.0), 0.0)
    for (s0, sc0, bc0, kx0, ky0), (s1, sc1, bc1, kx1, ky1) in zip(hips, hips[1:]):
        if share <= s1:
            t = (share - s0) / max(s1 - s0, 1e-6)
            t = t * t * (3 - 2 * t)
            sc, bc, kx, ky = sc0.lerp(sc1, t), bc0.lerp(bc1, t), kx0 + (kx1 - kx0) * t, ky0 + (ky1 - ky0) * t
            break
    zb = b_crotch + (b_top - b_crotch) * share
    return Vector((bc.x + (p.x - sc.x) * kx, bc.y + (p.y - sc.y) * ky, zb))

out = []
for p in pts:
    if p.z < s_crotch - BLEND_CM:
        out.append(map_leg(p))
    elif p.z > s_crotch + BLEND_CM:
        out.append(map_hips(p))
    else:
        t = (p.z - (s_crotch - BLEND_CM)) / (2 * BLEND_CM)
        t = t * t * (3 - 2 * t)
        out.append(map_leg(p).lerp(map_hips(p), t))

# ---- kept off the skin.
keep = [t for t in tris if any(top[k] in COVER for k in t)]
tree = BVHTree.FromPolygons(verts, keep, all_triangles=True)
nbrs = [set() for _ in out]
for poly in polys:
    for a in poly:
        for b in poly:
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
push, worst, count = through()
print("[fit] through the skin before pushing: %d points, deepest %.2f cm" % (count, worst))
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
    s = sum(total.values())
    return {bone: w / s for bone, w in total.items()}

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

for i, v in enumerate(out):
    me.vertices[i].co = v
if me.has_custom_normals:
    bpy.context.view_layer.objects.active = pants
    bpy.ops.mesh.customdata_custom_splitnormals_clear()
for poly in me.polygons:
    poly.use_smooth = True
me.update()
pants.name = "MedievalTrousers"
if me.materials:
    me.materials[0].name = "MedievalTrousers"
groups = {}
for i, v in enumerate(out):
    best = sorted(weigh(v).items(), key=lambda e: -e[1])[:4]
    tot = sum(w for _, w in best)
    for b, w in best:
        if w / tot > 0.02:
            g = groups.get(b)
            if g is None:
                g = groups[b] = pants.vertex_groups.new(name=names[b])
            g.add([i], w / tot, "REPLACE")
print("[fit] weighted to %d bones" % len(groups))
pants.parent = arm_obj
mod = pants.modifiers.new("Armature", "ARMATURE")
mod.object = arm_obj

bpy.ops.object.select_all(action="DESELECT")
arm_obj.select_set(True)
pants.select_set(True)
bpy.context.view_layer.objects.active = arm_obj
# The scene's units are centimetres, as the body's are.
bpy.context.scene.unit_settings.system = "METRIC"
bpy.context.scene.unit_settings.scale_length = 0.01
bpy.ops.export_scene.fbx(filepath=OUT, use_selection=True, object_types={"ARMATURE", "MESH"}, global_scale=1.0, apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE", axis_forward="-Y", axis_up="Z", bake_space_transform=False, use_mesh_modifiers=False, mesh_smooth_type="FACE", add_leaf_bones=False, primary_bone_axis="Y", secondary_bone_axis="X", use_armature_deform_only=False, armature_nodetype="NULL", bake_anim=False)

# ---- preview: the trousers on the body, front, side and back.
bme = bpy.data.meshes.new("Body")
bme.from_pydata([tuple(v) for v in verts], [], tris)
bme.update()
bo = bpy.data.objects.new("Body", bme)
bpy.context.scene.collection.objects.link(bo)
scn = bpy.context.scene
scn.render.engine = "BLENDER_WORKBENCH"
scn.render.resolution_x, scn.render.resolution_y = 700, 900
focus = Vector((0, 0, 60))
for name, direction in (("front", (0, -1, 0.05)), ("side", (1, 0, 0.05)), ("back", (0, 1, 0.05)), ("aslant", (0.7, -0.7, 0.3))):
    cam = bpy.data.cameras.new(name)
    cam.type = "ORTHO"
    cam.ortho_scale = 125
    co = bpy.data.objects.new(name, cam)
    scn.collection.objects.link(co)
    dd = Vector(direction).normalized()
    co.location = focus + dd * 400
    co.rotation_euler = (-dd).to_track_quat("-Z", "Y").to_euler()
    cam.clip_end = 2000
    scn.camera = co
    scn.render.filepath = PREVIEW + "_" + name + ".png"
    bpy.ops.render.render(write_still=True)
print("[fit] done")
