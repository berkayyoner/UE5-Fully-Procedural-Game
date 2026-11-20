# blender -b --python fit_shirt.py -- shirt.fbx manny.json out.fbx preview_prefix
# Fits the A-posed linen shirt onto the UE5 mannequin's bind pose and skins it, keeping the garment's own cut:
# - the torso is only moved down to the neck and widened as a whole, by one scale across and one front to back, just
#   enough for the body to fit inside it everywhere, so it still falls straight from the chest as it was made;
# - each sleeve is laid along the arm's bones, its own shape kept;
# - where the body still shows through, the cloth is eased out by a broad, smooth shift spread over its neighbours, never
#   dented point by point;
# - weights come from the skin under it (trunk skin for the trunk, arm skin for the sleeves, both over the shoulders);
# - the cloth is simulated on a sparser copy (the ClothProxy section) that the garment drawn follows.
import bpy, sys, json
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree
argv = sys.argv[sys.argv.index("--") + 1:]
SRC, BODY, OUT, PREVIEW = argv[0], argv[1], argv[2], argv[3]

CLEARANCE = 0.6          # cm the body keeps inside the shirt
FIT_SHARE = 0.7          # share of the chest's bands the widening fits as it is
SHOW_THROUGH_ROOM = 1.2  # cm a point the body still reaches is eased out to
HIP_ROOM = 1.3           # cm more of it round the hips
EASE_STEPS = 90          # how far that easing is spread over the cloth
PROXY_RATIO = 0.3        # share of the points the simulated copy keeps
SHOULDER_SCALE = 1.0     # how much wider the shoulders are made than the shirt's own
MAX_EASE_GAIN = 2.5      # how much the spread easing is scaled back up

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
ARMSKIN = {i for i, n in enumerate(names) if n.startswith(("upperarm", "lowerarm", "hand"))}
TORSO = {i for i, n in enumerate(names) if n.startswith(("pelvis", "spine", "neck", "clavicle"))}
HANDS = {i for i, n in enumerate(names) if n.startswith(("hand", "thumb", "index", "middle", "ring", "pinky"))}
bvh = BVHTree.FromPolygons(verts, [t for t in tris if not any(top[k] in HANDS for k in t)], all_triangles=True)

def smooth(x, a, b):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.context.scene.unit_settings.scale_length = 0.01
bpy.ops.import_scene.fbx(filepath=SRC)
shirt = [o for o in bpy.data.objects if o.type == "MESH"][0]
src = [(shirt.matrix_world @ v.co) for v in shirt.data.vertices]
unit = 100.0 if max(v.z for v in src) - min(v.z for v in src) < 10 else 1.0
src = [v * unit for v in src]
src_polys = [tuple(p.vertices) for p in shirt.data.polygons]
print("[fit] shirt z %.1f..%.1f, %d points" % (min(v.z for v in src), max(v.z for v in src), len(src)))

# ---- torso: down to the neck, then one scale across and one front to back, the least that holds the body inside.
neck = bone_pos("neck_01")
dz = (neck.z + 2.0) - max(v.z for v in src)
SHIRT_SIDE = 18.5  # past this across, the shirt below the armpits is sleeve
def band(points, z, half=1.5):
    return [p for p in points if abs(p.z - z) < half]
body_torso = [verts[i] for i in range(len(verts)) if top[i] in TORSO]
shirt_torso = [Vector((v.x, v.y, v.z + dz)) for v in src if abs(v.x) < SHIRT_SIDE]
cy_shirt = (min(v.y for v in band(shirt_torso, 120, 4)) + max(v.y for v in band(shirt_torso, 120, 4))) / 2
cy_body = (min(v.y for v in band(body_torso, 120, 4)) + max(v.y for v in band(body_torso, 120, 4))) / 2
# Each band of the chest and belly asks a widening across and a depth; the shirt takes what most of them ask (not the
# single worst), the body's centre front to back at each height, and the few points the body still reaches are eased.
asks_x, asks_y, centres = [], [], []
for z in range(104, 136, 2):
    b, s = band(body_torso, z), band(shirt_torso, z)
    if len(b) < 10 or len(s) < 10:
        continue
    asks_x.append((max(abs(p.x) for p in b) + CLEARANCE) / max(abs(p.x) for p in s))
    depth_b = max(p.y for p in b) - min(p.y for p in b)
    depth_s = max(p.y for p in s) - min(p.y for p in s)
    asks_y.append((depth_b + 2.0 * CLEARANCE) / depth_s)
    centres.append(((min(p.y for p in b) + max(p.y for p in b)) / 2, (min(p.y for p in s) + max(p.y for p in s)) / 2))
def share_of(values, share):
    v = sorted(values)
    return v[min(len(v) - 1, int(len(v) * share))]
SX = max(1.0, share_of(asks_x, FIT_SHARE))
SY = max(1.0, share_of(asks_y, FIT_SHARE))
cy_body = sum(c[0] for c in centres) / len(centres)
cy_shirt = sum(c[1] for c in centres) / len(centres)
print("[fit] torso down %.1f, scale across %.3f, front to back %.3f" % (-dz, SX, SY))
def torso_map(v):
    # Over the shoulders and up to the collar the shirt keeps its own size: the chest's widening fades out above the armpits.
    z = v.z + dz
    top = smooth(z, 134.0, 144.0)
    # Front to back the chest's depth holds on over the shoulder blades, up to the base of the neck.
    top_y = smooth(z, 140.0, 150.0)
    sx = SX + (SHOULDER_SCALE - SX) * top
    sy = SY + (SHOULDER_SCALE - SY) * top_y
    return Vector((v.x * sx, (v.y - cy_shirt) * sy + cy_body, z))

# ---- sleeves: the sleeve's own axis laid along upper arm and forearm, its shape kept.
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
    start = S + d1 * 2.0
    total = L1 + L2 - 2.0
    print("[fit] sleeve", side, "length %.1f -> %.1f" % (length, total))
    def mapper(v):
        rel = v - a
        t = rel.dot(axis) / length
        off = rel - axis * rel.dot(axis)
        along = t * total
        share = smooth(along, L1 - 8.0, L1 + 4.0)
        rot = rot1.slerp(rot2, share)
        p = start + d1 * along if along < L1 - 2.0 else E + d2 * (along - (L1 - 2.0))
        return p + rot @ off, t
    return mapper
maps = {1: sleeve_mapper(1.0), -1: sleeve_mapper(-1.0)}

out, sleeve_share = [], []
for v in src:
    side = 1 if v.x > 0 else -1
    pt, t = maps[side](v)
    s = smooth(abs(v.x), 17.0, 24.0) * (smooth(v.z + dz, 98.0, 104.0) if abs(v.x) < 30.0 else 1.0)
    out.append(torso_map(v).lerp(pt, s))
    sleeve_share.append(s)

# ---- where the body still shows through, eased out broadly.
probe = bvh.find_nearest(Vector((0, cy_body - 40, bone_pos("spine_03").z)), 60.0)
NSIGN = 1.0 if probe[1].y < 0 else -1.0
nbrs = [[] for _ in out]
for e in shirt.data.edges:
    a, b = e.vertices
    nbrs[a].append(b); nbrs[b].append(a)
def inside_shift():
    shift = [Vector() for _ in out]
    n = 0
    for i, v in enumerate(out):
        hit = bvh.find_nearest(v, 20.0)
        if hit[0] is not None:
            gap = (v - hit[0]).dot(hit[1] * NSIGN)
            # More room round the hips, which move most under the hem.
            room = SHOW_THROUGH_ROOM + HIP_ROOM * (1.0 - smooth(v.z, 100.0, 110.0)) * (1.0 - sleeve_share[i])
            if gap < room:
                shift[i] = hit[1] * NSIGN * (room - gap)
                n += 1
    return shift, n
for rnd in range(6):
    shift, n = inside_shift()
    print("[fit] round %d: %d points too close to the body" % (rnd, n))
    if n == 0:
        break
    # Spread out, then scaled back up so the points that were in are out, the rest moved with them gently.
    peak = [s.length for s in shift]
    for _ in range(EASE_STEPS):
        shift = [shift[i] * 0.5 + (sum((shift[j] for j in nbrs[i]), Vector()) / max(len(nbrs[i]), 1)) * 0.5 for i in range(len(out))]
    gain = max((peak[i] / shift[i].length for i in range(len(out)) if peak[i] > 0.0 and shift[i].length > 1e-6), default=1.0)
    gain = min(gain, MAX_EASE_GAIN)
    out = [out[i] + shift[i] * gain for i in range(len(out))]
# The last few are eased out over a few neighbours at a time, then set out exactly.
for rnd in range(5):
    shift, n = inside_shift()
    if n == 0:
        break
    peak = [x.length for x in shift]
    for _ in range(6):
        shift = [shift[i] * 0.5 + (sum((shift[j] for j in nbrs[i]), Vector()) / max(len(nbrs[i]), 1)) * 0.5 for i in range(len(out))]
    gain = min(max((peak[i] / shift[i].length for i in range(len(out)) if peak[i] > 0.0 and shift[i].length > 1e-6), default=1.0), 2.0)
    out = [out[i] + shift[i] * gain for i in range(len(out))]
shift, n = inside_shift()
out = [out[i] + shift[i] for i in range(len(out))]
print("[fit] %d points set out at the end" % n)

# ---- weights
def tree_of(allowed):
    ids = [i for i in range(len(verts)) if top[i] in allowed]
    t = KDTree(len(ids))
    for k, i in enumerate(ids):
        t.insert(verts[i], k)
    t.balance()
    return t, ids
tree_torso, ids_torso = tree_of(TORSO)
tree_arm, ids_arm = tree_of(ARMSKIN)
tree_all, ids_all = tree_of(TORSO | ARMSKIN)
def weigh(v, tree, ids):
    total = {}
    for co, k, dist in tree.find_n(v, 8):
        f = 1.0 / (dist * dist + 1.0)
        for b, w in weights[ids[k]].items():
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
    shoulder = smooth(v.z, 122.0, 132.0)
    wt = {}
    if s < 0.999:
        low = weigh(v, tree_torso, ids_torso)
        high = weigh(v, tree_all, ids_all) if shoulder > 0.0 else {}
        for b in set(low) | set(high):
            wt[b] = low.get(b, 0.0) * (1.0 - shoulder) + high.get(b, 0.0) * shoulder
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

def clean(obj, threshold):
    # Points on top of each other are one, loose edges are gone (they come out as triangles with no area), and so are
    # triangles with no area: cloth cannot be made of them.
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    nv = len(obj.data.vertices)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.remove_doubles(threshold=threshold)
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.delete_loose(use_verts=True, use_edges=True, use_faces=False)
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.dissolve_degenerate(threshold=threshold)
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.quads_convert_to_tris()
    bpy.ops.mesh.dissolve_degenerate(threshold=threshold)
    bpy.ops.object.mode_set(mode="OBJECT")
    print("[fit] %s: %d points, %d after cleaning" % (obj.name, nv, len(obj.data.vertices)))

clean(shirt, 0.005)
proxy = shirt.copy()
proxy.data = shirt.data.copy()
proxy.name = "MedievalShirtCloth"
bpy.context.scene.collection.objects.link(proxy)
proxy.data.materials.clear()
proxy.data.materials.append(bpy.data.materials.new("ClothProxy"))
dec = proxy.modifiers.new("Fewer", "DECIMATE")
dec.decimate_type = "COLLAPSE"
dec.ratio = PROXY_RATIO
dec.use_collapse_triangulate = True
bpy.ops.object.select_all(action="DESELECT")
proxy.select_set(True)
bpy.context.view_layer.objects.active = proxy
bpy.ops.object.modifier_move_to_index(modifier="Fewer", index=0)
bpy.ops.object.modifier_apply(modifier="Fewer")
clean(proxy, 0.15)

bpy.ops.object.select_all(action="DESELECT")
arm_obj.select_set(True)
shirt.select_set(True)
proxy.select_set(True)
bpy.context.view_layer.objects.active = arm_obj
bpy.ops.export_scene.fbx(filepath=OUT, use_selection=True, object_types={"ARMATURE", "MESH"}, global_scale=1.0, apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE", axis_forward="-Y", axis_up="Z", bake_space_transform=False, use_mesh_modifiers=False, mesh_smooth_type="FACE", add_leaf_bones=False, primary_bone_axis="Y", secondary_bone_axis="X", use_armature_deform_only=False, armature_nodetype="NULL", bake_anim=False)

# ---- preview: the original shirt beside the fitted one on the body, front and side.
proxy.hide_render = True
bme = bpy.data.meshes.new("Body")
bme.from_pydata([tuple(v) for v in verts], [], tris)
bme.update()
bo = bpy.data.objects.new("Body", bme)
bpy.context.scene.collection.objects.link(bo)
orig = bpy.data.meshes.new("Original")
orig.from_pydata([tuple(Vector((v.x, (v.y - cy_shirt) + cy_body, v.z + dz)) + Vector((90.0, 0.0, 0.0))) for v in src], [], src_polys)
orig.update()
oo = bpy.data.objects.new("Original", orig)
bpy.context.scene.collection.objects.link(oo)
c = Vector((45, cy_body, 115))
scn = bpy.context.scene
scn.render.engine = "BLENDER_WORKBENCH"
scn.render.resolution_x, scn.render.resolution_y = 900, 600
for name, direction in (("front", (0, -1, 0)), ("side", (1, 0, 0)), ("back", (0, 1, 0)), ("collar", (0, -1, 0.6)), ("collarside", (1, 0, 0.2))):
    cam = bpy.data.cameras.new(name)
    cam.type = "ORTHO"
    cam.ortho_scale = 45 if name.startswith("collar") else 210 if name != "side" else 140
    co = bpy.data.objects.new(name, cam)
    scn.collection.objects.link(co)
    dd = Vector(direction)
    focus = Vector((0, cy_body, neck.z - 4)) if name.startswith("collar") else c if name != "side" else Vector((0, cy_body, 115))
    dd = dd.normalized()
    co.location = focus + dd * 400
    co.rotation_euler = (-dd).to_track_quat("-Z", "Y").to_euler()
    cam.clip_end = 2000
    scn.camera = co
    scn.render.filepath = PREVIEW + "_" + name + ".png"
    bpy.ops.render.render(write_still=True)
print("[fit] done")
