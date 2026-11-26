#!/usr/bin/env python3
"""Prepare the Taipei named-sound-mount acceptance topic."""
import argparse
import json
import math
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def load(path):
    return json.loads(path.read_text())


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n")


def yaw_pose(seconds, position, heading):
    half_angle = math.radians(heading) / 2
    return {"seconds": seconds, "position_m": position,
            "rotation_xyzw": [0, math.sin(half_angle), 0, math.cos(half_angle)]}


def box(id_, role, motion, position, origin, size, cells, color):
    return {"id": id_, "role": role, "position_m": position,
            "source": {"type": "box", "origin_m": origin, "cell_size_m": size, "dimensions": cells},
            "physics": {"motion": motion}, "material": {"color_rgba": color, "density_kg_m3": 450}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    output = parser.parse_args().output.resolve()
    subprocess.run([sys.executable, str(ROOT / "tools/prepare_ah64_voxel.py"), str(output)], check=True)
    helicopter_scene = load(output / "ah64-slow.vscene.json")
    rotorcraft = helicopter_scene["objects"]
    fuselage = rotorcraft[0]
    fuselage["position_m"] = [-8, 22, 0]
    fuselage.pop("velocity_mps")
    fuselage["pose_track"] = [yaw_pose(0, [-8, 22, 0], 18), yaw_pose(5, [6, 24, 1], 48)]
    earth_map = ROOT / "config/earth/taipei-region.map.json"
    scene = load(ROOT / "config/earth/taipei-acoustic-flyover.vscene.json")
    scene["fixed_step_seconds"] = helicopter_scene["fixed_step_seconds"]
    terrain = next(obj for obj in scene["objects"] if obj["role"] == "terrain")
    terrain["source"]["map"] = str(earth_map)
    aircraft = next(obj for obj in scene["objects"] if obj["id"] == "flyover")
    aircraft["source"]["path"] = str(ROOT / "models/RealPlatforms/SkywalkerX8/x8_body.dae")
    aircraft["position_m"] = [-24, 26, -6]
    aircraft["velocity_mps"] = [8, 0, 0]
    pad = box("contact-pad", "structure", "static", [0, 0, 0], [8, 14, -10],
              1, [4, 1, 4], [.28, .38, .42, 1])
    crate = box("crate", "target", "dynamic", [9.6, 19, -8.35], [-.4, -.4, -.4],
                .4, [2, 2, 2], [.85, .53, .18, 1])
    scene["objects"] = [terrain, aircraft, *rotorcraft, pad, crate]
    save(output / "mounts.vscene.json", scene)
    equipment = {"format": "voxel-equipment-v1", "platforms": [
        {"id": 880, "object": "flyover", "name": "Skywalker X8", "kind": "uav", "model_id": "skywalker-x8"},
        {"id": 883, "object": "body", "name": "旋翼平台", "kind": "helicopter", "model_id": "ah64d",
         "mounts": {"camera": {"position_m": [.8, -.3, 2], "forward": [0, -1, 0], "up": [0, 0, -1]}}}],
        "equipment": [
            {"id": 887, "name": "可见光相机", "platform_id": 883, "platform_kind": "helicopter",
             "model_id": "optical-camera-1", "band": "TV", "period_seconds": .2,
             "width": 9, "height": 7, "fov_deg": 30, "output": "P0",
             "aperture": {"position_m": [0, 0, 0], "look_at_m": [0, 0, 1], "mount_id": "camera"}}]}
    topic = {"format": "earth-scene-topic-v1", "name": "台北具名声源挂点验收",
             "scene": "mounts.vscene.json", "earth_map": str(earth_map), "clouds": {"cover": 0},
             "paused": True, "camera": {"eye_m": [-25, 20, -27], "target_m": [2, 25, -1]},
             "equipment": equipment, "acoustics": {"version": 1, "probes": [
                 {"id": "mount-check", "position_m": [4, 9, -12]}]}}
    save(output / "mounts.vtopic.json", topic)


if __name__ == "__main__":
    main()
