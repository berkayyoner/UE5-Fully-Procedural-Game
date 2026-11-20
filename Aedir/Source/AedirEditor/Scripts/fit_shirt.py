# blender -b --python fit_shirt.py -- shirt.fbx body.json out.fbx preview_prefix [skeleton.json [face.json ...]]
# Fits the A-posed linen shirt onto the bind pose of the body the creatures wear (exported with AedirImportModels
# -ExportBody=) and skins it, keeping the garment's own cut. With skeleton.json (a body exported on the skeleton the shirt
# is imported onto) only that skeleton's bones are used, the body's extra ones (correctives, twists) given to the nearest
# of them above; the further meshes (the face, with the neck) are only kept out of the cloth.
# - the torso is only moved down to the neck and widened as a whole, by one scale across and one front to back, just
#   enough for the body to fit inside it everywhere, so it still falls straight from the chest as it was made;
# - each sleeve is laid along the arm's bones, its own shape kept;
# - then it is laid onto the body as a sheet: round after round each point moves toward where it belongs, the moves
#   spread over its neighbours, never dented point by point: out of the body to a little room everywhere, and in onto the
#   skin over the arms, shoulders, back of the neck and upper back, where a shirt sits close; the chest and belly in
#   front keep their own fall;
# - weights come from the skin right under it (trunk skin for the trunk, arm skin for the sleeves, both over the
#   shoulders and behind the armpits);
# - the cloth is simulated on a sparser copy (the ClothProxy section) that the garment drawn follows.
import bpy, sys, json
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
argv = sys.argv[sys.argv.index("--") + 1:]
SRC, BODY, OUT, PREVIEW = argv[0], argv[1], argv[2], argv[3]
SKELETON = argv[4] if len(argv) > 4 else None
EXTRA = argv[5:]

CLEARANCE = 0.6          # cm the body keeps inside the shirt
FIT_SHARE = 0.7          # share of the chest's bands the widening fits as it is
SHOW_THROUGH_ROOM = 0.9  # cm the cloth stands off the skin at the least
HIP_ROOM = 1.2           # cm more of it round the hips
PROXY_RATIO = 0.3        # share of the points the simulated copy keeps
SHOULDER_SCALE = 0.92    # how much wider the shoulders are made than the shirt's own
COLLAR_SCALE = 0.88      # and the collar, drawn in round the neck
SLEEVE_SCALE = 0.84      # how much of its width the sleeve keeps round the arm
# Laid onto the body: over these parts, where the shirt stands off the skin by more than this (cm), it is drawn in onto
# it, the pull spread over the neighbours so the folds keep their shape. The chest and belly in front keep their own fall.
SLEEVE_GAP = 1.4
SHOULDER_GAP = 1.0
UPPER_BACK_GAP = 1.1
LOWER_BACK_GAP = 1.8
SIDE_GAP = 1.3
NECK_BACK_GAP = 0.8
SIDE_ROOM = 1.0          # cm more room down the sides of the chest under the arms, which the arms press into
CONFORM_ROUNDS = 40
CURTAIN_TOP = 136.0       # height (cm) of the chest the front hangs straight from
CURTAIN_HALF_WIDTH = 15.0 # how far across the front hangs so, fading to the body's own shape at the sides
CONFORM_SPREAD = 4       # how many neighbours over the pull is spread each round
SMOOTH_STEPS = 24        # the cloth is smoothed this much once laid on, so the body's own shapes (the muscles, the
SMOOTH_FLOOR = 0.8       # joints of the mannequin) do not show in it: fully where it was drawn in, this much elsewhere

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
ARMSKIN = {i for i, n in enumerate(names) if n.startswith(("upperarm", "lowerarm", "hand"))}
TORSO = {i for i, n in enumerate(names) if n.startswith(("pelvis", "spine", "neck", "clavicle"))}
HANDS = {i for i, n in enumerate(names) if n.startswith(("hand", "thumb", "index", "middle", "ring", "pinky"))}
collide = list(verts)
collide_tris = [t for t in tris if not any(top[k] in HANDS for k in t)]
for path in EXTRA:
    e = json.load(open(path))
    base = len(collide)
    collide += [FLIP(v[:3]) for v in e["verts"]]
    collide_tris += [tuple(base + k for k in e["tris"][i:i + 3]) for i in range(0, len(e["tris"]), 3)]
bvh = BVHTree.FromPolygons(collide, collide_tris, all_triangles=True)

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
    # Round the neck the collar is drawn in closer still.
    collar = smooth(z, 146.0, 152.0)
    sx *= 1.0 + (COLLAR_SCALE - 1.0) * collar
    sy *= 1.0 + (COLLAR_SCALE - 1.0) * collar
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
        return p + rot @ (off * SLEEVE_SCALE), t
    return mapper
maps = {1: sleeve_mapper(1.0), -1: sleeve_mapper(-1.0)}

out, sleeve_share = [], []
for v in src:
    side = 1 if v.x > 0 else -1
    pt, t = maps[side](v)
    s = smooth(abs(v.x), 17.0, 24.0) * (smooth(v.z + dz, 98.0, 104.0) if abs(v.x) < 30.0 else 1.0)
    out.append(torso_map(v).lerp(pt, s))
    sleeve_share.append(s)

probe = bvh.find_nearest(Vector((0, cy_body - 40, bone_pos("spine_03").z)), 60.0)
NSIGN = 1.0 if probe[1].y < 0 else -1.0
nbrs = [[] for _ in out]
for e in shirt.data.edges:
    a, b = e.vertices
    nbrs[a].append(b); nbrs[b].append(a)

# ---- laid onto the body over the arms, shoulders, the back of the neck and the upper back.
def region_of(i, v):
    # Which part of the shirt a point is, by where it lies on the body (the front of the body is -Y here).
    if sleeve_share[i] > 0.5:
        return "sleeve"
    back = v.y > cy_body
    if v.z > neck.z - 6.0:
        return "neck_back" if back else "collar"
    if v.z > 132.0:
        return "shoulder"
    if back and v.z > 112.0:
        return "upper_back"
    if abs(v.x) > 11.0 and v.z > 108.0:
        return "chest_side"
    return "chest" if not back else "lower_back"
def target_gap(i, v):
    # How close the cloth is drawn in, and how much of the way (0 keeps the shirt's own fall).
    s = sleeve_share[i]
    back = smooth(v.y - cy_body, -4.0, 2.0)
    upper_back = back * smooth(v.z, 108.0, 118.0)
    shoulder = smooth(v.z, 128.0, 136.0)
    lower_back = back * (1.0 - smooth(v.z, 108.0, 118.0)) * smooth(v.z, 100.0, 106.0)
    neck_back = back * smooth(v.z, neck.z - 10.0, neck.z - 4.0)
    side = smooth(abs(v.x), 12.0, 16.0) * (1.0 - back)
    pulls = ((s, SLEEVE_GAP), (shoulder * (1.0 - s), SHOULDER_GAP), (upper_back * (1.0 - s), UPPER_BACK_GAP),
             (lower_back * (1.0 - s), LOWER_BACK_GAP), (side * (1.0 - s), SIDE_GAP), (neck_back, NECK_BACK_GAP))
    w = max(p for p, _ in pulls)
    if w <= 0.0:
        return None, 0.0
    gap = sum(p * g for p, g in pulls) / max(sum(p for p, _ in pulls), 1e-6)
    return gap, w
def gap_report(label):
    gaps = {}
    for i, v in enumerate(out):
        hit = bvh.find_nearest(v, 40.0)
        if hit[0] is None:
            continue
        gaps.setdefault(region_of(i, v), []).append((v - hit[0]).dot(hit[1] * NSIGN))
    parts = []
    for r in sorted(gaps):
        g = sorted(gaps[r])
        parts.append("%s med %.1f p90 %.1f min %.1f" % (r, g[len(g) // 2], g[int(len(g) * 0.9)], g[0]))
    print("[fit] gaps %s: %s" % (label, "; ".join(parts)))
gap_report("before")
def room_of(i, v):
    # The least the cloth stands off the skin anywhere: more round the hips, which move most under the hem, and down the
    # sides of the chest under the arms, which the arms press into.
    t = 1.0 - sleeve_share[i]
    room = SHOW_THROUGH_ROOM + HIP_ROOM * (1.0 - smooth(v.z, 100.0, 110.0)) * t
    side = smooth(abs(v.x), 9.0, 13.0) * (1.0 - smooth(abs(v.y - cy_body), 5.0, 9.0))
    room += SIDE_ROOM * side * smooth(v.z, 106.0, 112.0) * (1.0 - smooth(v.z, 128.0, 134.0)) * t
    return room
# Each round every point is moved toward where it belongs, the moves spread a little over its neighbours so the cloth
# moves as a sheet: points inside the body or too near it go out to their room, and over the arms, shoulders, back of the
# neck and upper back points standing off too far come in to the gap asked there. Elsewhere the shirt keeps its fall.
def lay_on(rounds):
    for rnd in range(rounds):
        move = [Vector() for _ in out]
        n_in = 0
        for i, v in enumerate(out):
            hit = bvh.find_nearest(v, 40.0)
            if hit[0] is None:
                continue
            nrm = hit[1] * NSIGN
            gap = (v - hit[0]).dot(nrm)
            room = room_of(i, v)
            want, w = target_gap(i, v)
            # Where it is drawn in it also stands off by the gap asked, not closer.
            if want is not None:
                room += max(want - room, 0.0) * w
            if gap < room:
                move[i] = nrm * (room - gap)
                n_in += 1
            elif want is not None and gap > room:
                move[i] = -nrm * (gap - room) * w * 0.6
        for _ in range(CONFORM_SPREAD):
            move = [move[i] * 0.5 + (sum((move[j] for j in nbrs[i]), Vector()) / max(len(nbrs[i]), 1)) * 0.5 for i in range(len(out))]
        out[:] = [out[i] + move[i] for i in range(len(out))]
        if rnd % 5 == 0:
            print("[fit] round %d: %d points too near the body" % (rnd, n_in))
lay_on(CONFORM_ROUNDS)
# Smoothed without shrinking (a step in, then a slightly larger step back out), then laid on again for what that moved.
pull_w = [max(target_gap(i, v)[1], SMOOTH_FLOOR) for i, v in enumerate(out)]
for _ in range(SMOOTH_STEPS):
    for k in (0.5, -0.53):
        avg = [sum((out[j] for j in nbrs[i]), Vector()) / len(nbrs[i]) if nbrs[i] else out[i] for i in range(len(out))]
        out = [out[i] + (avg[i] - out[i]) * (k * pull_w[i]) for i in range(len(out))]
# From the chest down the front hangs straight, as linen does off a man's chest, instead of following the chest's shape
# in under it: no point of the front lies further in than the cloth above it in its own column, up to the top of the
# chest. Toward the sides it keeps to the body.
def hang_front():
    front = [i for i, v in enumerate(out) if sleeve_share[i] < 0.5 and v.y < cy_body and v.z < CURTAIN_TOP + 4.0]
    cols = {}
    for i in front:
        cols.setdefault(int(out[i].x // 2), []).append(i)
    moved = 0
    new = list(out)
    for i in front:
        v = out[i]
        if v.z >= CURTAIN_TOP:
            continue
        w = (1.0 - smooth(abs(v.x), CURTAIN_HALF_WIDTH - 4.0, CURTAIN_HALF_WIDTH)) * (1.0 - sleeve_share[i])
        if w <= 0.0:
            continue
        c = int(v.x // 2)
        most = v.y
        for cc in (c - 1, c, c + 1):
            for j in cols.get(cc, ()):
                u = out[j]
                if v.z < u.z <= CURTAIN_TOP and abs(u.x - v.x) < 2.5:
                    most = min(most, u.y)
        if most < v.y:
            new[i] = Vector((v.x, v.y + (most - v.y) * w, v.z))
            moved += 1
    out[:] = new
    print("[fit] %d points of the front hung straight" % moved)
hang_front()
lay_on(CONFORM_ROUNDS // 2)
# The last few still in are set out exactly.
for _ in range(3):
    n = 0
    for i, v in enumerate(out):
        hit = bvh.find_nearest(v, 20.0)
        if hit[0] is not None:
            nrm = hit[1] * NSIGN
            gap = (v - hit[0]).dot(nrm)
            room = SHOW_THROUGH_ROOM * 0.7
            if gap < room:
                out[i] = v + nrm * (room - gap)
                n += 1
print("[fit] %d points set out at the end" % n)
gap_report("final")

# ---- weights: each point takes the weights of the skin right under it, read off the nearest point of the body's surface
# (between the three corners of the triangle there), not off the nearest few corners, which on a sparse body reach over
# to the inside of the arm from the side of the chest.
FINGERS = HANDS - {i for i, n in enumerate(names) if n.startswith("hand")}
def surface_of(allowed):
    keep = [t for t in tris if any(top[k] in allowed for k in t) and not any(top[k] in FINGERS for k in t)]
    return BVHTree.FromPolygons(verts, keep, all_triangles=True), keep
surf_arm = surface_of(ARMSKIN)
surf_all = surface_of(TORSO | ARMSKIN)
def weigh(v, surf):
    tree, keep = surf
    loc, _, face, _ = tree.find_nearest(v)
    a, b, c = (verts[k] for k in keep[face])
    # Barycentric share of each corner at the nearest point.
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
    # The trunk of the shirt follows whatever skin lies right under it: the trunk's, or over the shoulders and behind the
    # armpits the arm's (the deltoid, the back of the arm), which carries the cloth there with the arm.
    wt = weigh(v, surf_all) if s < 0.999 else {}
    wa = weigh(v, surf_arm) if s > 0.001 else {}
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
bme.from_pydata([tuple(v) for v in collide], [], collide_tris)
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
