# Does in a fresh project what the tutorial's editor steps do: the coins' colliders and scripts, the HUD entity and the kill zone First-Game-2D.md has you add in the editor.
# Usage: Setup.py <project folder> (RunTutorialChecks.py runs it).
import json, sys, os, shutil
root = sys.argv[1]; here = os.path.dirname(os.path.abspath(__file__))
scene = os.path.join(root, 'Assets/Scenes/Main.lvl')
d = json.load(open(scene))
for e in d['Entities']:
    if e['Name'].startswith('Coin '):
        e['Components'] += [{"Type": "CircleCollider2D", "Radius": 0.4, "IsTrigger": True},
                            {"Type": "ScriptComponent", "ScriptName": "Coin", "ScriptFields": json.dumps({"Value": 1})}]
d['Entities'].append({"GUID": "c0140000000002d1", "Name": "Hud", "DestroyOnLoad": True, "Components": [
    {"Type": "Transform", "Position": [0, 0, 0], "Rotation": [0, 0, 0], "Scale": [1, 1, 1]},
    {"Type": "UIDocument", "FilePath": "Assets/UI/Hud.rml", "Visible": True, "SortOrder": 10},
    {"Type": "ScriptComponent", "ScriptName": "CoinCounter"}]})
d['Entities'].append({"GUID": "c0140000000002d2", "Name": "Kill Zone", "DestroyOnLoad": True, "Components": [
    {"Type": "Transform", "Position": [14, -10, 0], "Rotation": [0, 0, 0], "Scale": [1, 1, 1]},
    {"Type": "BoxCollider2D", "Size": [200, 4], "IsTrigger": True},
    {"Type": "ScriptComponent", "ScriptName": "KillZone", "ScriptFields": json.dumps({"RespawnAt": {"x": 0.0, "y": 1.0}})}]})
json.dump(d, open(scene, 'w'), indent=4)
os.makedirs(os.path.join(root, 'Assets/UI'), exist_ok=True)
shutil.copy(os.path.join(here, 'Hud.rml'), os.path.join(root, 'Assets/UI/Hud.rml'))
