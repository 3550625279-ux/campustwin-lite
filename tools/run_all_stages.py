#!/usr/bin/env python3
"""Run numeric regression experiments and retain their CSV results."""
import argparse
from pathlib import Path
import sys
from run_lunch_experiments import ROOT, call


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
        stages = [("lunch", "run_lunch_experiments.py"),
                  ("dynamic", "run_dynamic_experiments.py"),
                  ("service", "run_service_experiments.py"),
                  ("schedule", "run_schedule_experiments.py")]
        for name, script in stages:
            print(call([sys.executable, ROOT / "tools" / script, "--executable", executable,
                        "--output", output / name]).strip())
        print(f"CSV results: {output}")
        return 0
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        print(f"all-stage run failed: {error}; output may be incomplete", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
