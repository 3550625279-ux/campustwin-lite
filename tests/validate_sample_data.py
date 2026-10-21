"""Validate the committed MVP fixtures, not the unimplemented C++ behavior."""
import csv
import heapq
import math
from pathlib import Path
import re
import sys


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_rows(path, columns):
    text = path.read_text(encoding="utf-8")
    require(not text.startswith("\ufeff") and '"' not in text, f"{path}: BOM/quoted fields unsupported")
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        require(reader.fieldnames == columns.split(","), f"{path}: unexpected header")
        rows = list(reader)
    for line, row in enumerate(rows, 2):
        require(None not in row and None not in row.values(), f"{path}:{line}: column count")
        require(all(v == v.strip() for v in row.values()), f"{path}:{line}: surrounding whitespace")
    return rows


def integer(value, minimum=0):
    require(re.fullmatch(r"[0-9]+", value) is not None, f"not an unsigned decimal integer: {value}")
    result = int(value)
    require(minimum <= result <= 2**31 - 1, f"out of range: {value}")
    return result


def positive(value):
    result = float(value)
    require(math.isfinite(result) and result > 0, f"not a finite positive number: {value}")
    return result


def unique_ids(rows):
    ids = [r["id"] for r in rows]
    require(len(set(ids)) == len(ids), "duplicate IDs")
    require(all(re.fullmatch(r"[A-Za-z0-9_-]+", value) for value in ids), "invalid ID")
    return set(ids)


def validate(root):
    scene = root / "data/scenarios/mvp"
    locations = read_rows(scene / "locations.csv", "id,name,kind,capacity")
    ids = unique_ids(locations)
    kinds = {r["id"]: r["kind"] for r in locations}
    for row in locations:
        require(row["kind"] in {"Teaching", "Canteen", "Dormitory", "Library"}, "invalid kind")
        require(bool(row["name"]), "empty location name")
        integer(row["capacity"], 1)
    roads = read_rows(scene / "roads.csv", "from,to,distance_m")
    edges, graph = set(), {key: [] for key in ids}
    for row in roads:
        a, b = row["from"], row["to"]
        require(a in ids and b in ids and a != b, "invalid road endpoints")
        key = tuple(sorted((a, b)))
        require(key not in edges, "duplicate undirected road")
        edges.add(key)
        distance = positive(row["distance_m"])
        graph[a].append((b, distance))
        graph[b].append((a, distance))

    def shortest(start, end):
        best, pending = {start: 0.0}, [(0.0, start)]
        while pending:
            distance, node = heapq.heappop(pending)
            if distance != best[node]:
                continue
            if node == end:
                return distance
            for nxt, length in graph[node]:
                candidate = distance + length
                if candidate < best.get(nxt, math.inf):
                    best[nxt] = candidate
                    heapq.heappush(pending, (candidate, nxt))
        raise ValueError(f"fixture is disconnected: {start} -> {end}")

    facilities = read_rows(scene / "facilities.csv", "id,location_id,window_count,service_minutes")
    facility_ids = unique_ids(facilities)
    require(len({r["location_id"] for r in facilities}) == len(facilities), "multiple facilities per location")
    for row in facilities:
        require(kinds.get(row["location_id"]) == "Canteen", "facility must refer to canteen")
        integer(row["window_count"], 1)
        integer(row["service_minutes"], 1)
    population = read_rows(root / "data/students/mvp.csv", "id,initial_location,lunch_facility,after_meal_location,departure_minute,walk_m_per_min,meal_minutes")
    unique_ids(population)
    by_facility = {r["id"]: r for r in facilities}
    for row in population:
        require(row["initial_location"] in ids and row["after_meal_location"] in ids, "student location missing")
        require(row["lunch_facility"] in facility_ids, "student facility missing")
        integer(row["departure_minute"])
        integer(row["meal_minutes"], 1)
        positive(row["walk_m_per_min"])
        canteen = by_facility[row["lunch_facility"]]["location_id"]
        require(row["initial_location"] != canteen != row["after_meal_location"], "zero-length lunch trip")
        shortest(row["initial_location"], canteen)
        shortest(canteen, row["after_meal_location"])

    # Freeze the particular input to which the hand-calculated golden outputs apply.
    require((len(locations), len(roads), len(facilities), len(population)) == (5, 6, 1, 20), "canonical fixture size changed")
    require(facilities == [dict(id="F_CANTEEN", location_id="L_CANTEEN", window_count="2", service_minutes="2")], "canonical facility changed")
    for index, row in enumerate(population, 1):
        require(row == dict(id=f"S{index:03}", initial_location="L_T1" if index <= 10 else "L_T2", lunch_facility="F_CANTEEN", after_meal_location="L_DORM", departure_minute="0", walk_m_per_min="60", meal_minutes="5"), "canonical population changed")
    require(shortest("L_T1", "L_CANTEEN") == 120, "T1 route changed")
    require(shortest("L_T2", "L_CANTEEN") == 180, "T2 route changed")
    require(shortest("L_CANTEEN", "L_DORM") == 120, "return route changed")
    fixture_dir = root / "data/fixtures"
    tiny = read_rows(fixture_dir / "resource_facilities.csv", "id,location_id,window_count,service_minutes")
    require(tiny == [dict(id="F_QUEUE_TEST", location_id="L_CANTEEN", window_count="1", service_minutes="2")], "tiny facility changed")
    arrivals = read_rows(fixture_dir / "resource_arrivals.csv", "request_id,student_id,facility_id,at")
    require(arrivals == [dict(request_id=f"S{i:03}:lunch:1", student_id=f"S{i:03}", facility_id="F_QUEUE_TEST", at=str(t)) for i, t in [(1, 0), (2, 0), (3, 2)]], "tiny arrival sequence changed")
    expected = read_rows(fixture_dir / "resource_expected_events.csv", "at,type,request_id,student_id,facility_id,window_id,wait_minutes")
    golden = []
    for at, kind, student, wait in [(0, "ServiceStarted", 1, "0"), (2, "ServiceCompleted", 1, ""), (2, "ServiceStarted", 2, "2"), (4, "ServiceCompleted", 2, ""), (4, "ServiceStarted", 3, "2"), (6, "ServiceCompleted", 3, "")]:
        golden.append(dict(at=str(at), type=kind, request_id=f"S{student:03}:lunch:1", student_id=f"S{student:03}", facility_id="F_QUEUE_TEST", window_id="0", wait_minutes=wait))
    require(expected == golden, "tiny hand-calculated reference changed")
    summary = read_rows(fixture_dir / "mvp_expected_summary.csv", "facility_id,window_count,observation_minutes,arrived,started,completed,max_queue_length,total_wait_minutes,mean_wait_minutes,busy_window_minutes,utilization")
    require(len(summary) == 1, "expected exactly one reference summary")
    require(summary[0] == dict(facility_id="F_CANTEEN", window_count="2", observation_minutes="120", arrived="20", started="20", completed="20", max_queue_length="18", total_wait_minutes="170", mean_wait_minutes="8.500000", busy_window_minutes="40", utilization="0.166666667"), "canonical reference changed")


if __name__ == "__main__":
    try:
        validate(Path(sys.argv[1]) if len(sys.argv) == 2 else Path(__file__).resolve().parents[1])
        print("PASS: committed input/reference data match the v0.1 contract; MVP behavior not tested.")
    except (OSError, ValueError, KeyError, csv.Error) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
