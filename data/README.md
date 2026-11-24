# 样例数据

展示版使用 `campus_flagship`。数据是合成的校园规划样例，参数如下。

| 项目 | 配置 |
| --- | --- |
| 时间与学生 | 08:00–18:00，9000 人，6 个片区，每区 30 班，每班 50 人 |
| 地图 | 42 个地点、66 条双向道路，步速 58–92 米/分钟 |
| 食堂 | 6 座食堂，每座 36 个取餐窗口、720 个就餐座位 |
| 单座食堂的窗口 | 米饭 11 窗 × 2 分钟、面食 7 窗 × 2 分钟、套餐 5 窗 × 1 分钟、现炒 5 窗 × 4 分钟、轻食 4 窗 × 2 分钟、清真 4 窗 × 2 分钟 |
| 图书馆 | 6 处，每处 480 个自习座位 |
| 教学楼 | 12 处，每处 900 个教学座位，代表楼内多个教室的合计容量 |
| 日程 | 上午两段课、分批午餐、下午课程及回宿舍休息，5400 人另有图书馆自习，共 50400 项活动 |
| 时间分布 | 默认午餐 12:00–12:39 分五批出发，实际用餐 16–26 分钟，自习 40–60 分钟 |

食堂、图书馆和教学楼的实际容量由座位池约束。宿舍每栋标注 520 人容量，安排 500 名固定住户。可以用 `python tools/generate_flagship.py` 按固定随机种子重新生成全部旗舰 CSV。同期生成的 `schedules/campus_flagship_wide_lunch.csv` 是 11:10–13:11 宽错峰参照，只有午餐计划出发时间与旗舰默认不同。

新增 `scenarios/campus_day/`、`students/campus_day.csv` 和 `schedules/campus_day.csv`：36 人、216 活动、有限座位及扩展地图，格式和行为见 [第三阶段说明](../docs/16_ACTIVITY_SCHEDULES.md)。

四个午餐实验也配有同名日程。学生取餐、吃饭后回宿舍休息 1 分钟，休息结束时进入 Finished。

所有输入均为自拟仿真数据，不含真实学生资料。

- `scenarios/mvp/`：A 加载的地点、道路、设施。
- `students/mvp.csv`：B 加载的 20 人午餐日程。
- `scenarios/two_canteens/`、`students/two_canteens.csv`：双食堂、40 人的出行与服务实验；使用方法见 [实验指南](../docs/13_LUNCH_EXPERIMENTS.md)。
- `scenarios/dynamic_campus/`、`students/dynamic_campus.csv`：9 地点、20 道路、60 人的动态选食堂对照；见 [动态午餐实验指南](../docs/14_DYNAMIC_LUNCH.md)。
- `scenarios/service_campus/`、`students/service_campus.csv`：12 地点、3 食堂、60 人及不同餐品/窗口的实验；见 [服务指南](../docs/15_COMPLEX_SERVICES.md)。
- `fixtures/resource_facilities.csv`、`resource_arrivals.csv`：C 的独立队列测试。
- `fixtures/resource_expected_events.csv`：C 应产生的服务开始/完成序列，不含外部喂入的到达事件。
- `fixtures/mvp_expected_summary.csv`：手算的 20 人汇总参考，不是仿真程序的实际输出。

基准参数改动会使手算结论失效，因此当前样例校验器刻意冻结其内容。新增实验请复制为另一个场景/学生文件，不改写基准。

字段定义见 `docs/05_DATA_FORMAT.md`，手算过程见 `docs/07_ACCEPTANCE.md`。
