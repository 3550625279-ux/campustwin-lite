"""Exercise route queries, two canteens and the controlled experiment workflow."""
import csv
import math
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def read(path):
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream))


def command(args, success=True):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    assert (result.returncode == 0) == success, (args, result.stdout, result.stderr)
    return result


def main():
    app, root = map(Path, sys.argv[1:])
    scenario = root / "data/scenarios/two_canteens"
    students = root / "data/students/two_canteens.csv"
    runner = root / "tools/run_lunch_experiments.py"
    route = command([app, "--scenario", scenario, "--route", "L_T1", "L_FAR"])
    assert list(csv.DictReader(route.stdout.splitlines())) == [
        {"from": "L_T1", "to": "L_FAR", "distance_m": "240", "path": "L_T1|L_LIBRARY|L_FAR"}]
    command([app, "--scenario", scenario, "--route", "L_T1"], success=False)
    command([app, "--scenario", scenario, "--route", "L_T1", "NO_SUCH_LOCATION"], success=False)
    command([app, "--scenario", scenario, "--route", "L_T1", "L_FAR", "--duration", "2"], success=False)

    with tempfile.TemporaryDirectory(prefix="campustwin-lunch-") as temp:
        root_out = Path(temp) / "双食堂测试"
        root_out.mkdir()
        args = [sys.executable, runner, "--executable", app]
        output = root_out / "first"
        command([*args, "--output", output])
        comparison = read(output / "comparison.csv")
        rows = {(r["experiment"], r["facility_id"]): r for r in comparison}
        assert len(rows) == 6
        assert (rows[("baseline", "F_NEAR")]["assigned"], rows[("baseline", "F_FAR")]["assigned"]) == ("20", "20")
        for r in comparison:
            assert (r["arrived"], r["started"], r["completed"], r["waiting"], r["in_service"]) == ("20", "20", "20", "0", "0")
            assert int(r["assigned"]) == sum(int(r[key]) for key in ("not_arrived", "waiting", "in_service", "completed"))
            expected_util = int(r["busy_window_minutes"]) / (int(r["window_count"]) * int(r["observation_minutes"]))
            assert math.isclose(float(r["utilization"]), expected_util, abs_tol=1e-6)
        assert (rows[("baseline", "F_NEAR")]["window_count"], rows[("more_windows", "F_NEAR")]["window_count"]) == ("2", "3")
        assert (rows[("baseline", "F_NEAR")]["total_wait_minutes"], rows[("more_windows", "F_NEAR")]["total_wait_minutes"]) == ("160", "94")
        assert float(rows[("baseline", "F_NEAR")]["mean_departure_to_eating_minutes_completed"]) > float(rows[("baseline", "F_FAR")]["mean_departure_to_eating_minutes_completed"])
        assert float(rows[("faster_walk", "F_FAR")]["mean_travel_minutes_arrived"]) < float(rows[("baseline", "F_FAR")]["mean_travel_minutes_arrived"])
        assert rows[("faster_walk", "F_FAR")]["total_wait_minutes"] != rows[("baseline", "F_FAR")]["total_wait_minutes"]

        baseline = output / "runs/baseline/input"
        windows = output / "runs/more_windows/input"
        faster = output / "runs/faster_walk/input"
        assert (baseline / "students.csv").read_bytes() == (windows / "students.csv").read_bytes() == students.read_bytes()
        for name in ("locations.csv", "roads.csv"):
            assert (baseline / "scenario" / name).read_bytes() == (windows / "scenario" / name).read_bytes() == (faster / "scenario" / name).read_bytes()
        assert (baseline / "scenario/facilities.csv").read_bytes() == (faster / "scenario/facilities.csv").read_bytes()
        f1, f2 = read(baseline / "scenario/facilities.csv"), read(windows / "scenario/facilities.csv")
        for left, right in zip(f1, f2):
            if left["id"] == "F_NEAR":
                assert {**left, "window_count": "3"} == right
            else:
                assert left == right
        for left, right in zip(read(baseline / "students.csv"), read(faster / "students.csv")):
            assert float(right["walk_m_per_min"]) == 2 * float(left["walk_m_per_min"])
            assert {**left, "walk_m_per_min": right["walk_m_per_min"]} == right
        baseline_events = read(output / "runs/baseline/output/events.csv")
        window_events = read(output / "runs/more_windows/output/events.csv")
        assert [e for e in baseline_events if e["type"] == "StudentArrival"] == [e for e in window_events if e["type"] == "StudentArrival"]
        assert [e for e in baseline_events if e["facility_id"] == "F_FAR"] == [e for e in window_events if e["facility_id"] == "F_FAR"]

        journeys = read(output / "student_journeys.csv")
        assert len(journeys) == 3 * 40
        for row in journeys:
            travel = int(row["arrival_minute"]) - int(row["departure_minute"])
            assert travel == int(row["travel_minutes"]) == math.ceil(float(row["route_distance_m"]) / float(row["walk_m_per_min"]))
            assert int(row["service_start_minute"]) - int(row["arrival_minute"]) == int(row["wait_minutes"])
            assert int(row["eating_start_minute"]) - int(row["service_start_minute"]) == 2
            assert int(row["departure_to_eating_minutes"]) == int(row["eating_start_minute"]) - int(row["departure_minute"])
            assert row["final_state"] == "Finished"
        near_t1 = [r for r in journeys if r["experiment"] == "baseline" and r["origin"] == "L_T1" and r["facility_id"] == "F_NEAR"]
        far_t1 = [r for r in journeys if r["experiment"] == "baseline" and r["origin"] == "L_T1" and r["facility_id"] == "F_FAR"]
        assert len(near_t1) == len(far_t1) == 12
        assert sum(int(r["departure_to_eating_minutes"]) for r in near_t1) / 12 == 9
        assert sum(int(r["departure_to_eating_minutes"]) for r in far_t1) / 12 == 8

        report = (output / "report.html").read_text(encoding="utf-8")
        assert "控制出发条件后的比较" in report and report.count("<svg ") == 3
        assert "截断较早时" in report and "指定" in report
        original = (output / "comparison.csv").read_bytes()
        command([*args, "--output", output], success=False)
        assert (output / "comparison.csv").read_bytes() == original
        second = root_out / "second"
        command([*args, "--output", second])
        for path in output.rglob("*"):
            if path.is_file():
                assert path.read_bytes() == (second / path.relative_to(output)).read_bytes(), path

        cutoff = root_out / "cutoff"
        command([*args, "--output", cutoff, "--duration", "1"])
        cutoff_rows = read(cutoff / "comparison.csv")
        for row in cutoff_rows:
            assert row["completed"] == "0"
            assert int(row["assigned"]) == sum(int(row[key]) for key in ("not_arrived", "waiting", "in_service", "completed"))
            if row["started"] == "0":
                assert row["mean_wait_minutes_started"] == ""
            assert row["mean_departure_to_eating_minutes_completed"] == ""
        quick_near = next(r for r in cutoff_rows if r["experiment"] == "faster_walk" and r["facility_id"] == "F_NEAR")
        assert (quick_near["arrived"], quick_near["not_arrived"], quick_near["waiting"], quick_near["in_service"], quick_near["not_completed"]) == ("12", "8", "10", "2", "20")
        assert (cutoff / "report.html").exists()
        invalid = root_out / "invalid"
        command([*args, "--output", invalid, "--target-facility", "MISSING"], success=False)
        assert not invalid.exists()
        broken = root_out / "disconnected"
        shutil.copytree(scenario, broken)
        road_path = broken / "roads.csv"
        road_path.write_text("from,to,distance_m\nL_T1,L_NEAR,120\n", encoding="utf-8")
        command([app, "--scenario", broken, "--route", "L_T1", "L_FAR"], success=False)
    print("PASS: route queries, multi-canteen runs, controls, cutoff and reproducibility")


if __name__ == "__main__":
    main()
