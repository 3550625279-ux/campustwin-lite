#!/usr/bin/env python3
"""Generate the 9,000-student synthetic demonstration campus."""
import csv
import random
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "data"
SCENE = ROOT / "scenarios" / "campus_flagship"
MEALS = (("rice", 11, 2), ("noodle", 7, 2), ("setmeal", 4, 1),
         ("stirfry", 6, 4), ("light", 4, 2), ("halal", 4, 2))
ZONES = ("North", "Northeast", "Southeast", "South", "Southwest", "Northwest")
STUDENTS_PER_ZONE = 1500
DISMISSAL = 210  # 11:30 from the 08:00 simulation start
FAST_MEAL_FROM = 300  # 13:00
DEFAULT_STAGGER_MINUTES = 40
LUNCH_STRATEGY_WEIGHTS = (25, 10, 65)  # fixed, nearest, fastest
SHARED_CANTEEN_LINKS = (("H2", "C1", 108), ("H6", "C1", 132),
                        ("H3", "C4", 114), ("H5", "C4", 126))


def write(path, header, rows):
    with path.open("w", encoding="utf-8", newline="") as stream:
        table = csv.writer(stream, lineterminator="\n")
        table.writerow(header.split(","))
        table.writerows(rows)


def main():
    SCENE.mkdir(parents=True, exist_ok=True)
    rng = random.Random(20261124)
    locations, facilities, services, seats = [], [], [], []
    roads = list(SHARED_CANTEEN_LINKS)
    students, schedules = [], []
    population = len(ZONES) * STUDENTS_PER_ZONE
    enrollment_ranks = list(range(population))
    # Entrants from all areas share a school-wide ID sequence. Release remains in ID order.
    random.Random(20261124).shuffle(enrollment_ranks)
    for z, name in enumerate(ZONES, 1):
        hub = f"H{z}"
        canteen = f"C{z}"
        library = f"L{z}"
        sports = f"A{z}"
        facility = f"F{z}"
        locations.extend(((hub, f"{name} teaching commons", "Teaching", 900),
                          (f"T{z}", f"{name} teaching building", "Teaching", 900),
                          (canteen, f"{name} dining hall", "Canteen", 720),
                          (library, f"{name} library and study center", "Library", 340),
                          (sports, f"{name} sports ground", "Sports", 200)))
        # Dining halls 1 and 4 have direct access from three teaching areas each.
        dining_route = 95 + z * 5 if z in (1, 4) else 230 + z * 5
        roads.extend(((hub, f"T{z}", 145 + z * 8), (hub, canteen, dining_route),
                      (hub, library, 175 + z * 8), (canteen, library, 195 + z * 4),
                      (hub, f"H{z % 6 + 1}", 270 + z * 20),
                      (hub, sports, 210 + z * 8), (library, sports, 155 + z * 5)))
        for d in range(1, 4):
            dorm = f"D{z}{d}"
            locations.append((dorm, f"{name} residence {d}", "Dormitory", 520))
            roads.append((hub, dorm, 165 + d * 45 + z * 7))
            roads.append((canteen, dorm, 205 + d * 43 + z * 9))
        seats.extend(((hub, 900), (f"T{z}", 900), (canteen, 720), (library, 340), (sports, 200)))
        window = 0
        for food, count, minutes in MEALS:
            for _ in range(count):
                services.append((facility, window, food, minutes))
                window += 1
        facilities.append((facility, canteen, window, 2))

        # 30 cohorts per zone, each with a common timetable and 50 students.
        for cohort in range(30):
            teaching = hub if cohort < 15 else f"T{z}"
            afternoon = f"T{z}" if cohort % 3 == 0 else teaching
            afternoon_slot = cohort % 3
            for student in range(50):
                local_index = (z - 1) * STUDENTS_PER_ZONE + cohort * 50 + student
                rank = enrollment_ranks[local_index]
                sid = f"S{rank + 1:04d}"
                dorm = f"D{z}{cohort % 3 + 1}"
                departure = DISMISSAL + rank * DEFAULT_STAGGER_MINUTES // population
                meal_time = rng.randint(16, 26)
                food = rng.choices([m[0] for m in MEALS], weights=(30, 20, 18, 13, 10, 9))[0]
                policy = rng.choices(("fixed", "nearest", "fastest"), weights=LUNCH_STRATEGY_WEIGHTS)[0]
                speed = rng.randint(58, 92)
                students.append((sid, dorm, facility, dorm, departure, speed, meal_time, policy, food))
                day = [("morning", "class", teaching, 30, 80, 1, ""),
                       ("workshop", "class", teaching, 120, DISMISSAL - 120, 1, ""),
                       ("lunch", "lunch", "", departure, meal_time, 0, FAST_MEAL_FROM)]
                if student < 22:
                    day.append(("afternoon", "class", afternoon, 330 + 30 * afternoon_slot, 50, 1, ""))
                elif student < 35:
                    start = 330 + 15 * afternoon_slot + rng.randrange(8)
                    day.append(("study", "study", library, start, rng.randint(45, 60), 1, ""))
                elif student < 43:
                    start = 345 + 15 * afternoon_slot + rng.randrange(8)
                    day.append(("exercise", "sport", sports, start, rng.randint(35, 50), 1, ""))
                day.append(("home", "rest", dorm, "", 1, 0, ""))
                schedules.extend((sid, n, *activity) for n, activity in enumerate(day, 1))

    write(SCENE / "locations.csv", "id,name,kind,capacity", locations)
    write(SCENE / "roads.csv", "from,to,distance_m", roads)
    write(SCENE / "facilities.csv", "id,location_id,window_count,service_minutes", facilities)
    write(SCENE / "window_services.csv", "facility_id,window_id,meal_type,service_minutes", services)
    write(SCENE / "seats.csv", "location_id,capacity", seats)
    write(ROOT / "students/campus_flagship.csv",
          "id,initial_location,lunch_facility,after_meal_location,departure_minute,walk_m_per_min,meal_minutes,lunch_policy,meal_type",
          sorted(students))
    header = "student_id,sequence,activity_id,kind,location_id,start_minute,duration_minutes,requires_seat,fast_meal_from"
    write(ROOT / "schedules/campus_flagship.csv", header, sorted(schedules))


if __name__ == "__main__":
    main()
