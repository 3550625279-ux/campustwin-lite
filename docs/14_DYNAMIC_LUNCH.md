# 动态午餐出行与服务：第一阶段

## 能做什么

学生按固定食堂、最短距离或预计最快三种策略选择食堂。动态策略在出发时读取同一份食堂状态，查询 A 的真实最短路，并记录所有候选方案及最终选择。学生出发后不重新选食堂。每个食堂仍是共享 FIFO 队列，窗口同质、服务时间固定；没有预约或撤队。

`data/scenarios/dynamic_campus/` 是 9 个地点、20 条无向道路、2 个食堂的合成地图；`data/students/dynamic_campus.csv` 是 60 人、6 批出发的合成人口。近食堂 2 窗口，远食堂 4 窗口，每次服务 2 分钟。原 MVP 和双食堂实验输入保持不变。

## 构建和运行

在仓库根目录：

```bash
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
python3 tools/run_dynamic_experiments.py --executable build/campustwin --output outputs/dynamic_demo
```

Windows PowerShell，Ninja/MinGW 单配置示例：

```powershell
py -3 tools/run_dynamic_experiments.py --executable .\build\campustwin.exe --output .\outputs\dynamic_demo
Start-Process .\outputs\dynamic_demo\report.html
```

Visual Studio 生成器通常使用 `.\build\Debug\campustwin.exe`。脚本只需 Python 3.9+ 标准库；`--output` 必须指向尚不存在的新目录。失败后可能有不完整输出，请换目录重跑。

单独运行一个策略：

```bash
./build/campustwin --scenario data/scenarios/dynamic_campus \
  --students data/students/dynamic_campus.csv --policy fastest --duration 60 --output outputs/fastest
```

Windows 换成对应的 `.exe` 路径。`--policy fixed|nearest|fastest` 覆盖本次所有学生的个人策略；不传时读取学生行中的策略，旧 7 列文件默认为 `fixed`。路线查询如：

```bash
./build/campustwin --scenario data/scenarios/dynamic_campus --route L_T1 L_FAR
```

得到 `L_T1|L_LIBRARY|L_FAR`，240 米。路径是 A 使用 Dijkstra 算出的最短距离路径；B 根据总距离按分钟计算移动，不逐段模拟。

## 学生策略和输入

旧文件保持兼容，7 列表头不变，`lunch_facility` 是固定目标：

```csv
id,initial_location,lunch_facility,after_meal_location,departure_minute,walk_m_per_min,meal_minutes
S001,L_T1,F_NEAR,L_DORM_N,0,60,5
```

如需个人策略，使用严格的 8 列表头（每行最后一列必填）：

```csv
id,initial_location,lunch_facility,after_meal_location,departure_minute,walk_m_per_min,meal_minutes,lunch_policy
S001,L_T1,F_NEAR,L_DORM_N,0,60,5,fixed
S002,L_T1,,L_DORM_N,0,60,5,nearest
S003,L_T1,,L_DORM_N,0,60,5,fastest
```

- `fixed`：去 `lunch_facility` 指定的设施，此字段必填。
- `nearest`：在可达、符合端点限制的候选食堂中选择最短距离；同距离按设施 ID 字典序选最小者。
- `fastest`：最小化预计 `步行时间 + 排队等待 + 窗口服务时长`，同值按设施 ID 字典序选择。步行时间为 `ceil(最短距离 / 速度)`，正距离至少 1 分钟。

动态策略可以留空 `lunch_facility`；非空时它必须是存在的设施，但不参与动态选择。若用 `--policy fixed` 覆盖整个人口，所有人都必须有有效固定设施。`demo_b` 是固定路线/服务的独立替身；包含 `fastest` 的人口应运行统一 `campustwin`。

候选食堂为场景中全部设施；设施与学生起点或饭后地点相同的候选会跳过（当前两个行程要求不同端点），不可达候选也会跳过，并在决策 CSV 中标明原因。如果没有合法可达食堂，仿真明确失败，不静默指定目标。

## 估计等待与时间顺序

每个 t 开始时，调度器从 C 获取 `t-1` 窗口分配完成后的不可变快照；t=0 使用初始化空状态，标记为 `observed_at=-1`。然后 C 完成 t 时到期的服务，B 处理通知并使用之前冻结的快照决策；同一 t 出发的所有学生看到同一个快照。之后新到达者入队、空闲窗口分配，B 和 C 输出快照。

C 的估计会考虑**快照中已有队列、每个窗口的预计释放时刻、服务时长和该学生预计到达时刻**：假设快照后没有新增到达，先安排已有队列，再计算该学生抵达后还要等多久。估计不会修改 C 的真实队列。它不包含在途学生、同分钟做决定的人或其他未来到达者，也不是预约，因此预计与实际可能不同。

例如默认场景中，`fastest` 策略的 D031 在 t=6 从 `L_T4` 出发，使用 t=5 快照。近食堂预计步行 5、等待 3、服务 2 分钟，总计 10 分钟；远食堂预计步行 5、等待 0、服务 2 分钟，总计 7 分钟，于是选远食堂。一次完整运行中他实际用 8 分钟开始吃饭：后续到达使实际值偏离了预测。

## 结果与验收

实验脚本使用同一组场景、学生和出发时间，只改变三种策略。每组的 `runs/<策略>/input/` 保存输入副本，`output/` 保存真实仿真的五份 CSV：原有 `events.csv`、`student_snapshots.csv`、`facility_snapshots.csv`、`summary.csv`，以及新增 `decisions.csv`。后者每个候选食堂一行，记录路线、观察时刻、队列、预计耗时、是否选择与原因。

| 汇总输出 | 内容 |
| --- | --- |
| `report.html` | 离线策略和食堂对照表、逐分钟队列曲线、逐人样例 |
| `policy_summary.csv` | 各策略人数、完成数、未完成数、平均等待、预测误差 |
| `facility_comparison.csv` | 各食堂选择人数、到达与完成数、队列、等待和利用率 |
| `student_journeys.csv` | 每人的选择、预计和实际开始吃饭时间及误差 |
| `candidate_decisions.csv` | 所有候选路线、评分和被选理由 |

默认 60 分钟结果（相同确定性输入可复现）：

| 策略 | 近 / 远食堂选择人数 | 已完成窗口服务 | 平均等待（已开始者） | 出发到开始吃饭（已完成者） |
| --- | --- | ---: | ---: | ---: |
| fixed | 30 / 30 | 60 / 60 | 4.58 分钟 | 10.75 分钟 |
| nearest | 60 / 0 | 58 / 60 | 22.50 分钟 | 27.17 分钟 |
| fastest | 22 / 38 | 60 / 60 | 4.67 分钟 | 10.87 分钟 |

等待均值只包括已开始服务的人，出发到吃饭和预测误差只包括已完成服务的人；未完成数包括未出发、在途、排队和服务中的学生。提前截止时不能只看均值。以上是合成数据，且固定组预先均衡分流；该次实验中 `fastest` 不保证优于均衡的 `fixed`。

`dynamic_lunch_decisions` 使用真实 A/B/C/调度器运行三策略，检查所有候选评分、并列处理、同分钟快照、事件归属、混合个人策略、不可达候选、截断、防覆盖及可复现性。`resource_engine_behavior` 另将窗口释放预测与手算 FIFO 实际等待对照，并验证快照不随后续运行改变。

## 模型边界

预测假设之后没有新增到达；路线只按距离最短、整段移动；设施位置与学生两次行程端点不能相同；没有中途改道、取消排队、座位、自行车、不同窗口餐品或全天日程。地点容量仍不限制进入人数。新增不同窗口服务时间或其他类型设施需要再修改三方公共契约。
