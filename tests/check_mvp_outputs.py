"""Check actual outputs for the canonical 20-student, 120-minute scenario only."""
import csv
import math
from pathlib import Path
import sys

from validate_sample_data import read_rows, require, validate


EVENT_COLUMNS = "at,type,request_id,student_id,facility_id,window_id,wait_minutes"
STUDENT_COLUMNS = "at,student_id,state,location_id,from,to,depart_at,arrive_at,distance_m"
FACILITY_COLUMNS = "at,facility_id,queue_length,in_service,arrived,started,completed,max_queue_length,total_wait_minutes,busy_window_minutes"
SUMMARY_COLUMNS = "facility_id,window_count,observation_minutes,arrived,started,completed,max_queue_length,total_wait_minutes,mean_wait_minutes,busy_window_minutes,utilization"


def close(value, expected, label):
    numeric = float(value)
    require(math.isfinite(numeric) and abs(numeric - expected) <= 1e-6, f"{label}: got {value}, expected {expected}")


def check(output):
    validate(Path(__file__).resolve().parents[1])
    events = read_rows(output / "events.csv", EVENT_COLUMNS)
    snapshots = read_rows(output / "student_snapshots.csv", STUDENT_COLUMNS)
    facilities = read_rows(output / "facility_snapshots.csv", FACILITY_COLUMNS)
    summaries = read_rows(output / "summary.csv", SUMMARY_COLUMNS)
    require(len(events) == 60, "expected 60 events")
    require(len(snapshots) == 2420, "expected 2420 student snapshots")
    require(len(facilities) == 121, "expected 121 facility snapshots")
    require(len(summaries) == 1, "expected one facility summary")

    # This is the algebraic schedule derived in docs/07_ACCEPTANCE.md, not a simulator.
    schedule = {}
    expected_events = []
    for i in range(20):
        student = f"S{i + 1:03}"
        arrival = 2 if i < 10 else 3
        start = 2 + 2 * (i // 2)
        end = start + 2
        schedule[student] = (arrival, start, end)
        base = dict(request_id=f"{student}:lunch:1", student_id=student, facility_id="F_CANTEEN")
        expected_events += [dict(base, at=str(arrival), type="StudentArrival", window_id="", wait_minutes=""), dict(base, at=str(start), type="ServiceStarted", window_id=str(i % 2), wait_minutes=str(start - arrival)), dict(base, at=str(end), type="ServiceCompleted", window_id=str(i % 2), wait_minutes="")]
    phase = {"ServiceCompleted": 0, "StudentArrival": 1, "ServiceStarted": 2}

    def event_key(row):
        tie = row["student_id"] if row["type"] == "StudentArrival" else int(row["window_id"])
        return (int(row["at"]), phase[row["type"]], row["facility_id"], tie)

    expected_events.sort(key=event_key)
    require(events == expected_events, "events differ from canonical order, times, requests, windows or waiting times")

    for index, row in enumerate(snapshots):
        t, number = divmod(index, 20)
        student = f"S{number + 1:03}"
        require(row["at"] == str(t) and row["student_id"] == student, f"student snapshot order at row {index + 2}")
        arrival, start, end = schedule[student]
        if t < arrival:
            state, location = "Moving", ""
            movement = ("L_T1" if number < 10 else "L_T2", "L_CANTEEN", 0, arrival, 120 if number < 10 else 180)
        elif t < start:
            state, location, movement = "Queued", "L_CANTEEN", None
        elif t < end:
            state, location, movement = "InService", "L_CANTEEN", None
        elif t < end + 5:
            state, location, movement = "Eating", "L_CANTEEN", None
        elif t < end + 7:
            state, location = "Moving", ""
            movement = ("L_CANTEEN", "L_DORM", end + 5, end + 7, 120)
        else:
            state, location, movement = "Finished", "L_DORM", None
        require(row["state"] == state and row["location_id"] == location, f"state/location mismatch: t={t} {student}")
        if movement:
            a, b, departure, due, distance = movement
            require((row["from"], row["to"], row["depart_at"], row["arrive_at"]) == (a, b, str(departure), str(due)), f"movement mismatch: t={t} {student}")
            close(row["distance_m"], distance, f"distance: t={t} {student}")
        else:
            require(all(row[key] == "" for key in ["from", "to", "depart_at", "arrive_at", "distance_m"]), f"unexpected movement fields: t={t} {student}")

    max_queue = 0
    for t, row in enumerate(facilities):
        require(row["at"] == str(t) and row["facility_id"] == "F_CANTEEN", f"facility snapshot order: t={t}")
        arrived = sum(arrival <= t for arrival, _, _ in schedule.values())
        started = sum(start <= t for _, start, _ in schedule.values())
        completed = sum(end <= t for _, _, end in schedule.values())
        queue = arrived - started
        max_queue = max(max_queue, queue)
        total_wait = sum(start - arrival for arrival, start, _ in schedule.values() if start <= t)
        busy = sum(max(0, min(t, end) - start) for _, start, end in schedule.values())
        expected = dict(queue_length=queue, in_service=started - completed, arrived=arrived, started=started, completed=completed, max_queue_length=max_queue, total_wait_minutes=total_wait, busy_window_minutes=busy)
        for key, value in expected.items():
            require(row[key] == str(value), f"facility {key}: t={t}, got {row[key]}, expected {value}")

    summary = summaries[0]
    expected = dict(facility_id="F_CANTEEN", window_count="2", observation_minutes="120", arrived="20", started="20", completed="20", max_queue_length="18", total_wait_minutes="170", busy_window_minutes="40")
    for key, value in expected.items():
        require(summary[key] == value, f"summary {key}: got {summary[key]}, expected {value}")
    close(summary["mean_wait_minutes"], 8.5, "mean waiting time")
    close(summary["utilization"], 1 / 6, "utilization")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python3 tests/check_mvp_outputs.py OUTPUT_DIRECTORY", file=sys.stderr)
        sys.exit(2)
    try:
        check(Path(sys.argv[1]))
        print("PASS: actual output matches the canonical 20-student MVP scenario.")
    except (OSError, ValueError, KeyError, csv.Error) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
