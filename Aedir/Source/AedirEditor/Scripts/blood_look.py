# UnrealEditor-Cmd Aedir.uproject -run=pythonscript -script=blood_look.py
# The look of shed blood on the skin (M_AedirBloodSkin): fresh it is a bright red and glossy wet; as it dries (the
# material's dry share, which the game takes from 0 to 1 over BloodDryGameHours) it darkens and dulls. Its edge is sharp,
# ragged but not blurred (BLOOD_EDGE); a bruise is a deep purple patch going yellow as it heals, plain to see
# (BRUISE_FLOOR of its full darkness at the least blow).
import unreal

FRESH = unreal.LinearColor(0.62, 0.022, 0.016, 1.0)
DRIED = unreal.LinearColor(0.16, 0.02, 0.014, 1.0)
FRESH_ROUGHNESS = 0.03
DRIED_ROUGHNESS = 0.5
BRUISE_FRESH = unreal.LinearColor(0.045, 0.01, 0.04, 1.0)
BRUISE_OLD = unreal.LinearColor(0.1, 0.075, 0.02, 1.0)
OPACITY = 0.98
# How steeply the blood comes in at its ragged edge (shares of its radius over which it goes from none to all: 1 / this).
BLOOD_EDGE = 8.0
# How steeply a run's sides and its end come in.
RUN_EDGE = 4.0
RUN_TAIL_SHARE = 0.08
# A bruise from the least blow is this dark, the hardest the rest of the way to full; it is full dark over the inner part
# of it and fades out softly over the outer 1 / BRUISE_CORE of its radius.
BRUISE_FLOOR = 0.85
BRUISE_CORE = 2.0

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
# The code's result is (how much there is, how dry, how much of it is bruise): each read alone, on its own channel. Read
# two or three channels at once they bled into each other, and a bruise came out the red of blood.
for mask_name, channel in (("MaterialExpressionComponentMask_0", "r"), ("MaterialExpressionComponentMask_1", "g"), ("MaterialExpressionComponentMask_2", "b")):
    for c in ("r", "g", "b", "a"):
        by_name[mask_name].set_editor_property(c, c == channel)

# The shape of each mark, in the material's own code: the edge of the blood, its runs, and how dark a bruise is.
custom = by_name["MaterialExpressionCustom_0"]
code = str(custom.get_editor_property("code"))
import re
code = re.sub(r"\* \(0\.\d+ \+ 0\.\d+ \* saturate\(D\.x\)\)", "* (%.2f + %.2f * saturate(D.x))" % (BRUISE_FLOOR, 1.0 - BRUISE_FLOOR), code)
code = re.sub(r"saturate\(\(?1\.0 - length\(Q\) / \(W\.w \* \(0\.8 \+ 0\.4 \* Noise\)\)\)?( \* [\d.]+\))?", "saturate((1.0 - length(Q) / (W.w * (0.8 + 0.4 * Noise))) * %.1f)" % BRUISE_CORE, code)
code = re.sub(r"saturate\(\(1\.0 - length\(Q\) / \(W\.w \* \(0\.7 \+ 0\.6 \* Noise\)\)\) \* [\d.]+\)", "saturate((1.0 - length(Q) / (W.w * (0.7 + 0.6 * Noise))) * %.1f)" % BLOOD_EDGE, code)
code = re.sub(r"saturate\(\(?1\.0 - Across / \(W\.w \* 0\.55\)\)?( \* [\d.]+\))?", "saturate((1.0 - Across / (W.w * 0.55)) * %.1f)" % RUN_EDGE, code)
code = re.sub(r"saturate\(\(Length - Down\) / max\(Length \* [\d.]+, [\d.]+\)\)", "saturate((Length - Down) / max(Length * %.2f, 0.3))" % RUN_TAIL_SHARE, code)
custom.set_editor_property("code", code)
lib.recompile_material(mat)
unreal.EditorAssetLibrary.save_loaded_asset(mat)
unreal.log("[BloodLook] fresh %s rough %.2f, dried %s rough %.2f, bruise %s to %s, edge %.1f, bruise floor %.2f" % (FRESH, FRESH_ROUGHNESS, DRIED, DRIED_ROUGHNESS, BRUISE_FRESH, BRUISE_OLD, BLOOD_EDGE, BRUISE_FLOOR))
for line in code.splitlines():
    if "Spot" in line or "Run =" in line:
        unreal.log("[BloodLook] %s" % line.strip())
