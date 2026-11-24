#!/usr/bin/env python3
"""Compare dining seats, study seats and an alternate road map with identical schedules."""
import argparse
import csv
from pathlib import Path
import shutil
import sys

from run_lunch_experiments import call, number, read_csv, write_csv

ROOT = Path(__file__).resolve().parents[1]
VARIANTS = ("baseline", "dining_seats", "study_seats", "road_detour")
CLOSED = {frozenset(pair) for pair in (("L_T1", "L_NEAR"), ("L_LAB", "L_WEST"), ("L_LIBRARY_E", "L_FAR"))}


def analyze(variant, raw, duration, population):
    activities = read_csv(raw / "activities.csv")
    snapshots = read_csv(raw / "student_snapshots.csv")
    seats = read_csv(raw / "seat_snapshots.csv")
    final = [r for r in snapshots if int(r["at"]) == duration]
    if len(final) != population:
        raise ValueError("missing final student snapshots")
    started_classes = [r for r in activities if r["kind"] == "class" and r["started_at"]]
    total_classes = sum(r["kind"] == "class" for r in activities)
    late_classes = [r for r in started_classes if int(r["start_delay_minutes"]) > 0]
    final_seats = [r for r in seats if int(r["at"]) == duration]
    grants = [r for r in read_csv(raw / "seat_events.csv") if r["type"] == "SeatGranted"]
    seat_rows = []
    for s in final_seats:
        location = s["location_id"]
        seen = [g for g in grants if g["location_id"] == location]
        busy = sum(min(duration, int(g["until"])) - int(g["at"]) for g in seen)
        if busy != int(s["busy_seat_minutes"]) or len(seen) != int(s["granted"]):
            raise ValueError("seat grants and snapshots disagree: " + location)
        seat_rows.append({"variant": variant, "location_id": location, "capacity": s["capacity"],
                          "granted": s["granted"], "waiting_at_end": s["queued"], "occupied_at_end": s["occupied"],
                          "total_wait_granted": s["total_wait_minutes"], "busy_minutes": busy,
                          "utilization": number(busy / (int(s["capacity"]) * duration))})
    finished = sum(r["state"] == "Finished" for r in final)
    total = {"variant": variant, "students": population, "finished": finished, "unfinished": population - finished,
             "activities": len(activities), "activities_completed": sum(bool(r["completed_at"]) for r in activities),
             "classes_started": len(started_classes), "classes_not_started": total_classes - len(started_classes),
             "late_classes": len(late_classes),
             "class_delay_minutes": sum(int(r["start_delay_minutes"]) for r in started_classes),
             "seat_wait_observed": sum(int(r["seat_wait_minutes"]) for r in activities),
             "distance_m_departed": number(sum(float(r["distance_m"]) for r in activities)),
             "last_completion_at": max((int(r["completed_at"]) for r in activities if r["completed_at"]), default="")}
    return total, seat_rows, activities


def run(args):
    executable, output = args.executable.resolve(), args.output.resolve()
    if not executable.is_file():
        raise ValueError("executable not found")
    if not 1 <= args.duration <= 2**31 - 1:
        raise ValueError("invalid duration")
    if output.exists():
        raise ValueError("output directory already exists")
    inputs = ROOT / "data"
    if inputs == output or inputs in output.parents:
        raise ValueError("output must not be inside input data")
    output.mkdir(parents=True)
    totals, seat_rows, all_activities = [], [], []
    for variant in VARIANTS:
        base = output / "runs" / variant
        scene = base / "input" / "scenario"
        shutil.copytree(inputs / "scenarios/campus_day", scene)
        shutil.copyfile(inputs / "students/campus_day.csv", base / "input/students.csv")
        shutil.copyfile(inputs / "schedules/campus_day.csv", base / "input/schedules.csv")
        if variant in ("dining_seats", "study_seats"):
            seats = read_csv(scene / "seats.csv")
            kinds = {r["id"]: r["kind"] for r in read_csv(scene / "locations.csv")}
            for row in seats:
                if kinds[row["location_id"]] == ("Canteen" if variant == "dining_seats" else "Library"):
                    row["capacity"] = str(2 * int(row["capacity"]))
            write_csv(scene / "seats.csv", seats, ["location_id", "capacity"])
        if variant == "road_detour":
            roads = read_csv(scene / "roads.csv")
            roads = [r for r in roads if frozenset((r["from"], r["to"])) not in CLOSED]
            write_csv(scene / "roads.csv", roads, ["from", "to", "distance_m"])
        raw = base / "output"
        call([executable, "--scenario", scene, "--students", base / "input/students.csv", "--schedule",
              base / "input/schedules.csv", "--duration", args.duration, "--output", raw])
        total, seats, activities = analyze(variant, raw, args.duration, 36)
        totals.append(total); seat_rows.extend(seats)
        all_activities.extend(dict(variant=variant, **r) for r in activities)
    for name, rows in (("summary.csv", totals), ("seat_comparison.csv", seat_rows), ("activities.csv", all_activities)):
        write_csv(output / name, rows, list(rows[0]))
    for r in totals:
        print(f"{r['variant']}: finished={r['finished']}/36 late_classes={r['late_classes']} seat_wait={r['seat_wait_observed']}")
    print(f"CSV results: {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--duration", type=int, default=160)
    try:
        run(parser.parse_args())
    except (OSError, ValueError, KeyError, csv.Error, RuntimeError) as error:
        print(f"schedule experiment failed: {error}; output may be incomplete", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
