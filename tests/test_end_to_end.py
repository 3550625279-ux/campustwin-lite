"""Run the real A/B/C modules and check outcomes, variants and failure paths."""
import csv
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from check_mvp_outputs import check


def run(args, expected_success=True):
    result = subprocess.run([str(arg) for arg in args], capture_output=True,
                            text=True, encoding="utf-8", errors="replace")
    if (result.returncode == 0) != expected_success:
        raise AssertionError(f"unexpected exit {result.returncode}: {args}\n{result.stdout}\n{result.stderr}")
    return result


def rows(path):
    with path.open(encoding="utf-8", newline="") as file:
        return list(csv.DictReader(file))


def write_rows(path, data):
    with path.open("w", encoding="utf-8", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=data[0].keys(), lineterminator="\n")
        writer.writeheader()
        writer.writerows(data)


def main():
    app, demo, demo_b, root = map(Path, sys.argv[1:])
    scenario = root / "data/scenarios/mvp"
    students = root / "data/students/mvp.csv"
    fixtures = root / "data/fixtures"
    with tempfile.TemporaryDirectory(prefix="campustwin-integration-") as directory:
        temporary = Path(directory) / "校园测试"
        temporary.mkdir()
        unicode_students = temporary / "学生.csv"
        shutil.copyfile(students, unicode_students)
        result = run([demo_b, "--students", unicode_students, "--all", "--wait", 3, "--duration", 20])
        assert "SUMMARY students=20 arrivals=20 finished=20" in result.stdout

        def simulation(destination, *, scenario_path=scenario, students_path=students, duration=120,
                       expected_success=True):
            return run([app, "--scenario", scenario_path, "--students", students_path,
                        "--duration", duration, "--output", destination], expected_success)

        first, second = temporary / "run1", temporary / "run2"
        simulation(first)
        check(first)  # Every event, every snapshot, and the full canonical summary.
        simulation(second)
        filenames = ("events.csv", "student_snapshots.csv", "facility_snapshots.csv", "summary.csv")
        for name in filenames:
            assert (first / name).read_bytes() == (second / name).read_bytes(), f"nondeterministic {name}"
        before = {name: (first / name).read_bytes() for name in filenames}
        simulation(first, expected_success=False)
        assert all((first / name).read_bytes() == before[name] for name in filenames), "overwrote results"

        one_window = temporary / "one-window-scenario"
        shutil.copytree(scenario, one_window)
        data = rows(one_window / "facilities.csv")
        data[0]["window_count"] = "1"
        write_rows(one_window / "facilities.csv", data)
        out = temporary / "one-window"
        simulation(out, scenario_path=one_window)
        summary = rows(out / "summary.csv")[0]
        assert (summary["arrived"], summary["started"], summary["completed"]) == ("20", "20", "20")
        assert (summary["max_queue_length"], summary["total_wait_minutes"],
                summary["busy_window_minutes"]) == ("19", "370", "40"), summary

        longer_roads = temporary / "longer-roads-scenario"
        shutil.copytree(scenario, longer_roads)
        data = rows(longer_roads / "roads.csv")
        for road in data:
            road["distance_m"] = str(float(road["distance_m"]) * 2)
        write_rows(longer_roads / "roads.csv", data)
        out = temporary / "longer-roads"
        simulation(out, scenario_path=longer_roads)
        arrivals = {row["student_id"]: int(row["at"]) for row in rows(out / "events.csv")
                    if row["type"] == "StudentArrival"}
        assert arrivals["S001"] == 4 and arrivals["S011"] == 6, arrivals

        out = temporary / "cutoff"
        simulation(out, duration=3)
        summary = rows(out / "summary.csv")[0]
        assert (summary["arrived"], summary["started"], summary["completed"]) == ("20", "2", "0"), summary
        final = [row for row in rows(out / "student_snapshots.csv") if row["at"] == "3"]
        assert sum(row["state"] == "Queued" for row in final) == 18
        assert sum(row["state"] == "InService" for row in final) == 2

        bad_students = temporary / "bad-students.csv"
        data = rows(students)
        data[0]["lunch_facility"] = "MISSING"
        write_rows(bad_students, data)
        bad_output = temporary / "bad-output"
        simulation(bad_output, students_path=bad_students, expected_success=False)
        assert not bad_output.exists(), "invalid inputs should be rejected before output begins"
        excessive_windows = temporary / "excessive-windows"
        shutil.copytree(scenario, excessive_windows)
        data = rows(excessive_windows / "facilities.csv")
        data[0]["window_count"] = "100001"
        write_rows(excessive_windows / "facilities.csv", data)
        result = simulation(temporary / "bad-window-output", scenario_path=excessive_windows,
                            expected_success=False)
        assert "window_count" in result.stderr and "100000" in result.stderr
        disconnected = temporary / "disconnected"
        shutil.copytree(scenario, disconnected)
        data = [row for row in rows(disconnected / "roads.csv")
                if "L_CANTEEN" not in (row["from"], row["to"])]
        write_rows(disconnected / "roads.csv", data)
        result = simulation(temporary / "failed-run", scenario_path=disconnected, expected_success=False)
        assert "output may be incomplete" in result.stderr, result.stderr
        for args in (["--duration", "0"], ["--duration", "abc"], ["--duration", "2147483648"],
                     ["--unknown", "value"], ["--output"], ["--duration", "1", "--duration", "2"]):
            run([app, *args], expected_success=False)

        result = run([demo, "--facilities", fixtures / "resource_facilities.csv",
                      "--arrivals", fixtures / "resource_arrivals.csv", "--duration", 6])
        expected = (fixtures / "resource_expected_events.csv").read_text(encoding="utf-8")
        assert result.stdout.splitlines() == expected.splitlines(), "C demo differs from hand calculation"
        assert "tick=2 facility=F_QUEUE_TEST queue=1 in_service=1" in result.stderr
        assert "summary facility=F_QUEUE_TEST arrived=3 started=3 completed=3 max_queue=1 total_wait=4" in result.stderr
        run([demo, "--facilities", fixtures / "resource_facilities.csv",
             "--arrivals", fixtures / "resource_arrivals.csv", "--duration", 1], expected_success=False)
        run([demo, "--arrivals", temporary / "missing.csv"], expected_success=False)
    print("PASS: real A/B/C integration, variants, reproducibility and errors")


if __name__ == "__main__":
    main()
