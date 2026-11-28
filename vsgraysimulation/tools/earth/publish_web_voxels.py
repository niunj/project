#!/usr/bin/env python3
"""Publish an earth_voxel_bake package as immutable browser terrain chunks."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import tempfile
import zlib


def png(width, height, rgb):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

    stride = width * 3
    rows = b''.join(b'\0' + rgb[row:row+stride] for row in range(0, len(rgb), stride))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def read_tile(path, key):
    data = json.loads(path.read_bytes())
    side = data['side']
    columns = data['columns']
    profiles = data['profiles']
    if data['tile'] != list(key) or not 2 <= side <= 128 or len(columns) != side * side:
        raise ValueError(f'Invalid column grid in {path}')
    if not 1 <= len(profiles) <= 65536 or any(not 0 <= p < len(profiles) for p in columns):
        raise ValueError(f'Invalid profile indices in {path}')
    size = data.get('texture_side', side)
    if 'texture_rgb' in data:
        rgb = bytes.fromhex(data['texture_rgb'])
    else:
        rgb = bytes(component for index in columns for component in profiles[index]['rgb'])
    if not 1 <= size <= 512 or len(rgb) != size * size * 3:
        raise ValueError(f'Invalid imagery in {path}')
    return side, columns, profiles, size, rgb


def payload(side, columns, profiles, quantum):
    table = bytearray()
    runs = []
    for profile in profiles:
        intervals = profile['runs']
        previous = -2**31
        for low, high in intervals:
            if not previous <= low < high <= 2**31 - 1:
                raise ValueError('Invalid voxel interval')
            previous = high
        table.extend(struct.pack('<II3Bx', len(runs), len(intervals), *profile['rgb']))
        runs.extend(intervals)
    return (struct.pack('<4sHHIId', b'VXT1', side, 0, len(profiles), len(runs), quantum)
            + struct.pack(f'<{len(columns)}H', *columns) + table
            + b''.join(struct.pack('<ii', *interval) for interval in runs))


def children(key):
    z, x, y = key
    return [(z+1, x*2+i, y*2+j) for j in range(2) for i in range(2)]


def publish(src, stage):
    source = json.loads((src / 'earth.map.json').read_bytes())
    if source['format'] != 'voxel-geodetic-volume-v1' or source['profile'] != 'global-geodetic':
        raise ValueError('An original geodetic volume package is required')
    quantum = source['vertical_step_m']
    if not math.isfinite(quantum) or quantum <= 0:
        raise ValueError('Invalid vertical quantum')
    roots = [(0, 0, 0), (0, 1, 0)]
    files = {tuple(map(int, p.relative_to(src / 'tiles').with_suffix('').parts)): p
             for p in (src / 'tiles').glob('*/*/*.json')}
    if any(root not in files for root in roots):
        raise ValueError('The data package must contain both geodetic root tiles')
    split = set()
    for key in files:
        z, x, y = key
        if not 0 <= z <= 20 or not (0 <= x < (2 << z) and 0 <= y < (1 << z)):
            raise ValueError(f'Invalid geodetic key {key}')
        split.update((i, x >> (z-i), y >> (z-i)) for i in range(z))

    (stage / 'chunks').mkdir()
    nodes = {}

    def store(body, extension):
        address = hashlib.sha256(body).hexdigest() + extension
        path = stage / 'chunks' / address
        if not path.exists():
            path.write_bytes(body)
        return address

    def crop(values, size, origin, divisor, channels=1):
        x0, y0 = origin
        for y in range(size):
            for x in range(size):
                sx = (2*(x0*size+x)+1) // (2*divisor)
                sy = (2*(y0*size+y)+1) // (2*divisor)
                start = (sy * size + sx) * channels
                yield from values[start:start+channels]

    def visit(key, ancestor=None, data=None):
        z, x, y = key
        if key in files:
            ancestor, data = key, read_tile(files[key], key)
        side, columns, profiles, size, rgb = data
        if key != ancestor:
            divisor = 1 << (z-ancestor[0])
            origin = x % divisor, y % divisor
            columns = list(crop(columns, side, origin, divisor))
            rgb = bytes(crop(rgb, size, origin, divisor, 3))
        active = {index for index in columns}
        bounds = [hi for i in active for interval in profiles[i]['runs'] for hi in interval]
        high = max(bounds, default=0) * quantum
        low = min(bounds, default=0) * quantum
        span = 180 / (1 << z)
        region_bounds = [x*span-180, 90-(y+1)*span, (x+1)*span-180, 90-y*span]
        nodes['/'.join(map(str, key))] = {
            'bounds_deg': region_bounds, 'height_m': [low, high], 'source_level': ancestor[0],
            'error_m': max(quantum, 111320 * span / side * 2), 'children': key in split,
            'voxels': store(payload(side, columns, profiles, quantum), '.vxt'),
            'image': store(png(size, size, rgb), '.png'),
        }
        if key in split:
            for child in children(key):
                visit(child, ancestor, data)

    for root in roots:
        visit(root)
    manifest = {'format': 'vsgraysim-web-earth-v1', 'crs': 'WGS84', 'tile_scheme': 'global-geodetic-xyz',
                'solid_bottom_m': source.get('solid_bottom_m'),
                'earth': source['earth'].get('data_provenance', {}), 'roots': roots, 'tiles': nodes}
    version = hashlib.sha256(json.dumps(manifest, sort_keys=True).encode()).hexdigest()
    (stage / 'manifest.json').write_text(json.dumps({**manifest, 'version': version}, separators=(',', ':')))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('native', type=Path, help='earth_voxel_bake output directory')
    parser.add_argument('output', type=Path, help='new browser data directory')
    args = parser.parse_args()
    if args.output.exists():
        parser.error('output directory already exists')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.output.parent) as temp:
        stage = Path(temp) / 'publication'
        stage.mkdir()
        publish(args.native, stage)
        stage.rename(args.output)


if __name__ == '__main__':
    main()
