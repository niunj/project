#!/usr/bin/env python3
"""Run the two Taipei projects against the actual headless service and sensor GPU."""
import argparse
import base64
from concurrent.futures import ThreadPoolExecutor
import json
import math
import os
from pathlib import Path
import subprocess
import time
from urllib.error import HTTPError
from urllib.request import Request, urlopen
from uuid import uuid4


class Client:
    def __init__(self, origin):
        self.origin = origin + "/api/v1"
        self.id = ""
        self.leases = []
        self.id = self.request("/sessions", {})["client_id"]

    def request(self, path, body=None, *, method=None, lease=None, expected=200, key=None, through=None):
        headers = {"X-Client-Id": self.id}
        if lease:
            headers["X-Lease-Id"] = lease
        if through is not None:
            headers["X-Through-Tick"] = str(through)
        if body is not None:
            headers.update({"Content-Type": "application/json", "Idempotency-Key": key or str(uuid4())})
        request = Request(self.origin + path, None if body is None else json.dumps(body).encode(),
                          headers, method=method or ("POST" if body is not None else "GET"))
        try:
            with urlopen(request, timeout=5) as response:
                actual, raw = response.status, response.read()
        except HTTPError as err:
            actual, raw = err.code, err.read()
        if expected == 200 and body is not None and path == "/sessions":
            expected = 201
        assert actual == expected, (path, actual, raw[:600])
        return json.loads(raw) if raw else None

    def lease(self, run, kind, **fields):
        lease = self.request(f"/runs/{run['run_id']}/leases",
                             {"kind": kind, "expected_generation_id": run["generation_id"], **fields},
                             expected=201)["lease_id"]
        self.leases.append(lease)
        return lease

    def renew(self):
        if not self.leases:
            self.request("/capabilities")
        for lease in self.leases:
            self.request(f"/leases/{lease}/renew", {})

    def release(self, lease):
        self.request(f"/leases/{lease}", method="DELETE", expected=204)
        self.leases.remove(lease)

    def close(self):
        self.request(f"/sessions/{self.id}", method="DELETE", expected=204)


def wait(check, *, timeout=90, tick=None):
    deadline = time.monotonic() + timeout
    next_renew = time.monotonic() + 3
    while time.monotonic() < deadline:
        if value := check():
            return value
        if tick and time.monotonic() >= next_renew:
            tick()
            next_renew = time.monotonic() + 3
        time.sleep(.15)
    raise TimeoutError(f"Timed out waiting for {check.__name__}")


def validate(url, *, recover=False):
    supervisor, aircraft, ship, viewer = (Client(url) for _ in range(4))

    def start(project):
        data = {"project_id": project, "published_revision_id": "initial"}
        key = str(uuid4())
        run = supervisor.request("/runs", data, expected=202, key=key)
        assert supervisor.request("/runs", data, expected=202, key=key) == run
        return run

    with ThreadPoolExecutor(2) as pool:
        coast, land = pool.map(start, ("taipei-coast", "taipei-land"))

    def status(run):
        return supervisor.request("/runs/" + run["run_id"])

    def await_job(job):
        def check_job():
            state = supervisor.request("/jobs/" + job["job_id"])
            assert state["state"] != "failed", state
            return state if state["state"] == "succeeded" else None
        return wait(check_job, tick=lambda: [c.renew() for c in (supervisor, aircraft, ship, viewer)])

    await_job(coast)
    await_job(land)
    initial_coast, initial_land = status(coast), status(land)
    assert initial_coast["pid"] != initial_land["pid"]
    assert initial_coast["state"] == initial_land["state"] == "paused"

    coast_clock = supervisor.lease(coast, "run_control")
    land_clock = supervisor.lease(land, "run_control")
    land_world = supervisor.lease(land, "world")
    plane_lease = aircraft.lease(coast, "platform_control", platform_id=801)
    ship_lease = ship.lease(coast, "platform_control", platform_id=854)
    ship.request(f"/runs/{coast['run_id']}/leases", {
        "kind": "platform_control", "platform_id": 801, "expected_generation_id": coast["generation_id"]
    }, expected=409)

    def clock(run, lease, operation, **fields):
        return await_job(supervisor.request(f"/runs/{run['run_id']}/commands", {
            "expected_generation_id": run["generation_id"], "control_lease_id": lease,
            "command": operation, **fields
        }, expected=202))

    def course(client, platform, lease, heading, speed, expected=202):
        job = client.request(f"/runs/{coast['run_id']}/platforms/{platform}/commands", {
            "expected_generation_id": coast["generation_id"], "control_lease_id": lease,
            "command_id": "set_prescribed_course", "arguments": {"heading_deg": heading, "speed_mps": speed}
        }, expected=expected)
        if expected == 202:
            return await_job(job)
        return job

    course(ship, 801, ship_lease, 90, 10, expected=403)
    with ThreadPoolExecutor(2) as pool:
        plane_command = pool.submit(course, aircraft, 801, plane_lease, 45, 12)
        ship_command = pool.submit(course, ship, 854, ship_lease, 290, 0.8)
        assert plane_command.result()["applied_at"]["state_revision"] != ship_command.result()["applied_at"]["state_revision"]

    clock(coast, coast_clock, "play")
    waiting = wait(lambda: (s if (s := status(coast))["state"] == "waiting_for_leases" else None))
    assert waiting["tick"] == initial_coast["tick"]
    nav = aircraft.lease(coast, "equipment_data", equipment_id=811)
    sonar = ship.lease(coast, "equipment_data", equipment_id=855)
    redundant = viewer.lease(coast, "equipment_data", equipment_id=855)
    visible = viewer.lease(land, "equipment_data", equipment_id=7101)
    infrared = viewer.lease(land, "equipment_data", equipment_id=7102)
    clock(land, land_clock, "play")

    def renew():
        for client in (supervisor, aircraft, ship, viewer):
            client.renew()

    def running():
        coast_now, land_now = status(coast), status(land)
        if min(coast_now["tick"], land_now["tick"]) >= 22:
            return coast_now, land_now

    coast_live, land_live = wait(running, tick=renew)
    assert coast_live["running"] and land_live["running"]
    platform_by_id = {p["platform_id"]: p for p in coast_live["platforms"]}
    for platform, heading, speed in [(801, 45, 12), (854, 290, .8)]:
        telemetry = platform_by_id[platform]
        assert abs(math.remainder(telemetry["yaw"] - heading, 360)) < .02, telemetry
        assert abs(telemetry["speed"] - speed) < 1e-3, telemetry

    def product(client, run, unit, lease, min_time=0.0):
        path = f"/runs/{run['run_id']}/equipment/{unit}/preview"

        def received():
            try:
                value = client.request(path, lease=lease)
            except AssertionError as err:
                if err.args[0][1] != 404:
                    raise
                return None
            assert value["run_id"] == run["run_id"] and value["generation_id"] == run["generation_id"]
            assert value["equipment_id"] == unit
            return value if value["sim_time_s"] >= min_time else None
        return wait(received, tick=renew)

    fix = product(aircraft, coast, 811, nav, .2)
    depth = product(ship, coast, 855, sonar, .2)
    visual = product(viewer, land, 7101, visible, .2)
    thermal = product(viewer, land, 7102, infrared, .2)
    assert fix["platform_id"] == 801 and fix["kind"] == "navigation" and 25 < fix["navigation"]["latitude_deg"] < 26
    assert depth["platform_id"] == 854 and depth["bottom"] == 0 and depth["water_depth_m"] > 0 and depth["two_way_ms"] > 0
    ship.request(f"/runs/{coast['run_id']}/equipment/855/preview", lease=sonar, through=0, expected=404)
    for frame, kind in [(visual, "visible"), (thermal, "infrared")]:
        assert frame["platform_id"] == 71 and frame["kind"] == kind
        image = frame["image"]
        pixels = base64.b64decode(image["pixels_base64"], validate=True)
        assert len(pixels) == image["width"] * image["height"] and min(pixels) < max(pixels)
        image.pop("pixels_base64")
        image["display_range"] = [min(pixels), max(pixels)]

    viewer.release(redundant)
    depth_after = product(ship, coast, 855, sonar, depth["sim_time_s"] + .4)
    viewer.request(f"/runs/{coast['run_id']}/equipment/855/preview", lease=visible, expected=403)
    clock(coast, coast_clock, "pause")
    paused, land_tick = status(coast)["tick"], status(land)["tick"]
    wait(lambda: status(land)["tick"] > land_tick + 5, tick=renew)
    assert status(coast)["tick"] == paused
    stale_step = supervisor.request(f"/runs/{coast['run_id']}/commands", {
        "expected_generation_id": coast["generation_id"], "control_lease_id": coast_clock,
        "command": "step", "expected_tick": str(paused - 1)
    }, expected=202)

    def bad_step():
        value = supervisor.request("/jobs/" + stale_step["job_id"])
        assert value["state"] != "succeeded", value
        return value["state"] == "failed"

    wait(bad_step)
    single = clock(coast, coast_clock, "step", expected_tick=str(paused))
    assert int(single["applied_at"]["tick"]) == paused + 1 and status(coast)["tick"] == paused + 1
    land_measured = status(land)
    recovered = None
    if recover:
        # Sacrifice only the fresh run created by this test. The other run stays active.
        before_fault = status(coast)
        os.kill(before_fault["pid"], 9)
        wait(lambda: status(coast)["state"] == "fault")
        for client in (aircraft, ship):
            client.close()
        new = clock(coast, coast_clock, "recover")
        coast = {"run_id": coast["run_id"], "generation_id": new["generation_id"]}
        supervisor.leases.remove(coast_clock)
        recovered = status(coast)
        assert recovered["recovered"] and recovered["state"] == "paused"
        assert recovered["generation_id"] != before_fault["generation_id"] and recovered["tick"] == paused + 1
        assert status(land)["running"]
    else:
        for client in (aircraft, ship):
            client.close()
    clock(land, land_clock, "stop")
    if not recover:
        clock(coast, coast_clock, "stop")
    viewer.close()
    supervisor.close()
    return {
        "coast": {k: coast_live[k] for k in ("run_id", "generation_id", "pid", "load_ms", "tick", "simulation_cycle_p95_ms")},
        "land": {k: land_measured[k] for k in ("run_id", "generation_id", "pid", "load_ms", "tick", "simulation_cycle_p95_ms", "measurement_batches", "measurement_roundtrips")},
        "platforms": {str(k): {field: p[field] for field in ("name", "yaw", "speed", "position")}
                      for k, p in platform_by_id.items()},
        "products": [fix, depth, visual, thermal],
        "depth_frame_after_other_viewer_left": depth_after["frame_id"],
        "single_step_tick": paused + 1,
        "recovered": {k: recovered[k] for k in ("pid", "generation_id", "tick", "state")} if recovered else None,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", default="build")
    parser.add_argument("--output", default="build/service-validation/api")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    destination = (root / args.output).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    executable = root / args.build / "service/vsgraysim_sim_service"
    with (destination / "backend.log").open("w") as log:
        with subprocess.Popen([executable, "--port", "0"], cwd=root, text=True,
                              stderr=log, stdout=subprocess.PIPE) as service:
            try:
                address = service.stdout.readline().strip()
                assert address.startswith("LISTENING http://127.0.0.1:"), address
                report = validate(address.split()[-1], recover=True)
                (destination / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
                print(json.dumps(report, ensure_ascii=False, indent=2))
            finally:
                service.terminate()
                try:
                    service.wait(timeout=6)
                except subprocess.TimeoutExpired:
                    service.kill()


if __name__ == "__main__":
    main()
