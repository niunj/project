#!/usr/bin/env python3
"""Run one continuous native Earth/physics/GPU route and retain its evidence."""
import argparse
import csv
import datetime as dt
import json
import os
from pathlib import Path
import re
import subprocess
import time


def kernel_faults():
    run = subprocess.run(["journalctl", "-k", "-b", "--no-pager"],
                         capture_output=True, text=True)
    if run.returncode:
        return None
    return [line for line in run.stdout.splitlines() if "NVRM: Xid" in line]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--scene", type=Path, required=True)
    parser.add_argument("--duration", type=int, default=1800)
    parser.add_argument("--validation", action="store_true")
    parser.add_argument("--report-dir", type=Path)
    args = parser.parse_args()
    if not 1 <= args.duration <= 43200:
        parser.error("duration must be 1..43200 seconds")
    repo = Path(__file__).resolve().parents[2]
    build = args.build_dir.resolve()
    scene = args.scene.resolve()
    binary = build / "gui/sim_earth_gui_tests"
    if not binary.is_file() or not scene.is_file():
        parser.error("the built GUI acceptance binary and scene must exist")
    output = args.report_dir or build / "stability" / dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    output.mkdir(parents=True, exist_ok=False)
    env = os.environ.copy()
    env["QT_QPA_PLATFORM"] = "xcb"
    env["VSGRAYSIM_SOAK_SECONDS"] = str(args.duration)
    env["VSGRAYSIM_BENCH_EARTH_SCENE"] = str(scene)
    env["VSGRAYSIM_BENCH_SWITCH_CSV"] = str(output.resolve() / "switches.csv")
    if args.validation:
        sdk = env.get("VULKAN_SDK", "")
        for line in (build / "CMakeCache.txt").read_text().splitlines():
            if line.startswith("VULKAN_SDK:"):
                sdk = line.split("=", 1)[1]
        layer = Path(sdk) / "share/vulkan/explicit_layer.d"
        if not (layer / "VkLayer_khronos_validation.json").is_file():
            parser.error("the configured SDK validation-layer manifest is missing")
        env["VK_LAYER_PATH"] = str(layer)
        env["VK_INSTANCE_LAYERS"] = "VK_LAYER_KHRONOS_validation"
        env["VK_LAYER_ENABLES"] = "VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT"
        env["VK_LOADER_DEBUG"] = "layer"
    before = kernel_faults()
    started = time.monotonic()
    timed_out = False
    with (output / "run.log").open("w") as log, (output / "gpu.csv").open("w", buffering=1) as telemetry:
        writer = csv.writer(telemetry)
        writer.writerow(["wall_seconds", "gpu", "temperature_C", "utilization_percent",
                         "memory_MiB", "power_W", "host_rss_KiB"])
        process = subprocess.Popen([str(binary), "--orbital-stream-benchmark"],
                                   cwd=repo, env=env, stdout=log, stderr=subprocess.STDOUT)
        next_sample = 0.0
        try:
            while process.poll() is None:
                elapsed = time.monotonic() - started
                if elapsed > args.duration + 120:
                    timed_out = True
                    process.terminate()
                    break
                if elapsed >= next_sample:
                    gpu = subprocess.run(
                        ["nvidia-smi", "--query-gpu=name,temperature.gpu,utilization.gpu,memory.used,power.draw",
                         "--format=csv,noheader,nounits"], capture_output=True, text=True, timeout=10)
                    status = Path(f"/proc/{process.pid}/status")
                    rss = 0
                    if status.exists():
                        for line in status.read_text().splitlines():
                            if line.startswith("VmRSS:"):
                                rss = int(line.split()[1])
                    for row in csv.reader(gpu.stdout.splitlines()):
                        writer.writerow([round(elapsed, 3), *row, rss])
                    next_sample = elapsed + 10
                time.sleep(1)
        finally:
            try:
                code = process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                code = process.wait()
    elapsed = time.monotonic() - started
    after = kernel_faults()
    faults = None if before is None or after is None else [line for line in after if line not in before]
    text = (output / "run.log").read_text(errors="replace")
    errors = [line for line in text.splitlines() if re.search(
        r"VUID-|SYNC-HAZARD-|Validation Error|VK_ERROR_DEVICE_LOST|VkResult=-4|"
        r"was not found but was requested|Failed to load layer", line)]
    groups = {}
    for match in re.finditer(r"(switched|LOD|adjacent|observer|weather_overlap) n=(\d+) "
                             r"p50_ms=([\d.]+) p95_ms=([\d.]+) max_ms=([\d.]+) over_33ms=(\d+)", text):
        name, count, p50, p95, maximum, over = match.groups()
        groups[name] = dict(count=int(count), p50_ms=float(p50), p95_ms=float(p95),
                            max_ms=float(maximum), over_33ms=int(over))
    required = {"switched", "LOD", "adjacent", "observer", "weather_overlap"}
    passed = (code == 0 and not timed_out and elapsed >= args.duration and not errors
              and faults == [] and required <= groups.keys()
              and all(group["p95_ms"] <= 33 for group in groups.values()))
    report = dict(passed=passed, exit_code=code, elapsed_wall_seconds=elapsed,
                  requested_seconds=args.duration, validation_requested=args.validation,
                  validation_loaded="Insert instance layer \"VK_LAYER_KHRONOS_validation\"" in text,
                  validation_errors=errors, new_kernel_xids=faults, performance=groups,
                  scene=str(scene), binary=str(binary), timed_out=timed_out)
    if args.validation and not report["validation_loaded"]:
        report["passed"] = False
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
