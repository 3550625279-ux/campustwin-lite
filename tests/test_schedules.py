"""Real A/B/C integration; hand-computed timeline plus cross-checked output ledgers."""
import csv
import pathlib
import shutil
import subprocess
import sys
import tempfile
from collections import Counter, defaultdict

EXE = pathlib.Path(sys.argv[1]).resolve()
ROOT = pathlib.Path(sys.argv[2]).resolve()


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def read(path):
    with path.open(encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def run(scene, students, schedule, output, duration=30, error=None):
    args = [str(EXE), "--scenario", str(scene), "--students", str(students),
            "--output", str(output), "--duration", str(duration)]
    if schedule:
        args += ["--schedule", str(schedule)]
    result = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", timeout=30)
    if error:
        assert result.returncode != 0 and error in result.stderr, (args, result.stdout, result.stderr)
    else:
        assert result.returncode == 0, (args, result.stdout, result.stderr)
    return result


def audit(output, duration, population):
    snapshots = read(output / "student_snapshots.csv")
    assert len(snapshots) == (duration + 1) * population
    waiting = Counter((r["at"], r["location_id"]) for r in snapshots if r["state"] == "WaitingSeat")
    events = read(output / "seat_events.csv")
    requests = {r["request_id"]: r for r in events if r["type"] == "SeatRequested"}
    grants = {r["request_id"]: r for r in events if r["type"] == "SeatGranted"}
    assert len(requests) + len(grants) == len(events)
    assert grants.keys() <= requests.keys()
    students = {(r["at"], r["student_id"]): r for r in snapshots}
    occupied = Counter()
    for key, grant in grants.items():
        start, end = int(grant["at"]), int(grant["until"])
        assert end - start == int(grant["duration_minutes"])
        assert int(grant["wait_minutes"]) == start - int(requests[key]["at"])
        for t in range(start, min(duration + 1, end)):
            state = students[(str(t), grant["student_id"])]
            assert state["state"] in ("InActivity", "Eating") and state["location_id"] == grant["location_id"]
            occupied[(str(t), grant["location_id"])] += 1
    busy = Counter()
    for row in read(output / "seat_snapshots.csv"):
        at, loc = row["at"], row["location_id"]
        t = int(at)
        if t:
            busy[loc] += occupied[(str(t - 1), loc)]
        assert int(row["queued"]) == waiting[(at, loc)]
        assert int(row["occupied"]) == occupied[(at, loc)] <= int(row["capacity"])
        assert int(row["busy_seat_minutes"]) == busy[loc]
        assert int(row["arrived"]) == int(row["queued"]) + int(row["granted"])
        assert int(row["granted"]) == int(row["occupied"]) + int(row["released"])
    by_student = defaultdict(list)
    for row in read(output / "activities.csv"):
        by_student[row["student_id"]].append(row)
        if row["started_at"]:
            assert int(row["started_at"]) >= int(row["planned_start"])
        if row["completed_at"]:
            assert int(row["completed_at"]) <= duration
            if row["kind"] != "lunch":
                assert int(row["completed_at"]) - int(row["started_at"]) == int(row["duration_minutes"])
    for rows in by_student.values():
        for before, after in zip(rows, rows[1:]):
            if after["departed_at"]:
                assert before["completed_at"] and int(after["departed_at"]) >= int(before["completed_at"])


with tempfile.TemporaryDirectory(prefix="campustwin_schedule_") as temp:
    base = pathlib.Path(temp)
    scene = base / "校园"
    write(scene / "locations.csv", "id,name,kind,capacity\nT,Teaching,Teaching,10\nF,Canteen,Canteen,10\nL,Library,Library,10\nD,Dorm,Dormitory,10\n")
    write(scene / "roads.csv", "from,to,distance_m\nT,F,60\nF,L,60\nL,D,60\nT,D,300\n")
    write(scene / "facilities.csv", "id,location_id,window_count,service_minutes\nC,F,2,1\n")
    write(scene / "seats.csv", "location_id,capacity\nF,1\nL,1\n")
    people = base / "学生.csv"
    write(people, "id,initial_location,lunch_facility,after_meal_location,departure_minute,walk_m_per_min,meal_minutes\nA,T,C,D,2,60,2\nB,T,C,D,2,60,2\n")
    schedule = base / "日程.csv"
    header = "student_id,sequence,activity_id,kind,location_id,start_minute,duration_minutes,requires_seat\n"
    body = "".join(f"{sid},1,lecture,class,T,0,2,0\n{sid},2,lunch,lunch,,2,2,0\n{sid},3,study,study,L,6,3,1\n{sid},4,home,rest,D,8,1,0\n" for sid in ("A", "B"))
    write(schedule, header + body)
    output = base / "out"
    run(scene, people, schedule, output)
    audit(output, 30, 2)
    activities = {(r["student_id"], r["activity_id"]): r for r in read(output / "activities.csv")}
    # Both reach F at 3, finish windows at 4. A eats [4,6), B [6,8).
    # A studies [7,10), B arrives 9 and studies [10,13), then returns.
    for sid, ends, study_start, home_end in (("A", 6, 7, 12), ("B", 8, 10, 15)):
        assert int(activities[sid, "lunch"]["completed_at"]) == ends
        assert int(activities[sid, "study"]["started_at"]) == study_start
        assert int(activities[sid, "home"]["completed_at"]) == home_end
    assert activities["B", "lunch"]["seat_wait_minutes"] == "2"
    assert activities["B", "study"]["seat_wait_minutes"] == "1"
    assert activities["B", "home"]["start_delay_minutes"] == "6"
    short = base / "short"
    run(scene, people, schedule, short, duration=5)
    audit(short, 5, 2)
    pending = {(r["student_id"], r["activity_id"]): r for r in read(short / "activities.csv")}
    assert pending["B", "lunch"]["completed_at"] == "" and pending["B", "lunch"]["seat_wait_minutes"] == "1"
    # Reorder students and schedule rows: no ID-order or file-order bias.
    original_people = people.read_text(encoding="utf-8")
    lines = original_people.splitlines()
    write(people, "\n".join([lines[0], *reversed(lines[1:])]) + "\n")
    write(schedule, header + "\n".join(reversed(body.splitlines())) + "\n")
    second = base / "second"
    run(scene, people, schedule, second)
    for file in output.glob("*.csv"):
        assert file.read_bytes() == (second / file.name).read_bytes(), file.name
    write(people, original_people)
    write(schedule, header + body)
    preserved = (output / "activities.csv").read_bytes()
    run(scene, people, schedule, output, error="refusing to overwrite")
    assert (output / "activities.csv").read_bytes() == preserved
    bads = [(body.replace("A,2,lunch", "A,1,lunch"), "duplicate schedule sequence"),
            (body.replace("A,3,study", "A,8,study"), "contiguous"),
            (body.replace("A,1,lecture", "Z,1,lecture"), "unknown schedule student"),
            (body.replace(",study,L,", ",study,T,"), "location kind"),
            (body.replace(",6,3,1", ",6,0,1"), "outside the supported range"),
            (body.replace(",6,3,1", ",6,3,2"), "requires_seat"),
            (body.replace(",2,2,0", ",2147483647,2,0"), "outside the supported range"),
            (body.replace(",lunch,,", ",lunch,F,"), "lunch location"),
            (body.replace("A,4,home,rest,D,8,1,0\n", "" ).replace("A,3,study,study,L,6,3,1\n", "").replace("A,2,lunch,lunch,,2,2,0\n", ""), "exactly one lunch")]
    for i, (bad, error) in enumerate(bads):
        write(schedule, header + bad)
        run(scene, people, schedule, base / f"bad{i}", error=error)
    write(schedule, header + body)
    write(scene / "seats.csv", "location_id,capacity\nF,1\n")
    run(scene, people, schedule, base / "missing_pool", error="configured seat pool")
    # Legacy meal-and-return flow can use seats without any schedule.
    run(scene, people, None, base / "legacy_seats")
    audit(base / "legacy_seats", 30, 2)
    write(scene / "seats.csv", "location_id,capacity\nF,0\n")
    run(scene, people, None, base / "zero_seats", error="must be positive")
    # No artificial one-minute movement for an activity at the current location.
    write(scene / "seats.csv", "location_id,capacity\nF,1\nL,1\n")
    write(people, original_people.replace(",T,C,", ",F,C,"))
    write(schedule, header + "".join(f"{sid},1,lunch,lunch,,0,2,0\n{sid},2,home,rest,D,8,1,0\n" for sid in ("A", "B")))
    run(scene, people, schedule, base / "same_location")
    assert {r["at"] for r in read(base / "same_location/events.csv") if r["type"] == "StudentArrival"} == {"0"}
    # Supplied multi-map scene, policies, heterogeneous windows and repeated activity loops.
    full = base / "day"
    run(ROOT / "data/scenarios/campus_day", ROOT / "data/students/campus_day.csv",
        ROOT / "data/schedules/campus_day.csv", full, 160)
    audit(full, 160, 36)
    records = read(full / "activities.csv")
    assert len(records) == 216 and all(r["status"] == "completed" for r in records)
    assert any(r["kind"] == "class" and int(r["start_delay_minutes"]) > 0 for r in records)
    assert {r["policy"] for r in read(full / "decisions.csv")} == {"fixed", "nearest", "fastest"}
print("PASS schedule timelines, seats, delays, conservation, cutoff, repeatability and invalid inputs")
