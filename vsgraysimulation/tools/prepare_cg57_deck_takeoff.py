#!/usr/bin/env python3
"""Prepare the Taipei coastal CG-57 / AH-64 voxel takeoff scene."""
import argparse
import json
import math
from pathlib import Path
import shutil

from prepare_ah64_voxel import MAP, MODEL, ROOT, read_mesh, scene_for, split_mesh, write_obj


CONFIG = ROOT / 'config' / 'earth'
# CG-57.ac's flight-deck-large-texture in the imported metre-scale hull frame.
DECK_HEIGHT = 11.3963
DECK_Z = -42.
ROTOR_START, ROTOR_RAMP, LIFTOFF, HOVER, DEPARTURE, END = .8, 5.6, 6.7, 8.8, 10.2, 14.4


def smooth(start, end, t):
    u = min(1., max(0., (t-start)/(end-start)))
    return u**3*(10.+u*(-15.+6.*u))


def write_json(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


def attitude(pitch, yaw, roll):
    sx, sy, sz = (math.sin(angle*.5) for angle in (pitch, yaw, roll))
    cx, cy, cz = (math.cos(angle*.5) for angle in (pitch, yaw, roll))
    # Apply roll, then pitch, then yaw in the asset's local frame.
    return (cy*sx*cz+sy*cx*sz, sy*cx*cz-cy*sx*sz,
            cy*cx*sz-sy*sx*cz, cy*cx*cz+sy*sx*sz)


def build(output):
    output.mkdir(parents=True, exist_ok=True)
    tables = read_mesh(MODEL / 'AH-64.obj')
    parts = split_mesh(tables[0], tables[3])
    for name, faces in parts.items():
        write_obj(output / f'{name}.obj', name, faces, tables[:3])
    for asset in ('AH-64.mtl', 'AH-64.jpg'):
        shutil.copy2(MODEL / asset, output / asset)

    # Sit the main wheels and tail wheel on the same deck plane.
    gear = [tables[0][i] for i in {v[0] for f in parts['body'] for v in f}]
    front = min((p for p in gear if 1.8 < p[2] < 2.4), key=lambda p: p[1])
    rear = min((p for p in gear if p[2] < -8.), key=lambda p: p[1])
    parked_pitch = math.atan2(front[1]-rear[1], front[2]-rear[2])
    contacts = [min((p for p in gear if sign*p[0] > .6 and 1.8 < p[2] < 2.4),
                    key=lambda p: math.cos(parked_pitch)*p[1]-math.sin(parked_pitch)*p[2])
                for sign in (-1, 1)] + [rear]
    floor = min(math.cos(parked_pitch)*y-math.sin(parked_pitch)*z for _, y, z in gear)
    base_height = DECK_HEIGHT-floor+.02

    scene = json.loads((CONFIG / 'taipei-coast-ocean.vscene.json').read_text())
    ship, terrain = scene['objects']
    ship['source']['path'] = str(ROOT / 'models' / 'CG-57' / 'cg57.vsgb')
    ship['source']['surface_detail'] = 'source_clipped'
    ship['physics'] = {'motion': 'prescribed'}
    for name in ('position_m', 'rotation_deg', 'velocity_mps'):
        ship.pop(name, None)
    terrain_source = terrain['source']
    terrain_source['map'] = str(MAP)
    terrain_source['sea_surface_mask']['map'] = str(CONFIG / 'taipei-sea-level.map.json')
    terrain_source['earth'].update(
        coverage={'min_x': -6400, 'max_x': 6400, 'min_z': -6400, 'max_z': 6400},
        grid_size=321, imagery_size=1024,
    )
    context_map = json.loads(MAP.read_text())
    context_map['earth'].update(terrain_source['earth'])
    write_json(output / 'taipei-deck-context.map.json', context_map)
    ocean = scene['ocean']
    ocean['reference_surface']['map'] = str(CONFIG / 'taipei-sea-level.map.json')
    ocean.update(wave_height_m=.34, swell_height_m=.12, wind_speed_mps=4)
    ocean['provenance'].pop('body_density', None)
    ocean['provenance']['vessel'] = 'Metre-scale FlightGear CG-57 on a prescribed path'

    helicopter, main, tail = scene_for(286, -1130)['objects']
    for name in ('position_m', 'rotation_deg', 'velocity_mps'):
        helicopter.pop(name, None)
    main['rotor_drive'].update(start_seconds=ROTOR_START, spin_up_seconds=ROTOR_RAMP, phase_deg=13)
    tail['rotor_drive'].update(start_seconds=ROTOR_START, spin_up_seconds=ROTOR_RAMP, phase_deg=19)
    helicopter['source']['presentation_phases'] = [
        {'seconds': seconds, 'state': state}
        for seconds, state in ((0., 'landed'), (ROTOR_START, 'spooling_up'),
                               (LIFTOFF, 'climbing'), (HOVER, 'hovering'), (DEPARTURE, 'flying'))
    ]
    ship_keys, helicopter_keys = [], []
    times = sorted({round(i*.06, 3) for i in range(241)} | {LIFTOFF, HOVER, DEPARTURE})
    for t in times:
        heave = .09*math.sin(t*math.tau/6.8)
        travel = 1.5*t
        ship_keys.append({'seconds': t, 'position_m': [0., heave, travel],
                          'rotation_xyzw': [0., 0., 0., 1.]})
        rise = smooth(LIFTOFF, HOVER, t)
        leave = smooth(DEPARTURE, END, t)
        pitch = parked_pitch*(1.-rise)+math.radians(2.)*leave
        helicopter_keys.append({
            'seconds': t,
            'position_m': [15.*leave, base_height+heave*(1.-rise)+4.*rise+9.*leave,
                           travel+DECK_Z+5.*leave],
            'rotation_xyzw': attitude(pitch, math.radians(-9.)*leave, math.radians(4.)*leave),
        })
    ship['pose_track'] = ship_keys
    helicopter['pose_track'] = helicopter_keys
    scene.update(objects=[ship, terrain, helicopter, main, tail])
    write_json(output / 'cg57-deck-takeoff.vscene.json', scene)

    topic = {
        'format': 'earth-scene-topic-v1', 'name': '台北外海 · CG-57 甲板旋翼启动与起飞',
        'scene': 'cg57-deck-takeoff.vscene.json', 'earth_map': 'taipei-deck-context.map.json', 'paused': False,
        'camera': {'eye_m': [-23., 15.8, DECK_Z-17.], 'target_m': [0., 14., DECK_Z]},
        'sky': {}, 'clouds': {'cover': .24},
        'equipment': {'format': 'voxel-equipment-v1', 'visual_only': True, 'equipment': [],
                      'platforms': [
                          {'id': 973, 'name': 'CG-57', 'kind': 'ship', 'model_id': 'cg57', 'object': 'vessel'},
                          {'id': 974, 'name': 'AH-64', 'kind': 'helicopter', 'model_id': 'ah64d', 'object': 'body'},
                      ]},
    }
    write_json(output / 'cg57-deck-takeoff.vtopic.json', topic)
    write_json(output / 'deck-takeoff-reference.json', {
        'deck_height_m': DECK_HEIGHT, 'gear_contacts_local_m': contacts, 'rotor_start_s': ROTOR_START,
        'ramp_s': ROTOR_RAMP, 'liftoff_s': LIFTOFF, 'hover_s': HOVER, 'departure_s': DEPARTURE, 'end_s': END,
    })


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    build(parser.parse_args().output.resolve())
