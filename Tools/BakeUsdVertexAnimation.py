#!/usr/bin/env python3
"""Convert a baked USD mesh animation to OBJ + vertex cache for MitchEngine.

Requires usd-core and numpy (pip install usd-core numpy). Geometry is grouped by
material and playback updates a vertex buffer once, shared by all render passes.
The source USD is preserved. Usage: script INPUT.usd OUTPUT.obj
"""
import argparse
import json
import struct
from pathlib import Path

import numpy as np
from pxr import Usd, UsdGeom, UsdShade


def bake(source, output):
    stage = Usd.Stage.Open(str(source))
    start, end = int(stage.GetStartTimeCode()), int(stage.GetEndTimeCode())
    fps = stage.GetTimeCodesPerSecond()
    meshes = [UsdGeom.Mesh(p) for p in stage.Traverse() if p.IsA(UsdGeom.Mesh)]
    output.parent.mkdir(parents=True, exist_ok=True)
    frames = end - start + 1
    count = sum(len(m.GetPointsAttr().Get(start)) for m in meshes)
    # Memmap keeps this conversion bounded even for a large animated USD.
    cache_path = Path(str(output) + '.vertexcache')
    raw_path = Path(str(output) + '.positions.tmp')
    positions = np.memmap(raw_path, dtype='<f4', mode='w+', shape=(frames, count, 3))
    descriptors, neighbors, materials = {}, [], {}
    material_faces = {}
    obj = ['mtllib ' + output.with_suffix('.mtl').name]
    offset = 0
    uv_offset = 0
    scale = UsdGeom.GetStageMetersPerUnit(stage)
    for mesh in meshes:
        name = mesh.GetPrim().GetName()
        points = mesh.GetPointsAttr()
        base = np.asarray(points.Get(start), dtype=np.float64) * scale
        n = len(base)
        for frame, time in enumerate(range(start, end + 1)):
            p = np.asarray(points.Get(time), dtype=np.float64) * scale
            if len(p) != n:
                raise ValueError('Changing topology: ' + name)
            matrix = np.asarray(UsdGeom.Xformable(mesh).ComputeLocalToWorldTransform(time))
            # USD row-vector matrices; translations are in authored units.
            matrix[3, :3] *= scale
            positions[frame, offset:offset+n] = p @ matrix[:3, :3] + matrix[3, :3]
        base = positions[0, offset:offset+n].copy()
        faces = np.asarray(mesh.GetFaceVertexIndicesAttr().Get(start))
        lengths = np.asarray(mesh.GetFaceVertexCountsAttr().Get(start))
        uv = UsdGeom.PrimvarsAPI(mesh).GetPrimvar('st')
        uvs = np.asarray(uv.ComputeFlattened(start)) if uv else np.zeros((len(faces), 2))
        interpolation = uv.GetInterpolation() if uv else 'faceVarying'
        # OBJ references UVs per face corner. Cache IDs in vertex colors survive
        # Assimp's welding, UV/normal splits, and vertex reordering without guessing
        # which coincident source vertex moves which way.
        corner_uvs = uvs[faces] if interpolation == 'vertex' else uvs
        if interpolation == 'constant': corner_uvs = np.repeat(uvs[:1], len(faces), axis=0)
        obj.extend('v %.9g %.9g %.9g %d 0 0' % (*p, offset+i) for i, p in enumerate(base))
        obj.extend('vt %.9g %.9g' % tuple(p) for p in corner_uvs)
        material = UsdShade.MaterialBindingAPI(mesh).ComputeBoundMaterial()[0]
        material_name = material.GetPrim().GetName() if material else 'Default'
        if material_name not in materials:
            surface = next((UsdShade.Shader(p) for p in Usd.PrimRange(material.GetPrim())
                            if p.IsA(UsdShade.Shader) and UsdShade.Shader(p).GetIdAttr().Get() == 'UsdPreviewSurface'), None) if material else None
            values = {'color': [0.5, 0.5, 0.5], 'metallic': 0., 'roughness': 0.6, 'maps': {}}
            if surface:
                for field, usd_name in [('color', 'diffuseColor'), ('metallic', 'metallic'), ('roughness', 'roughness')]:
                    inp = surface.GetInput(usd_name)
                    value = inp.Get()
                    if value is not None: values[field] = list(value) if field == 'color' else float(value)
                for field in ['diffuseColor', 'roughness', 'normal']:
                    inp = surface.GetInput(field)
                    conn = inp.GetConnectedSource() if inp else None
                    if conn:
                        tex = UsdShade.Shader(conn[0].GetPrim()).GetInput('file').Get()
                        if tex:
                            import os
                            values['maps'][field] = os.path.relpath((source.parent / tex.path).resolve(), output.parent.resolve())
            materials[material_name] = values
        local_neighbors = [[i, i] for i in range(n)]
        areas = np.zeros(n)
        cursor = 0
        output_faces = material_faces.setdefault(material_name, [])
        for length in lengths:
            polygon = list(range(cursor, cursor + int(length)))
            cursor += int(length)
            for k in range(1, len(polygon)-1):
                triangle = [polygon[0], polygon[k], polygon[k+1]]
                output_faces.append('f ' + ' '.join(f'{offset+int(faces[c])+1}/{uv_offset+c+1}' for c in triangle))
                ids = [int(faces[c]) for c in triangle]
                area = np.linalg.norm(np.cross(base[ids[1]]-base[ids[0]], base[ids[2]]-base[ids[0]]))
                for j, i in enumerate(ids):
                    if area > areas[i]:
                        areas[i] = area
                        local_neighbors[i] = [ids[(j+1)%3], ids[(j+2)%3]]
        neighbors.extend([[a+offset, b+offset] for a,b in local_neighbors])
        offset += n
        uv_offset += len(corner_uvs)
        print(name, n, 'vertices', flush=True)
    for material_name, faces in material_faces.items():
        name = 'Baked_' + material_name
        descriptors[name] = [0, count]
        obj.extend(['o ' + name, 'usemtl ' + material_name])
        obj.extend(faces)
    output.write_text('\n'.join(obj) + '\n')
    mtl = []
    for name, values in materials.items():
        mtl.extend(['newmtl '+name, 'Kd ' + ' '.join(map(str, values['color'])),
                    'Pm '+str(values['metallic']), 'Pr '+str(values['roughness'])])
        for field, texture in values['maps'].items():
            mtl.append({'diffuseColor':'map_Kd', 'roughness':'map_Pr', 'normal':'norm'}[field] + ' ' + texture)
    output.with_suffix('.mtl').write_text('\n'.join(mtl) + '\n')
    header = json.dumps({'version':2, 'vertex_ids':'color.r', 'vertices':count, 'frames':frames, 'fps':fps,
                         'meshes':descriptors, 'neighbors':neighbors}, separators=(',',':')).encode()
    positions.flush()
    with cache_path.open('wb') as target, raw_path.open('rb') as raw:
        target.write(b'MEVC0001')
        target.write(struct.pack('<I', len(header)))
        target.write(header)
        import shutil
        shutil.copyfileobj(raw, target)
    del positions
    raw_path.unlink()
    print('Saved', output, cache_path, count, 'vertices,', frames, 'frames,', len(material_faces), 'draws')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    bake(args.source, args.output)
