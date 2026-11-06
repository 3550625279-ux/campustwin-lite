# 统一调度与模块集成

`campustwin` 链接环境、学生、资源、调度和记录模块库，由 `app/main.cpp` 创建对象。`demo_a/b/c` 是独立验证入口，集成时不拼接这些可执行文件。运行交互是同进程内的接口调用，不依靠中间 CSV 反馈。

## 初始化

1. 入口解析命令行。路线模式只加载场景并查询路径，不进入仿真。
2. `CampusEnvironment::load_directory` 加载地点、道路、设施及可选窗口餐品/座位配置。
3. `read_student_specs` 读取学生；可选 `--policy` 覆盖策略；`read_schedules` 绑定可选日程。
4. 创建 `StudentEngine`、`FacilityEngine`、`SeatEngine` 和 `CsvRunWriter`。传入日程或配置有限座位时启用八份文件的详细输出。
5. `SimulationRunner::run` 通过接口引用接收对象，依次重置学生、窗口、座位，并调用 `writer.begin`。

Runner 的座位引擎参数为可选指针；场景有有限座位却没有提供引擎时明确拒绝运行。主 CLI 始终创建并传入 `SeatEngine`。

## 每分钟的准确调用顺序

以下伪代码对应 `src/simulation/simulation.cpp`，循环包含第 0 分钟和截止分钟：

```python
for now in range(duration + 1):
    advisor = facilities.decision_snapshot()  # 上一分钟末；t=0 为初始化状态
    completed = facilities.complete_due(now)
    for event in completed:
        students.on_service_completed(event)

    seats.release_due(now)
    arrivals = students.tick(now, environment, advisor)
    facilities.enqueue(arrivals, now)
    started = facilities.start_ready(now)
    for event in started:
        students.on_service_started(event)

    requests = students.seat_requests(now)
    seats.enqueue(requests, now)
    grants = seats.start_ready(now)
    for grant in grants:
        students.on_seat_granted(grant)

    student_states = students.snapshots(now)
    facility_states = facilities.snapshots(now)
    seat_states = seats.snapshots(now)
    check_window_and_student_counts(student_states, facility_states)
    check_seat_and_student_counts(student_states, seat_states)
    writer.write_tick(now, completed, arrivals, started, student_states, facility_states)
    writer.write_decisions(now, students.decisions(now))
    writer.write_seats(now, requests, grants, seat_states)

writer.write_activities(students.activity_records())
writer.finish(facilities.summaries(duration))
```

伪代码省略可选座位引擎的空指针分支，校验函数名用于说明含义。实际 C++ 校验也接收场景、学生配置和时间，以检查缺失快照、错地点和重复 ID。

顺序产生三项行为：同分钟新决策使用相同冻结视图；服务完成后可在本分钟申请座位；到期座位先释放，原等待者和新申请者随后按规则分配。学生收到入座事件以后，本分钟快照就显示 `Eating` 或 `InActivity`。

## 状态一致性

| 资源视图 | 必须对应的学生视图 |
| --- | --- |
| 食堂 `queue_length` | 同食堂 `Queued` 人数 |
| 食堂 `in_service` | 同食堂 `InService` 人数 |
| 座位 `queued` | 同地点 `WaitingSeat` 人数 |
| 座位 `occupied` | 同地点持有座位的 `Eating` / `InActivity` 人数 |

窗口累计值满足 `arrived - started = queue_length`、`started - completed = in_service`。座位累计值满足 `arrived - granted = queued`、`granted - released = occupied`。不满足时抛出异常，已经写出的输出可能不完整，不能继续当作成功结果。

取餐和占座时段均按 `[开始,结束)` 统计。截止分钟仍处理事件并记录快照，但该分钟刚开始的服务不贡献截止前的忙碌分钟。

## 独立验证与真实联调

| 范围 | 可用输入或替身 | 验证重点 |
| --- | --- | --- |
| 环境 | 固定查询、合法/非法 CSV | 图与文件一致性、同点/不可达、失败不污染旧场景 |
| 学生 | `IRouteProvider`、决策视图、显式服务/座位事件 | 等待真实反馈、事件匹配、活动时序 |
| 窗口 | 固定到达序列与餐品 | 完成/入队/开始顺序、兼容匹配、重复请求 |
| 座位 | 固定请求和占用时长 | FIFO、释放复用、容量与区间统计 |
| 调度 | 记录调用的小型替身，以及真实模块 | 时间连续、阶段顺序、快照守恒 |
| 端到端 | 原 MVP、复杂服务和 36 人日程样例 | 原始 CSV、最终状态、边界与确定性 |

测试替身只用于独立测试和演示，主程序始终接入真实模块。完整运行命令见 [README](../README.md)，测试覆盖见 [tests/README.md](../tests/README.md)。开发交接与 PR 流程见 [团队协作手册](TEAM_GUIDE.md)。
