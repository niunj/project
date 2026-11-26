#!/usr/bin/env python3
"""Build the three reusable, textured parts of the bundled AH-64 voxel demo."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import shutil


ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / 'models' / 'AH-64'
MAP = ROOT / 'config' / 'earth' / 'taipei-region.map.json'
FLIGHT_HEIGHT = 2490
PIVOTS = {'body': (0, 0, 0), 'main-rotor': (-.0025, 3.86585, .9613),
          'tail-rotor': (.77985, 3.33065, -8.12335)}


def read_mesh(path):
    vertices = []
    texcoords = []
    normals = []
    faces = []
    for line in path.read_text(encoding='ascii', errors='replace').splitlines():
        fields = line.split()
        if not fields:
            continue
        kind, *data = fields
        if kind == 'v':
            vertices.append(tuple(map(float, data)))
        elif kind == 'vt':
            texcoords.append(data)
        elif kind == 'vn':
            normals.append(data)
        elif kind == 'f':
            faces.append([tuple(int(i)-1 for i in token.split('/')) for token in data])
    if any(len(face) != 3 or any(len(v) != 3 or min(v) < 0 for v in face) for face in faces):
        raise ValueError('Expected triangulated position/UV/normal faces from the bundled AH-64')
    return vertices, texcoords, normals, faces


def split_mesh(vertices, faces):
    links = list(range(len(vertices)))

    def root(i):
        while links[i] != i:
            links[i] = links[links[i]]
            i = links[i]
        return i

    seen = {}
    for i, p in enumerate(vertices):
        # Weld only for connectivity. OBJ seams and their UVs stay intact.
        key = tuple(round(x, 4) for x in p)
        links[root(i)] = root(seen.setdefault(key, i))
    for face in faces:
        seed = face[0][0]
        for idx, _, _ in face[1:]:
            links[root(idx)] = root(seed)
    components = defaultdict(list)
    for face in faces:
        components[root(face[0][0])].append(face)

    parts = {name: [] for name in PIVOTS}
    blade_counts = [0, 0]
    for component in components.values():
        xyz = [vertices[v[0]] for face in component for v in face]
        low = tuple(min(column) for column in zip(*xyz))
        high = tuple(max(column) for column in zip(*xyz))
        main_blade = low[1] > 3.08 and high[1] < 4.01 and max(abs(low[0]), abs(high[0])) > 4.
        main_hub = low[0] > -.9 and high[0] < .9 and low[1] > 3.61 and high[1] < 4.015 \
            and low[2] > .1 and high[2] < 1.82
        tail = low[0] >= .50 and high[0] < 1.04 and low[1] > 1.83 and high[1] < 4.81 \
            and low[2] > -9.45 and high[2] < -6.72
        blade_counts[0] += main_blade
        blade_counts[1] += tail and max(high[1]-low[1], high[2]-low[2]) > .95
        name = 'main-rotor' if main_blade or main_hub else 'tail-rotor' if tail else 'body'
        parts[name].extend(component)
    if blade_counts != [4, 4]:
        raise ValueError(f'AH-64 geometry no longer matches the authored rotor pivots: {blade_counts}')
    return parts


def write_obj(path, name, faces, tables):
    original = [sorted({corner[i] for face in faces for corner in face}) for i in range(3)]
    remap = [{old: idx+1 for idx, old in enumerate(indices)} for indices in original]
    with path.open('w', encoding='ascii') as stream:
        stream.write(f'# Derived from AH-64.obj with original UVs and normals\nmtllib AH-64.mtl\no {name}\n')
        for tag, table, indices in zip(('v', 'vt', 'vn'), tables, original):
            for i in indices:
                values = table[i]
                if tag == 'v':
                    values = [f'{coord-offset:.6f}' for coord, offset in zip(values, PIVOTS[name])]
                stream.write(f'{tag} {" ".join(values)}\n')
        stream.write('usemtl AH-64\ns 1\n')
        for face in faces:
            corners = ['/'.join(str(mapping[old]) for mapping, old in zip(remap, vertex)) for vertex in face]
            stream.write(f'f {" ".join(corners)}\n')


def scene_for(main_rpm, tail_rpm):
    origin = [0, FLIGHT_HEIGHT, -6]
    settings = {'body': .09, 'main-rotor': .065, 'tail-rotor': .027}
    axes = {'main-rotor': [0, 1, .024], 'tail-rotor': [1, 0, 0]}
    rpm = {'main-rotor': main_rpm, 'tail-rotor': tail_rpm}
    objects = []
    for name, cell in settings.items():
        part = {'id': name, 'role': 'target', 'physics': {'motion': 'prescribed'},
                'source': {'type': 'mesh', 'path': f'{name}.obj', 'up_axis': 'y',
                           'voxel_size_m': cell, 'surface_detail': 'source_clipped', 'import_workers': 4}}
        if name == 'body':
            part.update(position_m=origin, velocity_mps=[1.8, 0, -.5], rotation_deg=[0, 18, 0])
        else:
            part['rotor_drive'] = {'carrier': 'body', 'pivot_m': PIVOTS[name], 'axis': axes[name], 'rpm': rpm[name]}
        objects.append(part)
    return {'format': 'voxel-scene-v1', 'fixed_step_seconds': .005,
            'geo_anchor': {'lat': 25.078, 'lon': 121.772, 'alt': 0},
            'utc_origin': {'year': 2026, 'month': 11, 'day': 18, 'hours_utc': 4},
            'loading': {'workers': 3, 'target_jobs': 3}, 'objects': objects}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    output = parser.parse_args().output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    tables = read_mesh(MODEL / 'AH-64.obj')
    parts = split_mesh(tables[0], tables[3])
    for name, faces in parts.items():
        write_obj(output / f'{name}.obj', name, faces, tables[:3])
    for asset in ('AH-64.mtl', 'AH-64.jpg'):
        shutil.copy2(MODEL / asset, output / asset)
    for speed, (main_rpm, tail_rpm) in {'slow': (24, -88), 'fast': (286, -1130)}.items():
        name = f'ah64-{speed}'
        scene = scene_for(main_rpm, tail_rpm)
        topic = {'format': 'earth-scene-topic-v1', 'name': f'AH-64 rotary voxel aircraft ({speed})',
                 'scene': f'{name}.vscene.json', 'earth_map': str(MAP), 'paused': False,
                 'camera': {'eye_m': [24, FLIGHT_HEIGHT+8, 11], 'target_m': [.8, FLIGHT_HEIGHT+2, -7]},
                 'sky': {}, 'clouds': {'cover': .12}}
        for extension, data in (('vscene', scene), ('vtopic', topic)):
            (output / f'{name}.{extension}.json').write_text(json.dumps(data, indent=2)+'\n')
    print({name: len(faces) for name, faces in parts.items()})


if __name__ == '__main__':
    main()
