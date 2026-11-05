#!/usr/bin/env python3
"""Compare dedicated versus flexible meal windows using the same population and map."""
import argparse
import csv
from pathlib import Path
import shutil
import sys

from run_lunch_experiments import call, mean, number, queue_chart, read_csv, table, write_csv

ROOT = Path(__file__).resolve().parents[1]
VARIANTS = ('dedicated', 'flexible')
LABEL = {'dedicated': '专用窗口', 'flexible': '通用窗口'}
MEALS = (('rice', 2), ('noodle', 3), ('stirfry', 5))


def render_report(destination, totals, meals, windows, duration):
    total_rows = [(LABEL[x['variant']], x['students'], x['completed'], x['unfinished'],
                   x['started'], x['mean_wait_started'] or '—', x['max_total_queue']) for x in totals]
    meal_rows = [(LABEL[x['variant']], x['meal_type'], x['demand'], x['started'], x['completed'],
                  x['mean_wait_started'] or '—') for x in meals]
    window_rows = [(LABEL[x['variant']], x['facility_id'], x['window_id'], x['started'], x['completed'],
                    x['busy_minutes'], x['utilization']) for x in windows]
    charts = ''.join(queue_chart(read_csv(destination / 'runs' / v / 'output' / 'facility_snapshots.csv'),
                                 LABEL[v], duration) for v in VARIANTS)
    html = f'''<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>食堂窗口分工实验</title>
<style>body{{font:16px/1.6 system-ui,sans-serif;background:#f4f7f8;color:#203340;max-width:1180px;margin:auto;padding:25px}}h2{{margin-top:2em}}.table-wrap{{overflow:auto;background:white}}table{{border-collapse:collapse;width:100%}}td,th{{padding:9px;border-bottom:1px solid #ddd;text-align:left;white-space:nowrap}}th{{background:#e8f1f4}}.grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:15px}}.chart{{background:white;padding:12px}}svg{{width:100%;height:auto}}</style></head><body>
<h1>食堂窗口分工：专用与通用窗口</h1><p>相同校园、学生、出发时刻、预先指定的食堂、窗口数量和每种餐品耗时；只改变各窗口能提供哪些餐品。观察时长 {duration} 分钟，所有数据均为合成数据。</p>
<p>专用：各食堂各有米饭、面食、现炒专用窗口各一个；通用：每个窗口都可提供三种餐品。每次服务分别耗时 2、3、5 分钟。单队列按到达顺序优先匹配合适窗口；可通过重新分配空闲窗口满足更多学生。</p>
<h2>整体对照</h2>{table(['方案','学生','完成服务','未完成服务','已开始服务','平均等待/分','总队列峰值'],total_rows)}
<p>平均等待仅含已开始服务者；未完成包含未出发、在途、排队和服务中的学生，请结合完成数阅读。通用窗口并不代表现实中转换餐品没有额外成本。</p>
<h2>餐品需求与等待</h2>{table(['方案','餐品','需求人数','开始服务','完成服务','平均等待/分'],meal_rows)}
<h2>各窗口负载</h2>{table(['方案','食堂','窗口','开始人数','完成人数','忙碌分钟','利用率'],window_rows)}
<h2>队列变化</h2><div class="grid">{charts}</div>
<p><a href="summary.csv">总体 CSV</a> · <a href="meal_comparison.csv">餐品 CSV</a> · <a href="window_comparison.csv">窗口 CSV</a> · <a href="student_services.csv">逐人 CSV</a></p>
<p>每组 <code>runs/方案/input</code> 保存实际输入，<code>output</code> 保存原始仿真结果。预测假定快照后无新到达；本实验为隔离窗口分工的影响，使用预先指定食堂。</p></body></html>'''
    (destination / 'report.html').write_text(html, encoding='utf-8')


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
        call([executable, '--scenario', input_scenario, '--students', base / 'input' / 'students.csv',
              '--policy', 'fixed', '--duration', args.duration, '--output', raw])
        total, meals, windows, people = analyze(variant, population, facilities, raw, args.duration)
        totals.append(total); all_meals.extend(meals); all_windows.extend(windows); all_people.extend(people)
    for filename, rows in (('summary.csv',totals),('meal_comparison.csv',all_meals),
                           ('window_comparison.csv',all_windows),('student_services.csv',all_people)):
        write_csv(output / filename, rows, list(rows[0]))
    render_report(output, totals, all_meals, all_windows, args.duration)
    print('Experiments complete. Open ' + str(output / 'report.html'))
    for item in totals:
        print(f"{item['variant']}: completed={item['completed']}/{item['students']} mean_wait={item['mean_wait_started']} max_queue={item['max_total_queue']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
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
