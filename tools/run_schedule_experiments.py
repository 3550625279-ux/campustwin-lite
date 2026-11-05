#!/usr/bin/env python3
"""Compare dining seats, study seats and an alternate road map with identical schedules."""
import argparse
from collections import defaultdict
import csv
import json
from pathlib import Path
import shutil
import sys

from run_lunch_experiments import call, number, read_csv, table, write_csv

ROOT = Path(__file__).resolve().parents[1]
VARIANTS = ("baseline", "dining_seats", "study_seats", "road_detour")
LABELS = dict(zip(VARIANTS, ("基准", "食堂座位翻倍", "图书馆座位翻倍", "三条道路封闭")))
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
    return total, seat_rows, activities, snapshots


def render(output, totals, seat_rows, datasets, duration):
    overall = table(["方案", "完成学生", "未完成学生", "完成活动", "迟到课程次数", "尚未开始课程", "课程总延迟/分", "已观察座位等待/分", "已出发路程/米"],
                    [(LABELS[r["variant"]], r["finished"], r["unfinished"], r["activities_completed"],
                      r["late_classes"], r["classes_not_started"], r["class_delay_minutes"], r["seat_wait_observed"],
                      r["distance_m_departed"]) for r in totals])
    resources = table(["方案", "地点", "座位", "已入座", "截止等待", "截止占用", "已入座者总等待", "利用率"],
                      [(LABELS[r["variant"]], r["location_id"], r["capacity"], r["granted"], r["waiting_at_end"],
                        r["occupied_at_end"], r["total_wait_granted"], r["utilization"]) for r in seat_rows])
    data = json.dumps(datasets, ensure_ascii=True, separators=(",", ":")).replace("<", "\\u003c")
    # A schematic topology, deliberately not a scaled or geographic campus map.
    html = '''<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>校园活动与座位调度实验</title><style>
body{font:16px/1.65 system-ui,sans-serif;max-width:1200px;margin:auto;padding:28px;color:#213547;background:#f3f7fa}
h1{font-size:30px}h2{margin-top:36px}.table-wrap{overflow:auto;background:white;border-radius:10px}table{border-collapse:collapse;width:100%}td,th{text-align:left;padding:10px;border-bottom:1px solid #dde5ec;white-space:nowrap}th{background:#e8eff5}
select{font:inherit;padding:8px;margin-right:15px}svg{width:100%;height:auto;background:white;border-radius:10px}.hint{color:#50677c}a{color:#1565c0}.card{background:white;border-radius:12px;padding:18px;margin-top:18px}.legend span{display:inline-block;padding:2px 8px;margin:4px;font-size:14px;border-radius:4px}#details{overflow:auto}.grid{display:grid;grid-template-columns:1fr 1fr;gap:18px}@media(max-width:850px){.grid{grid-template-columns:1fr}}</style></head><body>
<p class="hint">CAMPUS TWIN · 三模块联动 / 第三阶段</p><h1>一次排队，如何改变之后的日程？</h1>
<p>同一批 36 名学生，依次上课、午餐、上课、图书馆学习、研讨与回宿舍；观察 DURATION 分钟。四组只分别改变食堂座位、图书馆座位或道路，其余输入相同。全部为合成场景。</p>
<h2>对照结果</h2>OVERALL
<p class="hint">迟到按已开始的课程次数计算；截止未开始的课程单列。座位等待包括截止仍在等待的已观察时长；路程包括已出发但未走完的整条路径。早截止时，不能仅凭更少等待或迟到判定方案更好。</p>
<h2>逐人追踪</h2><label for="variant">方案 </label><select id="variant"></select><label for="student">学生 </label><select id="student"></select>
<div class="legend" id="legend"></div><svg id="timeline" viewBox="0 0 1100 110" role="img" aria-label="逐分钟学生状态时间线"></svg>
<p class="hint">时间线按 [t,t+1) 绘制，截止时刻状态单独显示。午餐活动从出发开始，包括路途、取餐、等座及用餐。其他活动从实际入场/入座开始，迟到后仍执行完整时长。</p><p id="final"></p>
<div id="details" class="card"></div>
<div class="grid"><div><h2>路线网络</h2><svg id="map" viewBox="0 0 620 480" role="img" aria-label="校园路线示意图"></svg><p class="hint">节点位置只为示意；距离以 CSV 的米数为准。绿色为当前学生已出发路线，细灰线为可用道路。悬停查看距离。</p></div>
<div><h2>原始数据</h2><p><a href="summary.csv">实验汇总 CSV</a> · <a href="seat_comparison.csv">座位统计 CSV</a> · <a href="activities.csv">全部活动 CSV</a></p><ul id="files"></ul><p>每组 input/ 保存地图、学生、日程的完整副本；output/ 保存八份原始 CSV。动态选食堂的 fastest 只预测到窗口服务结束，不预测座位等待，不保证实际最快吃完。</p></div></div>
<h2>各地点座位负载</h2>RESOURCES
<script>const DATA=DATA_JSON;const DURATION=TIME_VALUE;
const colors={AtLocation:'#b8c9d8',Moving:'#438cd0',Queued:'#edb54a',InService:'#9d79c5',WaitingSeat:'#db665b',Eating:'#45a691',InActivity:'#3a627f',Finished:'#e4e9ed'};
const names={AtLocation:'等待计划',Moving:'移动',Queued:'窗口排队',InService:'取餐',WaitingSeat:'等待座位',Eating:'就餐',InActivity:'活动',Finished:'完成'};
const v=document.getElementById('variant'),s=document.getElementById('student');
function el(tag,text,parent){const e=document.createElement(tag);if(text!==null)e.textContent=text;if(parent)parent.appendChild(e);return e}
function svg(tag,attrs,parent){const e=document.createElementNS('http://www.w3.org/2000/svg',tag);for(const [k,val]of Object.entries(attrs))e.setAttribute(k,val);parent.appendChild(e);return e}
for(const [key,d]of Object.entries(DATA)){const o=el('option',d.label,v);o.value=key}
for(const sid of Object.keys(DATA.baseline.people)){const o=el('option',sid,s);o.value=sid}
for(const [key,color]of Object.entries(colors)){const e=el('span',names[key],document.getElementById('legend'));e.style.background=color;e.style.color=['AtLocation','Queued','Finished'].includes(key)?'#213547':'white'}
function draw(){const d=DATA[v.value],person=d.people[s.value],line=document.getElementById('timeline');line.replaceChildren();
for(const r of person.intervals){if(r.end<=r.start)continue;const box=svg('rect',{x:50+r.start/DURATION*1000,y:35,width:(r.end-r.start)/DURATION*1000,height:30,fill:colors[r.state]},line);svg('title',{},box).textContent=names[r.state]+' '+r.start+'–'+r.end+' 分钟'}
for(let i=0;i<=8;i++)svg('text',{x:50+i*125,y:90,'text-anchor':'middle','font-size':14},line).textContent=Math.round(i*DURATION/8);
document.getElementById('final').textContent='截止状态：'+names[person.final.state]+'；位置：'+(person.final.location_id||person.final.from+' → '+person.final.to);
const detail=document.getElementById('details');detail.replaceChildren();const t=el('table',null,detail),head=el('tr',null,t);for(const name of ['活动','地点','计划开始','出发','到达','实际开始','结束','延迟','等座','路径'])el('th',name,head);
for(const a of person.activities){const row=el('tr',null,t);for(const key of ['activity_id','location_id','planned_start','departed_at','arrived_at','started_at','completed_at','start_delay_minutes','seat_wait_minutes','route_path'])el('td',a[key]||'—',row)}
const map=document.getElementById('map');map.replaceChildren();const pos={},chosen=new Set();d.locations.forEach((l,i)=>{const theta=2*Math.PI*i/d.locations.length;pos[l.id]=[310+210*Math.cos(theta),240+175*Math.sin(theta)]});
const edge=(a,b)=>[a,b].sort().join('|');for(const a of person.activities){const path=a.route_path.split('|');for(let i=1;i<path.length;i++)chosen.add(edge(path[i-1],path[i]))}
for(const r of d.roads){const [x1,y1]=pos[r.from],[x2,y2]=pos[r.to];const used=chosen.has(edge(r.from,r.to));const ln=svg('line',{x1,y1,x2,y2,stroke:used?'#168772':'#d9e2e8','stroke-width':used?3:1},map);svg('title',{},ln).textContent=r.from+' → '+r.to+' / '+r.distance_m+' m'}
for(const l of d.locations){const [cx,cy]=pos[l.id];const c=svg('circle',{cx,cy,r:9,fill:l.kind==='Canteen'?'#d58c38':l.kind==='Library'?'#168772':'#3a627f'},map);svg('title',{},c).textContent=l.name;svg('text',{x:cx,y:cy-15,'text-anchor':'middle','font-size':11},map).textContent=l.id}
const files=document.getElementById('files');files.replaceChildren();for(const name of ['activities.csv','student_snapshots.csv','decisions.csv','seat_events.csv','seat_snapshots.csv']){const a=el('a',name,el('li',null,files));a.href='runs/'+v.value+'/output/'+name}}
v.addEventListener('change',draw);s.addEventListener('change',draw);draw();</script></body></html>'''
    for key, value in (("OVERALL", overall), ("RESOURCES", resources), ("DURATION 分钟", f"{duration} 分钟"),
                       ("DATA_JSON", data), ("TIME_VALUE", str(duration))):
        html = html.replace(key, value)
    (output / "report.html").write_text(html, encoding="utf-8")


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
    totals, seat_rows, all_activities, datasets = [], [], [], {}
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
        total, seats, activities, snapshots = analyze(variant, raw, args.duration, 36)
        totals.append(total); seat_rows.extend(seats)
        all_activities.extend(dict(variant=variant, **r) for r in activities)
        people = defaultdict(lambda: {"intervals": [], "activities": []})
        for r in activities:
            people[r["student_id"]]["activities"].append(r)
        for r in snapshots:
            person = people[r["student_id"]]; at = int(r["at"])
            person["final"] = r
            if at == args.duration:
                continue
            intervals = person["intervals"]
            if intervals and intervals[-1]["state"] == r["state"]:
                intervals[-1]["end"] = at + 1
            else:
                intervals.append({"state": r["state"], "start": at, "end": at + 1})
        datasets[variant] = {"label": LABELS[variant], "people": people,
                             "locations": read_csv(scene / "locations.csv"), "roads": read_csv(scene / "roads.csv")}
    for name, rows in (("summary.csv", totals), ("seat_comparison.csv", seat_rows), ("activities.csv", all_activities)):
        write_csv(output / name, rows, list(rows[0]))
    render(output, totals, seat_rows, datasets, args.duration)
    for r in totals:
        print(f"{r['variant']}: finished={r['finished']}/36 late_classes={r['late_classes']} seat_wait={r['seat_wait_observed']}")
    print("Open " + str(output / "report.html"))


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
