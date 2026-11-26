#!/usr/bin/env python3
"""Prepare a Taipei demonstrator for vertical echo sounding."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "config"
DIESEL_ASSET = ROOT / "models/RealPlatforms/CC0-DieselSubmarine/diesel-submarine.glb"
DIESEL_SHA256 = "1641a433b6d4fcdbc9146aa4a781de2f7f2b902243003168b47e0813b8652d0e"
DIESEL_CELL_M = 0.17006583335498968
# Centre of the cylindrical propeller hub in the source metres, bow towards +X.
PROPELLER_PIVOT = (-.65, 3.15, 0.)


def write_json(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def split_diesel_model(output):
    raw = DIESEL_ASSET.read_bytes()
    if hashlib.sha256(raw).hexdigest() != DIESEL_SHA256:
        raise ValueError("Diesel submarine GLB must match the asset used to register the shaft pivot")
    size, tag = struct.unpack_from("<II", raw, 12)
    if tag != 0x4E4F534A:
        raise ValueError("Missing GLB JSON chunk")
    source_json = raw[20:20+size]
    binary_chunk = raw[20+size:]
    hull = output / "diesel-hull.glb"
    propeller = output / "diesel-propeller.glb"
    for path in (hull, propeller):
        is_propeller = path == propeller
        doc = json.loads(source_json)
        nodes = [node for node in doc["nodes"]
                 if "mesh" in node and (node["name"] == "Propeller") == is_propeller]
        if len(nodes) != (1 if is_propeller else 3):
            raise ValueError("Unexpected number of submarine mesh parts")
        doc["meshes"] = [doc["meshes"][node["mesh"]] for node in nodes]
        for i, node in enumerate(nodes):
            node["mesh"] = i
        if is_propeller:
            nodes[0]["translation"] = [-coordinate for coordinate in PROPELLER_PIVOT]
        doc.update(nodes=nodes, scenes=[{"nodes": list(range(len(nodes)))}], scene=0)
        text = json.dumps(doc, separators=(",", ":")).encode()
        text += b" " * (-len(text) % 4)
        header = struct.pack("<4sIIII", b"glTF", 2, 20+len(text)+len(binary_chunk), len(text), tag)
        path.write_bytes(header+text+binary_chunk)
    return hull, propeller


def build(output, diesel=False, marine_audio=False):
    diesel = diesel or marine_audio
    output.mkdir(parents=True, exist_ok=True)
    earth = CONFIG / "earth"
    scene = json.loads((earth / "taipei-underwater-survey.vscene.json").read_text())
    coast, bed, ship, submersible = scene["objects"]
    for body in (bed, submersible):
        body["source"]["path"] = str((earth / body["source"]["path"]).resolve())
    ship["source"].update(path=str(ROOT / "models/SurveyTrawler/Trawler.obj"),
                          max_dimension=384, surface_detail="source_clipped")
    ship["velocity_mps"] = [0., 0., .32]
    coast["source"]["map"] = str(earth / "taipei-region.map.json")
    coast["source"]["sea_surface_mask"]["map"] = str(earth / "taipei-sea-level.map.json")
    scene["ocean"]["reference_surface"]["map"] = str(earth / "taipei-sea-level.map.json")
    coast["source"]["earth"].update(
        coverage={"min_x": -6400, "max_x": 6400, "min_z": -6400, "max_z": 6400},
        grid_size=321, imagery_size=1024,
    )
    if diesel:
        # Keep the full-resolution hull atlas within the shared scene budget.
        coast["source"]["earth"].update(
            coverage={"min_x": -4800, "max_x": 4800, "min_z": -4800, "max_z": 4800},
            grid_size=241, imagery_size=512,
        )
    context = json.loads((earth / "taipei-region.map.json").read_text())
    context["earth"].update(coast["source"]["earth"])
    write_json(output / "taipei-sonar-context.map.json", context)

    # World velocities put Alvin across the vertical beam once during the pass.
    # The OBJ's bow is -Z. Orient it along the crossing towards +X.
    submersible.pop("pose_track")
    submersible.update(position_m=[-5.5, -7.6, 2.], rotation_deg=[0., 90., 0.],
                       velocity_mps=[.92, 0., .32])
    submersible["source"]["model_reference"] = "Alvin DSV-2 exterior visualization based on WHOI public dimensions"

    topic = json.loads((CONFIG / "topics" / "taipei-underwater-survey.vtopic.json").read_text())
    name = "taipei-sonar-crossing"
    topic.update(name="台北近海 · Alvin 穿越船载声呐波束", paused=False, earth_map="taipei-sonar-context.map.json",
                 camera={"eye_m": [42, 13, 36], "target_m": [0, 5.7, 2]})
    topic["equipment"]["depth_sounders"][0].update(
        name="近岸测量船单波束声呐", mount_position_m=[0., -3., 0.],
        period_seconds=.32, water_column_returns=True,
    )
    if diesel:
        name = "taipei-diesel-submarine"
        hull, propeller = split_diesel_model(output)
        submersible.update(id="diesel-submarine", position_m=[-69.5, -19.3, 2.], rotation_deg=[0., 0., 0.],
                           velocity_mps=[2.65, 0., .32])
        submersible["source"] = {
            "type": "mesh", "path": hull.name,
            "units_to_metres": 1, "voxel_size_m": DIESEL_CELL_M, "up_axis": "y", "surface_detail": "source_clipped",
            "fill_interior": True, "model_reference": "Innerscene original CC0 design, 65.265 m overall",
        }
        scene["objects"].append({
            "id": "diesel-propeller", "role": "target", "physics": {"motion": "prescribed"},
            "source": {"type": "mesh", "path": propeller.name, "up_axis": "y",
                       "voxel_size_m": .04, "surface_detail": "source_clipped"},
            "rotor_drive": {"carrier": submersible["id"], "pivot_m": PROPELLER_PIVOT, "axis": [1, 0, 0],
                            "rpm": 90., "start_seconds": .3, "spin_up_seconds": 3., "water_jet": True},
        })
        # Extend the synthetic test site's depth for the taller asset.
        bed["position_m"] = [0., -23.5, 0.]
        bed["source"]["max_dimension"] = 112
        ship["source"]["max_dimension"] = 160
        scene["ocean"].update(absorption_rgb_per_m=[.065, .021, .009], scattering_rgb_per_m=[.0025, .005, .007])
        scene["ocean"]["provenance"]["optics"] = "Assumed clear-water optical coefficients for the demonstration."
        topic["name"] = "台北近海 · 65 米级柴电潜艇"
        topic["equipment"]["platforms"][0].update(object="diesel-submarine", name="65 米级柴电潜艇",
                                                   model_id="cc0-diesel-submarine")
    if marine_audio:
        name = "taipei-marine-audio"
        topic["name"] = "台北近海 · 船舷至潜艇桨后的声音航线"
        topic["equipment"]["visual_only"] = True
        topic["equipment"].pop("depth_sounders")
        scene["ocean"].update(wave_height_m=.76, swell_height_m=.24, wind_speed_mps=10.)
        ship["motion_response"] = "waves"
        ship["pose_track"] = [
            {"seconds": seconds, "position_m": [0., 0., z], "rotation_xyzw": [0, 1, 0, 0]}
            for seconds, z in ((0., -3.), (4., -8.6), (8., -18.2), (13., -34.2), (20., -58.7))
        ]
        submersible.update(position_m=[-20., -16.6, 11.5], velocity_mps=[2.15, 0., -.12])
        scene["objects"][-1]["rotor_drive"].update(start_seconds=4.4, spin_up_seconds=8.8)
    topic["scene"] = f"{name}.vscene.json"
    write_json(output / topic["scene"], scene)
    write_json(output / f"{name}.vtopic.json", topic)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--diesel", action="store_true", help="use the full-scale CC0 diesel submarine")
    parser.add_argument("--marine-audio", action="store_true", help="prepare the diesel submarine audio dive")
    args = parser.parse_args()
    build(args.output.resolve(), args.diesel, args.marine_audio)
