#!/usr/bin/env python3
"""End-to-end stage-two contract and controlled-experiment checks."""
import csv
import filecmp
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

exe, root = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
scenario = root / 'data/scenarios/service_campus'
students = root / 'data/students/service_campus.csv'


def run(args, success=True):
    p = subprocess.run([str(x) for x in args], cwd=root, text=True, capture_output=True)
    if (p.returncode == 0) != success:
        raise AssertionError(f'command returned {p.returncode}: {args}\n{p.stdout}\n{p.stderr}')
    return p


def rows(path):
    with path.open(encoding='utf-8', newline='') as stream:
        return list(csv.DictReader(stream))


def require(value, message):
    if not value: raise AssertionError(message)


with tempfile.TemporaryDirectory(prefix='campus_service_test_') as td:
    tmp = Path(td)
    output = tmp / 'experiment'
    command = [sys.executable, root/'tools/run_service_experiments.py', '--executable', exe, '--output', output]
    run(command)
    summary = rows(output/'summary.csv')
    require([x['variant'] for x in summary] == ['dedicated', 'flexible'], 'both experiment variants')
    require(all(x['students'] == '60' for x in summary), 'population count')
    require((output/'report.html').is_file(), 'report missing')
    require(len(rows(output/'window_comparison.csv')) == 18, '3 facilities x 3 windows x 2 variants')
    require(len(rows(output/'student_services.csv')) == 120, 'one record per student and variant')
    for row in summary:
        require(int(row['completed']) + int(row['unfinished']) == 60, 'completion totals')
    a = output/'runs/dedicated/input'; b = output/'runs/flexible/input'
    require(filecmp.cmp(a/'students.csv', b/'students.csv', shallow=False), 'student inputs differ')
    for name in ('locations.csv', 'roads.csv', 'facilities.csv'):
        require(filecmp.cmp(a/'scenario'/name, b/'scenario'/name, shallow=False), 'uncontrolled input: '+name)
    require(len(rows(a/'scenario/window_services.csv')) == 9, 'dedicated offering count')
    require(len(rows(b/'scenario/window_services.csv')) == 27, 'flexible offering count')
    run(command, success=False)

    # Dynamic policy must retain the new window config when copying inputs.
    dynamic = tmp/'dynamic'
    run([sys.executable, root/'tools/run_dynamic_experiments.py', '--executable', exe,
         '--scenario', scenario, '--students', students, '--output', dynamic])
    for policy in ('fixed','nearest','fastest'):
        copied = dynamic/'runs'/policy/'input/scenario/window_services.csv'
        require(filecmp.cmp(copied,scenario/'window_services.csv',shallow=False), 'lost heterogeneous windows')
    fastest = rows(dynamic/'runs/fastest/output/decisions.csv')
    require(any(r['selected']=='1' and r['service_minutes']=='5' for r in fastest), 'meal-specific forecast missing')

    # Near canteen no longer serves noodles; nearest must reject that candidate.
    changed = tmp/'no_noodle'
    shutil.copytree(scenario, changed)
    path = changed/'window_services.csv'
    content = path.read_text(encoding='utf-8').replace('F_NEAR,1,noodle,3','F_NEAR,1,rice,2')
    path.write_text(content,encoding='utf-8')
    nearest_out = tmp/'nearest'
    run([exe,'--scenario',changed,'--students',students,'--policy','nearest','--duration','60','--output',nearest_out])
    decisions = rows(nearest_out/'decisions.csv')
    people = {r['id']:r for r in rows(students)}
    unavailable = [r for r in decisions if people[r['student_id']]['meal_type']=='noodle' and r['facility_id']=='F_NEAR']
    require(unavailable and all(r['reason']=='meal_unavailable' and r['selected']=='0' for r in unavailable),
            'unavailable meal must be excluded')
    # Fixed assignment must fail before it could enqueue an impossible request.
    failed = run([exe,'--scenario',changed,'--students',students,'--policy','fixed','--duration','60',
                  '--output',tmp/'fixed_bad'], success=False)
    require('cannot serve requested meal' in failed.stderr, 'incompatible fixed destination error')

    # Invalid window rows are rejected while loading, including route-only use.
    with path.open('a',encoding='utf-8') as stream: stream.write('F_NEAR,0,rice,2\n')
    bad = run([exe,'--scenario',changed,'--route','L_T1','L_NEAR'], success=False)
    require('duplicate' in bad.stderr, 'duplicate offering should fail')
    # Old window-count helper must refuse heterogeneous inputs, not drop them.
    old = run([sys.executable,root/'tools/run_lunch_experiments.py','--executable',exe,
               '--scenario',scenario,'--students',students,'--output',tmp/'legacy'], success=False)
    require('homogeneous' in old.stderr, 'old experiment must reject incompatible inputs')

print('complex service integration passed')
