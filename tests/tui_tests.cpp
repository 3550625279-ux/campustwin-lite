#include "campustwin/environment.hpp"
#include "campustwin/reporting.hpp"
#include "campustwin/resources.hpp"
#include "campustwin/seats.hpp"
#include "campustwin/simulation.hpp"
#include "campustwin/students.hpp"
#include "campustwin/tui.hpp"

#include <cassert>
#include <filesystem>
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
        assert(recorded.completed_activities_count() == 216);
        assert(recorded.late_classes_count() == 56);
        assert(recorded.total_class_delay_minutes() == 954);
        assert(recorded.total_observed_seat_wait() == 885);
        assert(recorded.all_finished_minute().has_value() && *recorded.all_finished_minute() == 124);
        assert(fs::exists(csv_out / "activities.csv"));
        assert(fs::exists(csv_out / "seat_snapshots.csv"));
    }

    // 2. Verify interactive ConsoleWorkbench workflow, RBAC permissions, playback, inspector, experiments & export
    {
        WorkbenchOptions opts;
        opts.data_root = root;
        opts.accounts_file = temp_dir / "private/accounts.db";
        opts.enable_color = false;

        std::ostringstream scripted_input;
        // Guest tries [6] editor (rejected) and [8] export (rejected)
        scripted_input << "6\n"
                       << "8\n"
                       // [2] Run simulation & test playback commands: next, prev, jump 25, peak, quit
                       << "2\nn\nb\n25\nk\nq\n"
                       // [3] Inspect student S001 then return
                       << "3\nS001\n0\n"
                       // [4] Run Stage-3 schedule bottleneck experiment
                       << "4\n3\n"
                       // [5] Query Dijkstra route L_T1 L_FAR and matrix
                       << "5\nL_T1 L_FAR\nmatrix\n0\n"
                       // [7] Demo Admin login (PBKDF2)
                       << "7\n1\n"
                       // [6] Admin edits L_LIBRARY seat pool capacity to 6, then returns
                       << "6\n4\nL_LIBRARY 6\n0\n"
                       // [8] Export current modified run to CSV directory
                       << "8\n1\n" << (temp_dir / "ui_exported").u8string() << "\n"
                       // [0] Quit dirty scenario with confirmation y
                       << "0\ny\n";

        std::istringstream in(scripted_input.str());
        std::ostringstream out;
        ConsoleWorkbench workbench(std::move(opts), in, out);
        const int rc = workbench.run();
        assert(rc == 0);

        const std::string rendered = out.str();
        assert(rendered.find("权限拒绝") != std::string::npos);
        assert(rendered.find("实时仿真态势看板") != std::string::npos);
        assert(rendered.find("学生档案: S001") != std::string::npos);
        assert(rendered.find("357m") != std::string::npos);
        assert(rendered.find("t=94m") != std::string::npos);
        assert(rendered.find("已通过 PBKDF2 校验登录为管理员 [admin]") != std::string::npos);
        assert(fs::exists(temp_dir / "ui_exported/activities.csv"));
        assert(fs::exists(temp_dir / "ui_exported/summary.csv"));
    }

    fs::remove_all(temp_dir, ec);
    std::cout << "PASS: TUI workbench, InMemoryRunObserver, CompositeRunWriter and RBAC verified\n";
    return 0;
}
