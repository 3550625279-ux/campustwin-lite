# 第三阶段：多活动日程、座位与三阶段交付

## 现在可以运行什么

学生可以执行“上课 → 午餐 → 上课 → 图书馆学习 → 研讨 → 回宿舍”的有序日程。A 计算每段最短路，B 决定何时出发、等待和推进活动，C 管理取餐窗口以及食堂/图书馆座位。前一个活动耗时过长时，后续活动推迟，程序保留计划时刻、实际时刻和延迟。

`data/scenarios/campus_day` 包含 16 个地点、38 条无向道路、3 个食堂、9 个异质窗口、6 组座位；36 名学生每人 6 项活动，共 216 项。地图、人数、餐品和日程均为合成数据。

一次生成原 MVP 验收、双食堂实验以及三个阶段的报告：

```bash
python3 tools/run_all_stages.py --executable build/campustwin --output outputs/all_stages
```

单独生成日程对照：

```bash
python3 tools/run_schedule_experiments.py --executable build/campustwin --output outputs/schedules
```

Windows PowerShell（Ninja/MinGW）用 `py -3` 和 `.\build\campustwin.exe`；Visual Studio 生成器通常使用 `.\build\Debug\campustwin.exe`。打开对应输出目录的 `report.html`。所有报告离线可用，不需要服务器或网络；输出目录必须不存在。

单次运行自定义日程：

```bash
./build/campustwin --scenario data/scenarios/campus_day --students data/students/campus_day.csv --schedule data/schedules/campus_day.csv --duration 160 --output outputs/day
```

## A：地图与座位配置

已有地点、道路和窗口 CSV 格式不变，Dijkstra 仍计算最短距离，移动时间为 `ceil(距离 / 步行速度)`。一段移动按总耗时推进，尚未逐道路更新位置；同一地点之间无需移动。

场景可以新增 `seats.csv`：

```csv
location_id,capacity
L_NEAR,3
L_FAR,3
L_WEST,3
L_LIBRARY,2
L_LIBRARY_E,2
L_LIBRARY_W,2
```

每个已有地点最多一组座位，容量为 1–100000 的整数。不提供该文件或只有表头时，没有有限座位资源。`locations.csv.capacity` 仍是描述字段，不自动充当座位数；只有 `seats.csv` 启用约束。A 加载、校验、保存此文件，非法替换不改变原场景。

每次运行选择一个场景目录。对照脚本保留基准地图，并生成另一份道路地图 `runs/road_detour/input/scenario`：移除 `L_T1–L_NEAR`、`L_LAB–L_WEST`、`L_LIBRARY_E–L_FAR` 三条无向道路，其余配置一致。每次出发都会重新查询对应地图的最短路；不存在运行中自动封路或跨地图传送。

## B：日程输入与状态

学生 CSV 继续兼容 7、8、9 列。新增独立 `--schedule FILE`，每位学生必须有日程：

```csv
student_id,sequence,activity_id,kind,location_id,start_minute,duration_minutes,requires_seat
S001,1,morning,class,L_T1,0,12,0
S001,2,lunch,lunch,,12,6,0
S001,3,afternoon,class,L_T3,32,10,0
S001,4,reading,study,L_LIBRARY,48,10,1
S001,5,home,rest,L_DORM_N,80,2,0
```

规则：

- `sequence` 在每人内部从 1 连续编号，文件行可以乱序；`activity_id` 在每人内部唯一，使用 ASCII 字母、数字、下划线或连字符。
- `kind` 为 `class`、`study`、`rest`、`lunch`。前三者分别绑定 Teaching、Library、Dormitory 地点。每人必须恰好有一个午餐，可配置任意多项其他活动。
- 计划时间使用从场景起点开始的非负整数分钟，按日程序号不递减；时长为正整数。CSV 不支持引号、字段空白或 BOM。
- 普通活动的 `start_minute` 是希望在目的地开始的时间。B 尽量在 `计划开始 − 行走耗时` 出发；若上一项尚未结束，则结束后立即赶路。到早了原地等，到晚了立即开始，仍执行完整时长，不自动跳课。
- 午餐的 `start_minute` 是最早出发时间；到时使用学生的 `fixed/nearest/fastest` 策略和餐品需求选食堂。午餐地点留空，`requires_seat=0`，但选中食堂如配置座位会自动申请。午餐时长指用餐时长，不含移动、窗口排队、取餐和等座。
- 普通活动 `requires_seat=1` 时，相应地点必须有座位池；到达且达到计划开始时间后申请。`0` 表示这项活动不消耗座位，不会因为同地点配置了座位而自动占用。
- 启用日程后，其午餐行覆盖学生 CSV 的 `departure_minute`、`meal_minutes`；不再使用 `after_meal_location` 自动返程，最终位置由最后一项活动决定。为兼容旧格式，学生 CSV 中这三列仍必须合法，`after_meal_location` 仍需指向已有地点。

新增状态 `WaitingSeat` 和 `InActivity`。普通活动结束后回到 `AtLocation` 规划下一项；只有最后一项完成才变为 `Finished`。等待座位期间没有占座；就餐或需座活动期间持有一张座位，到结束时释放。

午餐活动的 `started_at` 记录实际出发；其他活动记录实际开始执行。因而午餐的 `start_delay_minutes` 只代表前序活动导致的出发延迟，不把取餐等待当作“午餐迟到”。就餐开始时间可在 `Eating` 状态或 `SeatGranted` 事件中读取。

## C：座位调度与时间顺序

窗口仍按第二阶段的餐品兼容匹配工作。座位是独立资源：拿到餐不代表已经入座，窗口不会因学生等座而被占住。

每分钟按以下次序处理：

1. 冻结上一分钟窗口快照；释放已完成窗口并向 B 发送完成事件。
2. C 释放到期座位；B 推进到达、活动结束、出发和座位请求。
3. C 入窗口队列、分配窗口，B 接收开始服务事件。
4. C 将新座位请求排在旧请求后，按地点 FIFO 分配空位；同分钟新到者按学生 ID 排序。B 接收分配事件，开始就餐或学习。
5. 校验 B 的窗口等待/服务人数和 C 一致，再校验 B 的等座/占座人数和 C 一致，记录该分钟状态。

占用区间是 `[开始,结束)`，例如 `[4,6)` 的座位在第 6 分钟可立即复用。学生不能同时在两个座位池等待或占用，也不能重复提交同一请求。释放后可凭新活动请求再次申请。座位引擎独立于 B，可单测队列、时间顺序、容量与统计。

`fastest` 的目标仍是“路途 + 预计窗口等待 + 取餐服务”，不包含座位等待、之后的课程时间约束或未到达学生。增加座位后不会自动改变该决策目标；目前没有预约、抢占、中途换食堂、离队或优先级。

## 输出与复现

旧五份输出保留原字段：`events.csv`、`student_snapshots.csv`、`facility_snapshots.csv`、`summary.csv`、`decisions.csv`。日程或有限座位模式增加：

| 文件 | 内容 |
| --- | --- |
| `activities.csv` | 每人每项活动的计划、出发/到达/开始/完成时刻、延迟、已观察座位等待、路线和状态 |
| `seat_events.csv` | 请求和入座事件、申请时间、占用截止时间、入座前等待 |
| `seat_snapshots.csv` | 每分钟容量、等待/占用人数、累计请求/入座/释放、队列峰值、总等待和忙碌座位分钟 |

未发生的时刻为空，不伪造完成。活动状态 `planned` 表示尚未开始前往，`pending` 表示已进入准备/移动/等待过程，`active` 表示已开始，`completed` 表示完成。精确当前状态以逐分钟学生快照为准。

`seat_snapshots.total_wait_minutes` 只累计已入座者的等待；`activities.seat_wait_minutes` 还计入截止时仍在等座者的已观察等待。座位利用率为 `[0,T)` 的忙碌座位分钟除以 `容量 × T`；恰好在 T 入座计入已入座人数，但贡献 0 分钟利用率。

对照报告另外生成 `summary.csv`、`seat_comparison.csv` 和合并的 `activities.csv`，提供学生/方案切换、状态时间线、真实路径的拓扑示意和原始数据链接。拓扑图节点坐标仅用于排版，不是校园测量坐标。提前截止时，报告单列未完成学生、未开始课程，避免把尚未发生的等待当成改进。

默认 160 分钟、36 名学生的本次结果：

| 方案 | 完成活动 | 迟到课程次数 | 课程总延迟/分 | 座位等待/分 | 全部完成时刻 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 基准 | 216 / 216 | 56 | 954 | 885 | 124 |
| 只将食堂座位翻倍 | 216 / 216 | 50 | 806 | 849 | 124 |
| 只将图书馆座位翻倍 | 216 / 216 | 50 | 458 | 357 | 94 |
| 只封闭三条道路 | 216 / 216 | 58 | 1055 | 851 | 127 |

道路封闭后总出行距离从 30240 增至 34800 米。它也改变到达时序，因此座位等待反而稍降；课程延迟却增加。各指标需要一起看，不能只凭一个等待数判断方案优劣。

## 三阶段完成度与验证

| 阶段 | A | B | C | 验收入口 |
| --- | --- | --- | --- | --- |
| 一：动态午餐 | 多地点/多路线、距离最短路 | fixed/nearest/fastest、逐人决策记录 | 冻结窗口/队列状态和等待预测 | `run_dynamic_experiments.py`、原有集成测试 |
| 二：复杂服务 | 三食堂及窗口餐品配置 | 餐品需求、不可服务候选过滤 | 异质时长、兼容窗口匹配、负载统计 | `run_service_experiments.py`、复杂服务行为/集成测试 |
| 三：多活动 | 扩展校园、座位配置、道路变更对照 | 日程序列、等待/活动状态、迟到传播 | 食堂/图书馆座位 FIFO、释放复用与利用率 | `run_schedule_experiments.py`、4 项新增 CTest |

本地 Debug 构建通过，19 项 CTest 全部通过；一键运行包括原 MVP 手算基准及全部阶段的实验。新增测试包含：

- `seat_engine_behavior`：容量、FIFO、同分钟释放复用、多资源、重复请求、异常输入不改变有效状态、区间忙碌时间。
- `activity_contracts`：A 保存加载座位、非法重置原子性、B 拒绝错配/重复座位事件、活动边界与终态。
- `activity_schedule_integration`：真实 A/B/C 手算双人流程、按分钟核对资源守恒、36 人样例、中文路径、零距离午餐、提前截止、输入乱序、覆盖保护及非法配置。
- `all_stage_experiments`：全部报告能生成、静态数据链接存在、原输入未被改写、每组仅改变约定变量、早截止口径。

完整回归命令：

```bash
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

GitHub Actions 对 Linux、Windows 执行同一套检查。浏览器环境不能打开本地 `file:` 页面，因此本轮离线报告检查覆盖数据、链接与脚本，不宣称完成浏览器截图或跨浏览器视觉验收。

“全部完成”指这里列明的三个功能升级阶段。自行车/校车、跨日多餐、动态封路、主动取消活动、座位预测、实时地图动画，以及统一账号菜单和课程最终材料不属于这三阶段的已实现功能。
