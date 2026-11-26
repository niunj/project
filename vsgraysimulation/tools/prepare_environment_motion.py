#!/usr/bin/env python3
"""Prepare matched Taipei wave and gust motion topics for native Earth capture."""
import argparse
import json
from pathlib import Path

from prepare_sonar_crossing import CONFIG, ROOT, write_json


def track(start, velocity, rotation=(0, 0, 0, 1)):
    return [
        {"seconds": seconds,
         "position_m": [p + v * seconds for p, v in zip(start, velocity)],
         "rotation_xyzw": rotation}
        for seconds in (0., 12.)
    ]


def build(output):
    output.mkdir(parents=True, exist_ok=True)
    earth = CONFIG / "earth"
    scene = json.loads((earth / "taipei-underwater-survey.vscene.json").read_text())
    coast, _, vessel, _ = scene["objects"]
    coast["source"]["map"] = str(earth / "taipei-region.map.json")
    coast["source"]["earth"].update(
        coverage={"min_x": -6400, "max_x": 6400, "min_z": -6400, "max_z": 6400},
        grid_size=321, imagery_size=1024,
    )
    coast["source"]["sea_surface_mask"]["map"] = str(earth / "taipei-sea-level.map.json")
    scene["ocean"]["reference_surface"]["map"] = str(earth / "taipei-sea-level.map.json")
    scene["ocean"].update(wave_height_m=.88, swell_height_m=.32)
    scene["weather"] = {"initial": {"preset": "fair_clouds", "wind_speed_mps": 14., "wind_azimuth_deg": 70.}}
    vessel["source"].update(path=str(ROOT / "models/SurveyTrawler/Trawler.obj"),
                            max_dimension=224, surface_detail="source_clipped")
    vessel["pose_track"] = track([0., 0., 2.], [0., 0., -3.4], (0, 1, 0, 0))
    aircraft = {
        "id": "aircraft", "role": "target", "physics": {"motion": "prescribed"},
        "source": {"type": "mesh", "path": str(ROOT / "models/F-16/F-16.obj"),
                   "up_axis": "y", "max_dimension": 192, "surface_detail": "source_clipped"},
        "pose_track": track([-540., 230., -500.], [0., 0., 114.]),
    }
    for subject, response, target in (("ship", "waves", vessel), ("aircraft", "gusts", aircraft)):
        for enabled in (False, True):
            name = f"{subject}-{'response' if enabled else 'baseline'}"
            platform = dict(target)
            if enabled:
                platform["motion_response"] = response
            write_json(output / f"{name}.vscene.json", dict(scene, objects=[coast, platform]))
            write_json(output / f"{name}.vtopic.json", {
                "format": "earth-scene-topic-v1", "name": name, "paused": False,
                "scene": f"{name}.vscene.json", "earth_map": str(earth / "taipei-region.map.json"),
                "camera": {"eye_m": [35., 14., 29.], "target_m": [0., 4., 2.]},
                "equipment": {"format": "voxel-equipment-v1", "visual_only": True, "equipment": []},
            })


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    build(parser.parse_args().output.resolve())
