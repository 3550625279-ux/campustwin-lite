#!/usr/bin/env python3
"""Run reproducible lunch experiments against the real CampusTwin executable.

Only Python's standard library is required. The copied inputs, original output
and derived reports are kept together so that every number can be audited.
"""
import argparse
import csv
import html
import io
import math
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CASES = ("baseline", "more_windows", "faster_walk")
CASE_LABELS = {"baseline": "基准", "more_windows": "增加窗口", "faster_walk": "加快步行"}
COLORS = ("#1565c0", "#d1495b", "#138a72", "#8a5fb4")


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


def display(value, digits=2):
    if value == "" or value is None:
        return "—"
    if isinstance(value, float):
        return f"{value:.{digits}f}"
    return html.escape(str(value))


def table(headers, rows):
    heading = "".join(f"<th>{html.escape(str(h))}</th>" for h in headers)
    body = "".join("<tr>" + "".join(f"<td>{display(v)}</td>" for v in row) + "</tr>" for row in rows)
    return f"<div class='table-wrap'><table><thead><tr>{heading}</tr></thead><tbody>{body}</tbody></table></div>"


def queue_chart(snapshots, title, duration):
    groups = {}
    for row in snapshots:
        groups.setdefault(row["facility_id"], []).append((int(row["at"]), int(row["queue_length"])))
    ymax = max([1] + [n for points in groups.values() for _, n in points])
    lines, legend = [], []
    for i, (facility, points) in enumerate(sorted(groups.items())):
        color = COLORS[i % len(COLORS)]
        coords = " ".join(f"{50 + at / duration * 610:.2f},{190 - val / ymax * 150:.2f}" for at, val in points)
        lines.append(f"<polyline points='{coords}' fill='none' stroke='{color}' stroke-width='2.5'/>")
        legend.append(f"<span><i style='background:{color}'></i>{html.escape(facility)}</span>")
    return (f"<section class='chart'><h3>{html.escape(title)}</h3>"
            f"<svg viewBox='0 0 690 230' role='img' aria-label='{html.escape(title)}的队列变化图'>"
            f"<path d='M50 40 V190 H660' stroke='#9daeba' fill='none'/>"
            f"<text x='14' y='45'>{ymax}</text><text x='29' y='194'>0</text>"
            f"<text x='50' y='213'>0 分钟</text><text x='610' y='213'>{duration} 分钟</text>"
            + "".join(lines) + "</svg><div class='legend'>" + "".join(legend) + "</div></section>")


def group_comparison(journeys):
    groups = {}
    for person in journeys:
        key = (person["origin"], person["departure_minute"], person["walk_m_per_min"], person["facility_id"])
        groups.setdefault(key, []).append(person)
    rows = []
    for (origin, departure, speed, facility), people in sorted(groups.items()):
        completed = [int(p["departure_to_eating_minutes"]) for p in people if p["departure_to_eating_minutes"] != ""]
        rows.append((origin, departure, speed, facility, len(people), len(completed), mean(completed)))
    return rows


def make_report(output, summaries, journeys, routes, duration, target, window_count, speed_multiplier):
    summary_rows = []
    for row in summaries:
        summary_rows.append((CASE_LABELS[row["experiment"]], row["facility_id"], row["window_count"],
                             row["assigned"], row["arrived"], row["completed"], row["not_arrived"],
                             row["waiting"], row["in_service"], row["not_completed"],
                             row["max_queue_length"],
                             float(row["mean_wait_minutes_started"]) if row["mean_wait_minutes_started"] else None,
                             float(row["mean_departure_to_eating_minutes_completed"]) if row["mean_departure_to_eating_minutes_completed"] else None,
                             f"{float(row['utilization']) * 100:.1f}%"))
    route_rows = [(origin, facility, route["to"], float(route["distance_m"]), route["path"].replace("|", " → "))
                  for (origin, facility), route in sorted(routes.items())]
    charts = "".join(queue_chart(read_csv(output / "runs" / case / "output" / "facility_snapshots.csv"),
                                 CASE_LABELS[case], duration) for case in CASES)
    baseline = [r for r in journeys if r["experiment"] == "baseline"]
    report = f"""<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>校园午餐仿真对照报告</title><style>
:root{{font-family:system-ui,-apple-system,"Microsoft YaHei",sans-serif;color:#203340;background:#f4f7f8;line-height:1.6}}
body{{max-width:1180px;margin:auto;padding:28px}}h1,h2,h3{{line-height:1.25}}h1{{font-size:2rem}}h2{{margin-top:2rem}}
.intro,.notice,.chart{{background:white;border-radius:12px;padding:18px 22px;box-shadow:0 3px 15px #1933420c}}
.notice{{border-left:4px solid #d88138}}.grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(310px,1fr));gap:14px}}
.table-wrap{{overflow-x:auto;background:white;border-radius:10px}}table{{border-collapse:collapse;width:100%;font-size:.9rem}}
th,td{{padding:10px 12px;text-align:left;border-bottom:1px solid #e6edef;white-space:nowrap}}th{{color:#476176;background:#edf3f5}}
svg{{width:100%;height:auto}}svg text{{font-size:11px;fill:#536b78}}.legend{{display:flex;gap:18px;flex-wrap:wrap;font-size:.85rem}}
i{{display:inline-block;width:12px;height:12px;border-radius:50%;margin-right:6px}}code{{background:#edf3f5;padding:2px 5px}}a{{color:#1565c0}}
</style></head><body>
<h1>校园午餐出行与服务仿真</h1>
<p class="intro">三组实验使用同一份学生和校园配置。增加窗口组只改变 <code>{html.escape(target)}</code> 的窗口数为 {window_count}；加快步行组只把每人的速度乘以 {speed_multiplier:g}。观察区间为 [0, {duration}] 分钟。</p>
<h2>食堂结果</h2>
{table(['实验','食堂','窗口','指定人数','已到达','已完成服务','未到达','仍排队','服务中','未完成服务','最大队列','平均等待 / 分','出发到开始吃饭 / 分','窗口利用率'], summary_rows)}
<p class="notice">未完成服务人数包括尚未到达、正在排队和服务中的人。平均等待只计算已开始服务的人；出发到开始吃饭只计算已完成服务的人。截止时仍未到达、排队或服务中的人不计入相应平均值，因此截断较早时不能直接比较这些平均数。窗口利用率以整个观察区间计算。最大队列在当分钟窗口分配后测量。</p>
<h2>路线与距离</h2><p>使用校园环境模块实际计算的最短距离路径；路径中的箭头只表示路线，当前移动模型按整段总距离计算时间。</p>
{table(['起点','食堂','食堂地点','距离 / 米','路线'], route_rows)}
<h2>控制出发条件后的比较（基准）</h2><p>以下按起点、出发时间和速度分组：同组中不同食堂的学生具有相同出行条件。人数为指定人数，平均值只包含已开始吃饭的学生。</p>
{table(['起点','出发 / 分','速度 / 米每分','食堂','人数','已开始吃饭','出发到开始吃饭 / 分'], group_comparison(baseline))}
<h2>队列随时间变化</h2><p>每分钟的队列快照，记录当分钟可用窗口完成分配之后的人数。</p><div class="grid">{charts}</div>
<h2>核对与复现</h2><p><a href="comparison.csv">食堂对比 CSV</a> · <a href="student_journeys.csv">逐人行程 CSV</a> · <a href="routes.csv">路线 CSV</a></p>
<p>每组的 <code>runs/组名/input/</code> 保存实际输入，<code>runs/组名/output/</code> 保存程序的五份原始输出。地点与人口均为合成数据；食堂由输入预先指定，并非动态选择。速度对照只是速度变化，不模拟自行车和道路通行规则。</p>
</body></html>"""
    (output / "report.html").write_text(report, encoding="utf-8")


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
        call([executable, "--scenario", case_scenario, "--students", case_students,
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
    make_report(output, summaries, journeys, routes, args.duration, args.target_facility,
                args.window_count, args.speed_multiplier)
    print(f"Experiments complete. Open {output / 'report.html'}")
    for row in summaries:
        print(f"{row['experiment']}: {row['facility_id']} arrived={row['arrived']} completed={row['completed']} "
              f"waiting={row['waiting']} mean_wait={row['mean_wait_minutes_started'] or 'N/A'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
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
