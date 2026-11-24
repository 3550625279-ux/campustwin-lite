# 第三阶段：多活动日程、座位与三阶段交付

## 现在可以运行什么

学生按序上课、用餐、自习、运动和休息。A 计算每段最短路，B 决定何时出发并推进活动，C 管理取餐窗口和各场地的座位或名额。前序活动耗时会影响后续的实际开始时间，课程按课表时刻结束。程序保留计划时刻、实际时刻和延迟。

`data/scenarios/campus_day` 包含 16 个地点、38 条无向道路、3 个食堂、9 个异质窗口、6 组座位；36 名学生每人 6 项活动，共 216 项。地图、人数、餐品和日程均为合成数据。

终端演示使用 [旗舰工作台](../README.md#展示版)。原始小场景与资源变量对照由 CTest 做数值回归。

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

学生 CSV 继续兼容 7、8、9 列。每位学生必须有日程。内置学生文件自动读取同名日程，自定义路径使用 `--schedule FILE`：

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
- `kind` 可为 `class`、`study`、`sport`、`rest`、`lunch`。前四者分别绑定 Teaching、Library、Sports 和 Dormitory。每人必须恰好有一个午餐。
- 计划时间使用从场景起点开始的非负整数分钟，按日程序号不递减；时长为正整数。CSV 不支持引号、字段空白或 BOM。
- 普通活动的 `start_minute` 是希望在目的地开始的时间。B 尽量在 `计划开始 − 行走耗时` 出发。到早了原地等，上一项占用到原定出发时刻后，则在上一项结束时启程。
- `class` 的固定结束时刻是 `start_minute + duration_minutes`。晚到入场只参加剩余课时。到期仍未开始记为 `missed`，排队者退出队列，在途者沿已开始的路线抵达后继续下一项。
- `study`、`sport`、`rest` 从实际开始计时。`rest` 的 `start_minute` 可以为空，表示前一项结束后立即前往目的地。
- 午餐的 `start_minute` 是最早出发时间；到时使用学生的 `fixed/nearest/fastest` 策略和餐品需求选食堂。午餐地点留空，`requires_seat=0`，但选中食堂如配置座位会自动申请。午餐时长指用餐时长，不含移动、窗口排队、取餐和等座。
- 表头可以追加 `fast_meal_from`，只在午餐行填写，单位为仿真分钟。取餐完成的时刻达到该门槛时，用餐时长减半并向上取整。旗舰版门槛为第 300 分钟，即 13:00。
- 普通活动 `requires_seat=1` 时，相应地点必须有座位池；到达且达到计划开始时间后申请。`0` 表示这项活动不消耗座位，不会因为同地点配置了座位而自动占用。
- 启用日程后，其午餐行覆盖学生 CSV 的 `departure_minute`、`meal_minutes`；不再使用 `after_meal_location` 自动返程，最终位置由最后一项活动决定。为兼容旧格式，学生 CSV 中这三列仍必须合法，`after_meal_location` 仍需指向已有地点。

`WaitingSeat` 表示等待座位，`InActivity` 表示执行日程活动。完成活动或错过整节课后，B 会推进到下一项。日程处理完毕后进入 `Finished`。等待期间没有占座，持座活动结束时释放座位。

午餐活动的 `started_at` 记录实际出发；其他活动记录实际开始执行。因而午餐的 `start_delay_minutes` 只代表前序活动导致的出发延迟，不把取餐等待当作“午餐迟到”。就餐开始时间可在 `Eating` 状态或 `SeatGranted` 事件中读取。

## C：座位调度与时间顺序

窗口仍按第二阶段的餐品兼容匹配工作。座位是独立资源：拿到餐不代表已经入座，窗口不会因学生等座而被占住。

每分钟按以下次序处理：

1. 冻结上一分钟窗口快照；释放已完成窗口并向 B 发送完成事件。
2. C 释放到期座位并撤出过了入场截止时刻的申请。B 推进到达、活动完成或缺课、出发和座位请求。
3. C 入窗口队列、分配窗口，B 接收开始服务事件。
4. C 将新座位请求排在旧请求后，按地点 FIFO 分配空位；同分钟新到者按学生 ID 排序。B 接收分配事件，开始就餐或学习。
5. 校验 B 的窗口等待/服务人数和 C 一致，再校验 B 的等座/占座人数和 C 一致，记录该分钟状态。

占用区间是 `[开始,结束)`，例如 `[4,6)` 的座位在第 6 分钟可立即复用。固定结束时刻的请求带 `fixed_until`，同一分钟既是入场截止，也是座位释放时刻。其他请求从入座起占用指定时长。学生一次只能等待或占用一处座位，释放或请求过期后可凭新活动再次申请。

`fastest` 仍按“路途 + 预计窗口等待 + 取餐服务”评分，预测使用上一分钟的食堂快照。

## 输出与复现

旧五份输出保留原字段：`events.csv`、`student_snapshots.csv`、`facility_snapshots.csv`、`summary.csv`、`decisions.csv`。每次日程仿真还会写入：

| 文件 | 内容 |
| --- | --- |
| `activities.csv` | 计划和实绩、延迟、等位、路线、状态、缺课时刻、取餐时刻与确定的用餐时长 |
| `seat_events.csv` | 请求与入座事件、申请时间、占用分钟和截止时刻、等待分钟 |
| `seat_snapshots.csv` | 每分钟容量、等待/占用人数、累计请求/入座/释放/过期、队列峰值、总等待和忙碌座位分钟 |

未发生的时刻为空。活动状态有 `planned`、`pending`、`active`、`completed` 和 `missed`，分别表示尚未开始前往、在移动或等待、已经开始、已完成和错过整节课。精确时点状态以逐分钟学生快照为准。

`seat_snapshots.total_wait_minutes` 累计已入座者的等待。`activities.seat_wait_minutes` 还计入请求过期或截至仿真结束仍在等座的实际等待。座位利用率为 `[0,T)` 的忙碌座位分钟除以 `容量 × T`。恰好在 T 入座计入已入座人数，贡献 0 分钟利用率。

数值回归另外输出汇总、座位对照与合并活动 CSV。提前截止时，汇总单列未完成学生和未开始课程。

默认 160 分钟、36 名学生的本次结果：

| 方案 | 完成活动 | 迟到课程 | 缺课 | 课程总延迟/分 | 座位等待/分 | 全部学生走完日程时刻 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 基准 | 180 / 216 | 20 | 36 | 93 | 1053 | 114 |
| 只将食堂座位翻倍 | 191 / 216 | 25 | 25 | 99 | 902 | 114 |
| 只将图书馆座位翻倍 | 192 / 216 | 26 | 24 | 117 | 513 | 84 |
| 只封闭三条道路 | 176 / 216 | 18 | 40 | 90 | 1041 | 114 |

封路后有更多人错过整节课。将缺课单独显示，可以解释为什么已进入课堂的迟到人数有所下降。

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
- `all_stage_experiments`：各组 CSV 可读、原输入未被改写、每组仅改变约定变量、早截止口径。

完整回归命令：

```bash
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

GitHub Actions 对 Linux、Windows 执行同一套 CTest 检查。

“全部完成”指这里列明的三个功能升级阶段。自行车/校车、跨日多餐、动态封路、主动取消活动、座位预测、实时地图动画，以及统一账号菜单和课程最终材料不属于这三阶段的已实现功能。
