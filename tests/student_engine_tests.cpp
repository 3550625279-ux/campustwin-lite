#include "campustwin/students.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace campustwin;
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template<class F>
std::string rejects(F action) {
    try { action(); }
    catch (const ValidationError& error) { return error.what(); }
    throw std::runtime_error("expected ValidationError");
}

CampusScenario scene() {
    CampusScenario result;
    result.locations = {{"L_T1", "T1", LocationKind::Teaching, 200},
                        {"L_T2", "T2", LocationKind::Teaching, 200},
                        {"L_CANTEEN", "Canteen", LocationKind::Canteen, 100},
                        {"L_DORM", "Dormitory", LocationKind::Dormitory, 200}};
    result.facilities = {{"F_CANTEEN", "L_CANTEEN", 2, 2}};
    return result;
}
StudentSpec spec(std::string id = "S001") {
    return {std::move(id), "L_T1", "F_CANTEEN", "L_DORM", 0, 60.0, 5};
}
class Routes final : public IRouteProvider {
public:
    double t1{120.0}, t2{180.0}, back{120.0};
    bool t1_reachable{true}, t2_reachable{true}, back_reachable{true};
    mutable std::size_t calls{0};
    RouteInfo find_route(const LocationId& from, const LocationId& to) const override {
        ++calls;
        if (from == "L_T1" && to == "L_CANTEEN") return {t1_reachable, t1, {from, to}};
        if (from == "L_T2" && to == "L_CANTEEN") return {t2_reachable, t2, {from, to}};
        if (from == "L_CANTEEN" && to == "L_DORM") return {back_reachable, back, {from, to}};
        return {};
    }
};

void shape(const StudentSnapshot& snapshot, StudentState state) {
    require(snapshot.state == state, "unexpected student state at t=" + std::to_string(snapshot.at));
    require(snapshot.move.has_value() == (state == StudentState::Moving), "movement field mismatch");
    require(snapshot.location_id.has_value() == (state != StudentState::Moving), "location field mismatch");
}
StudentArrival reach_queue(StudentEngine& engine, const Routes& routes) {
    engine.reset(scene(), {spec()});
    require(engine.tick(0, routes).empty(), "early arrival at 0");
    require(engine.tick(1, routes).empty(), "early arrival at 1");
    const auto arrivals = engine.tick(2, routes);
    require(arrivals.size() == 1, "missing arrival at 2");
    return arrivals.front();
}
ServiceStarted start_at(SimTime at = 2, SimTime wait = 0) {
    return {"S001:lunch:1", "S001", "F_CANTEEN", 0, at, wait};
}
ServiceCompleted complete_at(SimTime at = 4) {
    return {"S001:lunch:1", "S001", "F_CANTEEN", 0, at};
}

struct TempDirectory {
    std::filesystem::path path;
    TempDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int i = 0; i < 100; ++i) {
            auto candidate = std::filesystem::temp_directory_path() /
                ("campustwin-students-" + std::to_string(stamp) + "-" + std::to_string(i));
            if (std::filesystem::create_directory(candidate)) { path = candidate; return; }
        }
        throw std::runtime_error("cannot create test directory");
    }
    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    std::filesystem::path write(const std::string& content) const {
        const auto file = path / "students.csv";
        std::ofstream out(file, std::ios::binary);
        out << content;
        out.close();
        if (!out) throw std::runtime_error("cannot write test fixture");
        return file;
    }
};
const std::string header =
    "id,initial_location,lunch_facility,after_meal_location,departure_minute,walk_m_per_min,meal_minutes";
const std::string row = "S001,L_T1,F_CANTEEN,L_DORM,0,60,5";
}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) { std::cerr << "Usage: student_engine_tests REPOSITORY_ROOT\n"; return 2; }
    const std::filesystem::path root = argv[1];
    int passed = 0, failed = 0;
    auto test = [&](const char* name, const std::function<void()>& action) {
        try { action(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) {
            ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };

    test("read canonical 20-student file", [&] {
        const auto students = read_student_specs(root / "data/students/mvp.csv");
        require(students.size() == 20, "wrong population size");
        require(students.front().id == "S001" && students.back().id == "S020", "wrong IDs");
        require(students[10].initial_location == "L_T2", "wrong second group");
    });
    test("CRLF, empty lines, scientific decimal and header-only file", [&] {
        TempDirectory temp;
        auto file = temp.write(header + "\r\n\r\nS001,L_T1,F_CANTEEN,L_DORM,3,6e1,5\r\n");
        const auto students = read_student_specs(file);
        require(students.size() == 1 && students[0].departure_minute == 3 &&
                students[0].walk_m_per_min == 60.0, "valid CRLF CSV rejected");
        require(read_student_specs(temp.write(header + "\n")).empty(), "empty population rejected");
    });
    test("reject malformed headers, columns, IDs and numbers with row context", [&] {
        TempDirectory temp;
        const std::vector<std::string> files = {
            "", row + "\n", "\xEF\xBB\xBF" + header + "\n" + row,
            header + "\n" + row + ",extra", header + "\nS001,L_T1",
            header + "\n\"S001\",L_T1,F_CANTEEN,L_DORM,0,60,5",
            header + "\nS 001,L_T1,F_CANTEEN,L_DORM,0,60,5",
            header + "\nS001,L_T1,F_CANTEEN,L_DORM,-1,60,5",
            header + "\nS001,L_T1,F_CANTEEN,L_DORM,2147483648,60,5",
            header + "\nS001,L_T1,F_CANTEEN,L_DORM,0,60x,5",
            header + "\nS001,L_T1,F_CANTEEN,L_DORM,0,nan,5",
            header + "\nS001,L_T1,F_CANTEEN,L_DORM,0,inf,5",
            header + "\nS001,L_T1,F_CANTEEN,L_DORM,0,0,5",
            header + "\nS001,L_T1,F_CANTEEN,L_DORM,0,-60,5",
            header + "\nS001,L_T1,F_CANTEEN,L_DORM,0, 60,5",
            header + "\nS001,L_T1,F_CANTEEN,L_DORM,0,60,0",
            header + "\n" + row + "\n" + row
        };
        for (const auto& content : files)
            rejects([&] { read_student_specs(temp.write(content)); });
        const auto message = rejects([&] {
            read_student_specs(temp.write(header + "\n\nS001,L_T1,F_CANTEEN,L_DORM,0,0,5"));
        });
        require(message.find(":3") != std::string::npos &&
                message.find("walk_m_per_min") != std::string::npos, "missing row/field context");
    });
    test("missing input file reports an I/O error", [&] {
        TempDirectory temp;
        bool rejected = false;
        try { read_student_specs(temp.path / "missing.csv"); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "missing file accepted");
    });
    test("reset validates object inputs and scene references", [&] {
        StudentEngine engine;
        std::vector<StudentSpec> invalids;
        auto add = [&](const std::function<void(StudentSpec&)>& change) {
            auto s = spec(); change(s); invalids.push_back(s);
        };
        add([](auto& s) { s.id.clear(); });
        add([](auto& s) { s.initial_location = "unknown"; });
        add([](auto& s) { s.after_meal_location = "unknown"; });
        add([](auto& s) { s.lunch_facility = "unknown"; });
        add([](auto& s) { s.initial_location = "L_CANTEEN"; });
        add([](auto& s) { s.after_meal_location = "L_CANTEEN"; });
        add([](auto& s) { s.departure_minute = -1; });
        add([](auto& s) { s.meal_minutes = 0; });
        add([](auto& s) { s.walk_m_per_min = std::numeric_limits<double>::infinity(); });
        for (const auto& s : invalids) rejects([&] { engine.reset(scene(), {s}); });
        rejects([&] { engine.reset(scene(), {spec(), spec()}); });
        auto bad_scene = scene(); bad_scene.facilities[0].location_id = "L_T1";
        rejects([&] { engine.reset(bad_scene, {spec()}); });
        bad_scene = scene(); bad_scene.facilities[0].window_count = 0;
        rejects([&] { engine.reset(bad_scene, {spec()}); });
        bad_scene = scene(); bad_scene.facilities[0].service_minutes = 0;
        rejects([&] { engine.reset(bad_scene, {spec()}); });
        bad_scene = scene(); bad_scene.facilities.push_back(bad_scene.facilities.front());
        rejects([&] { engine.reset(bad_scene, {spec()}); });
        bad_scene = scene(); bad_scene.locations.push_back(bad_scene.locations.front());
        rejects([&] { engine.reset(bad_scene, {spec()}); });
    });
    test("all runtime methods require initialization", [&] {
        StudentEngine engine; Routes routes;
        rejects([&] { engine.tick(0, routes); });
        rejects([&] { engine.snapshots(0); });
        rejects([&] { engine.on_service_started(start_at()); });
        rejects([&] { engine.on_service_completed(complete_at()); });
    });
    test("scheduled departure, ceil travel and one arrival request", [&] {
        StudentEngine engine; Routes routes; routes.t1 = 121.0;
        auto student = spec(); student.departure_minute = 2;
        engine.reset(scene(), {student});
        for (SimTime t = 0; t <= 7; ++t) {
            const auto arrivals = engine.tick(t, routes);
            const auto snapshot = engine.snapshots(t).front();
            shape(snapshot, t < 2 ? StudentState::AtLocation :
                  t < 5 ? StudentState::Moving : StudentState::Queued);
            if (t == 2) require(snapshot.move->depart_at == 2 && snapshot.move->arrive_at == 5 &&
                                snapshot.move->distance_m == 121.0, "incorrect movement");
            require(arrivals.size() == (t == 5 ? 1U : 0U), "arrival duplicated or mistimed");
            if (t == 5) require(arrivals[0].request_id == "S001:lunch:1" &&
                                arrivals[0].facility_id == "F_CANTEEN", "wrong arrival identity");
        }
        require(routes.calls == 1, "route queried again while already moving");
    });
    test("positive tiny route still takes one minute", [&] {
        StudentEngine engine; Routes routes; routes.t1 = std::numeric_limits<double>::min();
        auto student = spec(); student.walk_m_per_min = std::numeric_limits<double>::max();
        engine.reset(scene(), {student});
        require(engine.tick(0, routes).empty(), "zero-time travel");
        require(engine.tick(1, routes).size() == 1, "minimum travel time is not one");
    });
    test("continuous ticks and snapshot timestamps", [&] {
        StudentEngine engine; Routes routes;
        engine.reset(scene(), {spec()});
        rejects([&] { engine.snapshots(0); });
        rejects([&] { engine.tick(-1, routes); });
        rejects([&] { engine.tick(1, routes); });
        engine.tick(0, routes);
        rejects([&] { engine.tick(0, routes); });
        rejects([&] { engine.tick(2, routes); });
        rejects([&] { engine.snapshots(1); });
        shape(engine.snapshots(0).front(), StudentState::Moving);
        engine.tick(1, routes);
    });
    test("unreachable and invalid distances fail without consuming a tick", [&] {
        for (const double distance : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                                      std::numeric_limits<double>::infinity(), 2147483648.0 * 60.0}) {
            StudentEngine engine; Routes routes; routes.t1 = distance;
            engine.reset(scene(), {spec()});
            rejects([&] { engine.tick(0, routes); });
            routes.t1 = 120;
            engine.tick(0, routes);
        }
        StudentEngine engine; Routes routes; routes.t1_reachable = false;
        engine.reset(scene(), {spec()});
        rejects([&] { engine.tick(0, routes); });
        routes.t1_reachable = true;
        engine.tick(0, routes);
        auto tiny_speed = spec(); tiny_speed.walk_m_per_min = 1e-300;
        engine.reset(scene(), {tiny_speed});
        rejects([&] { engine.tick(0, routes); });
    });
    test("failed tick rolls back earlier arrivals in the same batch", [&] {
        StudentEngine engine; Routes routes; routes.t1 = 60; routes.t2_reachable = false;
        auto second = spec("S002"); second.initial_location = "L_T2"; second.departure_minute = 1;
        engine.reset(scene(), {spec(), second});
        engine.tick(0, routes);
        rejects([&] { engine.tick(1, routes); });
        shape(engine.snapshots(0)[0], StudentState::Moving);
        shape(engine.snapshots(0)[1], StudentState::AtLocation);
        routes.t2_reachable = true;
        const auto arrivals = engine.tick(1, routes);
        require(arrivals.size() == 1 && arrivals[0].student_id == "S001", "lost arrival after retry");
    });
    test("missing start notification leaves student queued", [&] {
        StudentEngine engine; Routes routes; reach_queue(engine, routes);
        for (SimTime t = 3; t <= 20; ++t) {
            require(engine.tick(t, routes).empty(), "duplicate arrival");
            shape(engine.snapshots(t).front(), StudentState::Queued);
        }
    });
    test("reject incorrect start notifications without changing state", [&] {
        StudentEngine engine; Routes routes; reach_queue(engine, routes);
        std::vector<ServiceStarted> bad;
        auto add = [&](const std::function<void(ServiceStarted&)>& change) {
            auto event = start_at(); change(event); bad.push_back(event);
        };
        add([](auto& e) { e.request_id = "wrong"; });
        add([](auto& e) { e.student_id = "unknown"; });
        add([](auto& e) { e.facility_id = "wrong"; });
        add([](auto& e) { e.window_id = 2; });
        add([](auto& e) { e.at = 1; });
        add([](auto& e) { e.wait_minutes = 1; });
        add([](auto& e) { e.wait_minutes = -1; });
        for (const auto& event : bad) {
            rejects([&] { engine.on_service_started(event); });
            shape(engine.snapshots(2).front(), StudentState::Queued);
        }
        engine.on_service_started(start_at());
        rejects([&] { engine.on_service_started(start_at()); });
        shape(engine.snapshots(2).front(), StudentState::InService);
    });
    test("missing completion never auto-completes service", [&] {
        StudentEngine engine; Routes routes; reach_queue(engine, routes);
        engine.on_service_started(start_at());
        for (SimTime t = 3; t <= 20; ++t) {
            engine.tick(t, routes);
            shape(engine.snapshots(t).front(), StudentState::InService);
        }
        rejects([&] { engine.on_service_completed(complete_at(21)); });
    });
    test("completion identity, time and phase validation", [&] {
        StudentEngine engine; Routes routes; reach_queue(engine, routes);
        rejects([&] { engine.on_service_completed(complete_at(3)); });
        engine.on_service_started(start_at());
        rejects([&] { engine.on_service_completed(complete_at(3)); });
        engine.tick(3, routes);
        std::vector<ServiceCompleted> bad;
        auto add = [&](const std::function<void(ServiceCompleted&)>& change) {
            auto event = complete_at(); change(event); bad.push_back(event);
        };
        add([](auto& e) { e.request_id = "wrong"; });
        add([](auto& e) { e.student_id = "unknown"; });
        add([](auto& e) { e.facility_id = "wrong"; });
        add([](auto& e) { e.window_id = 1; });
        add([](auto& e) { e.at = 5; });
        for (const auto& event : bad) rejects([&] { engine.on_service_completed(event); });
        shape(engine.snapshots(3).front(), StudentState::InService);
        engine.on_service_completed(complete_at());
        rejects([&] { engine.snapshots(3); });
        rejects([&] { engine.on_service_started(start_at(3, 1)); });
        rejects([&] { engine.on_service_completed(complete_at()); });
        engine.tick(4, routes);
        shape(engine.snapshots(4).front(), StudentState::Eating);
    });
    test("single-student closed loop responds to a delayed service start", [&] {
        StudentEngine engine; Routes routes;
        engine.reset(scene(), {spec()});
        std::size_t arrivals = 0;
        for (SimTime t = 0; t <= 20; ++t) {
            if (t == 7) engine.on_service_completed(complete_at(7));
            arrivals += engine.tick(t, routes).size();
            if (t == 5) engine.on_service_started(start_at(5, 3));
            const auto state = t < 2 ? StudentState::Moving : t < 5 ? StudentState::Queued :
                t < 7 ? StudentState::InService : t < 12 ? StudentState::Eating :
                t < 14 ? StudentState::Moving : StudentState::Finished;
            shape(engine.snapshots(t).front(), state);
        }
        require(arrivals == 1 && routes.calls == 2, "lunch request or return trip duplicated");
        require(engine.snapshots(20).front().location_id == "L_DORM", "wrong final location");
    });
    test("two same-minute completions and immutable sorted snapshot values", [&] {
        StudentEngine engine; Routes routes;
        engine.reset(scene(), {spec("S002"), spec("S001")});
        for (SimTime t = 0; t <= 2; ++t) engine.tick(t, routes);
        engine.on_service_started(start_at());
        engine.on_service_started({"S002:lunch:1", "S002", "F_CANTEEN", 1, 2, 0});
        engine.tick(3, routes);
        engine.on_service_completed(complete_at());
        engine.on_service_completed({"S002:lunch:1", "S002", "F_CANTEEN", 1, 4});
        engine.tick(4, routes);
        auto snapshots = engine.snapshots(4);
        require(snapshots[0].student_id == "S001" && snapshots[1].student_id == "S002", "not sorted");
        shape(snapshots[0], StudentState::Eating); shape(snapshots[1], StudentState::Eating);
        snapshots[0].student_id = "changed";
        require(engine.snapshots(4)[0].student_id == "S001", "snapshot exposes mutable engine state");
    });
    test("failed return-route lookup can be corrected and retried", [&] {
        StudentEngine engine; Routes routes; reach_queue(engine, routes);
        engine.on_service_started(start_at()); engine.tick(3, routes);
        engine.on_service_completed(complete_at());
        for (SimTime t = 4; t <= 8; ++t) engine.tick(t, routes);
        routes.back_reachable = false;
        rejects([&] { engine.tick(9, routes); });
        shape(engine.snapshots(8).front(), StudentState::Eating);
        routes.back_reachable = true;
        engine.tick(9, routes);
        shape(engine.snapshots(9).front(), StudentState::Moving);
    });
    test("service and meal time overflow is rejected before state changes", [&] {
        StudentEngine engine; Routes routes; auto scenario = scene();
        scenario.facilities[0].service_minutes = std::numeric_limits<SimTime>::max();
        engine.reset(scenario, {spec()});
        for (SimTime t = 0; t <= 2; ++t) engine.tick(t, routes);
        rejects([&] { engine.on_service_started(start_at()); });
        shape(engine.snapshots(2).front(), StudentState::Queued);
        auto student = spec(); student.meal_minutes = std::numeric_limits<SimTime>::max();
        engine.reset(scene(), {student});
        for (SimTime t = 0; t <= 2; ++t) engine.tick(t, routes);
        engine.on_service_started(start_at()); engine.tick(3, routes);
        rejects([&] { engine.on_service_completed(complete_at()); });
        shape(engine.snapshots(3).front(), StudentState::InService);
    });
    test("reset clears a previous run; failed reset preserves it", [&] {
        StudentEngine engine; Routes routes; reach_queue(engine, routes);
        rejects([&] { engine.reset(scene(), {spec(), spec()}); });
        shape(engine.snapshots(2).front(), StudentState::Queued);
        engine.reset(scene(), {spec()});
        require(engine.tick(0, routes).empty(), "reset retained arrivals or clock");
        shape(engine.snapshots(0).front(), StudentState::Moving);
        engine.reset(scene(), {});
        require(engine.tick(0, routes).empty() && engine.snapshots(0).empty(), "empty run failed");
    });
    test("canonical 20-student trajectory with EXTERNAL scripted service events", [&] {
        auto population = read_student_specs(root / "data/students/mvp.csv");
        std::reverse(population.begin(), population.end());
        StudentEngine concrete; IStudentEngine& engine = concrete; Routes routes;
        engine.reset(scene(), population);
        std::set<RequestId> requests;
        std::size_t snapshot_count = 0;
        SimTime all_finished_at = -1;
        for (SimTime t = 0; t <= 120; ++t) {
            // Hand-calculated test oracle from 07_ACCEPTANCE, not a production queue implementation.
            for (int i = 0; i < 20; ++i) {
                const auto id = "S" + std::string(i + 1 < 10 ? "00" : "0") + std::to_string(i + 1);
                const auto start = static_cast<SimTime>(2 + 2 * (i / 2));
                if (t == start + 2)
                    engine.on_service_completed({id + ":lunch:1", id, "F_CANTEEN",
                                                 static_cast<std::size_t>(i % 2), t});
            }
            const auto arrivals = engine.tick(t, routes);
            require(arrivals.size() == (t == 2 || t == 3 ? 10U : 0U), "wrong canonical arrivals");
            for (const auto& event : arrivals)
                require(event.at == t && requests.insert(event.request_id).second, "duplicate request");
            for (int i = 0; i < 20; ++i) {
                const auto id = "S" + std::string(i + 1 < 10 ? "00" : "0") + std::to_string(i + 1);
                const auto start = static_cast<SimTime>(2 + 2 * (i / 2));
                const auto arrival = i < 10 ? 2 : 3;
                if (t == start)
                    engine.on_service_started({id + ":lunch:1", id, "F_CANTEEN",
                                               static_cast<std::size_t>(i % 2), t, t - arrival});
            }
            const auto snapshots = engine.snapshots(t);
            require(snapshots.size() == 20, "student disappeared after finishing");
            bool all_finished = true;
            for (int i = 0; i < 20; ++i) {
                const auto arrival = i < 10 ? 2 : 3;
                const auto start = 2 + 2 * (i / 2), end = start + 2;
                const auto expected = t < arrival ? StudentState::Moving :
                    t < start ? StudentState::Queued : t < end ? StudentState::InService :
                    t < end + 5 ? StudentState::Eating : t < end + 7 ? StudentState::Moving :
                    StudentState::Finished;
                shape(snapshots[static_cast<std::size_t>(i)], expected);
                all_finished = all_finished && expected == StudentState::Finished;
                if (i > 0) require(snapshots[static_cast<std::size_t>(i-1)].student_id <
                                    snapshots[static_cast<std::size_t>(i)].student_id, "unsorted snapshots");
            }
            snapshot_count += snapshots.size();
            if (all_finished && all_finished_at < 0) all_finished_at = t;
        }
        require(requests.size() == 20 && snapshot_count == 2420 && all_finished_at == 29,
                "canonical trajectory anchors failed");
    });

    std::cout << "Student tests: " << passed << " passed, " << failed << " failed.\n";
    return failed == 0 ? 0 : 1;
}
