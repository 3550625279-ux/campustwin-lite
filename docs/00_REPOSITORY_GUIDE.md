# 代码导览

本文说明当前源码的层次、依赖和阅读顺序。下载、构建和运行见 [README](../README.md)，维护人与协作边界统一见 [团队协作手册](TEAM_GUIDE.md)。

## 从入口到结果

建议围绕一次带日程的实际运行阅读，而不是先逐个读完所有类：

1. `app/main.cpp`：解析参数，加载校园、学生及可选日程；创建环境、学生、窗口、座位和记录器，调用统一运行器。
2. `include/campustwin/contracts.hpp`：确认输入、事件、快照、记录和纯虚接口的数据形状。
3. `src/simulation/simulation.cpp`：看每分钟调用顺序与状态守恒，确定下面各模块何时被调用。
4. `src/students/students.cpp`：追踪一个学生的活动索引、移动、选食堂、服务反馈和座位反馈。
5. `src/environment/environment.cpp`、`src/resources/resources.cpp`、`src/resources/seats.cpp`：分别追溯路径、窗口和座位的计算。
6. `src/reporting/reporting.cpp` 与 `tools/run_schedule_experiments.py`：核对这些运行结果怎样成为 CSV、统计和离线报告。

日程样例对应 `data/scenarios/campus_day/`、`data/students/campus_day.csv`、`data/schedules/campus_day.csv`。只有原午餐输入时，同一程序仍能执行旧行为链。

## 目录与构建目标

| 路径 / 目标 | 作用 |
| --- | --- |
| `include/campustwin/` | 公共数据契约、接口与具体类声明 |
| `src/environment/` → `campustwin_environment` | 校园配置、路网和账号实现 |
| `src/students/` → `campustwin_students` | 学生输入、决策与行为状态 |
| `src/resources/` → `campustwin_resources` | 窗口与座位两个引擎 |
| `src/simulation/` → `campustwin_simulation` | 时钟、事件路由与跨模块一致性校验 |
| `src/reporting/` → `campustwin_reporting` | CSV 记录器 |
| `app/` → `campustwin` | 最终集成应用的唯一 `main` |
| `examples/` → `demo_a`、`demo_b`、`demo_c` | 独立演示入口，分别有自己的 `main` |
| `data/`、`tests/` | 输入样例、手算参考、模块及集成测试 |
| `tools/` | 使用真实 C++ 输出生成实验报告 |
| `docs/`、`.github/` | 设计、格式、使用与协作说明，CI 和提交模板 |

模块编译为静态库，公共契约通过 CMake 的 INTERFACE 目标提供头文件路径与 C++17 要求。新增源文件必须加入对应目标；`accounts.cpp` 和 `seats.cpp` 已分别加入环境库与资源库。业务库不定义 `main`，演示程序不被拼接到主程序内。

`build/` 和 `outputs/` 是本地生成产物，不进入版本库。项目可从源码重建，不需要复制他人的二进制。

## 声明与实现

| 声明 | 实现 | 核心对象 |
| --- | --- | --- |
| `contracts.hpp` | 无对应业务 cpp | 共享值对象、枚举与抽象接口 |
| `environment.hpp` | `environment/environment.cpp` | `CampusEnvironment` |
| `accounts.hpp` | `environment/accounts.cpp` | 本地账号系统 |
| `students.hpp` | `students/students.cpp` | `StudentEngine`、学生与日程读取函数 |
| `resources.hpp` | `resources/resources.cpp` | `FacilityEngine` |
| `seats.hpp` | `resources/seats.cpp` | `SeatEngine` |
| `simulation.hpp` | `simulation/simulation.cpp` | `SimulationRunner` |
| `reporting.hpp` | `reporting/reporting.cpp` | `CsvRunWriter` |

表中声明位于 `include/campustwin/`，实现位于 `src/`。`.hpp` 描述外部可调用的能力，`.cpp` 实现具体算法和状态维护。`not_implemented.hpp` 是早期骨架的辅助头文件，不代表当前主程序仍使用占位业务。

## 三种不同的数据

| 类别 | 代表对象 | 何时存在 |
| --- | --- | --- |
| 输入配置 | `CampusScenario`、`StudentSpec`、`ActivitySpec` | 从 CSV 读取并在初始化时校验 |
| 运行交互 | `StudentArrival`、`ServiceStarted`、`ServiceCompleted`、`SeatRequest`、`SeatGranted` | 每分钟通过接口和事件传递 |
| 可观察结果 | `StudentSnapshot`、`FacilitySnapshot`、`SeatSnapshot`、`ActivityRecord` | 阶段末或仿真结束时记录 |

服务和座位反馈影响后续日程，因此不能把整个程序替换成“先算学生轨迹，再离线统计排队”的单向流水线。结果由真实运行计算，`data/fixtures/` 的 expected 文件只是验收参考。

## 定位问题

| 现象 | 检查位置 |
| --- | --- |
| 找不到配置或程序 | 工作目录是否在仓库根目录；程序是否在 `build/Debug/` |
| 新 cpp 没有参与构建 / undefined reference | CMake 源文件列表、声明与定义签名、链接目标 |
| 学生取餐后不进入下一状态 | 服务完成事件、是否需要座位、入座反馈和活动截止时间 |
| 等座人数或窗口计数不一致 | Runner 的守恒检查，是否漏转发或重复处理事件 |
| 下一项活动迟到 | 前项结束、出行耗时、取餐与等座耗时；查看 `activities.csv` |
| 输出存在但缺少报告 | 单次 CLI 只写 CSV；HTML 由实验脚本生成 |
| 输出目录被拒绝 | 结果保护；换一个新输出目录 |

详细接口看 [公共契约说明](04_API_CONTRACT.md)，时间顺序看 [调度流程](06_INTEGRATION.md)，验收范围看 [测试说明](../tests/README.md)。
