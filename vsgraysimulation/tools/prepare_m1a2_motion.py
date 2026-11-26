#!/usr/bin/env python3
"""Separate the bundled M1A2 OBJ into textured display parts without altering the source."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]


def read_mesh(path):
    tables = {'v': [], 'vt': [], 'vn': []}
    faces = []
    for line in path.read_text(encoding='ascii', errors='replace').splitlines():
        fields = line.split()
        if not fields:
            continue
        tag, *values = fields
        if tag in tables:
            tables[tag].append(values)
        elif tag == 'f':
            face = [tuple(int(index)-1 for index in corner.split('/')) for corner in values]
            if len(face) != 3 or any(len(corner) != 3 or min(corner) < 0 for corner in face):
                raise ValueError('Expected triangular faces with position, UV and normal indices')
            faces.append(face)
    return tables, faces


def mesh_components(vertices, faces):
    parents = list(range(len(vertices)))

    def root(i):
        while parents[i] != i:
            parents[i] = parents[parents[i]]
            i = parents[i]
        return i

    positions = {}
    for i, point in enumerate(vertices):
        # Weld only for classification. The original seams, normals and UVs stay intact.
        parents[root(i)] = root(positions.setdefault(point, i))
    for face in faces:
        first = face[0][0]
        for index, _, _ in face[1:]:
            parents[root(index)] = root(first)
    components = defaultdict(list)
    for face in faces:
        components[root(face[0][0])].append(face)
    return components.values()


def write_obj(path, faces, tables, pivot=(0., 0., 0.)):
    used = [sorted({corner[i] for face in faces for corner in face}) for i in range(3)]
    index = [{old: new+1 for new, old in enumerate(axis)} for axis in used]
    with path.open('w', encoding='ascii') as out:
        out.write(f'# M1A2 visual mesh retaining original UV and normal indices\nmtllib M1A2.mtl\no {path.stem}\n')
        for tag, indices in zip(('v', 'vt', 'vn'), used):
            for original in indices:
                fields = tables[tag][original]
                if tag == 'v':
                    fields = [f'{float(value)-pivot[k]:.6f}' for k, value in enumerate(fields)]
                out.write(f'{tag} {" ".join(fields)}\n')
        out.write('usemtl M1A2\ns 1\n')
        for face in faces:
            corners = ['/'.join(str(index[k][v]) for k, v in enumerate(corner)) for corner in face]
            out.write(f'f {" ".join(corners)}\n')


def track_outline(points):
    profile = sorted({(p[2], p[1]) for p in points})

    def half(path):
        side = []
        for point in path:
            while len(side) >= 2:
                a, b = side[-2:]
                cross = (b[0]-a[0])*(point[1]-a[1])-(b[1]-a[1])*(point[0]-a[0])
                if cross > 1e-7:
                    break
                side.pop()
            side.append(point)
        return side[:-1]

    # Clockwise as seen in Z/Y. The lower run moves in -Z as the hull travels in +Z.
    return [[y, z] for z, y in reversed(half(profile)+half(reversed(profile))) ]


def prepare(model, output):
    tables, faces = read_mesh(model)
    vertices = [tuple(map(float, fields)) for fields in tables['v']]
    hull, turret, cannon = [], [], []
    wheels, tracks = [], []
    muzzle_points = []
    for component in mesh_components(vertices, faces):
        points = {vertices[corner[0]] for face in component for corner in face}
        low = tuple(min(column) for column in zip(*points))
        high = tuple(max(column) for column in zip(*points))
        centre = tuple((a+b)*.5 for a, b in zip(low, high))
        span = tuple(b-a for a, b in zip(low, high))
        outboard = low[0] > 1.04 or high[0] < -1.04
        if abs(centre[0]) < .16 and low[1] > 1.7 and high[2] > 4.6 and span[2] > 3.:
            cannon.extend(component)
            muzzle_points.extend(p for p in points if p[2] > high[2]-.015)
        elif outboard and high[1] < 1.25 and .58 < span[1] < .8 and .58 < span[2] < .8 and len(component) >= 120:
            wheels.append((centre, span[1]*.5, component))
        elif low[1] > 1.46 and high[1] > 1.80 and low[2] > -3.60:
            turret.extend(component)
        else:
            if outboard and high[1] < 1.27 and span[2] > 6.:
                tracks.append({'x': centre[0], 'width_m': span[0], 'outline_yz': track_outline(points)})
            hull.extend(component)
    if len(wheels)!=18 or len(tracks)!=2 or not turret or not cannon or not muzzle_points:
        raise ValueError(f'M1A2 mesh does not match expected display pieces: {len(wheels)} wheels, {len(tracks)} belts')
    output.mkdir(parents=True, exist_ok=True)
    write_obj(output/'body.obj', hull, tables)
    write_obj(output/'turret.obj', turret, tables)
    write_obj(output/'barrel.obj', cannon, tables)
    wheel_records = []
    for i,(centre,radius,component) in enumerate(sorted(wheels)):
        name = f'wheel-{i:02}.obj'
        write_obj(output/name, component, tables, centre)
        wheel_records.append({'file': name, 'pivot_m': centre, 'radius_m': radius})
    shutil.copy2(model.parent/'M1A2.png', output/'M1A2.png')
    # The supplied atlas already contains its tan/green color. Remove the
    # extra diffuse tint in this preview material so the tread details read.
    material = (model.parent/'M1A2.mtl').read_text(encoding='ascii', errors='replace')
    lines = ['\tKd 1.0 1.0 1.0' if line.strip().startswith('Kd ') else line for line in material.splitlines()]
    (output/'M1A2.mtl').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    # Approximate visual pivots measured from the turret ring and cannon base.
    manifest = {'source': str(model), 'hull': 'body.obj', 'turret': 'turret.obj',
                'turret_pivot_m': [.04, 1.67, -.41], 'barrel_pivot_m': [.055, 2.025, 1.39],
                'barrel': 'barrel.obj', 'preview_diffuse_tint': [1., 1., 1.],
                'muzzle_m': [sum(column)/len(column) for column in zip(*muzzle_points)],
                'wheels': wheel_records, 'tracks': sorted(tracks, key=lambda t:t['x'])}
    (output/'m1a2-rig.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    assert len(hull)+len(turret)+len(cannon)+sum(len(faces) for _,_,faces in wheels)==len(faces)
    print(f'{len(faces)} source triangles, 18 wheels, two tread loops, one turret and barrel')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--model', type=Path, default=ROOT/'models/M1A2/M1A2.obj')
    args = parser.parse_args()
    prepare(args.model.resolve(), args.output.resolve())


if __name__ == '__main__':
    main()
