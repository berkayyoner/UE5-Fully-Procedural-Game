# python -I shirt_colours.py <Fab Textures folder> <out folder> <Name> <r> <g> <b>
# One more colour of the linen shirt: T_<Name>_A/N/RMF for -run=AedirImportModels -Source=<out folder>, the colour given as
# 0-1 linen tint (the white one is MedievalShirtWhite 0.80 0.77 0.70). The item takes it with WornLookFolder = <Name>.
import sys
import numpy as np
from PIL import Image
Image.MAX_IMAGE_PIXELS = None
src, out, name = sys.argv[1], sys.argv[2], sys.argv[3]
tint = np.array([float(c) for c in sys.argv[4:7]], np.float32)
T = src.rstrip("/\\") + "/shirt_low_Main_mat_"
S = (2048, 2048)
base = np.asarray(Image.open(T + "BaseColor.png").convert("RGB").resize(S, Image.LANCZOS)).astype(np.float32) / 255
ao = np.asarray(Image.open(T + "AO.png").convert("L").resize(S, Image.LANCZOS)).astype(np.float32) / 255
lum = base.mean(axis=2)
detail = np.clip(lum / lum[lum > 0.01].mean(), 0.4, 1.8)
shade = 1 - (1 - ao) * 0.55
colour = tint[None, None, :] * (0.82 + 0.18 * detail)[..., None] * shade[..., None]
Image.fromarray((np.clip(colour, 0, 1) * 255).astype(np.uint8)).save("%s/T_%s_A.png" % (out, name))
Image.open(T + "Normal.png").convert("RGB").resize(S, Image.LANCZOS).save("%s/T_%s_N.png" % (out, name))
r = Image.open(T + "Roughness.png").convert("L").resize(S, Image.LANCZOS)
m = Image.open(T + "Metallic.png").convert("L").resize(S, Image.LANCZOS)
Image.merge("RGB", (r, m, Image.new("L", S, 0))).save("%s/T_%s_RMF.png" % (out, name))
print("made", name)
