#!/usr/bin/env python3
"""Run reproducible lunch experiments against the real CampusTwin executable.

Only Python's standard library is required. The copied inputs, original output
and derived CSV statistics are kept together so that every number can be audited.
"""
import argparse
import csv
import io
import math
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CASES = ("baseline", "more_windows", "faster_walk")


def read_csv(path):
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream))


def write_csv(path, rows, fields):
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def call(command):
    result = subprocess.run([str(x) for x in command], capture_output=True,
                            text=True, encoding="utf-8", errors="replace")
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}): {' '.join(map(str, command))}\n{result.stderr.strip()}")
    return result.stdout


def get_routes(executable, scenario, population, facilities):
    destinations = {row["id"]: row["location_id"] for row in facilities}
    pairs = sorted({(row["initial_location"], row["lunch_facility"]) for row in population})
    routes = {}
    for origin, facility in pairs:
        if facility not in destinations:
            raise ValueError(f"unknown lunch facility {facility}")
        output = call([executable, "--scenario", scenario, "--route", origin, destinations[facility]])
        values = list(csv.DictReader(io.StringIO(output)))
        if len(values) != 1 or values[0]["from"] != origin or values[0]["to"] != destinations[facility]:
            raise ValueError("unexpected output from route query")
        routes[(origin, facility)] = values[0]
    return routes


def prepare_case(destination, scenario, students, case, target, window_count, speed_multiplier):
    inputs = destination / "input"
    scenario_copy = inputs / "scenario"
    scenario_copy.mkdir(parents=True)
    for name in ("locations.csv", "roads.csv", "facilities.csv"):
        shutil.copyfile(scenario / name, scenario_copy / name)
    shutil.copyfile(students, inputs / "students.csv")

    if case == "more_windows":
        path = scenario_copy / "facilities.csv"
        facilities = read_csv(path)
        matches = [row for row in facilities if row["id"] == target]
        if len(matches) != 1:
            raise ValueError(f"target facility not found exactly once: {target}")
        matches[0]["window_count"] = str(window_count)
        write_csv(path, facilities, ["id", "location_id", "window_count", "service_minutes"])
    if case == "faster_walk":
        path = inputs / "students.csv"
        population = read_csv(path)
        for row in population:
            speed = float(row["walk_m_per_min"]) * speed_multiplier
            if not math.isfinite(speed) or speed <= 0:
                raise ValueError("speed multiplier produces an invalid speed")
            row["walk_m_per_min"] = format(speed, ".15g")
        with path.open(encoding="utf-8", newline="") as stream:
            fields = csv.DictReader(stream).fieldnames
        write_csv(path, population, fields)
    return scenario_copy, inputs / "students.csv"


def derive_journeys(case, population, output, routes, duration):
    events_by_student = {}
    for event in read_csv(output / "events.csv"):
        student_events = events_by_student.setdefault(event["student_id"], {})
        if event["type"] in student_events:
            raise ValueError(f"duplicate {event['type']} for {event['student_id']}")
        student_events[event["type"]] = event
    final_states = {}
    for snapshot in read_csv(output / "student_snapshots.csv"):
        if int(snapshot["at"]) == duration:
            final_states[snapshot["student_id"]] = snapshot["state"]
    rows = []
    for student in sorted(population, key=lambda row: row["id"]):
        events = events_by_student.get(student["id"], {})
        arrival = events.get("StudentArrival")
        start = events.get("ServiceStarted")
        completed = events.get("ServiceCompleted")
        route = routes[(student["initial_location"], student["lunch_facility"])]
        departure = int(student["departure_minute"])
        rows.append({
            "experiment": case, "student_id": student["id"],
            "origin": student["initial_location"], "facility_id": student["lunch_facility"],
            "departure_minute": departure, "walk_m_per_min": student["walk_m_per_min"],
            "route_distance_m": route["distance_m"], "route_path": route["path"],
            "arrival_minute": arrival["at"] if arrival else "",
            "travel_minutes": int(arrival["at"]) - departure if arrival else "",
            "service_start_minute": start["at"] if start else "",
            "wait_minutes": start["wait_minutes"] if start else "",
            "eating_start_minute": completed["at"] if completed else "",
            "departure_to_eating_minutes": int(completed["at"]) - departure if completed else "",
            "final_state": final_states[student["id"]],
        })
    return rows


def mean(values):
    return sum(values) / len(values) if values else None


def number(value):
    return "" if value is None else f"{value:.6f}"


def summarize(case, output, journeys):
    summaries = []
    for row in read_csv(output / "summary.csv"):
        facility = row["facility_id"]
        people = [person for person in journeys if person["facility_id"] == facility]
        arrivals = [int(p["travel_minutes"]) for p in people if p["travel_minutes"] != ""]
        eating = [int(p["departure_to_eating_minutes"]) for p in people if p["departure_to_eating_minutes"] != ""]
        started, completed, arrived = int(row["started"]), int(row["completed"]), int(row["arrived"])
        summaries.append({
            "experiment": case, "facility_id": facility,
            "assigned": len(people), "window_count": row["window_count"],
            "observation_minutes": row["observation_minutes"], "arrived": arrived,
            "started": started, "completed": completed,
            "not_arrived": len(people) - arrived, "not_completed": len(people) - completed,
            "waiting": arrived - started,
            "in_service": started - completed, "max_queue_length": row["max_queue_length"],
            "total_wait_minutes": row["total_wait_minutes"],
            "mean_wait_minutes_started": row["mean_wait_minutes"] if started else "",
            "busy_window_minutes": row["busy_window_minutes"], "utilization": row["utilization"],
            "mean_travel_minutes_arrived": number(mean(arrivals)),
            "mean_departure_to_eating_minutes_completed": number(mean(eating)),
            "completed_for_eating_mean": len(eating),
        })
    return summaries


def run(args):
    executable = args.executable.resolve()
    scenario, students = args.scenario.resolve(), args.students.resolve()
    output = args.output.resolve()
    if not executable.is_file():
        raise ValueError(f"executable not found: {executable}")
    if args.duration < 1 or args.duration > 2**31 - 1:
        raise ValueError("duration must be a positive 32-bit integer")
    if args.window_count < 1 or args.window_count > 100000:
        raise ValueError("window count must be between 1 and 100000")
    if not math.isfinite(args.speed_multiplier) or args.speed_multiplier <= 0:
        raise ValueError("speed multiplier must be finite and positive")
    if output.exists():
        raise ValueError(f"output directory already exists: {output}; choose a new directory")
    if (scenario / "seats.csv").exists() and read_csv(scenario / "seats.csv"):
        raise ValueError("seat-constrained scenarios require run_schedule_experiments.py")
    if (scenario / "window_services.csv").exists() and read_csv(scenario / "window_services.csv"):
        raise ValueError("window-count experiment requires homogeneous windows; use run_service_experiments.py")
    population, facilities = read_csv(students), read_csv(scenario / "facilities.csv")
    matches = [row for row in facilities if row["id"] == args.target_facility]
    if len(matches) != 1:
        raise ValueError(f"target facility not found exactly once: {args.target_facility}")
    if int(matches[0]["window_count"]) == args.window_count:
        raise ValueError("the comparison window count must differ from the baseline")
    if args.speed_multiplier == 1:
        raise ValueError("the comparison speed multiplier must differ from 1")
    routes = get_routes(executable, scenario, population, facilities)
    output.mkdir(parents=True, exist_ok=False)
    summaries, journeys = [], []
    for case in CASES:
        destination = output / "runs" / case
        case_scenario, case_students = prepare_case(destination, scenario, students, case,
                                                    args.target_facility, args.window_count,
                                                    args.speed_multiplier)
        raw_output = destination / "output"
        schedule = case_students.with_name('schedule.csv')
        shutil.copyfile(args.schedule or students.parent.parent / 'schedules' / students.name, schedule)
        call([executable, "--schedule", schedule, "--scenario", case_scenario, "--students", case_students,
              "--duration", args.duration, "--policy", "fixed", "--output", raw_output])
        case_journeys = derive_journeys(case, read_csv(case_students), raw_output, routes, args.duration)
        journeys.extend(case_journeys)
        summaries.extend(summarize(case, raw_output, case_journeys))
    comparison_fields = ["experiment", "facility_id", "assigned", "window_count", "observation_minutes",
                         "arrived", "started", "completed", "not_arrived", "waiting", "in_service", "not_completed", "max_queue_length",
                         "total_wait_minutes", "mean_wait_minutes_started", "busy_window_minutes", "utilization",
                         "mean_travel_minutes_arrived", "mean_departure_to_eating_minutes_completed", "completed_for_eating_mean"]
    journey_fields = ["experiment", "student_id", "origin", "facility_id", "departure_minute", "walk_m_per_min",
                      "route_distance_m", "route_path", "arrival_minute", "travel_minutes", "service_start_minute",
                      "wait_minutes", "eating_start_minute", "departure_to_eating_minutes", "final_state"]
    route_rows = [{"origin": origin, "facility_id": facility, "destination": route["to"],
                   "distance_m": route["distance_m"], "path": route["path"]}
                  for (origin, facility), route in sorted(routes.items())]
    write_csv(output / "comparison.csv", summaries, comparison_fields)
    write_csv(output / "student_journeys.csv", journeys, journey_fields)
    write_csv(output / "routes.csv", route_rows, ["origin", "facility_id", "destination", "distance_m", "path"])
    for row in summaries:
        print(f"{row['experiment']}: {row['facility_id']} arrived={row['arrived']} completed={row['completed']} "
              f"waiting={row['waiting']} mean_wait={row['mean_wait_minutes_started'] or 'N/A'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--schedule", type=Path, help="Activity schedule paired with the student file")
    parser.add_argument("--executable", type=Path, required=True, help="path to the built campustwin executable")
    parser.add_argument("--scenario", type=Path, default=ROOT / "data/scenarios/two_canteens")
    parser.add_argument("--students", type=Path, default=ROOT / "data/students/two_canteens.csv")
    parser.add_argument("--output", type=Path, required=True, help="new output directory; never overwritten")
    parser.add_argument("--duration", type=int, default=60)
    parser.add_argument("--target-facility", default="F_NEAR")
    parser.add_argument("--window-count", type=int, default=3)
    parser.add_argument("--speed-multiplier", type=float, default=2.0)
    args = parser.parse_args()
    try:
        run(args)
    except (OSError, ValueError, KeyError, csv.Error, RuntimeError) as error:
        print(f"experiment failed: {error}; if created, output may be incomplete", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
