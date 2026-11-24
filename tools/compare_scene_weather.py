#!/usr/bin/env python3
"""Compare paired native Earth weather renders and coupled simulation timings."""
import argparse
import json
import math
import statistics
from pathlib import Path

import numpy as np
from PIL import Image


def timing(records, key):
    values = sorted(record[key] for record in records)
    assert values and all(math.isfinite(value) and value >= 0 for value in values)
    return {
        "samples": len(values),
        "median_ms": statistics.median(values),
        "p95_ms": values[math.ceil(.95 * len(values)) - 1],
    }


def difference(a, b):
    images = []
    for path in (a, b):
        with Image.open(path) as source:
            frame = source.convert("RGBA")
            assert frame.size == (960, 540), f"Unexpected native resolution in {path}"
            assert frame.getextrema()[3] == (255, 255), f"Invalid framebuffer alpha in {path}"
            images.append(np.asarray(frame)[..., :3].astype(np.int16))
    delta = np.abs(images[0] - images[1])
    return {"mean_rgb_delta_8bit": float(delta.mean()), "p95_rgb_delta_8bit": float(np.quantile(delta, .95))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", nargs="?", default="build/weather-perf", type=Path)
    parser.add_argument("--before", default="before")
    parser.add_argument("--after", default="after")
    parser.add_argument("--output", default="comparison.json")
    parser.add_argument("--visual-only", action="store_true", help="Compare frozen physics captures and weather animation")
    args = parser.parse_args()
    root = args.root.resolve()
    before_dir, after_dir = root / args.before, root / args.after
    before, after = [json.loads((directory / "scene-weather.json").read_text())
                     for directory in (before_dir, after_dir)]
    assert before["source"] == after["source"], "Terrain source or geometry changed"
    assert before["physical_tick"] == after["physical_tick"] == 0, "Stable rendering advanced physics"
    assert before["disable_restored_legacy_environment"] and after["disable_restored_legacy_environment"]
    assert before["animation"] == after["animation"], "Animation timing changed"
    report = {"source": after["source"], "stable": {}, "transition": {}, "coupled": {}}

    after_views = {item["name"]: item for item in after["views"]}
    assert len(after_views) == len(after["views"]), "Duplicate weather view name"
    for a in before["views"]:
        b = after_views[a["name"]]
        view_keys = [key for key in ("coast_view", "land_view", "near_view") if key in a]
        for key in ("name", "utc", "eye", "focus", "weather", *view_keys):
            assert a.get(key) == b.get(key), f"Unmatched stable view {a['name']}: {key}"
        name = a["name"]
        row = {side: {"median_ms": v["render_median_ms"], "p95_ms": v["render_p95_ms"]}
               for side, v in (("before", a), ("after", b))}
        files = [name + ".png"] + [b[key]["file"] for key in view_keys]
        row["native_image_differences"] = {
            file: difference(before_dir / file, after_dir / file) for file in files
        }
        report["stable"][name] = row

    for section, key, prefix in (("celestial", "name", ""), ("sea_wind", "beaufort_force", "sea-wind-")):
        if not before.get(section):
            continue
        indexed = {row[key]: row for row in after[section]}
        assert len(indexed) == len(after[section]), f"Duplicate entries in {section}"
        report[section] = {}
        for row in before[section]:
            index = row[key]
            assert row == indexed[index], f"Unmatched {section} case {index}"
            file = f"{prefix}{index}.png"
            report[section][str(index)] = difference(before_dir / file, after_dir / file)

    a, b = [data["transition_frames"] for data in (before, after)]
    assert len(a) == len(b) == 65
    for i, (old, new) in enumerate(zip(a, b)):
        for key in ("seconds", "cover", "rain", "snow"):
            assert old[key] == new[key], f"Unmatched transition frame {i}: {key}"
    report["transition"] = {"before": timing(a, "render_ms"), "after": timing(b, "render_ms")}

    if args.visual_only:
        report.pop("coupled")
        report["visible_precipitation_pixels"] = {
            side: data["precipitation_visible_pixels"] for side, data in (("before", before), ("after", after))
        }
        report["precipitation_motion"] = {
            side: {kind: difference(directory / kind / "frame-000.png", directory / kind / "frame-047.png")
                   for kind in ("rain", "snow")}
            for side, directory in (("before", before_dir), ("after", after_dir))
        }
        if "rain_waterline" in after:
            last = after["rain_waterline"]["frames"] - 1
            report["rain_waterline"] = after["rain_waterline"] | {
                "image_motion": difference(after_dir / "rain-waterline" / "frame-000.png",
                                           after_dir / "rain-waterline" / f"frame-{last:03}.png")
            }
        (root / args.output).write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
        return

    a, b = [data["coupled_frames"] for data in (before, after)]
    assert len(a) == len(b) == 120
    for old, new in zip(a, b):
        for key in ("weather", "seconds", "simulation_interval_ms", "fixed_step_seconds", "steps", "pending_steps"):
            assert old[key] == new[key], f"Unmatched coupled workload: {key}"
        assert new["steps"] == 2 and new["pending_steps"] == 0
    report["coupled_collision_checks"] = after["coupled_collision_checks"]
    for weather in ("rain", "snow", "transition"):
        report["coupled"][weather] = {
            side: {key: timing([frame for frame in rows if frame["weather"] == weather], key)
                   for key in ("simulation_ms", "water_ms", "buoyancy_ms", "render_ms", "complete_wall_ms")}
            for side, rows in (("before", a), ("after", b))
        }
    (root / args.output).write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")


if __name__ == "__main__":
    main()
