#!/usr/bin/env python3
"""Compare fixed, nearest, and queue-aware lunch decisions with identical inputs."""
import argparse
import csv
from pathlib import Path
import shutil
import sys

from run_lunch_experiments import call, mean, number, queue_chart, read_csv, table, write_csv

ROOT = Path(__file__).resolve().parents[1]
POLICIES = ('fixed', 'nearest', 'fastest')
LABELS = {'fixed': '预先指定', 'nearest': '最近食堂', 'fastest': '预计最快'}


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


def render_report(output, overall, facilities, journeys, duration):
    overall_rows = [(LABELS[x['policy']], x['students'], x['decided'], x['arrived'], x['completed'],
                     x['not_completed'], x['max_total_queue'], x['mean_wait_started'] or '—',
                     x['mean_to_eating_completed'] or '—', x['mean_prediction_error_completed'] or '—')
                    for x in overall]
    facility_rows = [(LABELS[x['policy']], x['facility_id'], x['window_count'], x['chosen'], x['arrived'],
                      x['completed'], x['not_completed'], x['waiting'], x['max_queue_length'],
                      x['mean_wait_started'] or '—', x['mean_to_eating_completed'] or '—') for x in facilities]
    example_rows = [(LABELS[x['policy']], x['student_id'], x['origin'], x['chosen_facility'] or '—',
                     x['distance_m'] or '—', x['estimated_wait_minutes'] or '—',
                     x['estimated_to_eating_minutes'] or '—', x['actual_to_eating_minutes'] or '—',
                     x['final_state']) for x in journeys[:30]]
    charts = ''.join(queue_chart(read_csv(output / 'runs' / p / 'output' / 'facility_snapshots.csv'),
                                 LABELS[p], duration) for p in POLICIES)
    report = f'''<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>动态午餐策略对照</title><style>
:root{{font-family:system-ui,-apple-system,"Microsoft YaHei",sans-serif;color:#203340;background:#f4f7f8;line-height:1.6}}
body{{max-width:1180px;margin:auto;padding:28px}}h1,h2,h3{{line-height:1.25}}h2{{margin-top:2rem}}
.intro,.notice,.chart{{background:white;border-radius:12px;padding:18px 22px;box-shadow:0 3px 15px #1933420c}}
.notice{{border-left:4px solid #d88138}}.grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(310px,1fr));gap:14px}}
.table-wrap{{overflow-x:auto;background:white;border-radius:10px}}table{{border-collapse:collapse;width:100%;font-size:.9rem}}
th,td{{padding:10px 12px;text-align:left;border-bottom:1px solid #e6edef;white-space:nowrap}}th{{color:#476176;background:#edf3f5}}
svg{{width:100%;height:auto}}svg text{{font-size:11px;fill:#536b78}}.legend{{display:flex;gap:18px;flex-wrap:wrap;font-size:.85rem}}
i{{display:inline-block;width:12px;height:12px;border-radius:50%;margin-right:6px}}code{{background:#edf3f5;padding:2px 5px}}
</style></head><body><h1>校园午餐：动态选择食堂</h1>
<p class="intro">三组使用同一校园、学生、出发时刻和窗口配置，只改变选择策略。仿真观察时长 {duration} 分钟；输入是合成数据。</p>
<p class="notice">预计等待只使用出发前一刻的队列和窗口快照，假定之后没有新增到达。它忽略在途学生以及其他同时决策者，因此预计值不是保证值；误差 = 实际出发到开始吃饭 − 预计时间。</p>
<h2>策略对照</h2>{table(['策略','总人数','已选择','已到达','已开始吃饭','未完成服务','总队列峰值','平均等待/分','出发到吃饭/分','平均预测误差/分'], overall_rows)}
<p>平均等待只统计已开始窗口服务的人；出发到吃饭和预测误差只统计已完成窗口服务的人。未完成包括未出发、在路上、排队和服务中，提前截断时不能只比较均值。</p>
<h2>各食堂</h2>{table(['策略','食堂','窗口','选择人数','已到达','已完成','尚未完成','仍排队','峰值队列','平均等待/分','出发到吃饭/分'], facility_rows)}
<h2>队列变化</h2><div class="grid">{charts}</div>
<h2>逐人样例</h2><p>展示前 30 行；完整数据以及每个候选食堂的评分见 CSV。</p>
{table(['策略','学生','起点','所选食堂','距离/米','预计等待/分','预计总耗时/分','实际总耗时/分','最终状态'], example_rows)}
<h2>复现与核对</h2><p><a href="policy_summary.csv">策略汇总</a> · <a href="facility_comparison.csv">食堂对照</a> · <a href="student_journeys.csv">逐人行程</a> · <a href="candidate_decisions.csv">全部候选决策</a></p>
<p>每组 <code>runs/策略/input/</code> 保存真实输入；<code>output/</code> 保存原始事件、状态、汇总和决策 CSV。所有同分钟出发的学生共享同一份前一时刻快照；到达前不改目标。</p></body></html>'''
    (output / 'report.html').write_text(report, encoding='utf-8')


def run(args):
    executable, scenario, students, output = (p.resolve() for p in
                                               (args.executable, args.scenario, args.students, args.output))
    if not executable.is_file():
        raise ValueError('executable not found: ' + str(executable))
    if not 1 <= args.duration <= 2**31 - 1:
        raise ValueError('duration must be a positive 32-bit integer')
    if output.exists():
        raise ValueError('output directory already exists: ' + str(output))
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
        call([executable, '--scenario', input_scenario, '--students', input_students, '--policy', policy,
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
    render_report(output, overall, facilities, all_journeys, args.duration)
    print('Experiments complete. Open ' + str(output / 'report.html'))
    for item in overall:
        print(f"{item['policy']}: completed={item['completed']}/{item['students']} "
              f"mean_wait={item['mean_wait_started'] or 'N/A'} not_completed={item['not_completed']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
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
