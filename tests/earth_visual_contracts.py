import os
import pathlib
import subprocess
import sys
import tempfile

script = pathlib.Path(sys.argv[1]).resolve()
names = ['sky_noon', 'sky_dusk', 'sky_midnit', 'cloud_02', 'cloud_05', 'cloud_08',
         'ocean_w05', 'ocean_w10', 'ocean_w20', 'cloud_midnit']
with tempfile.TemporaryDirectory(prefix='earth_visual_contract_') as directory:
    root = pathlib.Path(directory)
    viewer = root / 'viewer'
    viewer.write_text('#!/bin/sh\nexit 0\n')
    viewer.chmod(0o700)
    environment = dict(os.environ, ENV_VIEWER=str(viewer), ENV_REGRESS_FRAMES='1')
    output = root / 'shots'
    output.mkdir()
    # Stale files from an old run must not hide the absence of new captures.
    for name in names:
        (output / (name + '.ppm')).write_bytes(b'P6\n16 16\n255\n' + bytes([100, 120, 180]) * 256)
    result = subprocess.run(['bash', str(script), str(output)], env=environment, capture_output=True, text=True, timeout=20)
    assert result.returncode != 0, 'missing captures falsely passed the visual matrix'
    assert '全部通过' not in result.stdout, result.stdout
    assert not any(output.glob('*.ppm')), 'stale captures were reused'
    viewer.write_text('#!/bin/sh\nexit 17\n')
    result = subprocess.run(['bash', str(script), str(output)], env=environment, capture_output=True, text=True, timeout=20)
    assert result.returncode != 0, 'failed render processes falsely passed'
    assert '全部通过' not in result.stdout, result.stdout
    assert len((output / 'summary.txt').read_text().splitlines()) == len(names), 'failed cases were not accumulated'
print('PASS: visual matrix fails on missing, stale and failed captures')
