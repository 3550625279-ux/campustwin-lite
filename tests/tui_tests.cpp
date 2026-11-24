#include "campustwin/environment.hpp"
#include "campustwin/reporting.hpp"
#include "campustwin/resources.hpp"
#include "campustwin/seats.hpp"
#include "campustwin/simulation.hpp"
#include "campustwin/students.hpp"
#include "campustwin/tui.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char* argv[]) {
    using namespace campustwin;
    namespace fs = std::filesystem;

    const fs::path root = argc > 1 ? fs::u8path(argv[1]) : detect_data_root();
    const fs::path temp_dir = fs::temp_directory_path() / "campustwin_tui_test_sandbox";
    std::error_code ec;
    fs::remove_all(temp_dir, ec);
    fs::create_directories(temp_dir, ec);

    // 1. Verify InMemoryRunObserver & CompositeRunWriter polymorphism on flagship campus_day
    {
        CampusEnvironment env;
        env.load_directory(root / "data/scenarios/campus_day");
        auto pop = read_student_specs(root / "data/students/campus_day.csv");
        pop = read_schedules(root / "data/schedules/campus_day.csv", pop);

        StudentEngine students;
        FacilityEngine facilities;
        SeatEngine seats;
        InMemoryRunObserver observer;
        observer.set_population(pop);
        const fs::path csv_out = temp_dir / "composite_csv";
        CsvRunWriter csv_writer(csv_out, true);
        CompositeRunWriter composite;
        composite.add_writer(observer);
        composite.add_writer(csv_writer);

        SimulationRunner{}.run(SimulationSettings{160}, env, pop, students, facilities, composite, &seats);

        const auto& recorded = observer.recorded_run();
        assert(recorded.frames.size() == 161);
        assert(recorded.completed_activities_count() == 180);
        assert(recorded.late_classes_count() == 20);
        assert(recorded.total_class_delay_minutes() == 93);
        assert(recorded.total_observed_seat_wait() == 1053);
        assert(recorded.all_finished_minute().has_value() && *recorded.all_finished_minute() == 114);
        assert(fs::exists(csv_out / "activities.csv"));
        assert(fs::exists(csv_out / "seat_snapshots.csv"));
    }

    // 2. Verify registration, playback and editing using one authenticated account.
    {
        WorkbenchOptions opts;
        opts.data_root = root;
        opts.accounts_file = temp_dir / "private/accounts.db";
        opts.enable_color = false;
        // Small scenario for exercising the full playback flow quickly.
        opts.scenario_dir = root / "data/scenarios/campus_day";
        opts.students_file = root / "data/students/campus_day.csv";
        opts.schedule_file = root / "data/schedules/campus_day.csv";

        std::ostringstream scripted_input;
        scripted_input << "2\nalice\nexample-password\n"
                       // [1] Set playback speed for the custom scene
                       << "1\n3\n\n\n16\n"
                       // [2] Browse the overview, scene and student details, then animate
                       << "2\nn\nb\ng 0\nr\nr\nv\nv S001\nv\nv S_UNKNOWN\nn\nr\np 2 0\nk\nq\n"
                       // [3] Inspect student S001 then return
                       << "3\nS001\n0\n"
                       // [5] Query Dijkstra route L_T1 L_FAR and matrix
                       << "5\nL_T1 L_FAR\nmatrix\n0\n"
                       // A normal account can save its own edited scene.
                       << "6\n4\nL_LIBRARY 6\n7\n0\n"
                       << "0\n";

        std::istringstream in(scripted_input.str());
        std::ostringstream out;
        ConsoleWorkbench workbench(std::move(opts), in, out);
        const int rc = workbench.run();
        assert(rc == 0);

        const std::string rendered = out.str();
        assert(rendered.find("回放 1 倍") != std::string::npos);
        assert(rendered.find("回放 16 倍") != std::string::npos);
        assert(rendered.find("注册成功，已登录 alice") != std::string::npos);
        const std::string dashboard = "实时仿真态势看板";
        auto frame_start = rendered.find(dashboard);
        const auto next_frame = [&] {
            assert(frame_start != std::string::npos);
            const auto next = rendered.find(dashboard, frame_start + dashboard.size());
            const auto frame = rendered.substr(frame_start, next == std::string::npos ? next : next - frame_start);
            frame_start = next;
            return frame;
        };
        const auto assert_overview = [](const std::string& frame) {
            assert(frame.find("原地等待") != std::string::npos);
            assert(frame.find("全部完成") != std::string::npos);
            assert(frame.find("\n  场景明细\n") == std::string::npos);
            assert(frame.find("\n  学生明细") == std::string::npos);
            assert(frame.find("\n  本分钟事件明细\n") == std::string::npos);
        };
        assert_overview(next_frame());  // Initial view at the peak
        assert_overview(next_frame());  // Next minute
        assert_overview(next_frame());  // Previous minute
        const auto minute_zero = next_frame();
        assert_overview(minute_zero);
        assert(minute_zero.find("第 0 / 160 分钟") != std::string::npos);
        assert(minute_zero.find("开始活动") != std::string::npos);
        assert(minute_zero.find("S001") == std::string::npos);

        const auto scene = next_frame();
        assert(scene.find("第 0 / 160 分钟") != std::string::npos);
        assert(scene.find("\n  场景明细\n") != std::string::npos);
        assert(scene.find("地点与在途") != std::string::npos);
        assert(scene.find("本分钟事件明细") != std::string::npos);
        assert(scene.find("\n  学生明细") == std::string::npos);
        const auto scene_closed = next_frame();
        assert_overview(scene_closed);
        assert(scene_closed.find("第 0 / 160 分钟") != std::string::npos);

        const auto students = next_frame();
        assert(students.find("学生明细 · 预览") != std::string::npos);
        assert(students.find("S001") != std::string::npos && students.find("S002") != std::string::npos);
        const auto student = next_frame();
        assert(student.find("第 0 / 160 分钟") != std::string::npos);
        assert(student.find("学生明细 · S001") != std::string::npos);
        assert(student.find("S002") == std::string::npos);
        const auto student_closed = next_frame();
        assert_overview(student_closed);
        assert(student_closed.find("第 0 / 160 分钟") != std::string::npos);
        assert(next_frame().find("未找到学生 S_UNKNOWN") != std::string::npos);
        const auto minute_one = next_frame();
        assert_overview(minute_one);
        assert(minute_one.find("第 1 / 160 分钟") != std::string::npos);
        assert(next_frame().find("场景明细") != std::string::npos);
        const auto minute_two = next_frame();
        const auto minute_three = next_frame();
        assert_overview(minute_two);
        assert_overview(minute_three);
        assert(minute_two.find("第 2 / 160 分钟") != std::string::npos);
        assert(minute_three.find("第 3 / 160 分钟") != std::string::npos);
        assert_overview(next_frame());  // Jump back to the peak
        assert(rendered.find("学生档案: S001") != std::string::npos);
        const auto user = temp_dir / "private/accounts.db.users/u-616c696365";
        assert(fs::exists(fs::directory_iterator(user / "scenarios")->path() / "seats.csv"));
        assert(fs::exists(user / "runs"));
    }

    // A tiny flagship fixture covers login, both experiment modes, and the active plan.
    {
        const auto demo_root = temp_dir / "flagship_choices";
        const auto data = demo_root / "data";
        fs::create_directories(data / "scenarios");
        fs::create_directories(data / "students");
        fs::create_directories(data / "schedules");
        fs::copy(root / "data/scenarios/campus_day", data / "scenarios/campus_flagship",
                 fs::copy_options::recursive);
        std::ofstream(data / "students/campus_flagship.csv")
            << "id,initial_location,lunch_facility,after_meal_location,departure_minute,"
            << "walk_m_per_min,meal_minutes,lunch_policy,meal_type\n"
            << "S002,L_T1,F_NEAR,L_DORM_N,230,60,6,fixed,rice\n"
            << "S001,L_T1,F_NEAR,L_DORM_N,210,60,6,fixed,rice\n";
        std::ofstream(data / "schedules/campus_flagship.csv")
            << "student_id,sequence,activity_id,kind,location_id,start_minute,duration_minutes,requires_seat\n"
            << "S002,1,lunch,lunch,,230,6,0\n"
            << "S001,1,lunch,lunch,,210,6,0\n";
        WorkbenchOptions opts;
        opts.data_root = demo_root;
        opts.duration_minutes = 290;
        opts.enable_color = false;
        std::istringstream input("2\nbob\nexample-password\n"
                                 "3\nS002\n0\n"
                                 "4\n1\ninvalid\n1\nq\n3\nS002\n0\n"
                                 "4\n1\n2\nq\n3\nS002\n0\n"
                                 "4\n2\nS002\n0\n3\nS002\n0\n"
                                 "4\n1\n4\nq\n3\nS002\n0\n7\n1\nbob\nexample-password\n2\nq\n0\n");
        std::ostringstream output;
        const int rc = ConsoleWorkbench(opts, input, output).run();
        const auto result = output.str();
        assert(rc == 0);
        assert(result.find("[8] 导出") == std::string::npos);
        assert(result.find("操作异常") == std::string::npos);
        assert(result.find("请输入 1、2、3、4，或 0") != std::string::npos);
        const auto default_plan = result.find("决策时刻 t=230,");
        const auto selector = result.find("选择旗舰校园午餐实验");
        const auto concentrated = result.find("决策时刻 t=210,");
        const auto staggered20 = result.find("决策时刻 t=220,");
        const auto comparison = result.find("各方案汇总 · 截止 18:00");
        const auto unchanged = result.find("决策时刻 t=220,", comparison);
        const auto staggered60 = result.find("决策时刻 t=240,", unchanged);
        assert(default_plan < selector && selector < concentrated && concentrated < staggered20 &&
               staggered20 < comparison && comparison < unchanged && unchanged < staggered60 &&
               staggered60 != std::string::npos);
        assert(result.find("学生 S002 的四种日程", comparison) != std::string::npos);
        assert(result.find("按学号段对照", comparison) != std::string::npos);
        const auto logout = result.find("已退出登录。");
        assert(logout != std::string::npos);
        assert(result.find("已读取本地仿真结果", logout) != std::string::npos);
        assert(result.find("正在计算仿真", logout) == std::string::npos);
    }

    // Across process-like reopenings, only matching inputs for the same account may hit the cache.
    {
        WorkbenchOptions opts;
        opts.data_root = root;
        opts.accounts_file = temp_dir / "private/accounts.db";
        opts.scenario_dir = root / "data/scenarios/campus_day";
        opts.students_file = root / "data/students/campus_day.csv";
        opts.schedule_file = root / "data/schedules/campus_day.csv";
        opts.enable_color = false;
        const auto session = [&](const std::string& commands) {
            std::istringstream input(commands);
            std::ostringstream output;
            if (ConsoleWorkbench(opts, input, output).run() != 0)
                throw std::runtime_error("workbench failed");
            return output.str();
        };

        auto text = session("1\nalice\nwrong-password\n1\nalice\nexample-password\n2\ns\nq\n0\n");
        assert(text.find("用户名或密码错误") != std::string::npos);
        assert(text.find("已读取本地仿真结果") != std::string::npos);
        assert(text.find("正在计算仿真") == std::string::npos);
        const auto users = temp_dir / "private/accounts.db.users";
        assert(text.find("截止完成人数: 36/36") != std::string::npos);
        assert(text.find("已完成活动 180/216") != std::string::npos);

        // The uppercase spelling is a different account even on Windows.
        text = session("1\nalice\nexample-password\n7\n2\nAlice\nexample-password\n2\nq\n0\n");
        const auto logout = text.find("已退出登录。");
        const auto login_page = text.find("CampusTwin Lite · 本地账号", logout);
        const auto new_account = text.find("注册成功，已登录 Alice");
        assert(logout < login_page && login_page < new_account && new_account != std::string::npos);
        assert(text.find("正在计算仿真") != std::string::npos);
        assert(text.find("已读取本地仿真结果") == std::string::npos);
        const auto second = users / "u-416c696365";
        const auto file = fs::directory_iterator(second / "runs")->path();
        assert(file.extension() == ".run");

        std::ofstream(file, std::ios::binary | std::ios::trunc) << "broken";
        text = session("1\nAlice\nexample-password\n2\nq\n0\n");
        assert(text.find("正在计算仿真") != std::string::npos);
        assert(text.find("本地结果保存失败") == std::string::npos);
        assert(fs::file_size(file) > 6);
        opts.duration_minutes = 80;
        text = session("1\nalice\nexample-password\n2\nq\n0\n");
        assert(text.find("正在计算仿真") != std::string::npos);
        assert(text.find("第 80 分钟") != std::string::npos || text.find("/ 80 分钟") != std::string::npos);

        // The importer only accepts numbered bundles in the signed-in user's directory.
        const auto imports = users / "u-616c696365/imports";
        text = session("1\nalice\nexample-password\n1\n2\n0\n0\n");
        assert(fs::is_directory(imports));
        assert(text.find("暂无可选场景") != std::string::npos);
        fs::create_directories(imports / "incomplete-campus");
        const auto imported = imports / "a-campus";
        fs::copy(root / "data/scenarios/campus_day", imported, fs::copy_options::recursive);
        fs::copy_file(root / "data/students/campus_day.csv", imported / "students.csv");
        fs::copy_file(root / "data/schedules/campus_day.csv", imported / "schedule.csv");
        const auto broken = imports / "b-campus";
        fs::copy(imported, broken, fs::copy_options::recursive);
        std::ofstream(broken / "schedule.csv", std::ios::trunc) << "invalid\n";

        text = session("1\nalice\nexample-password\n1\n2\n0\n1\n2\n99\n1\n1\n2\n2\n0\n");
        assert(text.find("[1] a-campus") < text.find("[2] b-campus"));
        assert(text.find("incomplete-campus") == std::string::npos);
        assert(text.find("场景目录路径>") == std::string::npos);
        assert(text.find("学生 CSV 路径>") == std::string::npos);
        assert(text.find("请输入列表中的场景编号") != std::string::npos);
        const auto loaded = text.find("已加载 自定义场景 (a-campus)");
        const auto rejected = text.find("操作异常", loaded);
        assert(loaded != std::string::npos && rejected != std::string::npos);
        assert(text.find("自定义场景 (a-campus)", rejected) != std::string::npos);
    }

    fs::remove_all(temp_dir, ec);
    std::cout << "PASS: TUI authentication, per-account cache, playback and lunch experiment menus\n";
    return 0;
}
