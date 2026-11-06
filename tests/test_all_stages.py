"""Verify the one-command delivery, copied inputs and controlled-variable experiments."""
import csv
import hashlib
from html.parser import HTMLParser
from pathlib import Path
import subprocess
import sys
import tempfile

exe, root = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()


def rows(path):
    with path.open(encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def inputs():
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in (root / "data").rglob("*.csv")}


class Links(HTMLParser):
    def __init__(self):
        super().__init__(); self.links = []

    def handle_starttag(self, tag, attrs):
        if tag == "a":
            self.links.extend(v for k, v in attrs if k == "href")


with tempfile.TemporaryDirectory(prefix="campustwin_all_") as temp:
    output = Path(temp) / "all"
    before = inputs()
    cmd = [sys.executable, str(root / "tools/run_all_stages.py"), "--executable", str(exe), "--output", str(output)]
    run = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", timeout=120)
    assert run.returncode == 0, (run.stdout, run.stderr)
    assert inputs() == before, "experiments changed committed inputs"
    for report in output.rglob("report.html"):
        parser = Links(); parser.feed(report.read_text(encoding="utf-8"))
        for link in parser.links:
            if not link.startswith(("http", "#")):
                assert (report.parent / link).is_file(), (report, link)
    day = output / "schedule"
    totals = {r["variant"]: r for r in rows(day / "summary.csv")}
    assert len(totals) == 4 and all(int(r["finished"]) == 36 and int(r["activities_completed"]) == 216 for r in totals.values())
    assert int(totals["study_seats"]["seat_wait_observed"]) < int(totals["baseline"]["seat_wait_observed"])
    baseline = day / "runs/baseline/input"
    for variant in ("dining_seats", "study_seats", "road_detour"):
        current = day / "runs" / variant / "input"
        for file in baseline.rglob("*.csv"):
            if file.name == ("roads.csv" if variant == "road_detour" else "seats.csv"):
                continue
            assert file.read_bytes() == (current / file.relative_to(baseline)).read_bytes(), (variant, file)
    original_roads = rows(baseline / "scenario/roads.csv")
    detour_roads = rows(day / "runs/road_detour/input/scenario/roads.csv")
    assert len(original_roads) - len(detour_roads) == 3
    assert all(r in original_roads for r in detour_roads)
    for variant, prefix in (("dining_seats", ("L_NEAR", "L_FAR", "L_WEST")), ("study_seats", ("L_LIBRARY", "L_LIBRARY_E", "L_LIBRARY_W"))):
        new = {r["location_id"]: int(r["capacity"]) for r in rows(day / "runs" / variant / "input/scenario/seats.csv")}
        for r in rows(baseline / "scenario/seats.csv"):
            assert new[r["location_id"]] == int(r["capacity"]) * (2 if r["location_id"] in prefix else 1)
    repeat = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", timeout=30)
    assert repeat.returncode != 0 and "already exists" in repeat.stderr
    early = Path(temp) / "early"
    result = subprocess.run([sys.executable, str(root / "tools/run_schedule_experiments.py"), "--executable", str(exe),
                             "--output", str(early), "--duration", "5"], capture_output=True, text=True, encoding="utf-8", timeout=30)
    assert result.returncode == 0, result.stderr
    assert all(int(r["unfinished"]) == 36 and int(r["classes_not_started"]) == 72 for r in rows(early / "summary.csv"))
    for script in ("run_lunch_experiments.py", "run_dynamic_experiments.py"):
        result = subprocess.run([sys.executable, str(root / "tools" / script), "--executable", str(exe),
                                 "--scenario", str(root / "data/scenarios/campus_day"), "--output", str(Path(temp) / script)],
                                capture_output=True, text=True, encoding="utf-8", timeout=30)
        assert result.returncode != 0 and "seat-constrained" in result.stderr
print("PASS all-stage experiments, report links, capacity/map controls, cutoff and input preservation")
