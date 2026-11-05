# CampusTwin Lite

基于 C++17 的校园午餐出行与服务仿真项目。学生从不同地点出发，可以选择固定、最近或预计最快的食堂；程序模拟出行、排队、窗口服务、就餐和返回，并记录路线、选择依据与实际结果。

**当前提供四个可运行场景：**原始 20 人单食堂 MVP、40 人双食堂窗口/速度对照、60 人动态选食堂，以及 60 人三食堂餐品与窗口服务场景。实验可生成 CSV 和离线 HTML 图表报告。所有场景都是合成数据。

程序由校园环境、学生行为和食堂资源三个模块组成，由统一调度器连接。动态决策使用出发前的队列和窗口快照；预测不包含未来到达者。暂不支持自行车、中途换食堂或完整一天的日程。

A 支持本地账号、场景编辑、CSV 保存与加载以及最短路径查询，详见 [A 部分交付说明](docs/A_DELIVERY.md)。B 支持学生数据读取、移动、服务事件响应和状态快照，详见 [B 模块实现与使用](docs/11_B_STUDENT_MVP.md)。

## 第二阶段：窗口分工对照

学生可以有餐品需求；每个窗口可以提供不同餐品并使用不同服务时间。下列实验在相同学生、路线和窗口数量下，对比专用窗口与通用窗口：

```bash
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
python3 tools/run_service_experiments.py --executable build/campustwin --output outputs/service_demo
```

Windows PowerShell（Ninja/MinGW）：

```powershell
py -3 tools/run_service_experiments.py --executable .\build\campustwin.exe --output .\outputs\service_demo
Start-Process .\outputs\service_demo\report.html
```

输出目录必须不存在；Visual Studio 生成器的可执行文件通常位于 `build/Debug/`。完整配置、匹配规则、结果和模型边界见 [餐品与窗口服务指南](docs/15_COMPLEX_SERVICES.md)。

## 快速运行动态选食堂实验

在仓库根目录构建后，运行固定食堂、最近食堂和预计最快三种策略的对照：

```bash
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
python3 tools/run_dynamic_experiments.py --executable build/campustwin --output outputs/dynamic_demo
```

Windows PowerShell（Ninja/MinGW 单配置构建）：

```powershell
py -3 tools/run_dynamic_experiments.py --executable .\build\campustwin.exe --output .\outputs\dynamic_demo
Start-Process .\outputs\dynamic_demo\report.html
```

Visual Studio 生成器通常使用 `.\build\Debug\campustwin.exe`。输出目录必须尚不存在；每组保留输入副本、原始结果和候选决策。策略、预测假设、结果口径及逐人示例见 [动态午餐实验指南](docs/14_DYNAMIC_LUNCH.md)。

## 运行窗口和步行速度对照

需要 C++17 编译器、CMake 3.16+ 和 Python 3.9+；运行和报告不需要额外 Python 包。在仓库根目录执行：

```bash
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
python3 tools/run_lunch_experiments.py --executable build/campustwin --output outputs/lunch_demo
```

最后一条适用于 Linux/macOS 的单配置构建。Windows PowerShell 使用对应的程序路径，例如 Visual Studio 生成器：

```powershell
py -3 tools/run_lunch_experiments.py --executable .\build\Debug\campustwin.exe --output .\outputs\lunch_demo
Start-Process .\outputs\lunch_demo\report.html
```

使用 Ninja/MinGW 等单配置生成器时，Windows 可执行文件一般是 `.\build\campustwin.exe`。Linux/macOS 可在文件管理器中打开 `outputs/lunch_demo/report.html`。实验会保留每组输入和原始结果；**输出目录必须不存在**，再次运行请换一个目录。

默认实验包括基准、近食堂 2→3 个窗口、全部学生速度 60→120 米/分钟三组；比较时只有指定变量改变。报告列出各食堂的等待、排队、利用率、出发到开始吃饭所需时间，并展示实际路线和逐分钟队列。详细说明、指标口径及自定义参数见 [双食堂实验指南](docs/13_LUNCH_EXPERIMENTS.md)。

也可以只查一条路线，输出由 A 模块计算的距离与路径：

```bash
./build/campustwin --scenario data/scenarios/two_canteens --route L_T1 L_FAR
```

输出为 `L_T1 → L_LIBRARY → L_FAR`，总距离 240 米。Windows 把可执行文件换成上面对应的 `.exe` 路径即可。

## 第一次打开仓库

先读 [START_HERE.md：成员开工指南](START_HERE.md)，再读 [仓库结构详细讲解](docs/00_REPOSITORY_GUIDE.md)。前者告诉你怎样开始，后者解释每层文件的用途、依赖和一次真实运行如何经过这些模块。

如果只想先建立整体认识：**A 提供校园和路线，B 决定学生状态与移动，C 决定排队与服务；调度器让它们按同一时钟交互。** `examples/` 用于独立演示，`app/` 用于最后集成；不是让三人各交一份互不相干的程序。

## 先读什么

| 目的 | 文件 |
| --- | --- |
| 第一次加入项目，完成首次构建和提交 | [成员开工指南](START_HERE.md) |
| 逐层理解文件、依赖、运行流与修改范围 | [仓库结构详细讲解](docs/00_REPOSITORY_GUIDE.md) |
| 审阅这次初始化方案、确认待定事项 | [仓库初始化审阅单](REVIEW.md) |
| 知道第一阶段最终要演示什么 | [MVP 范围](docs/01_MVP.md) |
| 理解三个子系统、统一时钟与状态所有权 | [架构设计](docs/02_ARCHITECTURE.md) |
| 看各自任务、目录、难点和交付要求 | [三人任务书](docs/03_TEAM_TASKS.md) |
| 运行、理解和验收 B 学生模块 | [B 模块实现与使用](docs/11_B_STUDENT_MVP.md) |
| 运行、理解和验收 C 资源模块 | [C 模块实现与使用](docs/12_C_RESOURCE_MVP.md) |
| 按同一份类型与函数接口开发 | [接口契约](docs/04_API_CONTRACT.md) / [公共头文件](include/campustwin/contracts.hpp) |
| 读写配置与仿真结果 | [数据格式](docs/05_DATA_FORMAT.md) |
| 把三个模块接成一个程序 | [集成与调度步骤](docs/06_INTEGRATION.md) |
| 判断功能是否真正完成 | [验收用例](docs/07_ACCEPTANCE.md) |
| 看本次实际检查范围 | [初始化验证记录](docs/10_VALIDATION.md) |
| 安排第一次提交到第一次闭环 | [阶段计划](docs/08_ROADMAP.md) |
| 核查课程要求和面向对象设计 | [课程要求对应](docs/09_COURSE_REQUIREMENTS.md) |
| 学会分支、push、PR 与合并 | [贡献指南](CONTRIBUTING.md) |
| 对照最初的讨论 | [原始分工 v0.1](docs/reference/original_v0.1.md) |

## 原始 MVP 基准

固定 20 名学生、2 栋教学楼、1 间食堂、1 栋宿舍、1 座图书馆；先完成一次午餐行为链：

**下课 → 选择固定食堂 → 按距离和速度移动 → 排队 → 窗口服务 → 就餐 → 返回宿舍。**

首版用控制台、CSV 配置与结果；不做图形地图、联网、数据库或复杂决策。样例数据是自拟仿真输入，不是真实校园测量数据。

## 编译与测试

需要 C++17 编译器、CMake 3.16+；测试数据还需要 Python 3.9+。运行库只依赖 C++ 标准库。

```bash
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

只编译、不运行样例检查时，可在配置命令加 `-DBUILD_TESTING=OFF`，这样无需 Python；功能完成后的正式验收仍需按文档执行。

构建目标：`campustwin`、`demo_a`、`demo_b`、`demo_c`。CTest 包含 A/B/C 独立测试、记录器测试和真实的三模块联调测试，覆盖固定基准、窗口与道路变化、提前截止及重复运行。

从仓库根目录运行 B 的独立演示：

```bash
./build/demo_b --students data/students/mvp.csv --student S001 --wait 3 --duration 20
```

演示读取 20 人配置后仅运行 S001：t=2 到食堂、等待 3 分钟、t=5 开始服务、t=7 开始就餐、t=12 返回、t=14 到宿舍。`--help` 查看选项。这个演示使用路线和服务事件测试替身，不调用真实 A、C 模块，也不写集成 CSV。

将 `--student S001` 换成 `--all` 可以独立显示所有学生的轨迹；每人的服务由测试脚本单独生成，不模拟真实共享队列。

独立演示 C 的单窗口排队（事件 CSV 输出到终端）：

```bash
./build/demo_c --facilities data/fixtures/resource_facilities.csv --arrivals data/fixtures/resource_arrivals.csv --duration 6
```

结果中的 6 条开始/完成事件应与 `data/fixtures/resource_expected_events.csv` 一致；该文件只是手算参考，程序不会读取它。

Linux/macOS 单配置生成器的入口一般在 `build/`；Windows Visual Studio 生成器一般在 `build/Debug/`。VS Code 或 CLion 可以打开仓库根目录的 CMake 工程。

## 目录与归属

| 目录 | 内容 | 维护人 |
| --- | --- | --- |
| `include/campustwin/contracts.hpp` | 唯一公共数据类型与接口 | 三人共同评审 |
| `include/campustwin/environment.hpp`, `src/environment/` | 场景、校验、路线与场景文件 | A |
| `include/campustwin/students.hpp`, `src/students/` | 学生日程、状态机、移动与学生文件 | B |
| `include/campustwin/resources.hpp`, `src/resources/` | 队列、窗口、服务与统计 | C |
| `include/campustwin/reporting.hpp`, `src/reporting/` | CSV 结果记录 | C |
| `include/campustwin/simulation.hpp`, `src/simulation/`, `app/` | 薄调度器、控制台入口 | A，三人联调 |
| `examples/`, `tests/`, `data/` | 独立演示、验收与样例 | 各模块负责人维护对应部分 |
| `docs/`, `.github/` | 设计、协作与自动构建 | 三人共同维护 |

每人向功能分支 push，完成一个小任务后开 PR 合入 `main`；三个子系统始终保留独立演示入口。合并是编译和接口层面的合并，不是把三份 `main.cpp` 复制到一起。

## 运行完整仿真

从仓库根目录执行：

```bash
./build/campustwin --scenario data/scenarios/mvp --students data/students/mvp.csv --duration 120 --output outputs/mvp
python3 tests/check_mvp_outputs.py outputs/mvp
```

运行成功后，输出目录包含 `events.csv`、`student_snapshots.csv`、`facility_snapshots.csv`、`summary.csv` 和新增的 `decisions.csv`。程序拒绝覆盖已有的结果文件；再次运行请指定新目录。若运行报错，已生成的文件可能不完整，不能作为成功结果。`--help` 可查看默认参数。

A 的 `demo_a` 提供本地账号和场景编辑；统一仿真 CLI 读取配置并运行，不提供账号角色权限。角色权限、完整一天、更多设施和课程最终提交材料仍待后续完成与人工核对。
