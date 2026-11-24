#!/usr/bin/env python3
"""Real Taipei three-product acceptance, two assembly GUIs and five equipment UIs.
No simulated measurements or alternate display application. BUILD_TESTING required.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', default='build/multi-display/accepted')
    parser.add_argument('--topic', help='alternate Taipei topic with the same lease-test camera and IR IDs')
    parser.add_argument('--receiver-only', action='store_true', help='verify products in the existing per-equipment windows without assembly preview requirements')
    parser.add_argument('--reuse-designer', action='store_true')
    parser.add_argument('--serial-gpu-reference', action='store_true', help='original per-product/per-pulse synchronous GPU path')
    parser.add_argument('--quiet-gpu-sampling', action='store_true', help='pause for up to 30 seconds awaiting other compute processes before sampling')
    a = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    topic_path = (repo / (a.topic or 'config/topics/taipei-equipment.vtopic.json')).resolve()
    profile = json.loads(topic_path.read_text())['equipment']
    if isinstance(profile, str):
        profile = json.loads((topic_path.parent / profile).read_text())
    equipment_ids = [entry['id'] for entry in profile['equipment']]
    if not {7101, 7103}.issubset(equipment_ids):
        parser.error('the lease checks require devices 7101 and 7103')
    out = (repo / a.output).resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out / 'report.json').unlink(missing_ok=True)
    (out / 'stable-sample.json').unlink(missing_ok=True)
    dirs = {name: out / name for name in ('designer', 'simulation', 'assembly-a', 'assembly-b')}
    for name, folder in dirs.items():
        folder.mkdir(exist_ok=True)
        if not (a.reuse_designer and name == 'designer'):
            for f in folder.glob('*.json'):
                f.unlink()
    env = dict(os.environ, QT_QPA_PLATFORM='xcb', LP_NUM_THREADS='2', VSGRAYSIM_BENCHMARK_FENCES='1')
    env['VSGRAYSIM_FORMAL_EQUIPMENT_COUNT'] = str(len(equipment_ids))
    env.pop('VSGRAYSIM_GPU_SERIAL_REFERENCE', None)
    if a.serial_gpu_reference:
        env['VSGRAYSIM_GPU_SERIAL_REFERENCE'] = '1'
    processes, logs = [], []
    sequences = dict.fromkeys(dirs, 0)
    last_states = {}

    def read(file):
        try:
            return json.loads(file.read_text())
        except (FileNotFoundError, json.JSONDecodeError):
            return {}

    def state(role):
        kind = 'simulation' if role == 'simulation' else 'display'
        # Test probes may be sampled during a write. Keep the last complete
        # observation; an incomplete file is not a new application state.
        current = read(dirs[role] / ('probe-' + kind + '.json'))
        if current:
            last_states[role] = current
        return last_states.get(role, {})

    def gpu_context():
        try:
            gpu = subprocess.check_output(['nvidia-smi', '--query-gpu=name,driver_version,memory.total,utilization.gpu', '--format=csv,noheader,nounits'], text=True, timeout=3).strip()
            apps = subprocess.check_output(['nvidia-smi', '--query-compute-apps=pid,process_name,used_memory', '--format=csv,noheader,nounits'], text=True, timeout=3).strip()
            return {'gpu_name_driver_memory_MiB_utilization_percent': gpu,
                    'compute_processes': [dict(pid=int(parts[0]), executable=Path(parts[1].strip()).name, memory_MiB=parts[2].strip()) for row in apps.splitlines() if len(parts := row.split(',')) == 3]}
        except (OSError, ValueError, subprocess.SubprocessError):
            return {'available': False}

    def views(role):
        return {v['id']: v for v in state(role).get('views', [])}

    def wait(predicate, timeout=65):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for folder in dirs.values():
                for error in folder.glob('*-error.json'):
                    raise AssertionError(error.read_text())
            if predicate():
                return
            time.sleep(.1)
        raise TimeoutError('acceptance condition timed out; inspect ' + str(out))

    def command(role, action, **extra):
        sequences[role] += 1
        kind = 'simulation' if role == 'simulation' else 'display'
        target = dirs[role] / ('probe-command-' + kind + '.json')
        tmp = target.with_suffix('.pending')
        tmp.write_text(json.dumps(dict(seq=sequences[role], action=action, **extra)))
        tmp.replace(target)

    def launch(app, arguments, role, extra=None, managed=False):
        local = dict(env, VSGRAYSIM_FORMAL_CHAIN_DIR=str(dirs[role]), XDG_CONFIG_HOME=str(dirs[role] / 'settings'))
        if role != 'designer':
            local['VSGRAYSIM_WORKER_PROBE'] = '1'
        local.update(extra or {})
        log = (dirs[role] / (app + '-' + str(len(processes)) + '.log')).open('w')
        logs.append(log)
        proc = subprocess.Popen([str(repo / 'build/app' / app), *arguments], cwd=repo, env=local,
                                stdin=subprocess.PIPE if managed else None, stdout=log, stderr=subprocess.STDOUT)
        processes.append(proc)
        return proc

    def toggle(running):
        command('simulation', 'play' if running else 'pause')
        wait(lambda: state('simulation').get('ui_command_seq', 0) >= sequences['simulation'] and state('simulation').get('running') == running and not state('simulation').get('pending_seq'))

    def counts(equipment, data, bindings=None):
        s = next((s for s in state('simulation').get('subscription_leases', []) if s['id'] == equipment), {})
        return s.get('data_subscribers', 0) == data and (bindings is None or s.get('binding_subscribers', 0) == bindings)

    def ready(role, selected, products=False):
        v = views(role)
        for equipment in selected:
            s = v.get(equipment, {})
            if s.get('process_state') != 2 or not s.get('sessions') or not s.get('leases'):
                return False
            if products and not (s.get('processing') and all(t.get('has_frame') and t.get('frames', 0) >= 3 for t in s['sessions'])):
                return False
        return True

    def finish(role, proc):
        command(role, 'quit')
        assert proc.wait(timeout=15) == 0

    def capture_children(role):
        children = views(role)
        for equipment, child in children.items():
            path = Path(child['state_file'])
            Path(str(path) + '.capture').touch()
        for equipment, child in children.items():
            image = Path(child['state_file'] + '.png')
            wait(image.exists, 10)
            shutil.copyfile(image, out / (role + '-equipment-' + str(equipment) + '.png'))
            if equipment == 7104:
                raw = Path(child['state_file'] + '.rd-f32')
                wait(lambda: raw.exists() and hashlib.sha256(raw.read_bytes()).hexdigest() == child['product']['payload_sha256'], 10)
                shutil.copyfile(raw, out / (role + '-equipment-' + str(equipment) + '.rd-f32'))

    report = {'data': 'Taipei Copernicus GLO-30 ellipsoidal DSM + 2024-02-14 Sentinel-2B', 'designer_reused': a.reuse_designer,
              'serial_gpu_reference': a.serial_gpu_reference,
              'timing_scopes': {'simulation': 'full backend cycle; fixed physics, due sensor products, publication and due status; pacing wait excluded',
                                'transport': 'source monotonic envelope timestamp before enqueue to receiver after decoding/cache injection; includes queueing, excludes body serialization before envelope and GUI processing',
                                'processing': 'background payload preparation, image mapping or RD dB/CFAR/tracking; immutable host inputs; latest-value display queue',
                                'render_2d': 'actual Qt image paintEvent CPU wall time; GPU/compositor completion excluded',
                                'render_3d': 'native Vulkan advance/update/submit/present plus fence; compilation excluded'}}
    try:
        with tempfile.TemporaryDirectory(prefix='vsg_multi_ipc_') as ipc:
            env['VSGRAYSIM_IPC_DIRECTORY'] = ipc
            saved = dirs['designer'] / 'saved.vtopic.json'
            reopened = dirs['designer'] / 'reopened.vtopic.json'
            if not a.reuse_designer:
                first = launch('vsgraysim_scene_designer', ['--scene-topic', str(topic_path)], 'designer', {'VSGRAYSIM_FORMAL_SCENE_SAVE': str(saved)})
                assert first.wait(140) == 0
                second = launch('vsgraysim_scene_designer', ['--scene-topic', str(saved)], 'designer', {'VSGRAYSIM_FORMAL_SCENE_SAVE': str(reopened), 'VSGRAYSIM_FORMAL_DESIGNER_NAME': 'designer-reopened'})
                assert second.wait(140) == 0
            before, after = read(saved), read(reopened)
            assert before['checkpoint'] == after['checkpoint'] and before['equipment'] == after['equipment']
            ground = next(o for o in after['checkpoint']['objects'] if 'earth_patch_ref' in o)
            assert not ground['provenance']['source_coarse'] and '2024-02-14' in ground['provenance']['earth']['data_provenance']['imagery']
            report['scene_and_equipment_roundtrip_equal'] = True
            sim = launch('vsgraysim_sim_control', [str(reopened)], 'simulation')
            wait(lambda: state('simulation').get('initialized'))
            assert state('simulation')['source']['gpu_pool']['pid'] == 0
            command('simulation', 'play')
            wait(lambda: state('simulation').get('run_requested') and state('simulation').get('state') == 'waiting_for_leases')
            initial_tick = state('simulation')['tick']
            time.sleep(.7)
            assert state('simulation')['tick'] == initial_tick and state('simulation')['source']['gpu_pool']['pid'] == 0
            toggle(False)
            report['no_task_lease_no_clock_advance_or_gpu_allocation'] = True
            if a.receiver_only:
                paths, receivers = {}, []
                scene_id = state('simulation')['scene_session_id']
                for entry in profile['equipment']:
                    id = entry['id']
                    mode = {'RADAR': 'radar', 'SAR': 'sar', 'TV': 'optical'}.get(entry['band'], 'infrared')
                    paths[id] = out / ('receiver-' + str(id) + '.json')
                    for stale in (paths[id], Path(str(paths[id]) + '.png'), Path(str(paths[id]) + '.capture')):
                        stale.unlink(missing_ok=True)
                    receivers.append(launch('vsgraysim_control', [
                        '--equipment-view=' + str(id), '--workmode=' + mode,
                        '--view-state=' + str(paths[id]), '--view-scene=' + scene_id,
                        '--view-managed=1'], 'assembly-a', managed=True))
                wait(lambda: all(counts(id, 1, 0) for id in equipment_ids))
                toggle(True)
                def receiving(id):
                    state = read(paths[id])
                    return state.get('processing') and state.get('transport') and state['sessions'][0]['frames'] >= 3
                wait(lambda: all(receiving(id) for id in equipment_ids))
                command('simulation', 'reset_metrics')
                wait(lambda: state('simulation').get('ui_command_seq', 0) >= sequences['simulation'])
                started, start_time = time.monotonic(), state('simulation')['sim_time']
                time.sleep(8)
                report['real_time_ratio'] = (state('simulation')['sim_time'] - start_time) / (time.monotonic() - started)
                report['running_simulation'] = state('simulation')
                report['running_views'] = {id: read(path) for id, path in paths.items()}
                toggle(False)
                sent = {p['id']: p for p in state('simulation')['channels']}
                def matches(id):
                    got = read(paths[id])
                    product = got.get('product', {})
                    source = sent.get(id, {})
                    processing = got.get('processing') or []
                    return not got.get('running', True) and bool(product) and bool(source) and bool(processing) and processing[0]['frame_id'] == source['frame_id'] and all(
                        product.get(key) == source.get(key) for key in ('frame_id', 'payload_sha256', 'surface_revision', 'ground_samples', 'ground_sha256'))
                wait(lambda: all(matches(id) for id in equipment_ids))
                report['source_and_all_equipment_views_payloads_equal'] = True
                report['canonical_views'] = {id: read(path) for id, path in paths.items()}
                report['canonical_simulation'] = state('simulation')
                for path in paths.values():
                    Path(str(path) + '.capture').touch()
                wait(lambda: all(Path(str(path) + '.png').exists() for path in paths.values()))
                (out / 'receiver-report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2))
                for receiver in receivers:
                    receiver.stdin.write(b'quit\n')
                    receiver.stdin.close()
                    assert receiver.wait(10) == 0
                finish('simulation', sim)
                return
            assembly_a = launch('vsgraysim_control', [], 'assembly-a')
            wait(lambda: state('assembly-a').get('online'))
            time.sleep(1)
            command('assembly-a', 'open_console', mode='aircraft', platform_id=72)
            wait(lambda: any(v.get('process_state') == 2 for v in state('assembly-a').get('console_processes', [])) and counts(0, 0, 1))
            console_pid = next(v['pid'] for v in state('assembly-a')['console_processes'] if v['process_state'] == 2)
            toggle(True)
            clock_before = state('simulation')['tick']
            wait(lambda: state('simulation')['tick'] > clock_before + 5)
            assert state('simulation')['source']['gpu_pool']['pid'] == 0 and all(not e['frames'] for e in state('simulation')['equipment'])
            toggle(False)
            command('assembly-a', 'close_consoles')
            wait(lambda: counts(0, 0, 0) and not Path('/proc/' + str(console_pid)).exists())
            report['original_aircraft_console_uses_own_process_and_time_only_lease'] = True
            command('assembly-a', 'bind', equipment=equipment_ids)
            wait(lambda: ready('assembly-a', equipment_ids))
            assembly_b = launch('vsgraysim_control', [], 'assembly-b')
            wait(lambda: state('assembly-b').get('online'))
            time.sleep(1)
            command('assembly-b', 'bind', equipment=[7101])
            wait(lambda: ready('assembly-b', [7101]) and counts(7101, 2, 2))
            assert all(not e['frames'] for e in state('simulation')['equipment'])
            toggle(True)
            wait(lambda: ready('assembly-a', equipment_ids, True) and ready('assembly-b', [7101], True))
            wait(lambda: state('simulation').get('view_ready'), 120)
            wait(lambda: state('assembly-a').get('view_ready') and state('assembly-b').get('view_ready'))
            time.sleep(8)
            own_pids = {p.pid for p in processes} | {state('simulation')['pid'], state('simulation')['source']['gpu_pool']['pid']}
            own_pids.update(v['pid'] for role in ('assembly-a', 'assembly-b') for v in views(role).values())
            def external_compute(context):
                return [p for p in context.get('compute_processes', []) if p['pid'] not in own_pids]
            if a.quiet_gpu_sampling:
                toggle(False)
                deadline = time.monotonic() + 30
                while external_compute(gpu_context()) and time.monotonic() < deadline:
                    time.sleep(.5)
                toggle(True)
            for role in ('simulation', 'assembly-a', 'assembly-b'):
                command(role, 'reset_metrics')
            wait(lambda: state('simulation').get('ui_command_seq', 0) >= sequences['simulation'] and not state('simulation').get('pending_seq') and
                 all(state(role).get('command_seq', 0) >= sequences[role] for role in ('assembly-a', 'assembly-b')))
            started, sim_started = time.monotonic(), state('simulation')['sim_time']
            report['stable_sample_gpu_context_start'] = gpu_context()
            time.sleep(8)
            report['running_simulation'] = state('simulation')
            gpu_pid = report['running_simulation']['source']['gpu_pool']['pid']
            assert gpu_pid > 0 and gpu_pid not in (sim.pid, report['running_simulation']['pid'])
            report['measurement_uses_separate_demand_started_gpu_process'] = True
            report['gpu_capacity'] = report['running_simulation']['process_pool']['gpu_capacity']

            report['running_assemblies'] = {role: state(role) for role in ('assembly-a', 'assembly-b')}
            report['real_time_ratio'] = (state('simulation')['sim_time'] - sim_started) / (time.monotonic() - started)
            report['stable_sample_gpu_context_end'] = gpu_context()
            report['external_compute_at_sample_endpoints'] = external_compute(report['stable_sample_gpu_context_start']) + external_compute(report['stable_sample_gpu_context_end'])
            (out / 'stable-sample.json').write_text(json.dumps(report, ensure_ascii=False, indent=2))
            for role in ('assembly-a', 'assembly-b'):
                assert state(role)['separate_views'] and not state(role)['sessions']
                assert all(p['received'] == 0 for p in state(role)['channels']), 'assembly UI received raw products'
                assert all(p['frame_wire_bytes'] == 0 for p in state(role)['leases']), 'assembly transport received raw product bytes'
                for child in views(role).values():
                    assert child['pid'] != state(role)['ui_pid']
                    assert child['processing'] and all(p['off_gui'] for p in child['processing'])
                    assert child['transport'] and child['render_2d']
            report['assembly_metadata_only'] = True
            report['separate_equipment_processes_and_background_processing'] = True
            toggle(False)
            capture_tick = state('simulation')['tick'] + 30 if a.topic else 1500
            command('simulation', 'play_to_tick', tick=capture_tick)
            wait(lambda: state('simulation').get('tick') == capture_tick and not state('simulation').get('running') and not state('simulation').get('pending_seq'), 45)
            wait(lambda: state('simulation').get('view_tick') == capture_tick and state('simulation').get('checkpoint_sha256'))
            wait(lambda: all(not v.get('running', True) and v.get('product') for role in ('assembly-a', 'assembly-b') for v in views(role).values()))
            time.sleep(.7)
            sent = {p['id']: p for p in state('simulation')['channels']}
            for role in ('assembly-a', 'assembly-b'):
                for equipment, view in views(role).items():
                    got = view['product']
                    assert got['frame_id'] == sent[equipment]['frame_id'] and got['payload_sha256'] == sent[equipment]['payload_sha256']
                    assert got['surface_revision'] == sent[equipment]['surface_revision']
                    wait(lambda r=role, e=equipment: views(r)[e]['processing'][0]['frame_id'] == sent[e]['frame_id'])
            report['source_and_all_equipment_views_payloads_equal'] = True
            if not a.topic:
                report['source_and_all_five_views_raw_payloads_equal'] = True
            report['canonical_simulation'] = state('simulation')
            report['canonical_assemblies'] = {role: state(role) for role in ('assembly-a', 'assembly-b')}
            command('simulation', 'capture', name='simulation-multidisplay')
            command('assembly-a', 'capture', name='assembly-a')
            command('assembly-b', 'capture', name='assembly-b')
            capture_children('assembly-a');capture_children('assembly-b')
            all_children = [v['pid'] for role in ('assembly-a', 'assembly-b') for v in views(role).values()]
            toggle(True)
            survivor = views('assembly-b')[7101]
            old_count = survivor['sessions'][0]['frames']
            command('assembly-a', 'kill_view', equipment_id=7101)
            wait(lambda: counts(7101, 1, 2), 12)
            wait(lambda: views('assembly-b')[7101]['sessions'][0]['frames'] > old_count + 3)
            assert views('assembly-b')[7101]['pid'] == survivor['pid']
            report['abnormal_exit_expired_only_own_lease'] = True
            command('assembly-a', 'close_view', equipment_id=7103)
            wait(lambda: counts(7103, 0, 1))
            old_frames = next(x['frames'] for x in state('simulation')['equipment'] if x['id'] == 7103)
            time.sleep(.8)
            assert next(x['frames'] for x in state('simulation')['equipment'] if x['id'] == 7103) == old_frames
            report['final_data_consumer_stops_acquisition_binding_remains'] = True
            command('assembly-a', 'open_view', equipment_id=7101)
            wait(lambda: counts(7101, 2, 2) and ready('assembly-a', [7101], True))
            all_children.append(views('assembly-a')[7101]['pid'])
            # A GPU task-process crash must not inject mixed truth. The
            # supervisor restores the last committed world checkpoint paused,
            # invalidates leases, and requires the original operator rebind.
            toggle(False)
            checkpoint_state = state('simulation')
            old_world = checkpoint_state['pid']
            old_gpu = checkpoint_state['source']['gpu_pool']['pid']
            old_epoch = checkpoint_state['scene_session_id']
            toggle(True)
            wait(lambda: state('simulation').get('tick', 0) > checkpoint_state['tick'] + 5)
            os.kill(old_gpu, 9)
            wait(lambda: state('simulation').get('initialized') and state('simulation').get('recovered') and state('simulation').get('pid') != old_world)
            assert not state('simulation')['running'] and state('simulation')['tick'] == checkpoint_state['recovery_tick']
            assert state('simulation')['source']['gpu_pool']['pid'] == 0
            wait(lambda: not state('assembly-a').get('linked_ids') and not state('assembly-b').get('linked_ids') and state('assembly-a').get('scene_session_id') != old_epoch)
            assert not Path('/proc/' + str(old_gpu)).exists()
            report['gpu_process_fault_recovered_paused_and_invalidated_leases'] = True
            time.sleep(1)
            command('assembly-a', 'bind', equipment=equipment_ids)
            command('assembly-b', 'bind', equipment=[7101])
            wait(lambda: ready('assembly-a', equipment_ids) and ready('assembly-b', [7101]) and counts(7101, 2, 2))
            toggle(True)
            wait(lambda: ready('assembly-a', equipment_ids, True) and ready('assembly-b', [7101], True))
            all_children.extend(v['pid'] for role in ('assembly-a', 'assembly-b') for v in views(role).values())
            gpu_pid = state('simulation')['source']['gpu_pool']['pid']
            old_count = views('assembly-b')[7101]['sessions'][0]['frames']
            command('assembly-a', 'detach')
            wait(lambda: not state('assembly-a').get('linked_ids') and counts(7101, 1, 1))
            wait(lambda: views('assembly-b')[7101]['sessions'][0]['frames'] > old_count + 3)
            report['one_assembly_detach_preserves_other_assembly_stream'] = True
            finish('assembly-a', assembly_a)
            finish('assembly-b', assembly_b)
            wait(lambda: not state('simulation').get('subscription_leases'))
            report['all_leases_released_after_normal_shutdown'] = True
            wait(lambda: state('simulation').get('state') == 'waiting_for_leases' and not state('simulation').get('running'))
            last_tick = state('simulation')['tick']
            time.sleep(.7)
            assert state('simulation')['tick'] == last_tick
            wait(lambda: state('simulation')['source']['gpu_pool']['pid'] == 0, 10)
            assert not Path('/proc/' + str(gpu_pid)).exists()
            report['unused_gpu_slot_reclaimed_and_clock_waits_for_lease'] = True
            toggle(False)
            worker_pid = state('simulation')['pid']
            finish('simulation', sim)
            assert not Path('/proc/' + str(worker_pid)).exists()
            assert all(not Path('/proc/' + str(pid)).exists() for pid in all_children)
            report['all_owned_children_reaped'] = True
            report['application_exit_codes'] = [assembly_a.returncode, assembly_b.returncode, sim.returncode]
            (out / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2))
            print(json.dumps({k: v for k, v in report.items() if not k.startswith('running_')}, ensure_ascii=False, indent=2))
    finally:
        for proc in reversed(processes):
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(5)
                except subprocess.TimeoutExpired:
                    proc.kill();proc.wait()
        for log in logs:
            log.close()


if __name__ == '__main__':
    main()
