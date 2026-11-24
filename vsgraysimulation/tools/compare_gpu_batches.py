#!/usr/bin/env python3
"""Compare sequential real multi-display runs and their canonical GPU products."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--reference', default='build/gpu-batch/reference')
    parser.add_argument('--optimized', default='build/gpu-batch/optimized')
    parser.add_argument('--output', default='build/gpu-batch/comparison.json')
    args = parser.parse_args()
    roots = [Path(args.reference), Path(args.optimized)]
    reports = [json.loads((root / 'report.json').read_text()) for root in roots]
    reference, optimized = reports
    assert reference['serial_gpu_reference'] and not optimized['serial_gpu_reference']
    for report in reports:
        assert report['all_owned_children_reaped'] and report['application_exit_codes'] == [0, 0, 0]
        assert report['source_and_all_five_views_raw_payloads_equal']
    worlds = [r['canonical_simulation'] for r in reports]
    assert worlds[0]['tick'] == worlds[1]['tick'] == 1500
    assert worlds[0]['sim_time'] == worlds[1]['sim_time'] == 30
    assert worlds[0]['fixed_step_seconds'] == worlds[1]['fixed_step_seconds'] == .02
    assert worlds[0]['earth']['utc_origin'] == worlds[1]['earth']['utc_origin']
    assert worlds[0]['earth']['voxel_camera'] == worlds[1]['earth']['voxel_camera']
    assert all(worlds[0]['render'][k] == worlds[1]['render'][k] for k in ('width', 'height'))
    assert worlds[0]['checkpoint_sha256'] == worlds[1]['checkpoint_sha256'], 'canonical physics or pending events changed'
    channels = [{p['id']: p for p in w['channels']} for w in worlds]
    for equipment in (7101, 7102, 7103, 7104):
        a, b = [c[equipment] for c in channels]
        for field in ('frame_id', 'sim_time', 'surface_revision'):
            assert a[field] == b[field], (equipment, field)
        if equipment != 7104:
            assert a['payload_sha256'] == b['payload_sha256'], 'optical/thermal radiance changed'
    payloads = [(root / 'assembly-a-equipment-7104.rd-f32').read_bytes() for root in roots]
    assert len(payloads[0]) == len(payloads[1]) and len(payloads[0]) % 8 == 0
    for payload, channel in zip(payloads, channels):
        assert hashlib.sha256(payload).hexdigest() == channel[7104]['payload_sha256']
    radar = [[complex(re, im) for re, im in struct.iter_unpack('<ff', payload)] for payload in payloads]
    assert all(math.isfinite(v.real) and math.isfinite(v.imag) for values in radar for v in values)
    peak = max(abs(v) for v in radar[0])
    assert peak > 0, 'radar equality must not be proved using empty products'
    maximum = max(abs(a - b) for a, b in zip(*radar))
    norm = sum(abs(v) ** 2 for v in radar[0])
    relative_l2 = math.sqrt(sum(abs(a - b) ** 2 for a, b in zip(*radar)) / norm)
    bound = 128 * 2 ** -23  # unchanged FP32 reduction bound used by numerical regression
    assert maximum <= bound * peak and relative_l2 <= bound, 'RD exceeds FP32 reduction bound'

    def metrics(report):
        source = report['running_simulation']
        children = [v for a in report['running_assemblies'].values() for v in a['views']]

        def interval(key):
            values = [p['p95_ms'] for child in children for p in child[key]]
            return [min(values), max(values)]

        return {
            'complete_simulation_mean_ms': source['simulation_cycle_mean_ms'],
            'complete_simulation_p95_ms': source['simulation_cycle_p95_ms'],
            'nonempty_measurement_batches': source['measurement_batches'],
            'nonempty_measurement_roundtrips': source['measurement_roundtrips'],
            'simulation_time_over_wall_time': report['real_time_ratio'],
            'transport_p95_ms_range': interval('transport'),
            'background_processing_p95_ms_range': interval('processing'),
            'image_cpu_paint_p95_ms_range': interval('render_2d'),
            'simulation_vulkan_render': source['render'],
            'assembly_vulkan_render': [a['render_3d'] for a in report['running_assemblies'].values()],
            'canonical_gpu_scheduler': report['canonical_simulation']['source']['gpu_pool']['scheduler'],
        }

    before, after = [metrics(r) for r in reports]
    def external_endpoints(report):
        source=report['running_simulation']
        own={source['pid'],source['ui_pid'],source['source']['gpu_pool']['pid']}
        for assembly in report['running_assemblies'].values():
            own.add(assembly['ui_pid'])
            own.update(v['pid'] for v in assembly['views'])
        return [p for key in ('stable_sample_gpu_context_start','stable_sample_gpu_context_end')
                for p in report[key].get('compute_processes',[]) if p['pid'] not in own]
    assert after['canonical_gpu_scheduler']['stationary_cpis'] > 0
    assert after['canonical_gpu_scheduler']['multi_equipment_batches'] > 0
    result = {
        'reference': str(roots[0]), 'optimized': str(roots[1]),
        'canonical_tick': 1500, 'canonical_sim_time_seconds': 30,
        'same_utc_origin': worlds[0]['earth']['utc_origin'],
        'canonical_physics_checkpoint_sha256_equal': True,
        'all_three_optical_thermal_payloads_exact': True,
        'radar': {'max_absolute_error': maximum, 'relative_l2_error': relative_l2,
                  'reference_peak': peak, 'relative_bound': bound},
        'before': before, 'after': after,
        'complete_simulation_p95_reduction_percent': 100 * (1 - after['complete_simulation_p95_ms'] / before['complete_simulation_p95_ms']),
        'measurement_batch_p95_reduction_percent': 100 * (1 - after['nonempty_measurement_batches']['p95_ms'] / before['nonempty_measurement_batches']['p95_ms']),
        'scopes': reference['timing_scopes'],
        'display_statistics_scope': 'per-window rolling samples, including warmup; 3D render and backend statistics reset after warmup',
        'other_gpu_compute_at_sample_endpoints': {'reference': external_endpoints(reference), 'optimized': external_endpoints(optimized)},
    }
    target = Path(args.output)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(result, ensure_ascii=False, indent=2))
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
