# 数据与回归工具

交互式展示通过 [CampusTwin Lite 工作台](../README.md#展示版) 运行。

- `generate_flagship.py` 按固定种子重新生成旗舰场景、学生和日程 CSV。
- `run_*_experiments.py` 和 `run_all_stages.py` 供集成回归使用，执行 C++ 程序并保留对照 CSV。统一通过 `ctest` 运行，见 [测试说明](../tests/README.md)。
