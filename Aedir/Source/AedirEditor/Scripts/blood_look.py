# UnrealEditor-Cmd Aedir.uproject -run=pythonscript -script=blood_look.py
# The look of shed blood on the skin (M_AedirBloodSkin): fresh it is a light, bright red and glossy wet; as it dries
# (the material's dry share, which the game takes from 0 to 1 over BloodDryGameHours) it darkens and dulls.
import unreal

FRESH = unreal.LinearColor(0.42, 0.015, 0.012, 1.0)
DRIED = unreal.LinearColor(0.11, 0.018, 0.012, 1.0)
FRESH_ROUGHNESS = 0.05
DRIED_ROUGHNESS = 0.5

mat = unreal.load_asset("/Game/Aedir/Materials/M_AedirBloodSkin")
lib = unreal.MaterialEditingLibrary
exprs = list(lib.get_material_expressions(mat)) if hasattr(lib, "get_material_expressions") else []
if not exprs:
    exprs = [o for o in unreal.ObjectIterator(unreal.MaterialExpression) if o.get_typed_outer(unreal.Material) == mat]
by_name = {e.get_name(): e for e in exprs}
# The wet and dry blood colours, and the wet and dry roughness, as the material was built.
by_name["MaterialExpressionConstant3Vector_0"].set_editor_property("constant", FRESH)
by_name["MaterialExpressionConstant3Vector_1"].set_editor_property("constant", DRIED)
by_name["MaterialExpressionConstant_0"].set_editor_property("r", FRESH_ROUGHNESS)
by_name["MaterialExpressionConstant_1"].set_editor_property("r", DRIED_ROUGHNESS)
lib.recompile_material(mat)
unreal.EditorAssetLibrary.save_loaded_asset(mat)
unreal.log("[BloodLook] fresh %s rough %.2f, dried %s rough %.2f" % (FRESH, FRESH_ROUGHNESS, DRIED, DRIED_ROUGHNESS))
