# 双食堂午餐出行与服务实验

## 想回答什么

从同一个地点出发，较近的食堂是否一定更快开始吃饭？当食堂增加窗口，或者所有学生走得更快，行程和排队会怎样变化？

这是**合成数据上的仿真实验**，不是对真实校园的测量或预测。A 提供校园和最短距离路线，B 按学生配置移动并接收服务事件，C 管理每个食堂独立的 FIFO 队列和窗口；统一调度器按同一时间轴连接三者。原 `data/scenarios/mvp/` 与其 20 人基准保持不变。

## 实验场景与控制变量

新场景位于 `data/scenarios/two_canteens/`，学生位于 `data/students/two_canteens.csv`：

| 设置 | 内容 |
| --- | --- |
| 地点 | 两栋教学楼、一座图书馆、一栋宿舍、近食堂和远食堂 |
| 食堂 | F_NEAR：2 个窗口；F_FAR：4 个窗口；每次服务均为 2 分钟 |
| 学生 | 两个食堂各 20 人；每人速度 60 米/分钟，就餐 5 分钟，之后去宿舍 |
| 成对出发 | 每个食堂各 12 人从 L_T1 于 t=0 出发，各 4 人从 L_T2 于 t=1 出发，各 4 人从 L_DORM 于 t=2 出发 |
| 路线 | L_T1→近食堂 120 米；L_T1→图书馆→远食堂 240 米；其余路线在生成的报告中列出 |

三个默认实验：

| 组 | 改变 | 与基准保持一致 |
| --- | --- | --- |
| `baseline` | 基准：近食堂 2 窗口、远食堂 4 窗口，速度 60 米/分钟 | — |
| `more_windows` | 只把近食堂改为 3 个窗口 | 学生、地点、道路、远食堂和服务时长 |
| `faster_walk` | 只把每个学生的移动速度乘以 2 | 所有学生的出发地点、时间、食堂、道路和窗口配置 |

分配食堂、出发条件和人数在输入中事先确定。出发时间和速度均相同的两组学生可作为示例对照；他们是不同学生，不是让同一个人同时到两座食堂。多组之间的结论只适用于这些明确的设置。

## 运行

在仓库根目录先构建并测试：

```bash
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Linux/macOS 单配置构建：

```bash
python3 tools/run_lunch_experiments.py --executable build/campustwin --output outputs/lunch_demo
```

Windows PowerShell，Visual Studio 生成器：

```powershell
py -3 tools/run_lunch_experiments.py --executable .\build\Debug\campustwin.exe --output .\outputs\lunch_demo
Start-Process .\outputs\lunch_demo\report.html
```

如果使用 Ninja/MinGW，程序通常在 `.\build\campustwin.exe`。所有相对路径以上述命令的当前目录为准。输出目录必须是**尚不存在**的目录；失败时该目录可能包含不完整结果，应换新目录重新运行。

如需调整实验：

```bash
python3 tools/run_lunch_experiments.py --executable build/campustwin --output outputs/custom_run \
  --scenario data/scenarios/two_canteens --students data/students/two_canteens.csv \
  --duration 60 --target-facility F_NEAR --window-count 3 --speed-multiplier 2
```

可替换场景目录、学生文件、观察时长、目标食堂、对照窗口数和速度倍数。窗口对照只改目标食堂，速度对照给所有学生乘同一倍数；窗口数必须与基准不同，倍数必须为有限正数且不等于 1。脚本给三组保存独立输入副本，不修改原始 CSV。可执行文件必须是本项目带 `--route` 选项的新版。

只运行一次双食堂仿真：

```bash
./build/campustwin --scenario data/scenarios/two_canteens --students data/students/two_canteens.csv --duration 60 --output outputs/two_canteens
```

只查询路线：

```bash
./build/campustwin --scenario data/scenarios/two_canteens --route L_T1 L_FAR
```

输出为 CSV：`from,to,distance_m,path`，路径中的 `|` 分隔地点。查询使用 A 的实际最短路径实现，不会运行午餐仿真；未知地点或不可达会报错。`--route` 可以与 `--scenario` 一起使用，但不能与 `--students`、`--duration` 或 `--output` 混用。

## 结果在哪里

| 文件 | 内容 |
| --- | --- |
| `report.html` | 可直接用浏览器打开的表格和队列曲线，不需要联网 |
| `comparison.csv` | 每组、每个食堂的汇总，可导入 Excel |
| `student_journeys.csv` | 每个学生的起点、指定食堂、路径、距离、到达、等待和开始吃饭时间 |
| `routes.csv` | 每个起点到对应食堂的实际最短路径和距离 |
| `runs/<组名>/input/` | 该组真实使用的场景和学生输入副本 |
| `runs/<组名>/output/` | 程序原始的 `events.csv`、两个逐分钟快照和 `summary.csv` |

`comparison.csv` 中 `assigned` 是配置中指定去该食堂的人数；`not_arrived = assigned - arrived`，包括尚未出发和正在路上的人；`waiting = arrived - started`；`in_service = started - completed`；`not_completed = assigned - completed`，包含未到达、等待和服务中的人。开始吃饭指窗口服务完成时，并不是吃完饭或返回宿舍。

平均等待只计算**已开始服务**的人；平均出发到开始吃饭只计算**已完成服务**的人；平均路程时间只计算**已到达**的人。对应的人数也提供在 CSV 中。没有符合条件的人时，派生平均值留空；原始 `summary.csv` 为兼容旧格式，对零开始人数的均值写 0。提前截止时，这些平均数会遗漏仍在等待的人，不能单独拿来判断方案优劣。

最大队列按每分钟完成窗口分配后的等待人数统计。利用率为 `busy_window_minutes / (window_count × observation_minutes)`：`duration=60` 表示 0 到 60 的 60 分钟区间，虽然记录了 0…60 共 61 次快照。单独延长观察时间会降低这个区间内的利用率，不应把它当成窗口服务效率的唯一指标。

## 默认数据的示例结果

运行 60 分钟后，两食堂的 20 人均完成窗口服务：

| 指标 | 近食堂基准 | 远食堂基准 | 近食堂增加到 3 窗口 |
| --- | ---: | ---: | ---: |
| 平均步行时间 / 分钟 | 2.4 | 4.4 | 2.4 |
| 平均等待 / 分钟 | 8.0 | 3.0 | 4.7 |
| 平均出发到开始吃饭 / 分钟 | 12.4 | 9.4 | 9.1 |
| 最大排队人数 | 16 | 12 | 14 |

同样从 L_T1、t=0、60 米/分钟出发的两组各 12 人：近食堂平均 9 分钟开始吃饭，远食堂平均 8 分钟。这说明**在本例中**，少走路的优势可以被等待时间抵消，不代表远食堂总是更快。

全体速度加倍后，远食堂平均步行时间从 4.4 分钟变为 2.2 分钟，平均等待从 3.0 分钟变为 3.2 分钟；到达时间的相对分布发生变化，因此走得更快不保证排队时间也下降。上述结果是固定输入与确定性调度的结果，可重新运行核对。

## 当前模型的边界

- 学生只执行一次固定午餐日程；没有动态根据距离或实时队列选食堂。
- 所有道路双向，路线按总距离最短选择，移动时间为 `ceil(距离 / 速度)`，不模拟逐段行进、路况或交通拥堵。
- 速度实验并不模拟自行车：没有车辆类型、可通行道路、停放或校车规则。
- 每个食堂有独立 FIFO 队列，共享该食堂的窗口；同一分钟到达的学生按 ID 排序。窗口服务后立即开始就餐，就餐不占窗口。
- 地点容量当前只是描述，不限制进入或就餐人数。角色权限、完整一天的多活动日程和其他服务仍未实现到统一程序。
