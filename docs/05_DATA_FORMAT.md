# 输入与输出格式 v0.1

## 通用约定

UTF-8 文本，无 BOM；第一行为固定表头；逗号分隔。首版字段不能包含逗号、引号或换行，不实现带引号 CSV 的转义；名称可以含中文。空行跳过，CRLF/LF 都接受。数字使用英文小数点，无千位分隔。输入字段不带首尾空白，读入时拒绝多列/少列和无法完整解析的数值。

配置全部是合成示例。输出按逻辑时钟排序，不写系统时间戳或随机运行 ID，保证可复现。浮点输出至少保留 6 位小数；验收数值比较用绝对容差 1e-6。

## 场景目录（A）

`data/scenarios/mvp/` 下三份文件缺一不可：

| 文件与表头 | 含义 |
| --- | --- |
| `locations.csv`: `id,name,kind,capacity` | kind 为 Teaching/Canteen/Dormitory/Library；capacity 是正整数 |
| `roads.csv`: `from,to,distance_m` | 无向正距离道路；端点存在；不重复录入反向边 |
| `facilities.csv`: `id,location_id,window_count,service_minutes` | 设施与地点分离；窗口数为 1…100000，服务时长为正整数 |

`replace_scenario` 与加载文件使用同一套校验。保存只写上述三份静态文件；学生配置归 B，运行结果不回写到场景。

## 学生文件（B）

`data/students/mvp.csv` 表头：

```csv
id,initial_location,lunch_facility,after_meal_location,departure_minute,walk_m_per_min,meal_minutes
S001,L_T1,F_CANTEEN,L_DORM,0,60,5
```

ID 唯一；起点/后续地点、午餐设施必须存在；departure_minute 为非负整数；速度、就餐时长为正。首版只支持此午餐日程，不假装已经支持任意活动脚本。

## C 独立测试文件

`data/fixtures/resource_facilities.csv` 和 `resource_arrivals.csv` 专供 `demo_c`，不是新增的集成输入。到达表头是 `request_id,student_id,facility_id,at`；同分钟事件按 student_id 排序后再入队。

`resource_expected_events.csv` 是手算验收参考，不是让程序直接复制的结果。此用例只有一窗口，服务 2 分钟，S001/S002 在 t=0 到，S003 在 t=2 到，用于检验“完成释放、旧队列优先、新到者排后面”。

## 运行输出（C 的记录器）

四份文件必须全部输出，含固定表头；按 `now=0…duration` 每分钟记录全部学生和设施，记录器不丢弃 Finished 学生。

### `events.csv`

```csv
at,type,request_id,student_id,facility_id,window_id,wait_minutes
```

type 只允许 StudentArrival / ServiceStarted / ServiceCompleted。到达事件窗口和等待列为空；开始事件填写窗口与等待；完成事件填写窗口，等待列为空。同分钟顺序：所有完成 → 所有到达 → 所有开始；各类内部排序见接口契约。

### `student_snapshots.csv`

```csv
at,student_id,state,location_id,from,to,depart_at,arrive_at,distance_m
```

state 为 AtLocation / Moving / Queued / InService / Eating / Finished。Moving 时 location_id 为空，另外五个移动字段全部填写；其他状态填写 location_id，移动字段全部为空。不能把在途学生同时计入起点。

### `facility_snapshots.csv`

```csv
at,facility_id,queue_length,in_service,arrived,started,completed,max_queue_length,total_wait_minutes,busy_window_minutes
```

计数均非负。应持续满足 `arrived = queue_length + in_service + completed` 和 `started = in_service + completed`。就餐学生不再占窗口，故不进入上述请求守恒式。

### `summary.csv`

```csv
facility_id,window_count,observation_minutes,arrived,started,completed,max_queue_length,total_wait_minutes,mean_wait_minutes,busy_window_minutes,utilization
```

duration=120 时 observation_minutes 为 120，即有 121 次快照、120 分钟观察区间；利用率不能除以 121。每个设施一行，按设施 ID 排序。

## 截止与重复运行

若 t=120 时未完成全部请求，仍按真实状态输出，并保留计数差异。用户应换输出目录运行第二次；首版拒绝覆盖已有的四个结果文件。对照实验另存 `outputs/one_window/`、`outputs/two_windows/`，不要把多次结果接在同一文件末尾。
