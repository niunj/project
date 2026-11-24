#!/usr/bin/env python3
"""Check actual Earth captures; never generate or substitute rendered imagery."""
import argparse
import json
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default="build/cloud-weather")
    args = parser.parse_args()
    root = Path(args.root).resolve()

    def read(name):
        return json.loads((root / name).read_text())

    before = read("before/object-cloud-acceptance.json")
    after = read("accepted/object-cloud-acceptance.json")
    fog = read("fog/object-cloud-acceptance.json")
    wind_before = read("wind-before/flight-acceptance.json")
    wind_after = read("wind-after/flight-acceptance.json")
    assert before["source"] == after["source"] == fog["source"]
    assert before["object_count"] == after["object_count"] == fog["object_count"]
    assert not after["surface"]["source_coarse"]
    # default.map mixes Taipei fine coverage with global fallback. Preserve its
    # existing provenance marker; a renderer test must not relabel source truth.
    assert after["surface"]["provenance_assumed"] == before["surface"]["provenance_assumed"]
    assert 29 < after["surface"]["dem_spacing_m"] < 32 and after["surface"]["imagery_spacing_m"] == 10
    for key in ("dem_spacing_m", "imagery_spacing_m", "surface_revision"):
        assert before["surface"][key] == after["surface"][key] == fog["surface"][key]
    assert after["utc"]["year"] == 2026 and after["utc"]["month"] == 9
    assert after["utc"]["day"] == 26 and after["utc"]["hours_utc"] == 4
    assert after["utc"] == fog["utc"]
    assert all(after["temporal_contract"].values()) and all(fog["temporal_contract"].values())
    assert after["fog_visibility_m"] == 0 and fog["fog_visibility_m"] == 18000
    views = {}
    for name in ("overview", "cloud-tops", "side"):
        a, b = before["frame_benchmark"][name], after["frame_benchmark"][name]
        assert a["eye_local_m"] == b["eye_local_m"] and a["focus_local_m"] == b["focus_local_m"]
        assert a["frames"] == b["frames"] == 15
        assert b["temporal_history_eligible"] and b["temporal_raw_width"] == 320
        assert b["temporal_output_width"] == 640
        for key in ("width", "height"):
            assert before["captures"][name][key] == after["captures"][name][key]
        views[name] = dict(before_median_ms=a["median_ms"], after_median_ms=b["median_ms"],
                           before_p90_ms=a["p90_ms"], after_p90_ms=b["p90_ms"],
                           median_reduction_percent=100 * (1 - b["median_ms"] / a["median_ms"]))
    assert len(wind_before["frames"]) == len(wind_after["frames"]) == 52
    density_error = 0
    for a, b in zip(wind_before["frames"], wind_after["frames"]):
        for key in ("eye", "focus", "seconds", "phase"):
            assert a[key] == b[key], "wind comparison changed the reference route or time"
        density_error = max(density_error, abs(a["eye_condensate"] - b["eye_condensate"]))
    assert density_error == 0, "physical cloud density changed"
    wind = {}
    for name in ("approach", "wind_hold", "cross"):
        a, b = wind_before["frame_benchmark"][name], wind_after["frame_benchmark"][name]
        assert a["frames"] == b["frames"]
        wind[name] = dict(before_median_ms=a["median_ms"], after_median_ms=b["median_ms"],
                          before_p90_ms=a["p90_ms"], after_p90_ms=b["p90_ms"])
    hd_before = read("hd-before/object-cloud-acceptance.json")
    hd_after = read("hd-after/object-cloud-acceptance.json")
    assert hd_before["source"] == hd_after["source"] == after["source"]
    assert hd_before["object_count"] == hd_after["object_count"] == after["object_count"]
    assert hd_after["utc"] == after["utc"]
    assert all(hd_after["temporal_contract"].values())
    hd_views = {}
    for name in views:
        a, b = hd_before["frame_benchmark"][name], hd_after["frame_benchmark"][name]
        assert a["eye_local_m"] == b["eye_local_m"] and a["focus_local_m"] == b["focus_local_m"]
        assert a["frames"] == b["frames"] == 15
        assert b["temporal_history_eligible"] and b["temporal_raw_width"] == 675
        assert b["temporal_output_width"] == 1349
        for captures in (hd_before["captures"], hd_after["captures"]):
            assert captures[name]["width"] == 1349 and captures[name]["height"] == 743
        hd_views[name] = dict(before_median_ms=a["median_ms"], after_median_ms=b["median_ms"],
                             before_p90_ms=a["p90_ms"], after_p90_ms=b["p90_ms"])
    overcast_before = read("overcast-before/overcast.json")
    overcast_after = read("overcast-after/overcast.json")
    for key in ("source", "objects", "blocked_vertical_columns", "mean_vertical_transmission",
                "cloud_base_m", "cloud_top_m", "wind_speed_mps"):
        assert overcast_before[key] == overcast_after[key], "overcast physical configuration changed"
    assert set(overcast_before["captures"]) == set(overcast_after["captures"])
    for key in ("year", "month", "day", "hours_utc"):
        assert overcast_after["utc"][key] == after["utc"][key]
    paused = read("cloud-pause/designer.json")
    assert paused["cloud_clock_seconds"] == paused["sky_clock_seconds"] == paused["tick"] == 0
    assert Path(paused["saved"]).is_file()
    try:
        gpu = subprocess.check_output(["nvidia-smi", "--query-gpu=name,driver_version,memory.total",
                                       "--format=csv,noheader"], text=True, timeout=5).strip()
    except (OSError, subprocess.SubprocessError):
        gpu = "not queried"
    report = dict(
        scope="actual native Vulkan Earth program; stable rendering and weather update only; excludes screenshot readback, destruction updates and full simulation stepping",
        gpu=gpu, output_resolution=[640, 360], cloud_sampling_resolution=[320, 180],
        field_architecture="existing single-cloud objects; density, random seeds, wind and voxel precision unchanged",
        utc=dict(year=2026, month=9, day=26, hours_utc=4),
        baseline_utc_provenance="fixed configuration in the archived pre-change Earth test executable",
        source=after["source"], surface=after["surface"], cloud_objects=after["object_count"],
        views=views, wind=wind, wind_reference_route_equal=True,
        larger_view=dict(output_resolution=[1349, 743], cloud_sampling_resolution=[675, 372],
                         views=hd_views, realtime_achieved=False),
        overcast=dict(captures=list(overcast_after["captures"]), objects=overcast_after["objects"],
                      physical_configuration_equal=True,
                      mean_vertical_transmission=overcast_after["mean_vertical_transmission"]),
        paused_designer=dict(cloud_clock_seconds=paused["cloud_clock_seconds"],
                             sky_clock_seconds=paused["sky_clock_seconds"], tick=paused["tick"],
                             saved_scene=paused["saved"]),
        wind_density_max_absolute_error=density_error,
        thin_cloud_frames=wind_after["thin_cloud_frames"],
        temporal_contract=after["temporal_contract"], fog_visibility_m=fog["fog_visibility_m"],
        limitations=["bounded three-order scattering approximation; not an exact radiative-transfer solution",
                     "1349x743 measured views remain slower than real time; no 1080p/4K real-time claim",
                     "formal equipment still has its existing clear-sky land restriction",
                     "ocean hydrodynamics, wave precision and underwater spectral transport were not upgraded"],
    )
    (root / "comparison.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
