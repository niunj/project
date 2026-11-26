#!/usr/bin/env python3
"""Run the three existing GUI executables, real Vulkan and isolated real IPC.

Requires BUILD_TESTING=ON, a working X11/Vulkan session and Taipei new data.
The in-app test driver clicks the existing tree/link/power controls. It never
creates a simulated data source in the display process or substitutes imagery.
"""
import argparse
import json
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", default="build")
    parser.add_argument("--output", default="build/formal-chain/accepted")
    parser.add_argument("--reuse-designer", action="store_true", help="reuse an already verified saved/reopened pair for a simulation/display-only rerun")
    parser.add_argument("--topic", default="config/topics/taipei-equipment.vtopic.json",
                        help="authored Taipei topic to open in the existing designer")
    parser.add_argument("--package", action="store_true", help="save/reopen versioned binary scene package")
    parser.add_argument("--authoring-edits", help="apply paused authoring transactions in the actual designer before its first save")
    parser.add_argument("--usd-edits", help="apply a composed USD target edit layer and compare exports across reopening")
    args = parser.parse_args()
    if args.usd_edits and args.reuse_designer:
        parser.error("--usd-edits requires running the designer")
    repo = Path(__file__).resolve().parents[1]
    build = (repo / args.build).resolve()
    output = (repo / args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    expected = ["designer.json", "designer-reopened.json", "simulation-ready.json",
                "simulation.json", "simulation-paused.json", "display-linked.json",
                "display-controls.json", "display.json"]
    for path in [*output.glob("*-error.json"), *(output / n for n in expected if not (args.reuse_designer and n.startswith("designer")))]:
        path.unlink(missing_ok=True)
    env = os.environ.copy()
    env["VSGRAYSIM_INLINE_DISPLAY_REFERENCE"] = "1"  # historical inline reference; production: validate_multi_display.py
    env["VSGRAYSIM_FORMAL_CHAIN_DIR"] = str(output)
    env["QT_QPA_PLATFORM"] = "xcb"
    env["LP_NUM_THREADS"] = "2"
    # Isolate saved dock layouts as well as the transport namespace.
    env["XDG_CONFIG_HOME"] = str(output / "settings")
    processes = []
    logs = []

    def launch(name, arguments, extra=None):
        local = dict(env)
        local.update(extra or {})
        label = "designer-reopened" if extra and extra.get("VSGRAYSIM_FORMAL_DESIGNER_NAME") else name
        log = (output / (label + ".log")).open("w")
        logs.append(log)
        process = subprocess.Popen([str(build / "app" / name), *arguments],
                                   cwd=repo, env=local, stdout=log, stderr=subprocess.STDOUT)
        processes.append(process)
        return process

    def wait(process, timeout=180):
        result = process.wait(timeout=timeout)
        if result:
            raise RuntimeError(f"{process.args[0]} exited with {result}; inspect {output}")

    try:
        with tempfile.TemporaryDirectory(prefix="vsg_formal_ipc_") as ipc:
            env["VSGRAYSIM_IPC_DIRECTORY"] = ipc
            topic = str((repo / args.topic).resolve())
            saved = output / ("saved.vsimscene" if args.package else "saved.vtopic.json")
            resaved = output / ("reopened.vsimscene" if args.package else "reopened.vtopic.json")
            usd_export = output / "target-edits.usda"
            usd_reopened = output / "target-edits-reopened.usda"
            if not args.reuse_designer:
                wait(launch("vsgraysim_scene_designer", ["--scene-topic", topic], {
                    "VSGRAYSIM_FORMAL_SCENE_SAVE": str(saved),
                    **({"VSGRAYSIM_FORMAL_USD_EDITS": str(Path(args.usd_edits).resolve()),
                        "VSGRAYSIM_FORMAL_USD_EXPORT": str(usd_export)} if args.usd_edits else {}),
                    **({"VSGRAYSIM_FORMAL_AUTHORING_EDITS": str(Path(args.authoring_edits).resolve())} if args.authoring_edits else {}),
                }))
                # A second actual process proves persistence across process exit.
                wait(launch("vsgraysim_scene_designer", ["--scene-topic", str(saved)], {
                    "VSGRAYSIM_FORMAL_SCENE_SAVE": str(resaved),
                    "VSGRAYSIM_FORMAL_DESIGNER_NAME": "designer-reopened",
                    **({"VSGRAYSIM_FORMAL_USD_EXPORT": str(usd_reopened)} if args.usd_edits else {}),
                }))
            def read_topic(path):
                document = json.loads(path.read_text())
                if document.get("format") != "vsgraysim-scene-package-v1":
                    return document
                # Independently decode and verify exactly the same content-addressed blobs.
                import zlib
                import cbor2
                def asset(digest):
                    entry = document["assets"][digest]
                    compressed = (path.parent / entry["uri"]).read_bytes()
                    raw = zlib.decompress(compressed)
                    assert len(compressed) == entry["stored_bytes"] and len(raw) == entry["expanded_bytes"]
                    assert hashlib.sha256(raw).hexdigest() == digest
                    return cbor2.loads(raw)
                if document.get("terrain_pages"):
                    for digest in document["assets"]:
                        asset(digest)
                    topic = document["topic"]
                    data = subprocess.check_output([str(build / "scene_package"), "checkpoint", str(path)],
                                                   cwd=repo, env=env)
                    topic["checkpoint"] = json.loads(data)
                    return topic
                state = document["checkpoint"]
                state["objects"] = []
                for entry in document["objects"]:
                    obj = asset(entry["state"])
                    obj["volume"] = asset(entry["volume"])
                    if "sensor_materials" in entry:
                        obj["sensor_materials"] = asset(entry["sensor_materials"])
                    state["objects"].append(obj)
                for key in ("earth_patches", "surface_appearances"):
                    if document[key]:
                        state[key] = {name: asset(digest) for name, digest in document[key].items()}
                topic = document["topic"]
                topic["checkpoint"] = state
                return topic
            before = read_topic(saved)
            after = read_topic(resaved)
            if before["checkpoint"] != after["checkpoint"]:
                raise AssertionError("scene save/reopen changed canonical voxel state")
            if before["equipment"] != after["equipment"]:
                raise AssertionError("scene save/reopen changed equipment configuration")
            terrains = [o for o in after["checkpoint"]["objects"] if "earth_patch_ref" in o]
            if not terrains:
                raise AssertionError("acceptance scene has no canonical Earth terrain")
            provenance = terrains[0]["provenance"]
            if provenance["source_coarse"] or provenance["provenance_assumed"] or "2024-02-14" not in provenance["earth"]["data_provenance"]["imagery"]:
                raise AssertionError("acceptance did not use the declared Taipei new data")
            camera_error = max(abs(a - b) for key in ("eye_m", "target_m", "up")
                               for a, b in zip(before["camera"][key], after["camera"][key]))
            if camera_error > 1e-6:
                raise AssertionError("saved camera changed across reopening")
            sim = launch("vsgraysim_sim_control", [str(resaved)])
            deadline = time.monotonic() + 180
            while not (output / "simulation-ready.json").exists():
                if sim.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("formal simulation failed to become ready")
                time.sleep(.2)
            display = launch("vsgraysim_control", [])
            wait(display)
            wait(sim, 30)
            reports = {name: json.loads((output / name).read_text()) for name in expected}
            geometry = Path(reports["display-linked.json"]["earth"]["voxel_scene"]["checkpoint_file"])
            # The cache may already be reclaimed at orderly source shutdown.
            # Source/view equality is also asserted in-process before capture.
            results = {
                "scene_roundtrip_equal": True,
                "authored_in_actual_designer": reports["designer.json"].get("authoring_commands_applied", False),
                "designer_reused": args.reuse_designer,
                "equipment_roundtrip_equal": True,
                "saved_checkpoint_restored_by_formal_simulation": reports["simulation-ready.json"]["saved_checkpoint_restored"],
                "remote_payloads_exactly_equal": reports["display.json"]["source_payloads_exactly_equal"],
                "source_scene": str(resaved),
                "utc_origin": after["checkpoint"]["utc_origin"],
                "camera_roundtrip_max_coordinate_error": camera_error,
                "terrain_provenance": provenance,
                "simulation_exit_code": sim.returncode,
                "display_exit_code": display.returncode,
                "formal_simulation_pid": reports["simulation-ready.json"].get("pid"),
                "remote_display_pid": reports["display.json"]["pid"],
                "channels": reports["display.json"]["channels"],
                "power_control": reports["display-controls.json"],
                "paused_retains_products": reports["display.json"]["simulation_paused"],
                "initially_unlinked_no_products": all(e["frames"] == 0 for e in reports["simulation-ready.json"]["equipment"]),
                "snapshot_cache": str(geometry),
                "scope": "actual three existing applications; Taipei GLO-30 + Sentinel-2B; clear-sky land, four actual GPU sensors over UDS; not all-weather/ocean spectral acceptance or a long-run performance benchmark",
            }
            results["terrain_display_pages"] = {
                role: reports[role + ".json"].get("terrain_display_pages", {})
                for role in ("designer", "designer-reopened", "simulation-ready", "display-linked")
            }
            if args.usd_edits:
                if not reports["designer.json"]["usd_edit_layer_applied"]:
                    raise AssertionError("designer did not apply the requested USD edit layer")
                if usd_export.read_bytes() != usd_reopened.read_bytes():
                    raise AssertionError("USD export changed after reopening the runtime package")
                results["usd_edit_layer_applied_in_designer"] = True
                results["usd_exports_exactly_equal_after_reopen"] = True
            if not results["initially_unlinked_no_products"]:
                raise AssertionError("measurement gate did not wait for display links")
            original_picture = output / "designer-earth.png"
            reopened_picture = output / "designer-reopened-earth.png"
            if not args.reuse_designer and original_picture.exists() and reopened_picture.exists():
                results["native_roundtrip_png_bytes_equal"] = hashlib.sha256(original_picture.read_bytes()).digest() == hashlib.sha256(reopened_picture.read_bytes()).digest()
            (output / "report.json").write_text(json.dumps(results, ensure_ascii=False, indent=2))
            print(json.dumps(results, ensure_ascii=False, indent=2))
    finally:
        for process in processes:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        for log in logs:
            log.close()


if __name__ == "__main__":
    main()
