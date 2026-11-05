# 第二阶段：餐品、窗口能力与服务调度

## 可以运行什么

`data/scenarios/service_campus/` 包含 12 个地点、28 条无向道路、3 个食堂；`data/students/service_campus.csv` 包含 60 名合成学生。每个食堂有 3 个窗口，学生各有一种餐品需求：`rice`、`noodle` 或 `stirfry`。A 加载地图、窗口配置并查询最短路，B 根据餐品、路线和预计等待选择食堂，C 依据各窗口能力安排服务。地点与人数均为虚构数据。

```bash
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
python3 tools/run_service_experiments.py --executable build/campustwin --output outputs/service_demo
```

Windows PowerShell，Ninja/MinGW 单配置示例：

```powershell
py -3 tools/run_service_experiments.py --executable .\build\campustwin.exe --output .\outputs\service_demo
Start-Process .\outputs\service_demo\report.html
```

Visual Studio 生成器通常使用 `.\build\Debug\campustwin.exe`。Python 3.9+ 只需要标准库。输出目录必须尚不存在；失败后请换目录重跑。

也可用真实仿真单独运行动态策略：

```bash
./build/campustwin --scenario data/scenarios/service_campus --students data/students/service_campus.csv --policy fastest --duration 60 --output outputs/service_fastest
```

## 新输入与兼容性

原有 `locations.csv`、`roads.csv` 和 `facilities.csv` 格式不变。可在场景目录增加 `window_services.csv`：

```csv
facility_id,window_id,meal_type,service_minutes
F_NEAR,0,rice,2
F_NEAR,1,noodle,3
F_NEAR,2,stirfry,5
```

窗口编号从 0 开始，必须小于 `window_count`。同一设施只要显式配置了窗口服务，就必须给它的每个窗口至少配置一行；同一窗口可有多个不同餐品，窗口与餐品组合不可重复，时长必须为正整数。`meal_type` 使用 ASCII 字母、数字、下划线或连字符；`*` 是该窗口可服务任意餐品的通配项。如果同一窗口同时配置了 `*` 和指定餐品，指定餐品的时长优先。

未出现在该文件的设施、缺少该文件的旧场景以及只有表头的文件，都保持原有行为：每个窗口以 `facilities.csv` 的统一时长服务任意餐品。加载失败不替换已加载场景；保存场景会写出窗口服务文件（没有配置时只写表头）。

学生 CSV 可在原有第八列 `lunch_policy` 后增加第九列 `meal_type`：

```csv
id,initial_location,lunch_facility,after_meal_location,departure_minute,walk_m_per_min,meal_minutes,lunch_policy,meal_type
S001,L_T1,F_NEAR,L_DORM_N,0,60,5,fixed,rice
S002,L_T2,,L_DORM_S,0,60,5,fastest,noodle
```

旧 7 列与 8 列仍可使用，餐品默认 `standard`；各列标题和顺序须严格匹配，新增字段不可为空。固定目的地必须能提供所需餐品；动态策略跳过没有该餐品的食堂，在 `decisions.csv` 中记录 `meal_unavailable`。如果没有可达且符合条件的食堂，运行会明确报错。`--policy` 仍可覆盖本次所有学生的个人策略。

## 服务顺序与预测

每个食堂是一条共享等待队列。同一分钟到达的学生按 ID 排序，旧请求排在新请求之前。每分钟完成释放、到达入队后，C 将排队请求与**空闲且能提供所需餐品**的窗口匹配：

1. 按队列先后处理请求，尽可能匹配更多空闲窗口，并优先保留较早到达的请求。
2. 为找到可行匹配，可以把本分钟已匹配的早到学生换到另一个兼容的空闲窗口，但不会挤掉他；无法由空闲窗口服务的学生继续等待。
3. 多种匹配均可时，由确定的窗口编号顺序和匹配过程决定；这不是最短服务时间或全局最优等待的调度器。

例如 0 号窗口可提供米饭和面食，1 号只提供米饭。若队列先后为米饭、面食，两人会分别去 1 号和 0 号，不会让面食学生等待而米饭窗口闲置。需要不同餐品的队首不能使用当前空闲窗口时，后面的兼容请求可以先开始，因此并非所有学生严格按同一顺序开始服务。

`StudentArrival` 携带餐品；`ServiceStarted` 带实际窗口及 `expected_end_at`。B 校验窗口与需求兼容，按该窗口实际服务时长等待 C 的完成事件，之后才进入就餐。旧同质窗口的测试替身可省略结束时刻；显式异质窗口必须提供。

每一分钟出发的学生使用相同的上一分钟结束快照。`fastest` 的 `步行 + 预计等待 + 预计服务` 会按目标餐品、已有排队请求、窗口能力和释放时刻重演上述匹配，但假设快照后没有新的到达，因此不含在途学生或同一分钟的其他决策，也不预约窗口。估计与实际可能不同。

## 窗口分工对照

脚本使用同一地图、60 名学生、出发时间、固定食堂、每食堂 3 个窗口及每种餐品的服务耗时，比较：

- `dedicated`：每个食堂的 3 个窗口分别只服务米饭、面食、现炒。
- `flexible`：每个窗口都服务三种餐品，各餐品仍分别耗时 2、3、5 分钟。

两组唯一改变的是窗口可提供的餐品集合。通用窗口模型假设切换餐品没有额外成本，这不一定符合真实食堂。脚本将输入副本、原始五份 CSV、HTML 报告、`summary.csv`、`meal_comparison.csv`、`window_comparison.csv` 和 `student_services.csv` 写入新输出目录。逐人服务表中的预计结束时刻由实际窗口、餐品和配置计算，并与实际完成事件交叉验证；窗口忙碌时间在观察截止处截断。

默认 60 分钟、确定性输入的结果：

| 方案 | 已完成窗口服务 | 平均等待（已开始者） | 总队列峰值 |
| --- | ---: | ---: | ---: |
| 专用窗口 | 60 / 60 | 3.97 分钟 | 21 |
| 通用窗口 | 60 / 60 | 2.57 分钟 | 17 |

未完成数包含未出发、在途、等待和服务中；平均等待只包括已开始服务的人。这个输入中通用窗口降低了总体平均等待，但面食组的等待由 0.83 分钟增加到 2.83 分钟，不能只看总体均值。不同需求比例、配置和模拟截止时间会改变结果。

## 边界与验收

仍为一次午餐，每名学生只请求一种餐品；没有价格、库存、窗口开关、转餐品成本、预约、离队重选、座位或全天日程。地图仍是一个运行场景，出行按最短距离整段计算。预测不考虑后续到达。

`complex_service_behavior` 检查兼容匹配、窗口不同耗时、快照预测、输入原子性、B 的开始/完成事件，并穷举三窗口的餐品资格组合对比独立的最优匹配枚举。`complex_service_integration` 使用真实 A/B/C 和调度器核对对照实验、原输入不变、动态餐品过滤、非法配置拒绝及旧实验工具的明确限制。原有 MVP 和动态午餐测试保留。
