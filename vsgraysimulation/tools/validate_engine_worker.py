#!/usr/bin/env python3
"""Exercise the existing GUI's controls and original equipment link UI over real IPC.
Requires a saved Taipei .vtopic.json, BUILD_TESTING, X11 and Vulkan. No fake sensors.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--topic', default='build/formal-chain/worker/saved.vtopic.json')
    p.add_argument('--output', default='build/engine-worker/accepted')
    p.add_argument('--reference', action='store_true')
    p.add_argument('--performance-only', action='store_true', help='sample a warm running workload; skip already verified reconnect/crash cases')
    p.add_argument('--warmup-seconds', type=float, default=15, help='warm the actual Earth background and display before sampling')
    a = p.parse_args()
    repo = Path(__file__).resolve().parents[1]
    out = (repo / a.output).resolve()
    out.mkdir(parents=True, exist_ok=True)
    for f in out.glob('*.json'):
        f.unlink()
    env = dict(os.environ, VSGRAYSIM_FORMAL_CHAIN_DIR=str(out), VSGRAYSIM_WORKER_PROBE='1',
               VSGRAYSIM_BENCHMARK_FENCES='1', XDG_CONFIG_HOME=str(out / 'settings'), LP_NUM_THREADS='2')
    if a.reference:
        env['VSGRAYSIM_INPROCESS_REFERENCE'] = '1'
    env['VSGRAYSIM_INLINE_DISPLAY_REFERENCE'] = '1'  # this script compares the historical inline UI
    processes, logs, sequence = [], [], {'simulation': 0, 'display': 0}

    def read(name):
        try:
            return json.loads((out / name).read_text())
        except (FileNotFoundError, json.JSONDecodeError):
            return {}

    def status(role):
        return read('probe-' + role + '.json')

    def wait(predicate, timeout=65):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for f in out.glob('*-error.json'):
                raise AssertionError(f.read_text())
            if predicate():
                return
            time.sleep(.1)
        raise TimeoutError('condition timed out; inspect ' + str(out))

    def command(role, action, **extra):
        sequence[role] += 1
        file = out / ('probe-command-' + role + '.json')
        tmp = file.with_suffix('.pending')
        tmp.write_text(json.dumps(dict(seq=sequence[role], action=action, **extra)))
        tmp.replace(file)

    def launch(name, args, label):
        log = (out / (label + '.log')).open('w')
        logs.append(log)
        proc = subprocess.Popen([str(repo / 'build/app' / name), *args], cwd=repo, env=env, stdout=log, stderr=subprocess.STDOUT)
        processes.append(proc)
        return proc

    def connected():
        s = status('display').get('sessions', [])
        return len(s) == 4 and all(x.get('connected') for x in s)

    def products():
        s = status('display').get('sessions', [])
        return len(s) == 4 and all(x.get('has_frame') and x.get('frames', 0) >= 3 for x in s)

    def paused():
        s = status('simulation')
        return s.get('initialized') and not s.get('running') and not s.get('pending_seq')

    def toggle(running):
        command('simulation', 'play' if running else 'pause')
        wait(lambda: status('simulation').get('ui_command_seq', 0) >= sequence['simulation'] and status('simulation').get('running') == running and not status('simulation').get('pending_seq'))

    def attach():
        wait(lambda: status('display').get('online'))
        time.sleep(1.5)
        command('display', 'bind')
        wait(connected)

    def finish(proc, role):
        command(role, 'quit')
        assert proc.wait(timeout=12) == 0

    report = {'reference': a.reference, 'topic': str((repo / a.topic).resolve()), 'render_scope': 'native advance/events/update/submit/present + fence; excludes UI feature preparation and graph compilation',
              'simulation_scope': 'background full numerical cycle: commands + fixed physics + geometry publication + actual due sensor GPU products + broker enqueue + due status publication; excludes pacing wait, asynchronous IPC delivery and display rendering'}
    try:
        with tempfile.TemporaryDirectory(prefix='vsg_worker_ipc_') as ipc:
            env['VSGRAYSIM_IPC_DIRECTORY'] = ipc
            start = time.monotonic()
            sim = launch('vsgraysim_sim_control', [str((repo / a.topic).resolve())], 'simulation')
            wait(lambda: bool(status('simulation')), 65)
            report['first_ui_probe_seconds'] = time.monotonic() - start
            wait(lambda: status('simulation').get('initialized') and status('simulation').get('view_ready'))
            report['first_ready_seconds'] = time.monotonic() - start
            report['initial'] = status('simulation')
            if not a.reference:
                assert report['initial']['pid'] != sim.pid and report['initial']['worker_process']
            display = launch('vsgraysim_control', [], 'display')
            attach()
            toggle(True)
            wait(products)
            if a.warmup_seconds > 0:
                # Let the actual Earth background and display GPU preparation
                # finish before taking the independent steady-state samples.
                time.sleep(a.warmup_seconds)
                command('simulation', 'reset_metrics')
                wait(lambda: status('simulation').get('ui_command_seq', 0) >= sequence['simulation'] and not status('simulation').get('pending_seq'))
            timing_begin = time.monotonic()
            simulation_begin = status('simulation')['sim_time']
            time.sleep(8)
            report['running'] = status('simulation')
            report['measured_wall_seconds'] = time.monotonic() - timing_begin
            report['measured_simulation_seconds'] = report['running']['sim_time'] - simulation_begin
            report['real_time_ratio'] = report['measured_simulation_seconds'] / report['measured_wall_seconds']
            command('simulation', 'capture', name='running-simulation')
            command('display', 'capture', name='running-display')
            time.sleep(1)
            toggle(False)
            wait(paused)
            settled = status('simulation')
            time.sleep(1)
            assert status('simulation')['tick'] == settled['tick'], 'pause did not hold canonical tick'
            report['pause_holds_tick'] = True
            report['paused'] = status('simulation')
            command('simulation', 'step')
            wait(lambda: paused() and status('simulation').get('tick') == settled['tick'] + 1)
            assert abs(status('simulation')['sim_time'] - settled['sim_time'] - settled['fixed_step_seconds']) < 1e-9
            report['single_step_exact_fixed_step'] = True
            if a.reference or a.performance_only:
                finish(display, 'display');finish(sim, 'simulation')
            else:
                # A new display process must use the same original discovery and link button.
                first_display = display.pid
                finish(display, 'display')
                time.sleep(.7)
                (out / 'probe-command-display.json').unlink(missing_ok=True)
                (out / 'probe-display.json').unlink(missing_ok=True)
                display = launch('vsgraysim_control', [], 'display-reconnected')
                attach();toggle(True);wait(products);toggle(False);wait(paused)
                report['display_reconnect_new_pid'] = display.pid != first_display
                before = status('simulation')
                checkpoint = json.loads(Path(before['recovery_checkpoint']).read_text())
                previous_pid, previous_generation = before['pid'], before['generation']
                previous_session = status('display')['scene_session_id']
                toggle(True)
                wait(lambda: status('simulation').get('tick', 0) > before['tick'] + 5)
                report['crash_while_running'] = status('simulation')['running']
                report['uncommitted_ticks_before_crash'] = status('simulation')['tick'] - before['tick']
                os.kill(previous_pid, signal.SIGKILL)
                wait(lambda: status('simulation').get('initialized') and status('simulation').get('pid') != previous_pid and status('simulation').get('recovered'), 65)
                recovered = status('simulation')
                assert not recovered['running'] and recovered['tick'] == before['recovery_tick']
                assert recovered['generation'] != previous_generation
                assert json.loads(Path(recovered['recovery_checkpoint']).read_text()) == checkpoint, 'full checkpoint changed after process recovery'
                wait(lambda: status('display').get('scene_session_id') not in ('', previous_session) and not status('display').get('linked_ids'))
                assert all(x.get('frames', 0) == 0 for x in recovered['equipment']), 'new process produced frames before operator rebind'
                report['crash_recovered_paused_exact_checkpoint'] = True
                report['crash_invalidated_previous_links'] = True
                report['recovered'] = recovered
                attach();toggle(True);wait(products);toggle(False);wait(paused)
                wait(lambda: not status('display').get('running'))
                time.sleep(1)
                sent = {x['id']: x for x in status('simulation')['channels']}
                got = {x['id']: x for x in status('display')['channels']}
                for key in sent:
                    assert sent[key]['frame_id'] == got[key]['frame_id']
                    assert sent[key]['payload_sha256'] == got[key]['payload_sha256']
                    assert sent[key]['surface_revision'] == got[key]['surface_revision']
                report['recovered_payloads_exactly_equal'] = True
                command('simulation', 'capture', name='recovered-simulation')
                command('display', 'capture', name='recovered-display')
                time.sleep(1)
                final_pid = status('simulation')['pid']
                finish(display, 'display');finish(sim, 'simulation')
                assert not Path('/proc/' + str(final_pid)).exists(), 'worker orphaned after GUI shutdown'
                report['normal_shutdown_reaped_worker'] = True
                report['simulation_exit_code'] = sim.returncode
            (out / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2))
            print(json.dumps({k: v for k, v in report.items() if k not in ('initial', 'running', 'paused', 'recovered')}, ensure_ascii=False, indent=2))
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
