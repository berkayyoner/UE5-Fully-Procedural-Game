# UnrealEditor-Cmd Aedir.uproject -run=pythonscript -script=blood_flow.py
# Blood on the skin (M_AedirBloodSkin) runs the way gravity takes it, not always down the body as it stands: the game
# gives each wound its streams under the two rows of the data texture (UAedirWoundComponent): a row with the wound's normal
# in the bind pose and how many streams it has, then a row for each stream with the way it ran (the skin's way down while
# it ran, in the bind pose) and how far it got in centimetres. Each stream is drawn as the old run was, a few thin streams
# of their own lengths, along its own way, across the skin (its side the way across the stream on the skin) and never
# deeper into or out of the skin than DEPTH_LEAST plus DEPTH_PER_CM for every centimetre down it (up to DEPTH_MOST), so it
# follows the skin round as it curves away without coming through on the other side of an arm. Run again after
# blood_look.py, or before it: each looks for its own lines.
import unreal

DEPTH_LEAST = 2.0
DEPTH_PER_CM = 0.6
DEPTH_MOST = 8.0
# Lanes of streams across a run, this many to the centimetre (the old ones were 0.9 to the cm along x + y).
LANES_PER_CM = 1.27

mat = unreal.load_asset("/Game/Aedir/Materials/M_AedirBloodSkin")
exprs = [o for o in unreal.ObjectIterator(unreal.MaterialExpression) if o.get_typed_outer(unreal.Material) == mat]
custom = [e for e in exprs if e.get_class().get_name() == "MaterialExpressionCustom"][0]
code = str(custom.get_editor_property("code"))
NL = chr(10)

# Far from the wound and all of its streams: nothing of it here.
old_reach = "    if (abs(Q.x) > Reach || abs(Q.y) > Reach || Q.z > Reach || -Q.z > max(Reach, max(D.z, 0) + 1.0)) continue;"
new_reach = "    if (length(Q) > Reach + max(D.z, 0) + 1.0) continue;"
if old_reach in code:
    code = code.replace(old_reach, new_reach)

start = code.index("    // Runs down the skin from it")
end = code.index("    float Here = ")
run = [
    "    // Runs down the skin from it, each stream the way gravity took it as it ran: a few thin streams of their own lengths.",
    "    float Run = 0;",
    "    float4 Nn = Data.Load(int3(i, 2, 0));",
    "    int Streams = (int)Nn.w;",
    "    [loop] for (int s = 0; s < Streams; s++)",
    "    {",
    "        float4 St = Data.Load(int3(i, 3 + s, 0));",
    "        float3 Way = St.xyz;",
    "        float3 Side = normalize(cross(Way, Nn.xyz) + float3(1e-5, 0, 0));",
    "        float Down = dot(Q, Way);",
    "        float Across = abs(dot(Q, Side));",
    "        float Deep = abs(dot(Q, Nn.xyz));",
    "        float Lane = frac(sin(floor(dot(P, Side) * %.2f + D.w * 5.0 + s * 3.1) * 91.345) * 4375.85);" % LANES_PER_CM,
    "        float Length = St.w * (0.35 + 0.65 * Lane);",
    "        float R = (Down > 0 && Down < Length && Deep < min(%.1f + %.2f * Down, %.1f)) ? saturate((1.0 - Across / (W.w * 0.55)) * 4.0) * saturate((Length - Down) / max(Length * 0.08, 0.3)) : 0;" % (DEPTH_LEAST, DEPTH_PER_CM, DEPTH_MOST),
    "        R *= step(0.45, Lane + 0.35 * Noise);",
    "        Run = max(Run, R);",
    "    }",
]
code = code[:start] + NL.join(run) + NL + code[end:]
custom.set_editor_property("code", code)
unreal.MaterialEditingLibrary.recompile_material(mat)
unreal.EditorAssetLibrary.save_loaded_asset(mat)
unreal.log_warning("OUT [BloodFlow] streams drawn, reach %s" % (new_reach in code))
for line in code.splitlines():
    if "Run" in line or "Stream" in line or "Reach" in line:
        unreal.log_warning("OUT %s" % line.strip())
