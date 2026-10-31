# CampusTwin Lite — MVP 1

CampusTwin Lite 是一个 C++17 校园行为仿真项目。当前提供命令行程序：读取校园场景和学生配置，在同一时间轴上模拟学生移动、食堂排队、窗口服务、就餐和返回，最后输出 CSV 结果。

仓库自带样例有 20 名学生、两栋教学楼、一间两窗口食堂和一栋宿舍。学生从教学楼出发，完成午餐后回到宿舍。样例数据是人为构造的，并非真实校园测量数据。

**目前没有图形地图或动画界面。** 运行成功后，终端只显示完成提示；学生轨迹、排队事件和统计数据保存在 CSV 文件里。

## 1. 准备环境

需要：

- Git、CMake 3.16 或更新版本。
- 支持 C++17 的编译器：Windows 可使用 Visual Studio C++ 工具链，或 MSYS2 MinGW-w64 的 GCC；Linux/macOS 可使用对应平台的 C++ 编译器。
- Python 3.9 或更新版本：用于自动测试和样例结果检查。程序运行本身不依赖 Python。
- 若使用 Ninja 生成器，需要安装 Ninja 并确保它可从终端运行。

确保编译器、CMake 和 Python 已安装并能被当前终端找到。例如可通过 `cmake --version`、`python --version` 检查；使用 GCC 时可通过 `g++ --version` 检查。Windows 上也可用 `py -3 --version` 检查 Python。

获取代码并进入仓库根目录：

```powershell
git clone https://github.com/3550625279-ux/campustwin-lite.git
cd campustwin-lite
```

下面所有相对路径和命令都以**仓库根目录**为当前目录。已经克隆的用户无需再次克隆。

## 2. 编译与自动测试

Windows PowerShell、Linux/macOS 终端均可执行：

```text
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

首次配置时，留意输出中的 `Building for: ...`。构建完成后，CTest 应显示 `100% tests passed`。集成测试会在临时目录运行真实程序并检查结果；它不会在仓库的 `outputs/mvp` 下留下这次演示的文件，需要按下一节手动运行。

普通 `Ninja` 和 Makefiles 是单配置生成器，构建时的 `--config Debug` 不会选择构建类型；需要调试配置时，可在配置阶段使用 `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`。Visual Studio 和 `Ninja Multi-Config` 则使用构建时的 `--config Debug`。切换生成器时请使用新的构建目录，以免与现有 CMake 缓存冲突。

## 3. 运行完整样例

**Windows PowerShell：根据实际生成器选择一条命令。**

配置输出为 `Building for: Ninja` 时：

```powershell
.\build\campustwin.exe --scenario data/scenarios/mvp --students data/students/mvp.csv --duration 120 --output outputs/mvp
```

使用 Visual Studio 或 `Ninja Multi-Config`，并按上面的命令构建 Debug 时：

```powershell
.\build\Debug\campustwin.exe --scenario data/scenarios/mvp --students data/students/mvp.csv --duration 120 --output outputs/mvp
```

如果不确定可执行文件在哪，可以在 PowerShell 中运行：

```powershell
Get-ChildItem .\build -Recurse -Filter campustwin.exe | Select-Object -ExpandProperty FullName
```

**Linux/macOS：**使用普通 Ninja 或 Makefiles 时：

```bash
./build/campustwin --scenario data/scenarios/mvp --students data/students/mvp.csv --duration 120 --output outputs/mvp
```

运行成功后会显示类似：

```text
Simulation complete: 20 students, 120 minutes; four CSV files written to outputs/mvp
```

参数含义：`--scenario` 指定包含校园 CSV 的目录；`--students` 指定学生 CSV；`--duration` 指定仿真时长（分钟）；`--output` 指定结果目录。使用相应的可执行文件运行 `--help` 可查看默认参数。

程序不会覆盖结果目录里已有的同名结果文件。再次运行时，请把 `--output outputs/mvp` 改成一个新目录，如 `--output outputs/mvp2`；后续查看和检查时也使用该新目录。如果运行中报错，已产生的文件可能不完整，不能当作成功结果。

## 4. 查看与理解结果

程序在 `outputs/mvp/` 生成四份 CSV：

| 文件 | 内容 | 适合回答的问题 |
| --- | --- | --- |
| `events.csv` | 到达食堂、开始窗口服务、完成窗口服务等事件 | 学生什么时候到达、等了多久？ |
| `student_snapshots.csv` | 每名学生每分钟的状态、地点或行程 | 某人在某一分钟正在做什么？最后到了哪里？ |
| `facility_snapshots.csv` | 食堂每分钟的队列、服务人数和累计统计 | 排队人数怎样随时间变化？ |
| `summary.csv` | 每个设施的总体统计 | 总共服务多少人？平均等待多久？ |

在 Windows PowerShell 中打开结果文件夹，可以用表格软件或文本编辑器查看 CSV：

```powershell
Invoke-Item .\outputs\mvp
```

直接在 PowerShell 查看食堂汇总：

```powershell
Import-Csv .\outputs\mvp\summary.csv | Format-List
```

查看所有学生在仿真结束时（`t=120`）的状态和地点：

```powershell
Import-Csv .\outputs\mvp\student_snapshots.csv |
  Where-Object { $_.at -eq '120' } |
  Select-Object student_id, state, location_id |
  Format-Table -AutoSize
```

查看 S020 在关键时刻的状态：

```powershell
Import-Csv .\outputs\mvp\student_snapshots.csv |
  Where-Object { $_.student_id -eq 'S020' -and $_.at -in @('0', '3', '20', '22', '27', '29') } |
  Select-Object at, student_id, state, location_id, from, to |
  Format-Table -AutoSize
```

样例中 S020 在 t=3 到达并排队，t=20 开始服务，t=22 开始就餐，t=27 离开食堂，t=29 返回宿舍。`Moving` 是移动中，`Queued` 是排队，`InService` 是接受窗口服务，`Eating` 是就餐，`Finished` 是该学生的午餐流程结束。移动时 `location_id` 为空，行程记在 `from` / `to` 列。

默认样例的 `summary.csv` 应包含：到达、开始服务、完成服务各 20 人；最大阶段末队列为 18 人；总等待 170 人·分钟，平均等待 8.5 分钟；两个窗口合计忙碌 40 窗口·分钟，120 分钟观察期内的利用率为 `40 / (2 × 120) = 16.67%`。`completed` 统计的是**完成窗口服务**，不直接表示已经吃完并回宿舍；后者应查看学生快照。

## 5. 检查样例结果

先成功运行第 3 节，再在仓库根目录执行：

```powershell
py -3 tests/check_mvp_outputs.py outputs/mvp
```

如果没有 `py` 启动器，可使用能运行 Python 3 的 `python` 命令。Linux/macOS 通常使用：

```bash
python3 tests/check_mvp_outputs.py outputs/mvp
```

预期输出：`PASS: actual output matches the canonical 20-student MVP scenario.`

这个检查器只适用于仓库自带的 20 人、120 分钟固定样例。修改学生、道路、窗口数或时长后，应按自己的场景检查输出，不要期待它通过固定样例的精确校验。更多基准参见[验收用例](docs/07_ACCEPTANCE.md)。

## 6. 修改输入并运行自己的场景

| 输入 | 文件 | 可以配置的内容 |
| --- | --- | --- |
| 校园地点 | `data/scenarios/mvp/locations.csv` | 地点 ID、名称、类型和容量 |
| 校园道路 | `data/scenarios/mvp/roads.csv` | 地点之间的道路和距离 |
| 设施 | `data/scenarios/mvp/facilities.csv` | 食堂位置、窗口数和服务时间 |
| 学生 | `data/students/mvp.csv` | 起点、食堂、饭后地点、出发时间、速度和就餐时长 |

建议复制样例场景目录和学生文件，修改副本后，通过 `--scenario` 与 `--students` 指向它们，并用新 `--output` 保存结果。地点和设施 ID 必须在相应文件之间对应；完整字段与约束见[数据格式](docs/05_DATA_FORMAT.md)。当前的地点容量为描述性配置，MVP 不按地点容量限制进入人数。

## 7. 其他文档

- [项目结构和一次运行的流程](docs/00_REPOSITORY_GUIDE.md)
- [架构设计](docs/02_ARCHITECTURE.md)与[公共接口](docs/04_API_CONTRACT.md)
- [A：环境与场景编辑](docs/A_DELIVERY.md)、[B：学生引擎](docs/11_B_STUDENT_MVP.md)、[C：资源引擎](docs/12_C_RESOURCE_MVP.md)
- [验收用例与手算基准](docs/07_ACCEPTANCE.md)、[贡献指南](CONTRIBUTING.md)

各模块也提供 `demo_a`、`demo_b`、`demo_c` 独立入口，使用方法见对应文档。统一仿真程序只读取配置并运行；本地账号和场景编辑入口属于 A 的独立演示。课程最终提交所需的角色权限等要求仍需团队核对。
