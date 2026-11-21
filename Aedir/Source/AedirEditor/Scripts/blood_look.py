# UnrealEditor-Cmd Aedir.uproject -run=pythonscript -script=blood_look.py
# The look of shed blood on the skin (M_AedirBloodSkin), the same as the drops that land on the ground (M_Blood_Splat):
# a deep red in the middle going darker out to its edge (EDGE_DARK of the way to the dried colour), glossy, its edge
# ragged at two sizes (a coarse wobble and a fine one, FINE_EDGE of it). As it dries (the material's dry share, which
# the game takes from 0 to 1 over BloodDryGameHours) it darkens and dulls all over. A bruise is a deep purple patch going
# yellow as it heals.
import unreal

FRESH = unreal.LinearColor(0.28, 0.004, 0.003, 1.0)
DRIED = unreal.LinearColor(0.12, 0.01, 0.01, 1.0)
FRESH_ROUGHNESS = 0.2
DRIED_ROUGHNESS = 0.35
EDGE_DARK = 0.6
FINE_EDGE = 0.2
BRUISE_FRESH = unreal.LinearColor(0.03, 0.006, 0.028, 1.0)
BRUISE_OLD = unreal.LinearColor(0.08, 0.06, 0.015, 1.0)
OPACITY = 0.98
# How steeply the blood comes in at its ragged edge (shares of its radius over which it goes from none to all: 1 / this).
BLOOD_EDGE = 8.0
# How steeply a run's sides and its end come in.
RUN_EDGE = 4.0
RUN_TAIL_SHARE = 0.08
# A bruise lies over the skin this thick from the least blow to the hardest, the skin's own grain and sheen still showing
# through (laid on thicker it looks painted on); it is full over the inner part of it and fades out softly over the outer
# 1 / BRUISE_CORE of its radius. Its sheen is the skin's (BRUISE_ROUGHNESS).
BRUISE_LEAST = 0.6
BRUISE_MOST = 0.85
BRUISE_CORE = 2.0
BRUISE_ROUGHNESS = 0.45
# A bruise is blotched, darker and lighter over it by this much, not one flat colour.
BRUISE_MOTTLE = 0.6

mat = unreal.load_asset("/Game/Aedir/Materials/M_AedirBloodSkin")
lib = unreal.MaterialEditingLibrary
exprs = list(lib.get_material_expressions(mat)) if hasattr(lib, "get_material_expressions") else []
if not exprs:
    exprs = [o for o in unreal.ObjectIterator(unreal.MaterialExpression) if o.get_typed_outer(unreal.Material) == mat]
by_name = {e.get_name(): e for e in exprs}
# The wet and dry blood colours, the fresh and healing bruise colours, the wet and dry roughness, and the opacity, as the
# material was built.
by_name["MaterialExpressionConstant3Vector_0"].set_editor_property("constant", FRESH)
by_name["MaterialExpressionConstant3Vector_1"].set_editor_property("constant", DRIED)
by_name["MaterialExpressionConstant3Vector_2"].set_editor_property("constant", BRUISE_FRESH)
by_name["MaterialExpressionConstant3Vector_3"].set_editor_property("constant", BRUISE_OLD)
by_name["MaterialExpressionConstant_0"].set_editor_property("r", FRESH_ROUGHNESS)
by_name["MaterialExpressionConstant_1"].set_editor_property("r", DRIED_ROUGHNESS)
by_name["MaterialExpressionMultiply_0"].set_editor_property("const_b", OPACITY)
by_name["MaterialExpressionConstant_2"].set_editor_property("r", BRUISE_ROUGHNESS)
# Drawn with the rest of the see-through things, before the hair is laid over them: drawn after (as the engine's default
# pass for them is), blood and bruises on the scalp showed through the hair.
mat.set_editor_property("translucency_pass", unreal.MaterialTranslucencyPass.MTP_BEFORE_DOF)
# Lit as a surface is, with its sheen and the shade of the body it lies on, as the drops on the ground are: lit as a
# volume (the engine's default for see-through things) it came out a flat bright pink in the sun.
unreal.log("[BloodLook] lit before as %s" % mat.get_editor_property("translucency_lighting_mode"))
mat.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
# The code's result is (how much there is, how dry, how much of it is bruise): each read alone, on its own channel. Read
# two or three channels at once they bled into each other, and a bruise came out the red of blood.
for mask_name, channel in (("MaterialExpressionComponentMask_0", "r"), ("MaterialExpressionComponentMask_1", "g"), ("MaterialExpressionComponentMask_2", "b")):
    for c in ("r", "g", "b", "a"):
        by_name[mask_name].set_editor_property(c, c == channel)

# The shape of each mark, in the material's own code: the edge of the blood, its runs, and how dark a bruise is.
custom = by_name["MaterialExpressionCustom_0"]
code = str(custom.get_editor_property("code"))
import re
code = re.sub(r"\* \(0\.\d+ \+ 0\.\d+ \* saturate\(D\.x\)\)( \* \(\d\.\d+ \+ \d\.\d+ \* Noise\))?", "* (%.2f + %.2f * saturate(D.x)) * (%.2f + %.2f * Noise)" % (BRUISE_LEAST, BRUISE_MOST - BRUISE_LEAST, 1.0 - BRUISE_MOTTLE * 0.5, BRUISE_MOTTLE), code)
code = re.sub(r"saturate\(\(?1\.0 - length\(Q\) / \(W\.w \* \(0\.8 \+ 0\.4 \* Noise\)\)\)?( \* [\d.]+\))?", "saturate((1.0 - length(Q) / (W.w * (0.8 + 0.4 * Noise))) * %.1f)" % BRUISE_CORE, code)
# A second, finer wobble of the edge, four times as close, as the ground's drops have.
if "float Fine" not in code:
    fine = ["    float3 C2 = C * 4.0 + 7.3; float3 F2 = frac(C2); float3 I2 = floor(C2);", "    F2 = F2 * F2 * (3.0 - 2.0 * F2);"]
    corners = []
    for x in (0, 1):
        for y in (0, 1):
            for z in (0, 1):
                name = "m%d%d%d" % (x, y, z)
                corners.append(name)
                fine.append("    float %s = frac(sin(dot(I2 + float3(%d,%d,%d), float3(127.1, 311.7, 74.7))) * 43758.5453);" % (name, x, y, z))
    fine.append("    float Fine = lerp(lerp(lerp(m000, m100, F2.x), lerp(m010, m110, F2.x), F2.y), lerp(lerp(m001, m101, F2.x), lerp(m011, m111, F2.x), F2.y), F2.z);")
    anchor = "    // A bruise (no run"
    code = code.replace(anchor, chr(10).join(fine) + chr(10) + anchor, 1)
code = re.sub(r"saturate\(\(1\.0 - length\(Q\) / \(W\.w \* \(0\.7 \+ 0\.6 \* Noise( \+ [\d.]+ \* \(Fine - 0\.5\))?\)\)\) \* [\d.]+\)", "saturate((1.0 - length(Q) / (W.w * (0.7 + 0.6 * Noise + %.2f * (Fine - 0.5)))) * %.1f)" % (FINE_EDGE, BLOOD_EDGE), code)
# Darker out to the edge, as a drop on the ground is: the dry share there is at least how far out it lies.
code = re.sub(r"    Dry \+= [^;]*;", "    Dry += max(D.y, %.2f * (1.0 - IsBruise) * saturate(length(Q) / max(W.w, 0.01))) * Here;" % EDGE_DARK, code)
code = re.sub(r"saturate\(\(?1\.0 - Across / \(W\.w \* 0\.55\)\)?( \* [\d.]+\))?", "saturate((1.0 - Across / (W.w * 0.55)) * %.1f)" % RUN_EDGE, code)
code = re.sub(r"saturate\(\(Length - Down\) / max\(Length \* [\d.]+, [\d.]+\)\)", "saturate((Length - Down) / max(Length * %.2f, 0.3))" % RUN_TAIL_SHARE, code)
custom.set_editor_property("code", code)
lib.recompile_material(mat)
unreal.EditorAssetLibrary.save_loaded_asset(mat)
unreal.log("[BloodLook] fresh %s rough %.2f, dried %s rough %.2f, bruise %s to %s, edge %.1f, bruise %.2f to %.2f, pass %s" % (FRESH, FRESH_ROUGHNESS, DRIED, DRIED_ROUGHNESS, BRUISE_FRESH, BRUISE_OLD, BLOOD_EDGE, BRUISE_LEAST, BRUISE_MOST, mat.get_editor_property("translucency_pass")))
for line in code.splitlines():
    if "Spot" in line or "Run =" in line:
        unreal.log("[BloodLook] %s" % line.strip())
