# 验证范围

`validate_sample_data.py` 校验仓库中的固定配置、引用和手算参考。当前CTest同时运行它及新增的A环境、账号和真实控制台测试；公共接口的一致性由C++编译链接检验。A独立验收通过不代表B/C或整组仿真完成，详见[A交付说明](../docs/A_DELIVERY.md)。

```bash
python3 tests/validate_sample_data.py
```

`check_mvp_outputs.py` 在业务实现完成并真实导出四份 CSV 后使用，检查规范 20 人、2 窗口、120 分钟这一固定场景，包括全部状态、事件顺序、移动区间、队列和统计。

```bash
python3 tests/check_mvp_outputs.py outputs/mvp
```

该工具没有通用模拟器，不能用它证明其他场景的行为；其他场景与非法输入测试由各模块成员补充。它缺少输出文件时会失败，不会拿手算文件冒充业务输出。

实现完成后在 CI 中增加真实应用运行和输出检查，再开展 `docs/07_ACCEPTANCE.md` 的边界与变参测试。为了不掩盖初始占位，当前没有把返回码 2 当作成功的业务测试。
