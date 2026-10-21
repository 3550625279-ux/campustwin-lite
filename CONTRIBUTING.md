# 三人协作方法

## 第一次开工

1. 在任务书中填入 A/B/C 与 GitHub 用户名，并确认接口。
2. 仓库负责人需要给另外两位成员写入权限；公开可读不代表人人可以 push。未取得权限时可先 fork 再提 PR。
3. 每人 clone 同一个仓库，完成 README 的构建和数据测试。
4. 从最新 `main` 建一个短期功能分支，认领 `docs/planning/INITIAL_ISSUES.md` 中的小任务。

## 每个任务的最小流程

以下以 B 的移动功能为例，使用计划仓库地址；仓库建立后执行。每个任务换一个新分支，不为每个人保留一个永远不合并的长期大分支。

```bash
git clone https://github.com/3550625279-ux/campustwin-lite.git
cd campustwin-lite
git switch main
git pull --ff-only origin main
git switch -c feat/b-movement
```

编辑自己的文件，构建与验证，然后明确选择要提交的文件：

```bash
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
git status
git add src/students include/campustwin/students.hpp examples/demo_b.cpp
git commit -m "feat(students): implement timed movement and arrivals"
git push -u origin feat/b-movement
```

在 GitHub 提 PR：`feat/b-movement → main`，按模板说明输入、行为和验证。至少另一位成员审阅；公共协议变更需三人都知悉并确认。合并完成后从最新 main 开下一个分支。

## 与 main 同步和解决冲突

功能分支上有未提交修改时先妥善提交，保持工作区干净，再执行：

```bash
git fetch origin
git merge origin/main
```

有冲突就查看 `git status`，逐文件处理冲突标记，编译验证后 `git add` 对应文件并提交合并。不要把整份文件改回自己的旧版本以“解决”冲突，也不向 main force push。

## 什么可以自己改

| 变更 | 处理方式 |
| --- | --- |
| 自己目录内的私有算法/字段 | 普通 PR，附测试结果 |
| 公共头文件、CSV 列、单位、事件顺序 | 先给出接口变更说明，三人确认后合入，再迁移调用方 |
| 根 CMake、CI、公共样例 | 告知维护人，避免同时编辑同一段 |
| 结果目录、编译产物、个人 IDE 文件 | 不提交；由 `.gitignore` 排除 |

每个 `.cpp` 自行包含所需标准头文件，禁止 `bits/stdc++.h`、绝对本地路径和 `using namespace std;` 放进公共头文件。业务库中不写独立 `main`，独立入口放 `examples/`。

## PR 合并标准

- 改动可在仓库根目录通过统一构建，不依赖成员私有电脑目录。
- 对应功能有独立演示或有意义的测试；初始骨架的绿色 CI 不替代功能验收。
- 没有绕过接口修改他人状态；同步了必要文档。
- 新函数不再使用 `not_implemented` 占位；明确列出尚未完成的其他功能。
- 实现与解释由成员负责；按课程要求记录 AI 辅助的范围，不把生成的完整成品作为本人作品。

## 仓库设置建议（尚未执行）

等初始 CI 在远端真实运行后，再由管理员为 `main` 配置 PR 合并、至少 1 个批准、Linux/Windows 构建检查通过、限制 force push。公共头文件由团队额外评审；未填真实账号前不启用含占位用户的 CODEOWNERS。

模板只提供流程，不会自动开 Issue、发 PR、邀请成员或改变仓库设置。

官方参考：

- [GitHub：使用分支、提交和 PR 协作](https://docs.github.com/en/pull-requests/concepts/writing-code-for-a-project)
- [GitHub：受保护分支](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches/about-protected-branches)
- [GitHub 官方 checkout action](https://github.com/actions/checkout)（初始工作流使用 v6，权限仅 contents: read）
