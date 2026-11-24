#!/usr/bin/env python3
"""Compare fixed, nearest, and queue-aware lunch decisions with identical inputs."""
import argparse
import csv
from pathlib import Path
import shutil
import sys

from run_lunch_experiments import call, mean, number, read_csv, write_csv

ROOT = Path(__file__).resolve().parents[1]
POLICIES = ('fixed', 'nearest', 'fastest')


def make_journeys(policy, population, output, duration):
    choices = {}
    candidates = read_csv(output / 'decisions.csv')
    for row in candidates:
        if row['selected'] == '1':
            if row['student_id'] in choices:
                raise ValueError('multiple selected destinations for ' + row['student_id'])
            choices[row['student_id']] = row
    events = {}
    for row in read_csv(output / 'events.csv'):
        person = events.setdefault(row['student_id'], {})
        if row['type'] in person:
            raise ValueError('duplicate event for ' + row['student_id'])
        person[row['type']] = row
    final = {row['student_id']: row['state'] for row in read_csv(output / 'student_snapshots.csv')
             if int(row['at']) == duration}
    if len(final) != len(population):
        raise ValueError('missing final student snapshots')
    journeys = []
    for person in sorted(population, key=lambda row: row['id']):
        sid, departure = person['id'], int(person['departure_minute'])
        chosen, ev = choices.get(sid), events.get(sid, {})
        if (departure <= duration) != (chosen is not None):
            raise ValueError('missing or early decision for ' + sid)
        if ev and not chosen:
            raise ValueError('events without a decision for ' + sid)
        if chosen and any(e['facility_id'] != chosen['facility_id'] for e in ev.values()):
            raise ValueError('events disagree with chosen facility for ' + sid)
        arrived, started, completed = (ev.get(name) for name in
                                       ('StudentArrival', 'ServiceStarted', 'ServiceCompleted'))
        predicted = int(chosen['estimated_total_minutes']) if chosen and chosen['estimated_total_minutes'] else None
        actual = int(completed['at']) - departure if completed else None
        journeys.append({
            'policy': policy, 'student_id': sid, 'origin': person['initial_location'],
            'configured_facility': person['lunch_facility'], 'chosen_facility': chosen['facility_id'] if chosen else '',
            'departure_minute': departure, 'decision_snapshot_at': chosen['observed_at'] if chosen else '',
            'queue_at_snapshot': chosen['queue_length'] if chosen else '',
            'distance_m': chosen['distance_m'] if chosen else '', 'route_path': chosen['route_path'] if chosen else '',
            'estimated_travel_minutes': chosen['travel_minutes'] if chosen else '',
            'estimated_wait_minutes': chosen['estimated_wait_minutes'] if chosen else '',
            'estimated_to_eating_minutes': predicted if predicted is not None else '',
            'arrival_minute': arrived['at'] if arrived else '',
            'service_start_minute': started['at'] if started else '',
            'actual_wait_minutes': started['wait_minutes'] if started else '',
            'eating_start_minute': completed['at'] if completed else '',
            'actual_to_eating_minutes': actual if actual is not None else '',
            'prediction_error_minutes': actual - predicted if actual is not None and predicted is not None else '',
            'choice_reason': chosen['reason'] if chosen else '', 'final_state': final[sid],
        })
    return journeys, candidates


def summarize(policy, journeys, output):
    rows = read_csv(output / 'summary.csv')
    facilities = []
    for row in rows:
        people = [p for p in journeys if p['chosen_facility'] == row['facility_id']]
        completed = [int(p['actual_to_eating_minutes']) for p in people if p['actual_to_eating_minutes'] != '']
        started, arrived, done = (int(row[x]) for x in ('started', 'arrived', 'completed'))
        facilities.append({
            'policy': policy, 'facility_id': row['facility_id'], 'window_count': row['window_count'],
            'chosen': len(people), 'arrived': arrived, 'started': started, 'completed': done,
            'not_completed': len(people) - done, 'waiting': arrived - started,
            'in_service': started - done, 'max_queue_length': row['max_queue_length'],
            'mean_wait_started': row['mean_wait_minutes'] if started else '',
            'mean_to_eating_completed': number(mean(completed)), 'utilization': row['utilization'],
        })
    completed_people = [int(p['actual_to_eating_minutes']) for p in journeys if p['actual_to_eating_minutes'] != '']
    errors = [int(p['prediction_error_minutes']) for p in journeys if p['prediction_error_minutes'] != '']
    total_started = sum(int(x['started']) for x in rows)
    total_completed = sum(int(x['completed']) for x in rows)
    total_wait = sum(int(x['total_wait_minutes']) for x in rows)
    queues_by_time = {}
    for snapshot in read_csv(output / 'facility_snapshots.csv'):
        queues_by_time.setdefault(int(snapshot['at']), 0)
        queues_by_time[int(snapshot['at'])] += int(snapshot['queue_length'])
    overall = {
        'policy': policy, 'students': len(journeys), 'decided': sum(bool(p['chosen_facility']) for p in journeys),
        'not_decided': sum(not p['chosen_facility'] for p in journeys),
        'arrived': sum(int(x['arrived']) for x in rows), 'started': total_started,
        'completed': total_completed, 'not_completed': len(journeys) - total_completed,
        'max_total_queue': max(queues_by_time.values(), default=0),
        'mean_wait_started': number(total_wait / total_started if total_started else None),
        'mean_to_eating_completed': number(mean(completed_people)),
        'mean_prediction_error_completed': number(mean(errors)),
    }
    return overall, facilities


def run(args):
    executable, scenario, students, output = (p.resolve() for p in
                                               (args.executable, args.scenario, args.students, args.output))
    if not executable.is_file():
        raise ValueError('executable not found: ' + str(executable))
    if not 1 <= args.duration <= 2**31 - 1:
        raise ValueError('duration must be a positive 32-bit integer')
    if output.exists():
        raise ValueError('output directory already exists: ' + str(output))
    if (scenario / 'seats.csv').exists() and read_csv(scenario / 'seats.csv'):
        raise ValueError('seat-constrained scenarios require run_schedule_experiments.py')
    if output == scenario or scenario in output.parents:
        raise ValueError('output must not be inside the input scenario')
    population = read_csv(students)
    if not population:
        raise ValueError('student population is empty')
    output.mkdir(parents=True, exist_ok=False)
    all_journeys, all_candidates, overall, facilities = [], [], [], []
    for policy in POLICIES:
        base = output / 'runs' / policy
        input_scenario = base / 'input' / 'scenario'
        input_scenario.mkdir(parents=True)
        for name in ('locations.csv', 'roads.csv', 'facilities.csv'):
            shutil.copyfile(scenario / name, input_scenario / name)
        if (scenario / 'window_services.csv').exists():
            shutil.copyfile(scenario / 'window_services.csv', input_scenario / 'window_services.csv')
        input_students = base / 'input' / 'students.csv'
        shutil.copyfile(students, input_students)
        raw = base / 'output'
        schedule = input_students.with_name('schedule.csv')
        shutil.copyfile(args.schedule or students.parent.parent / 'schedules' / students.name, schedule)
        call([executable, "--schedule", schedule, '--scenario', input_scenario, '--students', input_students, '--policy', policy,
              '--duration', args.duration, '--output', raw])
        journeys, candidates = make_journeys(policy, population, raw, args.duration)
        one_overall, one_facilities = summarize(policy, journeys, raw)
        overall.append(one_overall)
        facilities.extend(one_facilities)
        all_journeys.extend(journeys)
        all_candidates.extend({'experiment': policy, **row} for row in candidates)
    write_csv(output / 'policy_summary.csv', overall, list(overall[0]))
    write_csv(output / 'facility_comparison.csv', facilities, list(facilities[0]))
    write_csv(output / 'student_journeys.csv', all_journeys, list(all_journeys[0]))
    candidate_fields = ['experiment', 'at', 'student_id', 'policy', 'facility_id', 'destination', 'observed_at',
                        'queue_length', 'distance_m', 'route_path', 'travel_minutes', 'estimated_wait_minutes',
                        'service_minutes', 'estimated_total_minutes', 'selected', 'reason']
    write_csv(output / 'candidate_decisions.csv', all_candidates, candidate_fields)
    for item in overall:
        print(f"{item['policy']}: completed={item['completed']}/{item['students']} "
              f"mean_wait={item['mean_wait_started'] or 'N/A'} not_completed={item['not_completed']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--schedule", type=Path, help="Activity schedule paired with the student file")
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--scenario', type=Path, default=ROOT / 'data/scenarios/dynamic_campus')
    parser.add_argument('--students', type=Path, default=ROOT / 'data/students/dynamic_campus.csv')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--duration', type=int, default=60)
    args = parser.parse_args()
    try:
        run(args)
    except (OSError, ValueError, KeyError, csv.Error, RuntimeError) as error:
        print(f'experiment failed: {error}; if created, output may be incomplete', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
