# Does in a fresh project what the tutorial's editor steps do: the coins, the goal pad and the HUD lines First-Game-3D.md has you add in the editor.
# Usage: Setup.py <project folder> (RunTutorialChecks.py runs it).
import json, sys, os
root = sys.argv[1]
scene = os.path.join(root, 'Assets/Scenes/Main.lvl')
d = json.load(open(scene))
def entity(name, guid, pos, scale, color, size, script, fields):
    return {"GUID": guid, "Name": name, "DestroyOnLoad": True, "Components": [
        {"Type": "Transform", "Position": pos, "Rotation": [0, 0, 0], "Scale": scale},
        {"Type": "Mesh", "MeshType": "Cube", "Material": {"Type": "StandardMaterial", "DiffuseColor": color, "Metallic": 0.0, "Roughness": 0.5}},
        {"Type": "BoxCollider", "Size": size, "IsTrigger": True},
        {"Type": "ScriptComponent", "ScriptName": script, "ScriptFields": json.dumps(fields)}]}
coins = [(-4, 1, -6), (4, 1, -6), (-7, 2.1, 7.5), (7, 2.6, 12), (-10, 1, -12)]
for i, p in enumerate(coins, 1):
    d['Entities'].append(entity(f"Coin {i}", f"c01400000000000{i}", list(p), [0.2, 0.2, 0.05], [1.0, 0.75, 0.1], [4, 4, 16], "Pickup",
        {"Item": "Coin", "Amount": 1, "Heal": 0.0, "SpinDegreesPerSecond": 120.0}))
d['Entities'].append(entity("Goal", "c014000000000099", [0, 0.05, -12], [1, 0.05, 1], [0.2, 0.9, 0.4], [2, 40, 2], "Goal", {"CoinsNeeded": 5}))
json.dump(d, open(scene, 'w'), indent=4)
hud = os.path.join(root, 'Assets/UI/Hud.rml')
s = open(hud).read()
s = s.replace('''	<div id="prompt" data-if="visible">[E] {{ prompt }}</div>''', '''	<div id="prompt" data-if="visible">[E] {{ prompt }}</div>
	<div id="coins">Coins {{ coins }} / {{ needed }}</div>
	<div id="message">{{ message }}</div>''')
s = s.replace('''	</style>''', '''		#coins {
			position: absolute;
			right: 24dp;
			top: 20dp;
			padding: 6dp 12dp;
			background-color: #07090DB0;
			border-radius: 4dp;
		}
		#message {
			position: absolute;
			left: 0dp;
			right: 0dp;
			top: 30%;
			text-align: center;
			font-size: 36dp;
		}
	</style>''')
open(hud, 'w').write(s)
