# CampusTwin Lite · MVP 1

CampusTwin Lite 是一个 C++17 命令行校园仿真程序。给它一份校园场景和一份学生配置，它会按分钟模拟学生从出发、前往食堂、排队取餐、就餐到离开食堂的过程，并输出每个学生的状态和食堂统计。

当前版本聚焦一次午餐行为链。仓库自带的示例包含 20 名学生、两栋教学楼、一间食堂、宿舍和图书馆；这些都是虚构数据。

## 1. 准备环境

需要：

- 支持 C++17 的编译器，如 GCC、Clang 或 Visual Studio C++ 编译工具。
- CMake 3.16 或更新版本。
- Python 3.9 或更新版本：运行测试和样例结果检查需要。仿真程序本身不依赖 Python。

Windows 建议安装 Visual Studio 或 Build Tools 的“使用 C++ 的桌面开发”工作负载，并在 PowerShell 或 Visual Studio Developer PowerShell 中确认 `git`、`cmake` 和 `py -3` 可用。WSL/Ubuntu 可安装 `git`、`g++`、`cmake`、`python3`。

下载仓库并进入项目根目录：

```bash
git clone https://github.com/3550625279-ux/campustwin-lite.git
cd campustwin-lite
```

下面所有命令都从这个目录执行。若已经下载或克隆，只需进入已有的 `campustwin-lite` 目录。

## 2. 构建并运行自带场景

### Windows：PowerShell + Visual Studio 生成器

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure

.\build\Debug\campustwin.exe --scenario data/scenarios/mvp --students data/students/mvp.csv --duration 120 --output outputs/mvp
py -3 tests/check_mvp_outputs.py outputs/mvp
```

### WSL / Linux / macOS：单配置生成器

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure

./build/campustwin --scenario data/scenarios/mvp --students data/students/mvp.csv --duration 120 --output outputs/mvp
python3 tests/check_mvp_outputs.py outputs/mvp
```

若选择了其他 CMake 生成器，可执行文件的位置可能不同：Visual Studio 通常是 `build/Debug/campustwin.exe`，单配置生成器通常是 `build/campustwin` 或 `build/campustwin.exe`。

仿真成功后会显示 `Simulation complete`，结果检查成功后显示 `PASS: actual output matches the canonical 20-student MVP scenario.`。检查脚本专门用于仓库原样例和 120 分钟的结果；改动配置后不能用它判定自定义场景。

**再次运行时请换一个输出目录**，例如 `--output outputs/mvp2`。如果目标目录中已有任一结果文件，程序会拒绝覆盖。程序报错时，已经创建的输出可能不完整。

## 3. 看懂结果

每次运行会在输出目录生成四份 CSV：

| 文件 | 内容 | 适合回答 |
| --- | --- | --- |
| `events.csv` | 学生到达食堂、开始服务、完成服务的事件及时间 | 谁在什么时候到达、等了多久 |
| `student_snapshots.csv` | 每分钟每个学生的状态、位置或移动信息 | 某个时刻学生在哪里、最终处于什么状态 |
| `facility_snapshots.csv` | 每分钟各食堂的排队人数、服务人数和累计统计 | 排队如何随时间变化 |
| `summary.csv` | 每个食堂的到达、服务、等待和窗口利用率汇总 | 整次运行的食堂表现 |

原样例运行到 120 分钟时，检查结果应当看到：

- 到达、开始服务、完成服务均为 20 人；最大队列长度为 18；平均等待时间为 8.5 分钟。
- 20 人到第 29 分钟均已回到宿舍，最终状态为 `Finished`，位置为 `L_DORM`。
- `summary.csv` 的 `completed` 指**取餐服务完成**，不能直接当作学生已吃完并返回；学生终态应看 `student_snapshots.csv`。

例如，在 Windows PowerShell 中查看 t=120 的学生终态：

```powershell
Import-Csv outputs/mvp/student_snapshots.csv | Where-Object { $_.at -eq '120' } | Select-Object student_id,state,location_id
```

在 WSL/Linux/macOS 中：

```bash
awk -F, 'NR==1 || $1==120 {print $2, $3, $4}' outputs/mvp/student_snapshots.csv
```

状态包括 `AtLocation`（在原地点）、`Moving`（移动中）、`Queued`（排队）、`InService`（接受窗口服务）、`Eating`（就餐）和 `Finished`（完成并到达配置的后续地点）。`Moving` 时 `location_id` 为空，路线信息在 `from`、`to`、`depart_at`、`arrive_at` 列。

时间以分钟计，项目约定 t=0 为 12:00。`--duration 120` 会记录 t=0 到 t=120 的 121 次快照；如果截止时有人尚未完成，最后一行仍记录其真实状态。

## 4. 换成自己的校园和学生

建议先复制示例，再修改副本，保留原始样例以便随时验证。

Windows PowerShell：

```powershell
Copy-Item -Recurse data/scenarios/mvp data/scenarios/my_campus
Copy-Item data/students/mvp.csv data/students/my_students.csv
```

WSL / Linux / macOS：

```bash
cp -R data/scenarios/mvp data/scenarios/my_campus
cp data/students/mvp.csv data/students/my_students.csv
```

场景目录需要三份文件，学生配置是单独的一份文件：

| 文件 | 表头 | 作用 |
| --- | --- | --- |
| `locations.csv` | `id,name,kind,capacity` | 定义教学楼、食堂、宿舍等地点 |
| `roads.csv` | `from,to,distance_m` | 定义两个地点之间的无向道路和米数 |
| `facilities.csv` | `id,location_id,window_count,service_minutes` | 定义食堂地点、窗口数和每人服务时长 |
| `my_students.csv` | `id,initial_location,lunch_facility,after_meal_location,departure_minute,walk_m_per_min,meal_minutes` | 定义每个学生的午餐行程 |

例如，学生文件中的一行：

```csv
S001,L_T1,F_CANTEEN,L_DORM,0,60,5
```

表示 S001 在 t=0 从 `L_T1` 出发，以 60 米/分钟走向 `F_CANTEEN`；取餐完成后就餐 5 分钟，再前往 `L_DORM`。食堂的窗口数和服务时间从场景的 `facilities.csv` 读取，行走距离由 `roads.csv` 中的最短路径决定。

编辑时请注意：

- 使用 UTF-8 **无 BOM**，保留表头和列顺序。CSV 字段不支持逗号、双引号或换行；学生文件字段不能含空白。
- 学生、地点、设施 ID 使用英文字母、数字、下划线或连字符，且各自唯一；地点名称可用中文。
- 地点类型为 `Teaching`、`Canteen`、`Dormitory` 或 `Library`。设施须指向存在的 `Canteen` 地点，每个食堂地点最多配置一个设施，设施 ID 与地点 ID 不能相同。
- 道路的两个端点必须存在，距离必须大于 0；道路双向通行，不要重复写反向边。学生起点和后续地点必须能到达其食堂，且不能与该食堂地点相同。
- 学生引用的地点和设施必须存在；出发分钟为非负整数，步行速度大于 0，就餐分钟为正整数。窗口数为 1–100000，服务时长为正整数。
- `capacity` 目前只是地点描述字段，**尚不会限制人数**；决定取餐排队能力的是窗口数和服务时长。

编辑好副本后运行：

Windows：

```powershell
.\build\Debug\campustwin.exe --scenario data/scenarios/my_campus --students data/students/my_students.csv --duration 120 --output outputs/my_run
```

WSL / Linux / macOS：

```bash
./build/campustwin --scenario data/scenarios/my_campus --students data/students/my_students.csv --duration 120 --output outputs/my_run
```

路径含空格时请加引号。参数也可以省略：`--scenario`、`--students`、`--duration`、`--output` 默认依次为 `data/scenarios/mvp`、`data/students/mvp.csv`、`120`、`outputs/mvp`。运行 `campustwin --help` 可查看选项。

### 一个对照实验：减少食堂窗口

在复制出的 `data/scenarios/my_campus/facilities.csv` 中，把样例行的窗口数从 2 改为 1：

```csv
id,location_id,window_count,service_minutes
F_CANTEEN,L_CANTEEN,1,2
```

保留其他样例输入并运行 120 分钟，输出到新的 `outputs/my_run`。此时 20 人都能完成取餐，最大队列长度应为 19，总等待时间为 370 人·分钟，平均等待为 18.5 分钟。与原来的 2 个窗口相比，可以在 `summary.csv` 中看到等待增加，也可以在 `student_snapshots.csv` 中观察学生状态变化。

输入字段和输出列的完整说明见 [数据格式](docs/05_DATA_FORMAT.md)；固定样例的手算过程见 [验收用例](docs/07_ACCEPTANCE.md)。

## 5. 当前范围与其他入口

完整仿真入口是 `campustwin`：它读取 CSV 并串联校园路线、学生状态、食堂排队和结果输出。当前支持固定午餐行程，不支持任意课程表、完整一天、图形地图或联网；地点容量不参与人数限制。统一仿真入口也不包含账号登录或角色权限。

另外有三个模块独立演示：`demo_a` 是带本地账号的场景编辑/路线查询控制台，`demo_b` 使用测试路线和服务事件演示学生行为，`demo_c` 演示食堂队列。它们用于查看单个模块，不能代替完整仿真。各入口使用 `--help` 查看参数；细节见 [A 模块](docs/A_DELIVERY.md)、[B 模块](docs/11_B_STUDENT_MVP.md) 和 [C 模块](docs/12_C_RESOURCE_MVP.md)。

主要目录：`app/` 为完整程序入口，`src/` 和 `include/` 为仿真模块及接口，`data/` 为样例输入，`tests/` 为自动测试，`docs/` 为详细设计与验收文档。部分早期文档保留了项目规划过程，运行行为以当前代码和本说明为准。
