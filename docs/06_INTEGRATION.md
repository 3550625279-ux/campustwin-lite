# 如何把三个子系统拼接起来

## 拼接的是接口，不是三个可执行文件

每位成员都在同一 CMake 工程中维护自己的 `.cpp/.hpp`；`demo_a/b/c` 只是各自测试入口。最终 `campustwin` 链接三份模块库和调度库，只保留 `app/main.cpp` 一个主入口。CSV 是初始化和输出格式，运行反馈通过函数调用发生。

## 初始化次序

1. A 的入口解析 CLI，检查输入路径、duration 和输出目录。
2. A `load_directory(scenario_dir)`，得到有效场景。
3. B `read_student_specs(student_file)` 解析学生，错误不进入主循环。
4. 创建 B、C、结果记录器对象；把依赖以接口引用传给 `SimulationRunner::run`。
5. Runner 依次调用 B.reset、C.reset、writer.begin，然后进入唯一时间循环。

## 主循环与调用顺序

```text
for t = 0 .. duration:
    completed = C.complete_due(t)
    for each completed: B.on_service_completed(event)

    arrivals = B.tick(t, A)
    C.enqueue(arrivals, t)

    started = C.start_ready(t)
    for each started: B.on_service_started(event)

    students = B.snapshots(t)
    facilities = C.snapshots(t)
    检查跨模块状态守恒
    writer.write_tick(t, completed, arrivals, started, students, facilities)

writer.finish(C.summaries(duration))
```

每一个业务判断都在对应模块内部。Runner 不重新计算出行时长、不直接 `pop` C 的队列、不将学生直接写成 Eating。

## 独立开发时怎么替代队友

| 正在开发 | 用什么替代未完成模块 | 必须验证 |
| --- | --- | --- |
| A | 固定路线查询请求和自拟非法 CSV | 场景读写、图查询、失败不污染旧场景 |
| B | 测试用 `IRouteProvider`；假资源在到达时发开始、2 分钟后发完成 | 真正等待事件，不预先跳过排队/服务状态 |
| C | 按分钟喂 `resource_arrivals.csv` | 完成、入队、开始的顺序以及事件唯一性 |
| Runner | 短小的测试替身记录每次调用 | 阶段顺序、now 连续、最后 tick 没被漏掉 |

替身只放测试或独立 demo，不进入最终业务路径。B 的测试替身不做完整 C 算法，C 的测试输入不伪装成 B 已经完成。

## 最小联调次序

| 门槛 | 动作 | 通过证据 |
| --- | --- | --- |
| G0 | 公共类型编译，样例校验通过 | build + ctest 通过 |
| G1 | A 与 B 接上，C 暂用替身 | 20 人 t=2/t=3 的真实到达事件 |
| G2 | C 独立通过 3 人顺序样例 | 与 expected_events 的六条事件一致 |
| G3 | 替换成真实 C，开始/完成事件返回 B | 各分钟队列与学生状态一致 |
| G4 | 接 CSV 记录器，完整运行 120 分钟 | `check_mvp_outputs.py` 通过 |
| G5 | 分别修改窗口数、路程再运行 | 因果变化合理；原基准仍通过 |

## 运行命令

B 的独立演示现在可运行，使用固定 MVP 路线替身和单学生服务脚本，不依赖 A/C：

```bash
./build/demo_b --students data/students/mvp.csv --student S001 --wait 3 --duration 20
```

该演示不读取 `--scenario`，只支持其帮助信息列出的选项。完整 B 行为也由 CTest 的 `student_engine_behavior` 验证。以下命令运行 A、C 的独立演示及真实集成程序：

```bash
./build/demo_a --scenario data/scenarios/mvp
./build/demo_c --facilities data/fixtures/resource_facilities.csv --arrivals data/fixtures/resource_arrivals.csv
./build/campustwin --scenario data/scenarios/mvp --students data/students/mvp.csv --duration 120 --output outputs/mvp
python3 tests/check_mvp_outputs.py outputs/mvp
```

Windows 的可执行路径按生成器换成 `build/Debug/campustwin.exe` 等；参数和工作目录约定相同。必须从仓库根目录执行相对数据路径。

## 合并前后

先合入 A/B/C 中已经独立验证的小任务，再合入 Runner 集成 PR。共享头文件由独立 PR 修改，避免三个长期分支分别修改其不同副本。合并后每位成员 `fetch` 并把 `origin/main` 合入自己的分支，然后重新构建。

CTest 已加入真实业务运行、输出检查和变参测试，GitHub Actions 会在各平台执行这些测试。远端平台是否通过以 Actions 实际结果为准。保留独立演示，避免集成改动后个人模块失去可验证入口。
