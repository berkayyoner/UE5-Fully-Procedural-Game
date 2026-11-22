# python -I linen_tint.py <folder with T_<From>_A/N/RMF.png> <From> <out folder> <Name> <r> <g> <b>
# A garment's textures as undyed linen: its weave, folds and wear kept from the base colour's own light and dark, laid
# over the linen tint given (0-1; the braies are 0.78 0.74 0.64, the white shirt 0.80 0.77 0.70); the normal and the
# roughness masks as they are, a little rougher (ROUGHER), linen being duller than the trousers' cloth.
import sys
import numpy as np
from PIL import Image
Image.MAX_IMAGE_PIXELS = None
src, frm, out, name = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
tint = np.array([float(c) for c in sys.argv[5:8]], np.float32)
DETAIL = 0.25  # how much of the base colour's own light and dark shows through the tint
ROUGHER = 0.15
T = "%s/T_%s_" % (src.rstrip("/\\"), frm)
base = np.asarray(Image.open(T + "A.png").convert("RGB")).astype(np.float32) / 255
lum = base.mean(axis=2)
detail = np.clip(lum / max(float(lum[lum > 0.01].mean()), 1e-3), 0.3, 2.0)
colour = tint[None, None, :] * (1.0 - DETAIL + DETAIL * detail)[..., None]
Image.fromarray((np.clip(colour, 0, 1) * 255).astype(np.uint8)).save("%s/T_%s_A.png" % (out, name))
Image.open(T + "N.png").save("%s/T_%s_N.png" % (out, name))
rmf = np.asarray(Image.open(T + "RMF.png").convert("RGB")).astype(np.float32) / 255
rmf[..., 0] = np.clip(rmf[..., 0] + ROUGHER, 0, 1)
rmf[..., 1] = 0
Image.fromarray((rmf * 255).astype(np.uint8)).save("%s/T_%s_RMF.png" % (out, name))
print("made", name)
