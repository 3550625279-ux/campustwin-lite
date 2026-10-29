# 验证范围

`validate_sample_data.py` 校验仓库中的固定配置、引用和手算参考。它只校验样例数据，本身不验证 C++ 仿真逻辑。

CTest 还包含 A 的独立验证：

- `a_environment`：验证场景加载、校验、编辑、保存和路线查询。
- `a_accounts`：验证本地账号的注册、登录和保存。
- `a_console`：验证 A 的控制台操作和保存后重新加载。

A 的独立测试通过不代表完整仿真已经完成，详见 [A 部分交付说明](../docs/A_DELIVERY.md)。。

```bash
python3 tests/validate_sample_data.py
```

当前 CTest 另运行两项 B 验证：

- `student_engine_behavior`：`student_engine_tests.cpp` 的 21 组行为用例，覆盖 CSV、引用、移动取整、事件顺序、重复事件、缺失反馈、重置、数值溢出、失败回滚，以及 20 人的全部学生轨迹。
- `student_demo`：实际运行 `demo_b`，验证单学生等待 3 分钟后的完整闭环。

```bash
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

B 测试中的路线提供者和 C 服务事件脚本是测试替身。20 人轨迹用例通过提供外部开始、完成事件来检查 B 的响应；它不验证真实 A 路线、真实 C 队列和统一调度是否已经接通。B 的运行说明见 `docs/11_B_STUDENT_MVP.md`。

`check_mvp_outputs.py` 在业务实现完成并真实导出四份 CSV 后使用，检查规范 20 人、2 窗口、120 分钟这一固定场景，包括全部状态、事件顺序、移动区间、队列和统计。

```bash
python3 tests/check_mvp_outputs.py outputs/mvp
```

该工具没有通用模拟器，不能用它证明其他场景的行为；其他场景与非法输入测试由各模块成员补充。它缺少输出文件时会失败，不会拿手算文件冒充业务输出。

完成统一调度并接通真实 A、B、C 和记录器后，在 CI 中增加真实应用运行和输出检查，再开展 `docs/07_ACCEPTANCE.md` 的跨模块边界与变参测试。当前没有把返回码 2 当作成功的业务测试。
