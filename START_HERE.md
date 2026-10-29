# 新成员从这里开始

这个仓库是三个人共同开发同一个程序的起点。你第一次加入时，需要先做到三件事：知道自己负责什么；在本机编译公共工程；提交一个小而可验证的功能。

## 1. 先建立整体认识

项目模拟一次校园午餐：学生下课走到食堂，排队取餐，吃饭后回宿舍。出行时长由路程和速度决定；排队时长由真实队列和窗口决定。最终导出每分钟状态与资源统计。

| 角色 | 最接近本质的工作 | 第一次打开的实现文件 |
| --- | --- | --- |
| A | 定义校园环境，让其他模块能加载场景和查询路径 | `src/environment/environment.cpp` |
| B | 维护学生此刻在哪里、做什么、何时到达 | `src/students/students.cpp` |
| C | 维护谁在排队、谁被哪个窗口服务、何时完成 | `src/resources/resources.cpp` |

公共调度由 A 维护，三人联调；CSV 结果记录由 C 维护。角色到姓名的对应关系尚需团队填写。

## 2. 按这个顺序看文件

1. [MVP 范围](docs/01_MVP.md)：明确第一阶段必做和延期内容。
2. [三人任务书](docs/03_TEAM_TASKS.md)：找到自己的目录、任务和完成标准。
3. [接口契约](docs/04_API_CONTRACT.md)与 [contracts.hpp](include/campustwin/contracts.hpp)：读自己实际会调用和实现的类型，不必一次背完所有字段。
4. [数据格式](docs/05_DATA_FORMAT.md)：拿样例 CSV 对照字段。
5. [集成步骤](docs/06_INTEGRATION.md)与 [验收用例](docs/07_ACCEPTANCE.md)：知道独立模块最后怎样接入统一程序。

如果你不清楚 `.hpp`、`.cpp`、模块库、demo 和 `main` 之间的区别，先看[仓库结构详细讲解](docs/00_REPOSITORY_GUIDE.md)，再进入实现。

## 3. 在本机编译

使用 C++17 编译器、CMake 3.16+ 和 Python 3.9+。这些是本机开发工具；C++ 运行模块不依赖第三方业务库。

仓库建立后，从根目录执行：

```bash
git clone https://github.com/3550625279-ux/campustwin-lite.git
cd campustwin-lite
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

当前应当构建成功，样例数据和A业务测试通过。`demo_a`已实现账号、场景管理与路线查询，见[A交付说明](docs/A_DELIVERY.md)。执行`campustwin`、`demo_b`或`demo_c`仍输出`NOT_IMPLEMENTED`并返回2，整组MVP尚未完成。

Linux/macOS 的常见路径为 `build/campustwin`；Windows 的 Visual Studio 生成器常见路径为 `build/Debug/campustwin.exe`。运行相对数据路径时，工作目录保持在仓库根目录。

若没有 Python，只编译可使用 `cmake -S . -B build -DBUILD_TESTING=OFF`；这会跳过样例检查，不能当作完成测试。

## 4. 第一个小任务怎么选

| 角色 | 首个 PR 建议 | 可以暂时替代的外部依赖 | 第一份证据 |
| --- | --- | --- | --- |
| A | 加载并校验三份场景 CSV，提供只读场景 | 不依赖 B/C | `demo_a` 打印 5 地点、6 道路、1 设施；错误文件明确失败 |
| B | 用固定路线查询替身完成学生移动与到达 | A 用 `IRouteProvider` 测试替身；C 暂不接 | t=2/t=3 各到达 10 人，每人只发一次到达 |
| C | 实现三人单窗口队列用例 | 不依赖 B，直接读测试到达序列 | 开始/完成事件与手算序列一致 |

第一次不必写完整个子系统。实现当前小任务，删除对应函数的 `not_implemented` 调用；其他未完成函数保留明确占位。一个函数不能既留着占位抛错，又在后面写永远到不了的业务逻辑。

## 5. 每人怎样提交

先取得仓库写入权限；只有公开可读权限时，不能直接向这个仓库 push。仓库负责人明确邀请用户名后再处理协作权限。

每项功能从最新 main 建短期分支：`feat/a-scenario-load`、`feat/b-movement`、`feat/c-service-queue` 等。完成后 push 自己的分支并开 PR，描述实际输入、行为和验证结果；由队友审阅后合并到 main。

具体命令、冲突处理和 PR 标准见 [CONTRIBUTING.md](CONTRIBUTING.md)。先不要三个人直接同时往 main 提交。

## 6. 哪些地方必须先共同讨论

`contracts.hpp` 的字段、时间单位、事件先后顺序、CSV 列和统计含义是公共约定。确实需要修改时，先写影响说明，经三人确认后用一个专门 PR 更新，不在个人分支里另造第二套定义。

自己模块内部的数据结构和算法可以自行选择，只要遵守公共接口。例如 A 可以更换最短路实现，B 不需要知道其内部图容器是什么。

## 7. 什么时候算完成

个人任务完成：独立演示能运行，输入输出可复现，接口与文档一致。MVP 完成：真实 A/B/C 已接通，按 20 人样例输出四份 CSV，通过输出检查、边界与变参验收。

“编译通过”“所有文件已上传”“有绿色 CI”都不能单独说明 MVP 已完成。账号权限等课程必需项在后续节点补齐，最终提交前仍须核查老师要求。
