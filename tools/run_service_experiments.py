#!/usr/bin/env python3
"""Compare dedicated versus flexible meal windows using the same population and map."""
import argparse
import csv
from pathlib import Path
import shutil
import sys

from run_lunch_experiments import call, mean, number, read_csv, write_csv

ROOT = Path(__file__).resolve().parents[1]
VARIANTS = ('dedicated', 'flexible')
MEALS = (('rice', 2), ('noodle', 3), ('stirfry', 5))


def analyze(variant, population, facilities, raw, duration):
    students = {p['id']: p for p in population}
    by_person = {sid: {} for sid in students}
    for event in read_csv(raw / 'events.csv'):
        sid = event['student_id']
        if sid not in by_person or event['type'] in by_person[sid]:
            raise ValueError('unexpected or duplicate student event: ' + sid)
        by_person[sid][event['type']] = event
    servings = {(facility['id'], str(w), meal): minutes for facility in facilities
                for w in range(3) for meal, minutes in MEALS
                if variant == 'flexible' or MEALS[w][0] == meal}
    window_rows = []
    for facility in facilities:
        for w in range(3):
            window_rows.append({'variant': variant, 'facility_id': facility['id'], 'window_id': str(w),
                                'started': 0, 'completed': 0, 'busy_minutes': 0, 'utilization': ''})
    by_window = {(r['facility_id'], r['window_id']): r for r in window_rows}
    people = []
    for sid, student in sorted(students.items()):
        events = by_person[sid]
        arrival, start, done = (events.get(t) for t in ('StudentArrival', 'ServiceStarted', 'ServiceCompleted'))
        if done and not start or start and not arrival:
            raise ValueError('incomplete service event sequence: ' + sid)
        if any(e['facility_id'] != student['lunch_facility'] for e in events.values()):
            raise ValueError('event at wrong facility: ' + sid)
        row = {'variant': variant, 'student_id': sid, 'meal_type': student['meal_type'],
               'facility_id': student['lunch_facility'], 'arrival_at': '', 'window_id': '',
               'start_at': '', 'expected_end_at': '', 'completed_at': '', 'wait_minutes': ''}
        if arrival:
            row['arrival_at'] = arrival['at']
        if start:
            window = by_window[(start['facility_id'], start['window_id'])]
            key = (start['facility_id'], start['window_id'], student['meal_type'])
            if key not in servings: raise ValueError('window served an unsupported meal: ' + sid)
            started_at = int(start['at']); expected = started_at + servings[key]
            wait = started_at - int(arrival['at'])
            if wait != int(start['wait_minutes']): raise ValueError('incorrect wait: ' + sid)
            if done and (int(done['at']) != expected or done['window_id'] != start['window_id']):
                raise ValueError('service duration/window mismatch: ' + sid)
            if not done and expected <= duration: raise ValueError('missing completion: ' + sid)
            row.update(window_id=start['window_id'], start_at=started_at, expected_end_at=expected,
                       completed_at=done['at'] if done else '', wait_minutes=wait)
            window['started'] += 1
            window['completed'] += bool(done)
            window['busy_minutes'] += max(0, min(duration, expected) - started_at)
        people.append(row)
    for window in window_rows:
        intervals = sorted((r['start_at'], r['expected_end_at']) for r in people
                           if r['facility_id'] == window['facility_id'] and r['window_id'] == window['window_id'])
        if any(a[1] > b[0] for a,b in zip(intervals, intervals[1:])):
            raise ValueError('overlapping services at one window')
        window['utilization'] = number(window['busy_minutes'] / duration)
    meal_rows = []
    for meal, _ in MEALS:
        group = [r for r in people if r['meal_type'] == meal]
        waits = [r['wait_minutes'] for r in group if r['wait_minutes'] != '']
        meal_rows.append({'variant': variant, 'meal_type': meal, 'demand': len(group), 'started': len(waits),
                          'completed': sum(r['completed_at'] != '' for r in group),
                          'mean_wait_started': number(mean(waits))})
    summaries = read_csv(raw / 'summary.csv')
    started = sum(int(r['started']) for r in summaries)
    completed = sum(int(r['completed']) for r in summaries)
    if started != sum(r['started'] for r in window_rows) or completed != sum(r['completed'] for r in window_rows):
        raise ValueError('facility and window counters disagree')
    for facility in summaries:
        busy = sum(r['busy_minutes'] for r in window_rows if r['facility_id'] == facility['facility_id'])
        if busy != int(facility['busy_window_minutes']):
            raise ValueError('facility and window busy time disagree: ' + facility['facility_id'])
    queues = {}
    for row in read_csv(raw / 'facility_snapshots.csv'):
        queues[int(row['at'])] = queues.get(int(row['at']), 0) + int(row['queue_length'])
    total_wait = sum(int(r['total_wait_minutes']) for r in summaries)
    total = {'variant': variant, 'students': len(population), 'started': started, 'completed': completed,
             'unfinished': len(population)-completed, 'mean_wait_started': number(total_wait/started if started else None),
             'max_total_queue': max(queues.values(), default=0)}
    return total, meal_rows, window_rows, people


def run(args):
    executable, scenario, students, output = (x.resolve() for x in (args.executable, args.scenario, args.students, args.output))
    if not executable.is_file(): raise ValueError('executable not found: ' + str(executable))
    if not 1 <= args.duration <= 2**31 - 1: raise ValueError('invalid duration')
    if output.exists(): raise ValueError('output directory already exists: ' + str(output))
    if output == scenario or scenario in output.parents: raise ValueError('output must not be inside input scenario')
    population, facilities = read_csv(students), read_csv(scenario / 'facilities.csv')
    if not population or not facilities: raise ValueError('students and facilities must not be empty')
    if any(int(f['window_count']) != 3 for f in facilities): raise ValueError('experiment requires three windows per facility')
    if any(p.get('meal_type') not in dict(MEALS) for p in population): raise ValueError('each student needs rice, noodle, or stirfry')
    output.mkdir(parents=True)
    totals, all_meals, all_windows, all_people = [], [], [], []
    for variant in VARIANTS:
        base = output / 'runs' / variant
        input_scenario = base / 'input' / 'scenario'
        shutil.copytree(scenario, input_scenario)
        shutil.copyfile(students, base / 'input' / 'students.csv')
        offerings = [{'facility_id': f['id'], 'window_id': w, 'meal_type': meal, 'service_minutes': minutes}
                     for f in facilities for w in range(3) for meal, minutes in MEALS
                     if variant == 'flexible' or MEALS[w][0] == meal]
        write_csv(input_scenario / 'window_services.csv', offerings,
                  ['facility_id','window_id','meal_type','service_minutes'])
        raw = base / 'output'
        schedule = base / 'input' / 'schedule.csv'
        shutil.copyfile(args.schedule or students.parent.parent / 'schedules' / students.name, schedule)
        call([executable, "--schedule", schedule, '--scenario', input_scenario, '--students', base / 'input' / 'students.csv',
              '--policy', 'fixed', '--duration', args.duration, '--output', raw])
        total, meals, windows, people = analyze(variant, population, facilities, raw, args.duration)
        totals.append(total); all_meals.extend(meals); all_windows.extend(windows); all_people.extend(people)
    for filename, rows in (('summary.csv',totals),('meal_comparison.csv',all_meals),
                           ('window_comparison.csv',all_windows),('student_services.csv',all_people)):
        write_csv(output / filename, rows, list(rows[0]))
    for item in totals:
        print(f"{item['variant']}: completed={item['completed']}/{item['students']} mean_wait={item['mean_wait_started']} max_queue={item['max_total_queue']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--schedule", type=Path, help="Activity schedule paired with the student file")
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--scenario', type=Path, default=ROOT/'data/scenarios/service_campus')
    parser.add_argument('--students', type=Path, default=ROOT/'data/students/service_campus.csv')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--duration', type=int, default=60)
    try:
        run(parser.parse_args())
    except (OSError, ValueError, KeyError, csv.Error, RuntimeError) as error:
        print(f'experiment failed: {error}; if created, output may be incomplete', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
