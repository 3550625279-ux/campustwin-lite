#!/usr/bin/env python3
"""Run every completed upgrade experiment and produce one offline entry page."""
import argparse
from pathlib import Path
import sys
from run_lunch_experiments import ROOT, call, read_csv, table


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        output, executable = args.output.resolve(), args.executable.resolve()
        if output.exists():
            raise ValueError("output directory already exists")
        if not executable.is_file():
            raise ValueError("executable not found")
        if ROOT / "data" in output.parents or output == ROOT / "data":
            raise ValueError("output must not be inside input data")
        output.mkdir(parents=True)
        call([executable, "--scenario", ROOT / "data/scenarios/mvp", "--students", ROOT / "data/students/mvp.csv",
              "--duration", 120, "--output", output / "mvp"])
        print(call([sys.executable, ROOT / "tests/check_mvp_outputs.py", output / "mvp"]).strip())
        stages = [("lunch", "run_lunch_experiments.py", "基础对照：窗口与步速"),
                  ("dynamic", "run_dynamic_experiments.py", "阶段一：动态选食堂"),
                  ("service", "run_service_experiments.py", "阶段二：异质窗口"),
                  ("schedule", "run_schedule_experiments.py", "阶段三：日程与座位")]
        links = []
        for name, script, title in stages:
            print(call([sys.executable, ROOT / "tools" / script, "--executable", executable, "--output", output / name]).strip())
            links.append(f'<li><a href="{name}/report.html">{title}</a></li>')
        totals = read_csv(output / "schedule/summary.csv")
        overview = table(["方案", "完成学生", "迟到课程", "未完成活动"],
                         [(r["variant"], r["finished"], r["late_classes"], int(r["activities"]) - int(r["activities_completed"])) for r in totals])
        (output / "report.html").write_text('''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>CampusTwin 三阶段验收演示</title>
<style>body{font:17px/1.7 system-ui;max-width:950px;margin:40px auto;padding:20px;background:#f3f7fa;color:#213547}table{border-collapse:collapse;background:white;width:100%}td,th{padding:12px;text-align:left;border-bottom:1px solid #ddd}a{color:#1565c0}li{margin:12px}</style>
<h1>CampusTwin · 三阶段完整演示</h1><p>所有结果来自本次真实 C++ 仿真。原始 20 人 MVP 自动验收通过；各阶段保留独立的输入副本、原始 CSV 与对照报告。</p><ul>'''
            + "".join(links) + "</ul><h2>第三阶段概览</h2>" + overview
            + "<p>这是实验运行入口；完整回归请执行 CTest。数据为合成数据，预测与实际可能不同。</p></html>", encoding="utf-8")
        print("All stages complete. Open " + str(output / "report.html"))
        return 0
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        print(f"all-stage run failed: {error}; output may be incomplete", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
