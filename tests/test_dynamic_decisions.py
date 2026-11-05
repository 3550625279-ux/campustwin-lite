"""Exercise real routing, frozen service forecasts and student decisions end to end."""
import csv
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run(args, success=True):
    result = subprocess.run([str(x) for x in args], capture_output=True, text=True,
                            encoding='utf-8', errors='replace')
    if (result.returncode == 0) != success:
        raise AssertionError(f'unexpected exit {result.returncode}: {args}\n{result.stdout}\n{result.stderr}')
    return result


def read(path):
    with path.open(encoding='utf-8', newline='') as stream:
        return list(csv.DictReader(stream))


def write(path, fields, rows):
    with path.open('w', encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)


def main():
    app, root = map(Path, sys.argv[1:])
    scenario = root / 'data/scenarios/dynamic_campus'
    students = root / 'data/students/dynamic_campus.csv'
    runner = root / 'tools/run_dynamic_experiments.py'
    population = read(students)
    assert len(population) == 60
    result = run([app, '--scenario', scenario, '--route', 'L_T1', 'L_FAR'])
    assert 'L_T1,L_FAR,240,L_T1|L_LIBRARY|L_FAR' in result.stdout, result.stdout
    with tempfile.TemporaryDirectory(prefix='campustwin-dynamic-') as name:
        tmp = Path(name) / '动态测试'
        tmp.mkdir()
        output = tmp / 'experiment'
        command = [sys.executable, runner, '--executable', app, '--scenario', scenario,
                   '--students', students, '--output', output]
        run(command)
        summaries = {row['policy']: row for row in read(output / 'policy_summary.csv')}
        assert set(summaries) == {'fixed', 'nearest', 'fastest'}
        assert all(int(row['students']) == 60 and int(row['decided']) == 60 for row in summaries.values())
        assert all(int(row['completed']) + int(row['not_completed']) == 60 for row in summaries.values())
        choices = {}
        all_rows = read(output / 'candidate_decisions.csv')
        for policy in ('fixed', 'nearest', 'fastest'):
            raw = output / 'runs' / policy
            assert (raw / 'input/students.csv').read_bytes() == students.read_bytes()
            for file in ('locations.csv', 'roads.csv', 'facilities.csv'):
                assert (raw / 'input/scenario' / file).read_bytes() == (scenario / file).read_bytes()
            snapshots = {(int(x['at']), x['facility_id']): x for x in read(raw / 'output/facility_snapshots.csv')}
            by_student = {}
            for row in (x for x in all_rows if x['experiment'] == policy):
                assert row['policy'] == policy
                assert int(row['observed_at']) == int(row['at']) - 1
                if int(row['observed_at']) >= 0:
                    snapshot = snapshots[(int(row['observed_at']), row['facility_id'])]
                    assert row['queue_length'] == snapshot['queue_length']
                by_student.setdefault(row['student_id'], []).append(row)
            assert len(by_student) == 60
            for sid, candidates in by_student.items():
                selected = [row for row in candidates if row['selected'] == '1']
                assert len(selected) == 1, (policy, sid, candidates)
                choice = selected[0]
                choices[(policy, sid)] = choice['facility_id']
                if policy == 'fixed':
                    assert len(candidates) == 1 and choice['reason'] == 'fixed_assignment'
                elif policy == 'nearest':
                    expected = min(candidates, key=lambda row: (float(row['distance_m']), row['facility_id']))
                    assert choice == expected and choice['reason'] == 'shortest_distance'
                else:
                    expected = min(candidates, key=lambda row: (int(row['estimated_total_minutes']), row['facility_id']))
                    assert choice == expected and choice['reason'] == 'earliest_estimated_meal'
                assert int(choice['estimated_total_minutes']) == (int(choice['travel_minutes']) +
                    int(choice['estimated_wait_minutes']) + int(choice['service_minutes']))
            for event in read(raw / 'output/events.csv'):
                assert event['facility_id'] == choices[(policy, event['student_id'])]
        assert any(choices[('nearest', p['id'])] != choices[('fastest', p['id'])] for p in population), \
            'queue-aware decisions never responded to congestion'
        fastest_rows = [r for r in all_rows if r['experiment'] == 'fastest']
        assert any(len({r['estimated_total_minutes'] for r in fastest_rows if r['student_id'] == p['id']}) == 1
                   for p in population), 'fixture should exercise deterministic ties'
        journeys = read(output / 'student_journeys.csv')
        assert len(journeys) == 180
        for row in journeys:
            if row['actual_to_eating_minutes']:
                assert int(row['prediction_error_minutes']) == (int(row['actual_to_eating_minutes']) -
                                                                 int(row['estimated_to_eating_minutes']))
        assert (output / 'report.html').is_file()
        before = (output / 'policy_summary.csv').read_bytes()
        run(command, success=False)
        assert (output / 'policy_summary.csv').read_bytes() == before, 'existing output overwritten'

        repeat = tmp / 'repeat'
        run(command[:-1] + [repeat])
        for filename in ('policy_summary.csv', 'facility_comparison.csv', 'student_journeys.csv', 'candidate_decisions.csv'):
            assert (output / filename).read_bytes() == (repeat / filename).read_bytes(), filename
        early = tmp / 'early'
        run(command[:-1] + [early, '--duration', '2'])
        for row in read(early / 'policy_summary.csv'):
            assert int(row['not_decided']) > 0 and int(row['not_completed']) > 0
            assert int(row['completed']) + int(row['not_completed']) == 60

        # Mixed policies can be configured per student; dynamic rows need no fixed destination.
        mixed = tmp / 'mixed.csv'
        fields = list(population[0]) + ['lunch_policy']
        values = []
        for person, policy in zip(population[:3], ('fixed', 'nearest', 'fastest')):
            row = dict(person, lunch_policy=policy)
            if policy != 'fixed': row['lunch_facility'] = ''
            values.append(row)
        write(mixed, fields, values)
        mixed_out = tmp / 'mixed'
        run([app, '--scenario', scenario, '--students', mixed, '--duration', '30', '--output', mixed_out])
        selected = [x for x in read(mixed_out / 'decisions.csv') if x['selected'] == '1']
        assert {x['policy'] for x in selected} == {'fixed', 'nearest', 'fastest'}
        values[2]['lunch_policy'] = 'invalid'
        write(mixed, fields, values)
        run([app, '--scenario', scenario, '--students', mixed, '--output', tmp / 'invalid'], success=False)

        # An unreachable candidate is recorded and skipped; no reachable candidate fails explicitly.
        disconnected = tmp / 'disconnected'
        shutil.copytree(scenario, disconnected)
        roads = read(disconnected / 'roads.csv')
        remaining = [r for r in roads if 'L_FAR' not in (r['from'], r['to'])]
        write(disconnected / 'roads.csv', ['from', 'to', 'distance_m'], remaining)
        unreachable_out = tmp / 'unreachable'
        run([app, '--scenario', disconnected, '--students', students, '--duration', '12',
             '--policy', 'fastest', '--output', unreachable_out])
        decisions = read(unreachable_out / 'decisions.csv')
        assert any(r['facility_id'] == 'F_FAR' and r['reason'] == 'unreachable' and r['selected'] == '0'
                   for r in decisions)
        assert all(r['facility_id'] == 'F_NEAR' for r in decisions if r['selected'] == '1')
        remaining = [r for r in remaining if 'L_NEAR' not in (r['from'], r['to'])]
        write(disconnected / 'roads.csv', ['from', 'to', 'distance_m'], remaining)
        failed = run([app, '--scenario', disconnected, '--students', students, '--duration', '12',
                      '--policy', 'fastest', '--output', tmp / 'none'], success=False)
        assert 'no eligible reachable lunch facility' in failed.stderr
        print('PASS: route, three policies, frozen forecasts, mixed input, cutoffs and reproducibility')


if __name__ == '__main__':
    main()
