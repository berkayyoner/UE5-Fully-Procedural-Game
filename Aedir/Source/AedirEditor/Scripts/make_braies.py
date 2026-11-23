# blender -b --python make_braies.py -- body.json out_folder preview_prefix [skeleton.json|-] [trousers]
# With "trousers" it makes the wool trousers worn over them instead, the same way: down to the ankles, the shins wound
# round with bands, the cloth roomy over the thigh and bunched over the knee and the ankle.
# Makes the linen braies every body wears under all else, from the body itself: short, roomy drawers gathered on an
# elastic waistband tied with a drawstring at the front, the legs ending a hand's breadth down the thigh in a turned hem.
# Nothing is fitted from another model, so there is nothing to stretch out of shape: the cloth is lofted round the body.
# - The body (exported with AedirImportModels -ExportBody=) is cut across at every height; the cloth goes round the outer
#   hull of each cut (the buttocks bridged as cloth bridges them, never pulled into the cleft), ROOM off it: the seat
#   above the crotch, each leg below it, the two legs meeting under the crotch along a gusset seam.
# - The waistband is gathered into GATHERS folds that die out below it; the legs widen to the hem and hang in soft folds.
# - The top and the hems are turned over inside, so the openings show the cloth's thickness, not a paper edge.
# - Weights come from the skin under each point, smoothed over the cloth; the drawstring follows the pelvis.
# - The linen's colour, normal and masks are drawn here as a tiling weave (T_MedievalBraies_A/N/RMF.png).
import bpy, sys, json, math, random, os
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:]
BODY, OUTDIR, PREVIEW = os.path.abspath(argv[0]), os.path.abspath(argv[1]), os.path.abspath(argv[2])
SKELETON = argv[3] if len(argv) > 3 and argv[3] != "-" else None
NAME = "MedievalBraies"
os.makedirs(OUTDIR, exist_ok=True)

WAIST_ABOVE_PELVIS = 7.0   # cm the top of the waistband stands over the pelvis bone
BAND_CM = 3.2              # how tall the waistband is
HEM_INNER_CM = 9.0         # the hem lies this far under the crotch on the inside of the leg
HEM_OUTER_CM = 5.0         # and this far on the outside, the leg opening rising to the side
WAIST_ROOM = 1.0           # cm the elastic stands off the skin
SEAT_ROOM = 2.2            # round the hips and the seat
LEG_ROOM_TOP, LEG_ROOM_HEM = 2.8, 3.9   # the legs widen to the hem
INNER_ROOM = 1.6           # the inside of a leg keeps closer, where the thighs meet
CROTCH_SAG = 2.2           # cm the gusset hangs under the body
GATHERS = 26               # folds the waistband draws the cloth into
GATHER_CM = 0.55           # how deep they are just under the band
GATHER_FADE_CM = 7.0       # and how far down they die out
LEG_FOLD_CM = 0.45         # soft folds down the legs, deepest at the hem
K = 26                     # points round each half of the seat
M = 9                      # spans along the gusset between front and back
ROW_CM = 1.2               # rows of the seat this far apart
LEG_ROWS = 9
TILE_CM = 11.0             # the weave repeats every this many centimetres
WEIGHT_ROUNDS = 10
TROUSERS = len(argv) > 4 and argv[4] == "trousers"
WOOL = (74, 63, 52)       # the trousers' wool; the braies are undyed linen
LINEN = (201, 189, 163)
if TROUSERS:
    NAME = "MedievalTrousers"
    WAIST_ABOVE_PELVIS, BAND_CM = 9.0, 4.0
    GATHERS, GATHER_CM = 16, 0.4
    SEAT_ROOM, LEG_ROOM_TOP = 2.6, 3.2
    LEG_ROWS = 34
    LEG_FOLD_CM = 0.5
ANKLE_ABOVE_CM = 6.0      # the trousers end this far over the ankle joint, into the bands
BAND_FROM_KNEE_CM = 9.0   # the shin bands wind up to this far under the knee
BAND_WIDTH_CM, BAND_PITCH_CM = 4.2, 3.0
random.seed(7)

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
ARMS = {i for i in range(len(names)) if under(i, "clavicle_l") or under(i, "clavicle_r")}
# The trunk and the legs only: the arms hang beside the hips at these heights and are no part of what is clothed.
body_tris = [t for t in tris if not any(top[k] in ARMS for k in t)]

pelvis = bone_pos("pelvis")
thigh_l, thigh_r = bone_pos("thigh_l"), bone_pos("thigh_r")
knee_z = (bone_pos("calf_l").z + bone_pos("calf_r").z) * 0.5
MID_X = (thigh_l.x + thigh_r.x) * 0.5
LEFT_SIGN = 1.0 if thigh_l.x > MID_X else -1.0
FRONT = Vector((0, -1, 0))   # the body faces -Y here

def section(z, side=0):
    """Where the skin crosses the height z, as flat points; side -1/1 keeps one leg's (by which side of the middle)."""
    pts = []
    for t in body_tris:
        a, b, c = verts[t[0]], verts[t[1]], verts[t[2]]
        ps = []
        for p, q in ((a, b), (b, c), (c, a)):
            if (p.z - z) * (q.z - z) < 0:
                s = (z - p.z) / (q.z - p.z)
                ps.append(Vector((p.x + (q.x - p.x) * s, p.y + (q.y - p.y) * s)))
        for p in ps:
            if side == 0 or (p.x - MID_X) * side * LEFT_SIGN > 0:
                pts.append(p)
    return pts

def hull(pts):
    pts = sorted(set((round(p.x, 3), round(p.y, 3)) for p in pts))
    if len(pts) < 3:
        return [Vector(p) for p in pts]
    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return [Vector(p) for p in lower[:-1] + upper[:-1]]

def centre(h):
    return sum(h, Vector((0, 0))) / max(len(h), 1)

def ray_hull(h, c, way):
    """How far from c the hull's edge is along way."""
    best = 0.0
    for i in range(len(h)):
        p, q = h[i], h[(i + 1) % len(h)]
        e = q - p
        den = way.x * (-e.y) - way.y * (-e.x)
        if abs(den) < 1e-9:
            continue
        rel = p - c
        t = (rel.x * (-e.y) - rel.y * (-e.x)) / den
        s = (way.x * rel.y - way.y * rel.x) / den
        if t > 0 and -1e-6 <= s <= 1 + 1e-6:
            best = max(best, t)
    return best

# ---- the crotch: the lowest of the trunk's own skin in the middle between the legs (the thighs may touch lower down).
PELVIS_BONES = {idx["pelvis"]} | ({idx["spine_01"]} if "spine_01" in idx else set())
mid_low = [v.z for i, v in enumerate(verts) if abs(v.x - MID_X) < 1.2 and knee_z < v.z < pelvis.z and top[i] in PELVIS_BONES]
crotch_z = min(mid_low) if mid_low else pelvis.z - 10.0
waist_z = pelvis.z + WAIST_ABOVE_PELVIS
print("[braies] pelvis %.1f, crotch %.1f, waist %.1f, knee %.1f" % (pelvis.z, crotch_z, waist_z, knee_z))

# The way round each half of the seat, from the back to the front by the outside: angles in the flat, measured with the
# front at 0 and the left side at +90 (or -90 for the right half).
def seat_way(k, half):
    a = math.pi - math.pi * k / K   # pi at the back, 0 at the front
    side = LEFT_SIGN * half
    return Vector((math.sin(a) * side, -math.cos(a)))

seat_cache = {}
def seat_hull(z):
    key = round(z, 2)
    if key not in seat_cache:
        h = hull(section(z))
        seat_cache[key] = (h, centre(h))
    return seat_cache[key]

def leg_hull(z, half):
    h = hull(section(z, half))
    return h, centre(h)

def gather(z, arc_share):
    # Under the waistband the elastic draws the cloth into folds that die out lower down.
    below = waist_z - BAND_CM - z
    if below < 0:
        return 0.0
    return GATHER_CM * math.exp(-below / GATHER_FADE_CM) * (0.5 + 0.5 * math.cos(2 * math.pi * GATHERS * arc_share))

def seat_room(z):
    if z > waist_z - BAND_CM:
        # The band itself, rounded across its height like rolled elastic under the cloth.
        s = (waist_z - z) / BAND_CM
        return WAIST_ROOM + 0.35 * math.sin(math.pi * min(max(s, 0.0), 1.0))
    s = min(max((waist_z - BAND_CM - z) / 6.0, 0.0), 1.0)
    return WAIST_ROOM + (SEAT_ROOM - WAIST_ROOM) * (s * s * (3 - 2 * s))

def seat_point(z, k, half):
    h, c = seat_hull(z)
    way = seat_way(k, half)
    r = ray_hull(h, c, way)
    arc = (k / K) * 0.5 + (0.5 if half > 0 else 0.0)
    out = c + way * (r + seat_room(z) + gather(z, arc))
    return Vector((out.x, out.y, z))

# ---- rows: the turned-over top, the band and the seat down to the crotch, then the two legs.
rows = []
# The top edge rolls over to the inside: the inner lip first (hidden inside, against the skin), then the fold.
rows.append(("lip", waist_z - 1.4))
rows.append(("roll", waist_z + 0.35))
zz = waist_z
while zz > crotch_z + 0.6:
    rows.append(("seat", zz))
    zz -= ROW_CM if zz < waist_z - BAND_CM else BAND_CM / 4.0
rows.append(("seat", crotch_z + 0.6))

pts = []
uvs_of = {}
faces = []
ring_ids = []
for kind, z in rows:
    ids = []
    for half in (1, -1):
        for k in range(K + 1):
            if half == -1 and (k == 0 or k == K):
                continue  # the back and the front middle are shared by both halves
            p = seat_point(z, k, half)
            if kind == "lip":
                p = seat_point(waist_z - 1.4, k, half)
                c = seat_hull(waist_z - 1.4)[1]
                flat = Vector((p.x - c.x, p.y - c.y))
                flat.length = max(flat.length - seat_room(waist_z - 1.4) - 0.1 + 0.25, 0.1)
                p = Vector((c.x + flat.x, c.y + flat.y, waist_z - 1.4))
            elif kind == "roll":
                p = seat_point(waist_z, k, half)
                c = seat_hull(waist_z)[1]
                flat = Vector((p.x - c.x, p.y - c.y))
                flat.length -= 0.35
                p = Vector((c.x + flat.x, c.y + flat.y, waist_z + 0.35))
            ids.append(len(pts))
            pts.append(p)
    ring_ids.append(ids)
# Order each ring back -> left -> front -> right -> back: the left half went 0..K, the right K-1..1 must run front to back.
def ordered(ids):
    left = ids[:K + 1]
    right = ids[K + 1:]          # k = 1..K-1 of the right half, back to front
    return left + list(reversed(right))
ring_ids = [ordered(r) for r in ring_ids]
N = 2 * K
for a, b in zip(ring_ids, ring_ids[1:]):
    for i in range(N):
        faces.append((a[i], a[(i + 1) % N], b[(i + 1) % N], b[i]))

# ---- the legs. The top row of each is the seat's bottom row half (the outside) and the gusset (the inside), shared by both.
bottom = ring_ids[-1]
back_id, front_id = bottom[0], bottom[K]
back_p, front_p = pts[back_id], pts[front_id]
# The gusset runs under the crotch from the front middle to the back middle, hanging CROTCH_SAG under the skin there.
def body_bottom(y):
    best = crotch_z
    for v in verts:
        if abs(v.x - MID_X) < 1.5 and abs(v.y - y) < 1.5 and knee_z < v.z < pelvis.z and v.z < best + 3:
            best = min(best, v.z) if v.z > crotch_z - 6 else best
    return best
gusset = []
for m in range(1, M):
    s = m / M
    y = front_p.y + (back_p.y - front_p.y) * s
    z = min(front_p.z + (back_p.z - front_p.z) * s, body_bottom(y)) - CROTCH_SAG * math.sin(math.pi * s) ** 0.5
    gusset.append(len(pts))
    pts.append(Vector((MID_X, y, z)))

ankle_z = (bone_pos("foot_l").z + bone_pos("foot_r").z) * 0.5
def hem_z(outer_share):
    if TROUSERS:
        return ankle_z + ANKLE_ABOVE_CM
    return crotch_z - HEM_INNER_CM + (HEM_INNER_CM - HEM_OUTER_CM) * outer_share

def trouser_room(z, inward):
    # Roomy over the thigh, closer at the knee, close on the shin under the bands.
    shin_top = knee_z - BAND_FROM_KNEE_CM
    if z > knee_z:
        s_ = (crotch_z - z) / max(crotch_z - knee_z, 1.0)
        out = LEG_ROOM_TOP + 0.4 * math.sin(math.pi * min(max(s_, 0.0), 1.0))
    elif z > shin_top:
        s_ = (knee_z - z) / max(BAND_FROM_KNEE_CM, 1.0)
        out = LEG_ROOM_TOP - 0.4 - (LEG_ROOM_TOP - 1.3) * s_
    else:
        out = 0.9
    return out * (1 - inward) + min(out, INNER_ROOM) * inward

def trouser_fold(z, arc, half):
    # Bunched over the knee and where the bands take the cloth in.
    knee = math.exp(-((z - knee_z) / 5.0) ** 2)
    shin = math.exp(-((z - (knee_z - BAND_FROM_KNEE_CM)) / 3.0) ** 2)
    return (0.25 + 0.7 * knee + 0.6 * shin) * LEG_FOLD_CM * (0.6 * math.sin(2 * math.pi * 6 * arc + half + z * 0.15) + 0.4 * math.sin(2 * math.pi * 3 * arc + 2.1 * half - z * 0.07))

legs = {}
for half in (1, -1):
    # The top ring: back middle, the outside of this half to the front middle, then the gusset back.
    left = half == 1
    outside = bottom[:K + 1] if left else [bottom[0]] + list(reversed(bottom[K:]))
    top_ring = outside + gusset
    tops = [pts[i].copy() for i in top_ring]
    L = len(top_ring)
    inner_dir = Vector((-LEFT_SIGN * half, 0))
    prev = top_ring
    leg_rings = [top_ring]
    for r in range(1, LEG_ROWS + 1):
        t = r / LEG_ROWS
        ids = []
        for j in range(L):
            # How far round the outside this column is (0 at the inside of the leg, 1 at its outer side).
            if j <= K:
                outer = math.sin(math.pi * j / K)
            else:
                outer = 0.0
            zt = tops[j].z + (hem_z(outer) - tops[j].z) * t
            h, c = leg_hull(zt, half)
            # The way out from the middle of the leg this column goes: the top ring's point seen from the leg's middle
            # there, so the columns run straight down the leg.
            flat_top = Vector((tops[j].x, tops[j].y))
            way = (flat_top - c)
            if way.length < 1e-6:
                way = inner_dir.copy()
            way.normalize()
            rr = ray_hull(h, c, way)
            inward = max(way.dot(inner_dir), 0.0)
            arc = j / L
            if TROUSERS:
                room = trouser_room(zt, inward)
                fold = trouser_fold(zt, arc, half)
            else:
                room = (LEG_ROOM_TOP + (LEG_ROOM_HEM - LEG_ROOM_TOP) * t) * (1 - inward) + (INNER_ROOM + 0.8 * t) * inward
                fold = LEG_FOLD_CM * t * (0.6 * math.sin(2 * math.pi * 7 * arc + half) + 0.4 * math.sin(2 * math.pi * 3 * arc + 2.1 * half))
            target = c + way * (rr + room + fold)
            # Down from the gusset and the seat it comes round onto the leg over the first rows.
            w = min(t / (0.12 if TROUSERS else 0.45), 1.0)
            w = w * w * (3 - 2 * w)
            flat = Vector((tops[j].x, tops[j].y)) * (1 - w) + target * w
            p = Vector((flat.x, flat.y, zt))
            # The two legs never cross the middle into each other.
            if (p.x - MID_X) * LEFT_SIGN * half < 0.35:
                p.x = MID_X + 0.35 * LEFT_SIGN * half
            ids.append(len(pts))
            pts.append(p)
        leg_rings.append(ids)
    # The hem turns up inside: a fold a little out from the last row, then the lip back up inside.
    last = leg_rings[-1]
    fold_ids, lip_ids = [], []
    for j, i in enumerate(last):
        p = pts[i]
        h, c = leg_hull(p.z, half)
        way = Vector((p.x - c.x, p.y - c.y))
        way.normalize()
        fold_ids.append(len(pts))
        pts.append(Vector((p.x + way.x * 0.25, p.y + way.y * 0.25, p.z - 0.45)))
        lip_ids.append(len(pts))
        pts.append(Vector((p.x - way.x * 0.35, p.y - way.y * 0.35, p.z + 1.3)))
    leg_rings += [fold_ids, lip_ids]
    for a, b in zip(leg_rings, leg_rings[1:]):
        for j in range(L):
            jj = (j + 1) % L
            if left:
                faces.append((a[j], a[jj], b[jj], b[j]))
            else:
                faces.append((a[jj], a[j], b[j], b[jj]))
    legs[half] = leg_rings
    # The shins wound round with bands from the ankle up, each turn lying half over the one under it.
    if TROUSERS:
        z0, z1 = hem_z(0) - 2.0, knee_z - BAND_FROM_KNEE_CM
        turns = (z1 - z0) / BAND_PITCH_CM
        steps = int(turns * 20)
        edge_lo, edge_hi = [], []
        for i in range(steps + 1):
            a = 2 * math.pi * turns * i / steps * half
            zc = z0 + (z1 - z0) * i / steps
            for dz, store in ((-BAND_WIDTH_CM * 0.5, edge_lo), (BAND_WIDTH_CM * 0.5, edge_hi)):
                zb = zc + dz
                h, c = leg_hull(zb, half)
                way = Vector((math.sin(a), -math.cos(a)))
                rr = ray_hull(h, c, way)
                # Each turn stands over the cloth and over the turn under it, its lower edge tucked in.
                lift = 1.25 + (0.2 if dz > 0 else -0.05)
                store.append(len(pts))
                pts.append(Vector((c.x + way.x * (rr + lift), c.y + way.y * (rr + lift), zb)))
        for i in range(steps):
            faces.append((edge_lo[i], edge_lo[i + 1], edge_hi[i + 1], edge_hi[i]))

# ---- the cloth as a mesh, with its weave laid round it.
me = bpy.data.meshes.new(NAME)
me.from_pydata([tuple(p) for p in pts], [], faces)
me.validate()
me.update()
bpy.ops.wm.read_factory_settings(use_empty=True)
me = bpy.data.meshes.new(NAME)
me.from_pydata([tuple(p) for p in pts], [], faces)
me.update()
obj = bpy.data.objects.new(NAME, me)
bpy.context.scene.collection.objects.link(obj)
import bmesh
bm = bmesh.new()
bm.from_mesh(me)
bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
# Outward: the most of the cloth faces away from the body's middle.
out_votes = 0
for f in bm.faces:
    c = f.calc_center_median()
    out_votes += 1 if f.normal.dot(Vector((c.x - MID_X, c.y - pelvis.y, 0))) > 0 else -1
if out_votes < 0:
    for f in bm.faces:
        f.normal_flip()
uv = bm.loops.layers.uv.new("UVMap")
for f in bm.faces:
    for loop in f.loops:
        v = loop.vert.co
        # Round the body by the way out from the middle of the seat (or the leg), up by height: a tiling weave needs no
        # unwrapping, only to run on across the seams.
        ang = math.atan2(v.x - MID_X, -(v.y - pelvis.y))
        loop[uv].uv = (ang * 30.0 / TILE_CM, v.z / TILE_CM)
# Seams where the angle wraps round at the back: the far side of a face across the wrap is taken a turn on.
for f in bm.faces:
    us = [loop[uv].uv.x for loop in f.loops]
    if max(us) - min(us) > (math.pi * 30.0 / TILE_CM):
        for loop in f.loops:
            if loop[uv].uv.x < 0:
                loop[uv].uv.x += 2 * math.pi * 30.0 / TILE_CM
bm.to_mesh(me)
bm.free()
for poly in me.polygons:
    poly.use_smooth = True
# A little softening, the folds kept.
sm = obj.modifiers.new("Soft", "SMOOTH")
sm.factor, sm.iterations = 0.35, 2
bpy.context.view_layer.objects.active = obj
obj.select_set(True)
bpy.ops.object.modifier_apply(modifier="Soft")
mat = bpy.data.materials.new(NAME)
me.materials.append(mat)
print("[braies] cloth: %d points, %d faces" % (len(me.vertices), len(me.polygons)))

# ---- the drawstring, tied in a bow at the front of the band.
knot = seat_point(waist_z - BAND_CM * 0.5, K, 1)
knot = knot + Vector((0, -0.45, 0))
def string(points, radius=0.32):
    cu = bpy.data.curves.new("String", "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = radius
    cu.bevel_resolution = 2
    sp = cu.splines.new("POLY")
    sp.points.add(len(points) - 1)
    for i, p in enumerate(points):
        sp.points[i].co = (p.x, p.y, p.z, 1.0)
    o = bpy.data.objects.new("String", cu)
    bpy.context.scene.collection.objects.link(o)
    return o
pieces = []
for side in (1, -1):
    loop = [knot + Vector((side * 4.2 * math.sin(math.pi * s) * (0.6 + 0.4 * s), -0.45 * math.sin(math.pi * s) - 0.05, 2.2 * math.sin(2 * math.pi * s) * 0.6 + 0.4 * s)) for s in [i / 14 for i in range(15)]]
    pieces.append(string(loop))
    tail = [knot + Vector((side * (0.6 + 1.4 * s) + side * 0.6 * math.sin(3 * s), -0.4 - 0.25 * s, -9.0 * s)) for s in [i / 12 for i in range(13)]]
    pieces.append(string(tail))
knot_mesh = bpy.data.meshes.new("Knot")
bmk = bmesh.new()
bmesh.ops.create_uvsphere(bmk, u_segments=10, v_segments=6, radius=0.75)
bmesh.ops.scale(bmk, vec=(1.1, 0.7, 0.8), verts=bmk.verts)
bmesh.ops.translate(bmk, vec=knot, verts=bmk.verts)
bmk.to_mesh(knot_mesh)
bmk.free()
ko = bpy.data.objects.new("Knot", knot_mesh)
bpy.context.scene.collection.objects.link(ko)
bpy.ops.object.select_all(action="DESELECT")
for o in pieces:
    o.select_set(True)
bpy.context.view_layer.objects.active = pieces[0]
bpy.ops.object.convert(target="MESH")
string_objs = [o for o in bpy.context.scene.objects if o.name.startswith("String")] + [ko]
string_count = 0
for o in string_objs:
    string_count += len(o.data.vertices)
bpy.ops.object.select_all(action="DESELECT")
for o in string_objs:
    o.select_set(True)
    o.data.materials.clear()
    o.data.materials.append(mat)
    for poly in o.data.polygons:
        poly.use_smooth = True
    # The string is laid on the band as the weave lies: a strip of it along its length.
    if not o.data.uv_layers:
        o.data.uv_layers.new(name="UVMap")
    for poly in o.data.polygons:
        for li in poly.loop_indices:
            co = o.data.vertices[o.data.loops[li].vertex_index].co
            o.data.uv_layers[0].data[li].uv = (co.x * 0.2, co.z * 0.2)
cloth_count = len(me.vertices)
obj.select_set(True)
bpy.context.view_layer.objects.active = obj
bpy.ops.object.join()
me = obj.data
print("[braies] with the drawstring: %d points" % len(me.vertices))

# ---- weights: the skin's under each point, smoothed over the cloth; the drawstring on the pelvis alone.
body_bm = bmesh.new()
for v in verts:
    body_bm.verts.new(v)
body_bm.verts.ensure_lookup_table()
keep = [t for t in body_tris]
for t in keep:
    try:
        body_bm.faces.new([body_bm.verts[k] for k in t])
    except ValueError:
        pass
body_bm.faces.ensure_lookup_table()
body_bm.faces.index_update()
body_bm.verts.index_update()
body_bm.normal_update()
print("[braies] the skin to weigh by: %d points, %d faces" % (len(body_bm.verts), len(body_bm.faces)))
face_tris = [tuple(v.index for v in f.verts) for f in body_bm.faces]
tree = BVHTree.FromBMesh(body_bm)
def weigh(v):
    loc, _, face, _ = tree.find_nearest(v)
    a, b, c = (verts[k] for k in face_tris[face])
    v0, v1, v2 = b - a, c - a, loc - a
    d00, d01, d11, d20, d21 = v0.dot(v0), v0.dot(v1), v1.dot(v1), v2.dot(v0), v2.dot(v1)
    den = d00 * d11 - d01 * d01
    wb = (d11 * d20 - d01 * d21) / den if den > 1e-9 else 1.0 / 3.0
    wc = (d00 * d21 - d01 * d20) / den if den > 1e-9 else 1.0 / 3.0
    shares = (max(1.0 - wb - wc, 0.0), max(wb, 0.0), max(wc, 0.0))
    total = {}
    for k, f in zip(face_tris[face], shares):
        for bone, w in weights[k].items():
            if bone not in ARMS:
                total[bone] = total.get(bone, 0) + w * f
    s = sum(total.values()) or 1.0
    return {bone: w / s for bone, w in total.items()} if total else {idx["pelvis"]: 1.0}
co = [v.co.copy() for v in me.vertices]
raw = [weigh(co[i]) if i < cloth_count else {idx["pelvis"]: 1.0} for i in range(len(co))]
nbrs = [set() for _ in co]
for e in me.edges:
    a, b = e.vertices
    nbrs[a].add(b)
    nbrs[b].add(a)
for r in range(WEIGHT_ROUNDS):
    nxt = []
    for i, w in enumerate(raw):
        if i >= cloth_count or not nbrs[i]:
            nxt.append(w)
            continue
        mix = {b: x * 0.5 for b, x in w.items()}
        share = 0.5 / len(nbrs[i])
        for j in nbrs[i]:
            for b, x in raw[j].items():
                mix[b] = mix.get(b, 0) + x * share
        nxt.append(mix)
    raw = nxt

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
groups = {}
for i in range(len(co)):
    best = sorted(raw[i].items(), key=lambda e: -e[1])[:4]
    tot = sum(w for _, w in best)
    for b, w in best:
        if w / tot > 0.02:
            g = groups.get(b)
            if g is None:
                g = groups[b] = obj.vertex_groups.new(name=names[b])
            g.add([i], w / tot, "REPLACE")
print("[braies] weighted to %d bones" % len(groups))
obj.parent = arm_obj
mod = obj.modifiers.new("Armature", "ARMATURE")
mod.object = arm_obj

# How far the cloth stands off the skin, all round the seat and the legs.
gaps = []
for i in range(cloth_count):
    loc, normal, _, dist = tree.find_nearest(co[i])
    if loc is not None and co[i].z < waist_z - BAND_CM:
        gaps.append((co[i] - loc).dot(normal))
gaps.sort()
if gaps:
    print("[braies] off the skin: least %.1f, one in ten under %.1f, middle %.1f, most %.1f cm" % (gaps[0], gaps[len(gaps) // 10], gaps[len(gaps) // 2], gaps[-1]))

bpy.ops.object.select_all(action="DESELECT")
arm_obj.select_set(True)
obj.select_set(True)
bpy.context.view_layer.objects.active = arm_obj
bpy.context.scene.unit_settings.system = "METRIC"
bpy.context.scene.unit_settings.scale_length = 0.01
out_fbx = os.path.join(OUTDIR, "SK_%s.fbx" % NAME)
bpy.ops.export_scene.fbx(filepath=out_fbx, use_selection=True, object_types={"ARMATURE", "MESH"}, global_scale=1.0, apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE", axis_forward="-Y", axis_up="Z", bake_space_transform=False, use_mesh_modifiers=False, mesh_smooth_type="FACE", add_leaf_bones=False, primary_bone_axis="Y", secondary_bone_axis="X", use_armature_deform_only=False, armature_nodetype="NULL", bake_anim=False)
print("[braies] wrote %s" % out_fbx)

# ---- the linen: a plain weave of slubbed thread, the warp and the weft over and under each other.
S = 1024
THREADS = 72
yy, xx = np.mgrid[0:S, 0:S].astype(np.float64) / S
rng = np.random.default_rng(11)
def tile_noise(cells, amp):
    g = rng.standard_normal((cells, cells))
    gx = np.fft.fft2(g)
    f = np.fft.fftfreq(cells)
    fx, fy = np.meshgrid(f, f)
    gx *= np.exp(-(fx ** 2 + fy ** 2) * 60)
    small = np.real(np.fft.ifft2(gx))
    small = (small - small.mean()) / (small.std() + 1e-9)
    ix = (xx * cells).astype(int) % cells
    iy = (yy * cells).astype(int) % cells
    return small[iy, ix] * amp
warp_slub = np.repeat(rng.standard_normal(THREADS) * 0.18, S // THREADS + 1)[:S]
weft_slub = np.repeat(rng.standard_normal(THREADS) * 0.18, S // THREADS + 1)[:S]
u = xx * THREADS
v = yy * THREADS
cell_u = np.floor(u).astype(int)
cell_v = np.floor(v).astype(int)
over = ((cell_u + cell_v) % 2 == 0)
warp = np.clip(np.cos(np.pi * (u - np.floor(u) - 0.5)), 0, 1) ** 0.6 * (1 + warp_slub[(xx[0] * S).astype(int)][None, :])
weft = np.clip(np.cos(np.pi * (v - np.floor(v) - 0.5)), 0, 1) ** 0.6 * (1 + weft_slub[(yy[:, 0] * S).astype(int)][:, None])
height = np.where(over, warp * 0.6 + weft * 0.4, weft * 0.6 + warp * 0.4) + tile_noise(32, 0.06)
height = np.nan_to_num(height)
height = (height - height.min()) / (height.max() - height.min())
tone = 1.0 + tile_noise(16, 0.035) + (height - 0.5) * 0.16
base = np.array(WOOL if TROUSERS else LINEN, dtype=np.float64)
albedo = np.clip(base[None, None, :] * tone[..., None], 0, 255).astype(np.uint8)
gy, gx = np.gradient(height * 3.0)
nx, ny = -gx * 4, gy * 4      # the green way down, as the engine reads a normal
nz = np.ones_like(nx)
ln = np.sqrt(nx ** 2 + ny ** 2 + nz ** 2)
normal = np.stack([(nx / ln * 0.5 + 0.5) * 255, (ny / ln * 0.5 + 0.5) * 255, (nz / ln * 0.5 + 0.5) * 255], -1).astype(np.uint8)
rough = np.clip(222 + (0.5 - height) * 30 + tile_noise(16, 6), 0, 255)
masks = np.stack([rough, np.zeros_like(rough), np.zeros_like(rough)], -1).astype(np.uint8)
def save(arr, name):
    # Written straight out as a PNG: eight bits a channel, the rows top first.
    import zlib, struct
    raw = b"".join(bytes([0]) + arr[r].astype(np.uint8).tobytes() for r in range(arr.shape[0]))
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    png = bytes([137, 80, 78, 71, 13, 10, 26, 10]) + chunk(b"IHDR", struct.pack(">IIBBBBB", arr.shape[1], arr.shape[0], 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(os.path.join(OUTDIR, name + ".png"), "wb").write(png)
    print("[braies] %s: mean %s" % (name, arr.reshape(-1, 3).mean(0)))
save(albedo, "T_%s_A" % NAME)
save(normal, "T_%s_N" % NAME)
save(masks, "T_%s_RMF" % NAME)
print("[braies] the weave drawn")

# ---- preview: the braies on the body, front, side, back and aslant.
bme = bpy.data.meshes.new("Body")
bme.from_pydata([tuple(v) for v in verts], [], tris)
bme.update()
bo = bpy.data.objects.new("Body", bme)
bpy.context.scene.collection.objects.link(bo)
scn = bpy.context.scene
scn.render.engine = "BLENDER_WORKBENCH"
scn.render.resolution_x, scn.render.resolution_y = (560, 900) if TROUSERS else (700, 700)
focus = Vector((0, 0, (waist_z + ankle_z) * 0.5 if TROUSERS else (waist_z + crotch_z) * 0.5 - 4))
for name, direction in (("front", (0, -1, 0.05)), ("side", (1, 0, 0.05)), ("back", (0, 1, 0.05)), ("aslant", (0.7, -0.7, 0.3)), ("under", (0.3, -0.4, -0.85))):
    cam = bpy.data.cameras.new(name)
    cam.type = "ORTHO"
    cam.ortho_scale = 112 if TROUSERS else 62
    o = bpy.data.objects.new(name, cam)
    scn.collection.objects.link(o)
    dd = Vector(direction).normalized()
    o.location = focus + dd * 400
    o.rotation_euler = (-dd).to_track_quat("-Z", "Y").to_euler()
    cam.clip_end = 2000
    scn.camera = o
    scn.render.filepath = PREVIEW + "_" + name + ".png"
    bpy.ops.render.render(write_still=True)
print("[braies] done")
