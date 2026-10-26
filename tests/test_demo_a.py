"""End-to-end tests through the real independent A console; uses private temp data."""
import os
import pathlib
import subprocess
import sys
import tempfile

exe = str(pathlib.Path(sys.argv[1]).resolve())
root = pathlib.Path(sys.argv[2]).resolve()


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def run(directory, commands, *options):
    result = subprocess.run(
        [exe, "--accounts", str(directory / "accounts.db"), *options],
        input=commands, text=True, encoding="utf-8", errors="replace",
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=root, timeout=90,
    )
    return result, result.stdout + result.stderr


with tempfile.TemporaryDirectory(prefix="campustwin-console-") as tmp:
    directory = pathlib.Path(tmp)
    saved = directory / "saved scenario"
    commands = "\n".join([
        "register student_a", "example-pass-42",
        "login student_a", "example-pass-42",
        "list", "route L_T1 L_CANTEEN", "route L_T2 L_DORM",
        "road-set L_T1 L_CANTEEN 150.25",
        "facility-set F_CANTEEN L_CANTEEN 2 2",
        'location-add L_NEW "New Library" Library 50',
        'location-update L_NEW "Updated Library" Library 60',
        "route L_NEW L_T1",
        "location-remove L_NEW",
        f'save "{saved.as_posix()}"', "quit", "",
    ])
    result, output = run(directory, commands)
    check(result.returncode == 0, output)
    for expected in ["Logged in as student_a", "Locations=5 Roads=6 Facilities=1",
                     "distance_m=120", "distance_m=300", "UNREACHABLE", "Saved "]:
        check(expected in output, f"missing {expected!r}: {output}")
    check("L_CANTEEN,2,2" in (saved / "facilities.csv").read_text(), "windows not saved")
    check("L_NEW" not in (saved / "locations.csv").read_text(), "removed location saved")

    # A fresh process proves accounts and scene edits survived a restart.
    result, output = run(directory, "login student_a\nexample-pass-42\nlist\nroute L_T1 L_CANTEEN\nquit\n",
                         "--scenario", str(saved))
    check(result.returncode == 0, output)
    check("distance_m=150.25" in output and "windows=2" in output, output)

    if os.name == "nt":
        # Backslashes in quoted Windows paths must not disappear during tokenization.
        commands = f'login student_a\nexample-pass-42\nload "{saved}"\nroute L_T1 L_CANTEEN\nquit\n'
        result, output = run(directory, commands)
        check(result.returncode == 0 and "distance_m=150.25" in output, output)

    # Gate, malformed command, incorrect password and failed destructive edit are visible errors.
    result, output = run(directory, "list\nlogin student_a\nwrong-password\nquit\n")
    check(result.returncode == 1 and "login required" in output and "invalid username or password" in output, output)
    result, output = run(directory,
        "login student_a\nexample-pass-42\nroad-set L_T1 L_CANTEEN -10\n"
        "location-remove L_T1\nroute L_T1 L_CANTEEN\nquit\n")
    check(result.returncode == 1 and "distance_m=120" in output and "referenced by road" in output, output)

    # --empty supports constructing a new world without a sample or a B/C engine.
    fresh = directory / "fresh"
    commands = ("login student_a\nexample-pass-42\nnew\n"
                'location-add T "Classroom" Teaching 20\n'
                'location-add C "Canteen" Canteen 30\n'
                "road-set T C 10\nfacility-set F C 1 2\nroute T C\n"
                f'save "{fresh.as_posix()}"\nquit\n')
    result, output = run(directory, commands, "--empty")
    check(result.returncode == 0 and "distance_m=10" in output, output)

    # Unsaved edits may not silently disappear on load or EOF.
    result, output = run(directory, "login student_a\nexample-pass-42\nroad-set L_T1 L_CANTEEN 10\n"
                         'load "data/scenarios/mvp"\n')
    check(result.returncode == 1 and "unsaved edits" in output, output)
    result, output = run(directory, "quit\n", "--unknown")
    check(result.returncode == 1 and "unknown or incomplete option" in output, output)
print("PASS: A console login, edits, save/restart, route and error paths")
