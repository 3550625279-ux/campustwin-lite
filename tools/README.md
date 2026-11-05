# Experiment tools

一键运行所有阶段：`python3 tools/run_all_stages.py --executable build/campustwin --output outputs/all_stages`。单独运行日程/座位/地图对照：`python3 tools/run_schedule_experiments.py --executable build/campustwin --output outputs/schedules`。Windows 使用 `py -3` 和实际 `.exe` 路径；输出目录必须不存在。详见 [第三阶段指南](../docs/16_ACTIVITY_SCHEDULES.md)。

Run `run_dynamic_experiments.py` to compare fixed, nearest, and queue-aware decisions; see [the dynamic lunch guide](../docs/14_DYNAMIC_LUNCH.md). Run `run_lunch_experiments.py` for the fixed-destination window and speed experiment; see [its guide](../docs/13_LUNCH_EXPERIMENTS.md).
