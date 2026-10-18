#!/usr/bin/env python3
"""Regenerate the small resident terrain asset and MitchGame test scene (format version 1)."""

import json
import math
from pathlib import Path
import struct


def checksum(payload):
    value = 14695981039346656037
    for byte in payload:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def height(x, z):
    return 4 * math.exp(-(x * x + z * z) / 300) + 1.5 * math.sin(x * 0.13) * math.cos(z * 0.11) + x * 0.04


def main():
    engine = Path(__file__).resolve().parents[1]
    project = engine.parent
    assets = project / "Assets/Terrain"
    pages = assets / "ResidentDemo.source"
    pages.mkdir(parents=True, exist_ok=True)
    quads = 32
    manifest = {
        "Format": "MitchTerrain", "Version": 1, "AssetGUID": "7465727261696e01",
        "Settings": {"TileQuads": quads, "PatchQuads": 16, "SampleSpacing": 1,
                     "HeightBase": -512, "HeightRange": 1024, "MinTile": [-1, -1], "TilesX": 2, "TilesZ": 2},
        "Pages": [],
    }
    for tx in (-1, 0):
        for tz in (-1, 0):
            payload = b"".join(struct.pack("<f", height(tx * quads + x, tz * quads + z))
                               for z in range(quads + 1) for x in range(quads + 1))
            name = f"{tx}_{tz}.thp"
            header = struct.pack("<4sIIiiQQ", b"MTHP", 1, quads, tx, tz, len(payload), checksum(payload))
            (pages / name).write_bytes(header + payload)
            manifest["Pages"].append({"Tile": [tx, tz], "Source": f"ResidentDemo.source/{name}"})
    (assets / "ResidentDemo.terrain").write_text(json.dumps(manifest, indent=2) + "\n")
    (assets / "ResidentDemo.terrain.meta").write_text(json.dumps({"FileType": "terrain", "GUID": manifest["AssetGUID"]}, indent=2) + "\n")

    def transform(position, rotation=(0, 0, 0)):
        return {"Type": "Transform", "Position": list(position), "Rotation": list(rotation), "Scale": [1, 1, 1]}

    entities = [
        {"Name": "Main Camera", "Components": [transform((0, 25, -42), (28, 0, 0)),
            {"Type": "Camera", "ClearType": "Color", "ClearColor": [0.15, 0.22, 0.32],
             "IsCurrent": True, "Near": 0.1, "Far": 250, "Zoom": 55}]},
        {"Name": "Terrain", "Components": [transform((0, 0, 0)),
            {"Type": "Terrain", "Asset": "Assets/Terrain/ResidentDemo.terrain", "Color": [0.32, 0.52, 0.2],
             "Roughness": 0.9, "CastShadows": True, "Collision": True, "Friction": 0.6, "Restitution": 0}]},
        {"Name": "Sun", "Components": [transform((0, 15, 0), (50, -30, 0)),
            {"Type": "Light", "LightType": "Directional", "Color": [1, 0.93, 0.78], "Intensity": 3,
             "CastShadows": True}]},
    ]
    for index, x in enumerate((-5, 0, 5)):
        entities.append({"Name": f"Terrain Box {index + 1}", "Components": [
            transform((x, height(x, 0) + 0.5, 0)),
            {"Type": "Mesh", "MeshType": "Cube", "Material": {"Type": "StandardMaterial", "DiffuseColor": [0.85, 0.32, 0.1], "Roughness": 0.55}},
            {"Type": "Rigidbody", "Mass": 1}, {"Type": "BoxCollider", "Size": [1, 1, 1]},
        ]})
    scene = project / "Assets/Scenes/Tests/TerrainTest.lvl"
    scene.parent.mkdir(parents=True, exist_ok=True)
    # The existing v1 migration supplies stable-in-session entity GUIDs on load.
    scene.write_text(json.dumps({"Cores": [], "Scene": entities}, indent=2) + "\n")
    print(f"Wrote four source pages, terrain manifest, and {scene}")


if __name__ == "__main__":
    main()
