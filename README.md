# CampusTwin Lite

基于 C++17 的校园群体行为与食堂资源仿真课程项目。三位成员分别实现校园环境、学生行为、资源服务，再在同一进程、同一时间轴内集成。

**当前状态：初始化阶段。已有设计文档、公共头文件、可编译占位工程、样例配置和验收工具；A/B/C 业务与调度代码尚未实现，MVP 尚未完成。** 构建检查通过只表示骨架可编译、样例数据合法，不代表仿真已经跑通。

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
| 按同一份类型与函数接口开发 | [接口契约](docs/04_API_CONTRACT.md) / [公共头文件](include/campustwin/contracts.hpp) |
| 读写配置与仿真结果 | [数据格式](docs/05_DATA_FORMAT.md) |
| 把三个模块接成一个程序 | [集成与调度步骤](docs/06_INTEGRATION.md) |
| 判断功能是否真正完成 | [验收用例](docs/07_ACCEPTANCE.md) |
| 看本次实际检查范围 | [初始化验证记录](docs/10_VALIDATION.md) |
| 安排第一次提交到第一次闭环 | [阶段计划](docs/08_ROADMAP.md) |
| 核查课程要求和面向对象设计 | [课程要求对应](docs/09_COURSE_REQUIREMENTS.md) |
| 学会分支、push、PR 与合并 | [贡献指南](CONTRIBUTING.md) |
| 对照最初的讨论 | [原始分工 v0.1](docs/reference/original_v0.1.md) |

## 第一阶段的产品

固定 20 名学生、2 栋教学楼、1 间食堂、1 栋宿舍、1 座图书馆；先完成一次午餐行为链：

**下课 → 选择固定食堂 → 按距离和速度移动 → 排队 → 窗口服务 → 就餐 → 返回宿舍。**

首版用控制台、CSV 配置与结果；不做图形地图、联网、数据库或复杂决策。样例数据是自拟仿真输入，不是真实校园测量数据。

## 编译骨架

需要 C++17 编译器、CMake 3.16+；测试数据还需要 Python 3.9+。运行库只依赖 C++ 标准库。

```bash
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

只编译、不运行样例检查时，可在配置命令加 `-DBUILD_TESTING=OFF`，这样无需 Python；功能完成后的正式验收仍需按文档执行。

构建目标：`campustwin`、`demo_a`、`demo_b`、`demo_c`。当前四个入口运行均会明确提示 `NOT_IMPLEMENTED` 并返回退出码 2，这是待实现标记。未来业务验收要求退出码 0，不能把占位入口算作完成。

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

## 完成后的运行约定

下面是需要实现的 CLI 契约，**当前占位程序尚不支持这些参数**：

```bash
./build/campustwin --scenario data/scenarios/mvp --students data/students/mvp.csv --duration 120 --output outputs/mvp
python3 tests/check_mvp_outputs.py outputs/mvp
```

验收通过后才在 README 把状态改为“MVP 已完成”。第一阶段之后再补账号与权限、完整一天、更多设施和对照实验。课程最终交付要求见对应文档。
