# 样例数据

所有输入均为自拟仿真数据，不含真实学生资料。

- `scenarios/mvp/`：A 加载的地点、道路、设施。
- `students/mvp.csv`：B 加载的 20 人午餐日程。
- `fixtures/resource_facilities.csv`、`resource_arrivals.csv`：C 的独立队列测试。
- `fixtures/resource_expected_events.csv`：C 应产生的服务开始/完成序列，不含外部喂入的到达事件。
- `fixtures/mvp_expected_summary.csv`：手算的 20 人汇总参考，不是仿真程序的实际输出。

基准参数改动会使手算结论失效，因此当前样例校验器刻意冻结其内容。新增实验请复制为另一个场景/学生文件，不改写基准。

字段定义见 `docs/05_DATA_FORMAT.md`，手算过程见 `docs/07_ACCEPTANCE.md`。
