# The underwear baked into the body's textures, taken out: where the grey shorts and the band at the waist were drawn,
# skin, the colour of the skin round it, with the fine detail of the skin just above it (the thigh above the shorts, the
# belly above the band), in all four textures (colour, normal, roughness and the rest, scatter).
# python skin_fill.py <folder>: the four textures exported there as T_Body_<BC|N|SRMF|Scatter>_VT.png (by an asset export
# of /Game/Aedir/Characters/MetaHuman/Build/MHC_AedirMan/Body/Baked), written back beside them as *_skin.png, to be imported
# over the textures with their own settings kept.
import sys, numpy as np
from PIL import Image
from scipy import ndimage
Image.MAX_IMAGE_PIXELS = None
W = sys.argv[1]
bc = np.asarray(Image.open(W + r"\T_Body_BC_VT.png").convert("RGBA")).astype(np.float32) / 255
mx = bc[..., :3].max(2); mn = bc[..., :3].min(2)
grey = ((mx - mn) / np.maximum(mx, 1e-3) < 0.22) & (mx > 0.25)
# The band and the shorts, each with the skin above it to borrow detail from, in eighths of the texture.
REGIONS = [(3000, 3700, 150), (5900, 8192, 1900)]

def pushpull(img, known):
    # Fills what is not known from what is, coarser and coarser, then back up.
    levels = [(img * known[..., None], known.astype(np.float32))]
    while min(levels[-1][1].shape) > 4:
        c, w = levels[-1]
        h2, w2 = c.shape[0] // 2 * 2, c.shape[1] // 2 * 2
        c = c[:h2, :w2]; w = w[:h2, :w2]
        c = c.reshape(h2 // 2, 2, w2 // 2, 2, -1).sum((1, 3)); w = w.reshape(h2 // 2, 2, w2 // 2, 2).sum((1, 3))
        levels.append((c, w))
    c, w = levels[-1]
    fill = c / np.maximum(w, 1e-6)[..., None]
    # A coarsest cell with nothing known in it takes what is known over all.
    mean = c.reshape(-1, c.shape[-1]).sum(0) / max(w.sum(), 1e-6)
    fill[w <= 0] = mean
    for c, w in reversed(levels[:-1]):
        up = np.repeat(np.repeat(fill, 2, 0), 2, 1)
        up = np.pad(up, ((0, c.shape[0] - up.shape[0]), (0, c.shape[1] - up.shape[1]), (0, 0)), mode="edge")
        a = np.clip(w, 0, 1)[..., None]
        fill = (c / np.maximum(w, 1e-6)[..., None]) * a + up * (1 - a)
    return fill

def process(name, size, dilate_cm, detail_sigma, flat=None):
    img = np.asarray(Image.open(W + "\\" + name + ".png").convert("RGBA")).astype(np.float32) / 255
    scale = size / 8192
    out = img.copy()
    for y0, y1, shift in REGIONS:
        a, b = int(y0 * scale), int(y1 * scale); sh = int(shift * scale)
        m = grey[y0:y1]
        if scale != 1:
            m = np.asarray(Image.fromarray(m.astype(np.uint8) * 255).resize((size, b - a), Image.BOX)) > 20
        m = ndimage.binary_dilation(m, iterations=max(int(dilate_cm * scale), 1))
        soft = ndimage.gaussian_filter(m.astype(np.float32), max(3 * scale, 0.7))
        soft = np.clip(soft * 1.6, 0, 1)
        crop = img[a:b]
        base = pushpull(crop, ~m)
        # The fine detail of the skin above it, its own colour taken out.
        src = img[a - sh:b - sh]
        hp = src - ndimage.gaussian_filter(src, (detail_sigma * scale * 8, detail_sigma * scale * 8, 0))
        fill = base + hp * 0.9
        if flat is not None:
            fill[..., :3] = flat[0] * 0.0 + fill[..., :3]
        fill = np.clip(fill, 0, 1)
        out[a:b] = crop * (1 - soft[..., None]) + fill * soft[..., None]
        print(name, a, b, int(m.sum()))
    Image.fromarray((out * 255 + 0.5).astype(np.uint8), "RGBA").save(W + "\\" + name + "_skin.png")

process("T_Body_BC_VT", 8192, 20, 2.0)
process("T_Body_N_VT", 2048, 24, 2.0)
process("T_Body_SRMF_VT", 2048, 24, 2.0)
process("T_Body_Scatter_VT", 512, 24, 2.0)
