# A 部分交付与学习说明

核对日期：2026-10-26。依据用户上传的《CampusTwin Lite 项目简介》《架构与三人任务书 v0.1》，并核对公开仓库 main 基线 `377c83e7d5a06e5fce5663bc2739f3d0c3a71ba1`。

本文是 2026-10-26 的 A 交付历史记录，表格和“下一轮”描述反映当时状态。后续已实现 B/C 和统一仿真；当前状态与运行命令请看 [README](../README.md)。A 的独立测试通过本身不能证明整个系统正确。

## 1. 开工核对：原有内容与具体缺口

| A 的实现清单 | 仓库基线现状 | 本次结果 |
| --- | --- | --- |
| 本地账号注册、登录、重启后登录 | 没有账号模块；README原安排在MVP后 | 新增 A 私有 AccountStore；管理命令要求登录 |
| 读取地点/道路/设施 CSV | 三份样例齐全，load为NOT_IMPLEMENTED | 实现，错误提示含文件、行号及字段上下文 |
| 校验重复ID、数值、类型和引用 | 文档与Python样例校验器已有，C++缺失 | C++使用同一套静态校验；加载和编辑一致 |
| 新建、添加/修改/删除地点 | 只有接口草案，没有业务 | 空场景可逐步建立；依赖存在时拒绝删除 |
| 道路距离与食堂窗口配置 | Road/FacilityConfig已有，编辑缺失 | 双向道路新增/更新/删除，设施静态参数新增/更新/删除 |
| 文件保存、加载、重启恢复 | save/replace都是占位 | 只保存三份静态CSV；重启验收通过 |
| 地点间可达性、最短路线与距离 | find_route为占位 | Dijkstra；同点0米、不可达false/空路径、未知ID报错 |
| A独立演示与业务测试 | demo_a占位；CTest只有样例格式测试 | 真实交互入口，新增环境/账号/控制台验收 |
| 薄调度器与B/C联合仿真 | 全是占位；负责人有文档分歧 | 本次保留，列为团队下一阶段工作 |

仓库使用 C++17、CMake 3.16+，运行库依赖标准库。开启测试需 Python 3.9+。不引入GUI、数据库或网络账号。各模块仍通过同一公共契约组合，A入口只链接环境库。

## 2. 接口兼容建议与团队需确认事项

公共接口仍标为“v0.1，待团队确认”。本次把现有 `contracts.hpp` 当作兼容基线，未修改该文件、现有CSV表头/单位/样例，也未修改B/C业务文件。

- 原任务书称 `CampusConfig` / `RouteQuery(from,to,mode)`；仓库已经改为 `CampusScenario` / `IRouteProvider::find_route(from,to)`。继续使用后者。首版只有步行，不另加mode；未来交通方式另提契约变更。
- 原任务书有6地点/2食堂，仓库MVP明确为5地点/1食堂。保留仓库固定ID和5/6/1样例，可在A管理界面增加地点，不擅自改B/C输入。
- 原任务书说B负责统一循环，仓库后来将薄调度器分给A。建议沿用仓库分配，但三人确认前本次不实现/改写调度器和事件顺序。
- 当前集成场景的窗口数必须大于0。C可单独讨论0窗口边界；若要用0表示食堂关闭，三人先明确拒绝服务事件及语义，再同步契约、C实现和校验器。
- 账号不加入共享仿真对象。当前所有有效账号对本地场景有相同管理能力；管理员/访客角色、按账号隔离场景、修改密码均待另行约定。
- A只校验地点的道路/设施依赖。外部学生文件归B；删地点或设施后，B必须在reset时重新校验其引用。不能让A偷偷读取或改动学生运行状态来解决引用问题。
- 一次仿真开始前冻结静态场景，运行中不编辑。B/C只读取 `const CampusScenario&`；A不保存/修改学生位置、活动、队列、服务窗口运行状态。

具体类增加便利编辑方法：`add_location`、`update_location`、`remove_location`、`set_road`、`remove_road`、`set_facility`、`remove_facility`。这些方法属于A具体类，B/C仍只依赖原接口。`update_location`保留ID，改名不会改变ID；`set_road`按无向端点匹配，`set_facility`按设施ID匹配。

## 3. 编译与启动

先解压源码，进入含 `CMakeLists.txt` 的仓库根目录。建议使用 Visual Studio 2022 的开发者 PowerShell，或已配置好现代C++17编译器与CMake的终端。

Windows（Visual Studio）：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\demo_a.exe
```

若普通PowerShell提示找不到cmake，可从Visual Studio打开文件夹工程，或把Visual Studio自带CMake的bin目录加入当前终端PATH。本机已有VS2022/MSVC14.44，不需要安装新编译器；实际使用的CMake位于VS的 `Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/`。

Linux/macOS（现代工具链，提供构建方式，本次未实机验证）：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/demo_a
```

只运行不做测试：首次配置加 `-DBUILD_TESTING=OFF` 即不要求Python。本机旧MinGW/GCC8.1在标准库filesystem内部编译失败；本次使用现有VS2022验证，未为旧库改动公共接口。

Windows交互输入中文地点名时，请使用UTF-8终端，必要时在运行前执行 `chcp 65001`；程序文件、管道输入使用UTF-8。Windows入口使用wmain转换UTF-16参数，因此中文命令行路径可正确传入。

## 4. 五分钟独立演示

以下每行在提示符后输入。密码由下一行单独输入，**此控制台会回显密码**；使用课程演示密码。用户名需3–32位ASCII字母/数字/下划线，密码8–128字节且不含控制字符。无预设账号，也没有硬编码管理员密码。

```text
register student_a
example-pass-42
login student_a
example-pass-42
list
route L_T1 L_CANTEEN
route L_T2 L_DORM
road-set L_T1 L_CANTEEN 150.25
facility-set F_CANTEEN L_CANTEEN 2 2
save "outputs/my_campus"
quit
```

应看到初始 `Locations=5 Roads=6 Facilities=1`，两次查询分别是120米和300米；编辑后保存到新目录。

关闭程序，再运行：

```powershell
.\build\Release\demo_a.exe --scenario "outputs/my_campus"
```

输入login、同一用户名/密码，然后执行list、route，应看到窗口数2、教学楼1到食堂距离150.25米。这证明数据经过了磁盘和进程重启，而非仅留在内存。

从零建立场景用 `demo_a --empty`。登录后使用：

```text
new
location-add T "教学楼" Teaching 200
location-add C "食堂" Canteen 100
road-set T C 120
facility-set F C 1 2
route T C
save "outputs/new_campus"
quit
```

命令说明：

| 命令 | 作用 |
| --- | --- |
| `help` / `quit` / `logout` | 帮助、退出、注销 |
| `list` / `new` | 查看地点/道路/设施；开始空场景 |
| `load "DIR"` / `save "DIR"` | 加载或保存场景目录 |
| `location-add ID "NAME" KIND CAPACITY` | 新增地点；类别Teaching/Canteen/Dormitory/Library |
| `location-update ID "NAME" KIND CAPACITY` | 更新已有地点，ID不变 |
| `location-remove ID` | 删除无道路/设施引用的地点 |
| `road-set FROM TO METERS` / `road-remove FROM TO` | 新增/更新或删除无向道路 |
| `facility-set ID LOCATION WINDOWS SERVICE_MINUTES` | 新增/更新食堂静态资源参数 |
| `facility-remove ID` / `route FROM TO` | 删除设施配置；查询最短路线 |

含空格的路径或名称用双引号，Windows反斜杠保留，也可用 `C:/...`。CSV禁止字段内逗号、双引号、换行，不提供引号转义。未保存编辑以 `*` 标记；new/load/quit拒绝丢弃编辑，先save。输入结束而未保存返回1。处理过任何错误，本次退出返回1，便于脚本判断；无错误为0。

默认从仓库根目录加载 `data/scenarios/mvp`；默认账号在 `data/private/accounts.db`，已被gitignore排除。可用 `--scenario DIR` / `--accounts FILE` 指定路径。默认场景加载失败会退出；可用 `--empty`启动后再load。程序运行中load失败会保留旧场景并继续接受命令。

## 5. 怎样读懂实现

建议阅读顺序：公共 `contracts.hpp` → `environment.hpp` → `environment.cpp` → `demo_a.cpp` → 对应测试。账号模块可单独阅读。

`CampusScenario`只是一组输入数据，内部有地点、道路和设施三个vector。`CampusEnvironment`负责把它校验并转成邻接表：每条双向道路在两端各记录一次。查询时优先队列总是先取当前累计距离最小的地点；previous记录它从哪里来，最终反向还原路径。邻接表按ID排序，同距路线的选择稳定。算法不模拟学生行走，也不推进时间；B拿总距离后计算行走分钟数。

每次编辑先复制scenario，在副本改一项，再走replace_scenario。只有校验和新图构造全部成功才swap进入正式状态。这样一条负距离、悬空设施或重复ID不会污染当前图。load同样先读完三份文件再提交。

账号通过随机盐和PBKDF2-HMAC-SHA256保存口令摘要；不把密码原文写到文件。底层算法有NIST/RFC已知答案测试，仅在A私有模块使用。业务调用者只需理解register_user/login。该实现用于单进程离线课程演示；不提供并发注册、账户角色或网络认证。

## 6. 测试方法与已完成验证

统一运行CTest即可，所有测试都使用临时目录，不修改仓库样例：

| 测试名 | 核查内容 |
| --- | --- |
| sample_data_contract | 原仓库样例配置、学生文件及手算fixture合法性 |
| a_environment | 149项运行时检查：样例路由、反向/同点/断开、未知ID、非法替换不污染、CRUD、引用拒删、中文与浮点重载、坏CSV、I/O错误、溢出和极小距离 |
| a_accounts | SHA256/HMAC/PBKDF2标准向量；注册、重启登录、错误口令、重复用户、坏文件、不同盐、保存失败内存回滚 |
| a_console | 真实demo_a进程：登录门禁→编辑→保存→重启→恢复；新建场景、参数错误、Windows含空格/反斜杠/中文路径、未保存保护 |

2026-10-26，本机VS2022/MSVC14.44 Release完整构建成功，CTest四组全通过。环境模块149项检查通过。未在Linux/macOS实机运行。构建在临时目录时MSBuild提示MSB8029临时目录增量构建警告；交付源码请解压到普通工作目录后重新配置。

手工边界测试可登录后输入：负距离、unknown地点路线、0窗口、删除L_T1。均应报错；再查原路线应保持原值。把保存目录中的表头改错、删除一份CSV或录入反向重复边，应加载失败且原场景不变。

## 7. 交接范围与剩余工作

交付保留原工程目录，不交三份互不兼容的main。变更集中于A环境实现、A私有账号、demo_a、对应测试及少量CMake/状态文档。公共契约、B/C实现、原始数据、仿真入口均保持基线。

下一轮：三人确认v0.1接口与调度归属；B实现学生引擎，C实现排队/记录器；A按现有顺序实现薄调度器并接入。A不自行补写B/C状态机来假装闭环完成。窗口1→2的对照效果必须等真实B/C集成后测量，本次只证明静态参数可编辑并恢复。

保存会检查文件写入和关闭错误，但三份CSV不是断电时整体原子事务；错误后不要把半份输出用于实验，保留原目录并另存新目录。账号保存有临时文件/备份失败回滚，仍不承诺断电恢复和多进程同时注册。控制台没有编辑历史、撤销或角色权限。

与团队合并时，建议在自己的功能分支审阅改动并提交PR；公共接口不变，但CMake是共同文件，请在PR明确新增源文件与测试目标。本次只形成可审阅的本地参考实现，没有推送、合并或更改GitHub仓库设置。

原任务书要求成员理解实现并记录AI辅助范围。请逐步运行、解释算法与测试，再自行整理周记与答辩，按课程规则使用这份参考实现。
