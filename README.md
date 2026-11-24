# CampusTwin Lite

CampusTwin Lite 是一个 C++17 校园仿真程序。学生按日程在教学楼、食堂、图书馆和宿舍之间移动。午餐排队、取餐和等座造成的延迟会传到下一项活动。交互式终端工作台可以回放任意一分钟、查看学生当天的轨迹，以及对比不同午餐出发安排。

## 快速开始

从 GitHub 克隆仓库，在根目录执行以下命令。展示程序需要支持 C++17 的编译器和 CMake 3.16 或更新版本。下例使用 Ninja，构建和运行展示版无需 Python。

```powershell
git clone https://github.com/3550625279-ux/campustwin-lite.git
cd campustwin-lite
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --target campustwin
.\build\campustwin.exe
```

Windows 也可以双击 [run_ui.bat](run_ui.bat)。使用 Visual Studio 多配置生成器时，去掉 `-G Ninja -DCMAKE_BUILD_TYPE=Release`，构建时加 `--config Release`，程序位于 `build/Release/campustwin.exe`。Linux/macOS 使用 `./build/campustwin`。切换生成器时请使用干净的构建目录。

## 展示版

启动后先注册或登录本地账号，直接进入工作台。旗舰校园默认使用 40 分钟错峰。三个方案使用同一批 **9000 名学生**，只改变午餐计划出发时间。旗舰校园有 **6 座食堂、216 个取餐窗口、24 处座位池**，仿真覆盖 **08:00–18:00**。

| `4 → 1` 单方案选项 | 午餐计划出发 |
| --- | --- |
| `1` 集中出发 | 12:00 |
| `2` 20 分钟错峰 | 12:00–12:19 |
| `3` 40 分钟错峰 | 12:00–12:39，回车也选此方案 |

主菜单 `4 → 1` 选择表中的一个方案并直接打开回放。`4 → 2` 查看三个方案和 **11:10–13:11** 宽错峰参照的汇总及逐人对照。回到主菜单后，`2` 打开当前场景的看板，`3` 查看当前方案的学生日程。`1 → 1` 恢复旗舰默认场景，`1 → 2` 导入自定义场景。`7` 退出登录并返回登录/注册页，`0` 关闭程序。

看板中的 `n` 前进一分钟，`g 240` 跳到 12:00，`k` 跳到全校资源等待总人数最多的一分钟，`r` 展开场景，`v S9000` 查看指定学生。引擎每步推进一个校园分钟，自动回放默认 1:1，速度可在 `1 → 3` 调整。实验的午餐时段约束的是**计划出发时间**，排队和吃饭可以持续到时段结束以后。

## 个人仿真结果

首次进入看板、学生档案或实验对照时，工作台会查找当前账号的本地结果。同一地图、学生、日程、策略和仿真时长再次打开时，会显示「已读取本地仿真结果」。更换账号或修改输入时会保存一份独立结果，损坏的缓存会重新计算。

默认账号文件是 `data/private/accounts.db`，每个账号在 `data/private/accounts.db.users/` 下有自己的目录，其中 `runs/` 保存回放数据，`scenarios/` 保存本人编辑的场景。账号目录使用用户名编码，以区分 Windows 上仅大小写不同的账号。本地登录用于区分应用内的数据工作区，文件访问仍由操作系统账号管理。该目录已从 Git 排除。

## 自定义输入与批处理

一次仿真读取一份校园目录和一份学生、日程配置。可以从 [样例数据](data/README.md) 复制配置再修改。

在工作台打开 `1 → 2`，屏幕会显示当前账号的 `imports` 目录。用资源管理器在其中新建一个子文件夹，文件夹名就是列表里显示的场景名。把下表的必需 CSV 放进该子文件夹，再进入 `1 → 2` 选择场景编号即可加载。输入 `0` 返回主菜单。

| 输入 | 内容 |
| --- | --- |
| `locations.csv`、`roads.csv` | 校园地点及双向道路距离 |
| `facilities.csv`、可选的 `window_services.csv` | 食堂与窗口数量，窗口可供应的餐品与服务时长 |
| 可选的 `seats.csv` | 地点的实际座位容量 |
| `students.csv`、`schedule.csv` | 学生初始位置、步速、餐品、选食堂策略及按序执行的活动。每人需要一份日程，包含一次午餐。沿用样例时，将学生和日程 CSV 分别复制为这两个文件名 |

不经过工作台也可以通过 CLI 运行任意场景。以下命令将 36 人样例的结果写入新目录。

```powershell
.\build\campustwin.exe --scenario data/scenarios/campus_day --students data/students/campus_day.csv --schedule data/schedules/campus_day.csv --duration 160 --output outputs/campus_day
```

仿真输出八份 CSV，分别记录学生、窗口和座位的快照，窗口与座位事件、选食堂决策、活动计划与实绩，以及食堂统计。详细字段见 [数据格式](docs/05_DATA_FORMAT.md) 和 [日程与座位](docs/16_ACTIVITY_SCHEDULES.md)。

## 源码与验证

| 路径 | 职责 |
| --- | --- |
| [src/environment](src/environment) | 读写校园配置，计算最短路 |
| [src/students](src/students) | 推进学生状态和活动日程，按策略选食堂 |
| [src/resources](src/resources) | 分配取餐窗口和座位，维护等待队列 |
| [src/simulation](src/simulation)、[src/reporting](src/reporting) | 按分钟调度，核对状态，记录事件 |
| [src/tui](src/tui) | 午餐方案选择、逐分钟回放、学生档案和方案对照 |

使用 Python 3.9+ 运行集成校验。Windows 和 Linux 的 CI 执行相同的 CTest 套件。

```powershell
cmake -S . -B build-test -DBUILD_TESTING=ON
cmake --build build-test --config Debug
ctest --test-dir build-test -C Debug --output-on-failure
```

[代码导览](docs/00_REPOSITORY_GUIDE.md) · [接口契约](docs/04_API_CONTRACT.md) · [测试说明](tests/README.md)

样例使用合成数据。路径模型按最短距离和学生步速计算移动时间，`fastest` 根据上一分钟的窗口状态估计取餐耗时。仿真可用于比较配置在同一模型下的影响。
