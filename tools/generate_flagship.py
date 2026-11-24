#!/usr/bin/env python3
"""Generate the 9,000-student synthetic demonstration campus."""
import csv
import random
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "data"
SCENE = ROOT / "scenarios" / "campus_flagship"
MEALS = (("rice", 11, 2), ("noodle", 7, 2), ("setmeal", 5, 1),
         ("stirfry", 5, 4), ("light", 4, 2), ("halal", 4, 2))
ZONES = ("North", "Northeast", "Southeast", "South", "Southwest", "Northwest")


def write(path, header, rows):
    with path.open("w", encoding="utf-8", newline="") as stream:
        table = csv.writer(stream, lineterminator="\n")
        table.writerow(header.split(","))
        table.writerows(rows)


def main():
    SCENE.mkdir(parents=True, exist_ok=True)
    rng = random.Random(20261124)
    locations, roads, facilities, services, seats = [], [], [], [], []
    students, schedules, wide_schedules = [], [], []
    for z, name in enumerate(ZONES, 1):
        hub = f"H{z}"
        canteen = f"C{z}"
        library = f"L{z}"
        facility = f"F{z}"
        locations.extend(((hub, f"{name} teaching commons", "Teaching", 900),
                          (f"T{z}", f"{name} teaching building", "Teaching", 900),
                          (canteen, f"{name} dining hall", "Canteen", 720),
                          (library, f"{name} library and study center", "Library", 480)))
        roads.extend(((hub, f"T{z}", 145 + z * 8), (hub, canteen, 105 + z * 7),
                      (hub, library, 175 + z * 8), (canteen, library, 195 + z * 4),
                      (hub, f"H{z % 6 + 1}", 270 + z * 20)))
        for d in range(1, 4):
            dorm = f"D{z}{d}"
            locations.append((dorm, f"{name} residence {d}", "Dormitory", 520))
            roads.append((hub, dorm, 165 + d * 45 + z * 7))
            roads.append((canteen, dorm, 205 + d * 43 + z * 9))
        seats.extend(((hub, 900), (f"T{z}", 900), (canteen, 720), (library, 480)))
        window = 0
        for food, count, minutes in MEALS:
            for _ in range(count):
                services.append((facility, window, food, minutes))
                window += 1
        facilities.append((facility, canteen, window, 2))

        # 30 cohorts per zone, each with a common timetable and 50 students.
        for cohort in range(30):
            track = cohort % 10
            teaching = hub if cohort < 15 else f"T{z}"
            afternoon = f"T{z}" if cohort % 3 == 0 else teaching
            first = 15 + (cohort % 3) * 15
            second = 90 + (cohort % 4) * 15
            wide_lunch = 190 + 12 * track + 3 * ((z - 1) % 3)
            # Keep the existing afternoon timetable in every lunch variant.
            class_pm = max(345 + (cohort % 3) * 15, wide_lunch + 55)
            study = max(class_pm + 55, 405 + 18 * (cohort % 6))
            for student in range(50):
                sid = f"S{(z - 1) * 1500 + cohort * 50 + student + 1:04d}"
                dorm = f"D{z}{cohort % 3 + 1}"
                jitter = rng.randrange(8)
                departure = 240 + (track % 5) * 8 + jitter
                meal_time = rng.randint(16, 26)
                food = rng.choices([m[0] for m in MEALS], weights=(30, 20, 18, 13, 10, 9))[0]
                policy = rng.choices(("fixed", "nearest", "fastest"), weights=(70, 15, 15))[0]
                speed = rng.randint(58, 92)
                students.append((sid, dorm, facility, dorm, departure, speed, meal_time, policy, food))
                day = [("morning", "class", teaching, first, 45, 1),
                       ("workshop", "class", teaching, second, 45, 1),
                       ("lunch", "lunch", "", departure, meal_time, 0),
                       ("afternoon", "class", afternoon, class_pm, 50, 1)]
                # Thirty students per cohort have an on-campus library session.
                if student < 30:
                    day.append(("study", "study", library, study + rng.randrange(8), rng.randint(40, 60), 1))
                day.append(("home", "rest", dorm, 550 + rng.randrange(16), 15, 0))
                schedules.extend((sid, n, *activity) for n, activity in enumerate(day, 1))
                day[2] = ("lunch", "lunch", "", wide_lunch + jitter, meal_time, 0)
                wide_schedules.extend((sid, n, *activity) for n, activity in enumerate(day, 1))

    write(SCENE / "locations.csv", "id,name,kind,capacity", locations)
    write(SCENE / "roads.csv", "from,to,distance_m", roads)
    write(SCENE / "facilities.csv", "id,location_id,window_count,service_minutes", facilities)
    write(SCENE / "window_services.csv", "facility_id,window_id,meal_type,service_minutes", services)
    write(SCENE / "seats.csv", "location_id,capacity", seats)
    write(ROOT / "students/campus_flagship.csv",
          "id,initial_location,lunch_facility,after_meal_location,departure_minute,walk_m_per_min,meal_minutes,lunch_policy,meal_type",
          students)
    header = "student_id,sequence,activity_id,kind,location_id,start_minute,duration_minutes,requires_seat"
    write(ROOT / "schedules/campus_flagship.csv", header, schedules)
    write(ROOT / "schedules/campus_flagship_wide_lunch.csv", header, wide_schedules)


if __name__ == "__main__":
    main()
