#include "campustwin/students.hpp"

#include <algorithm>
#include <charconv>
#include <iostream>
#include <map>
#include <string>
#include <system_error>

namespace {
using namespace campustwin;

// Test double for A. This demo deliberately uses the documented fixed MVP routes.
class DemoRoutes final : public IRouteProvider {
public:
    RouteInfo find_route(const LocationId& from, const LocationId& to) const override {
        if (to == "L_CANTEEN" && from == "L_T1") return {true, 120.0, {from, to}};
        if (to == "L_CANTEEN" && from == "L_T2") return {true, 180.0, {from, to}};
        if (from == "L_CANTEEN" && to == "L_DORM") return {true, 120.0, {from, to}};
        return {};
    }
};

CampusScenario demo_scenario() {
    CampusScenario scene;
    scene.locations = {{"L_T1", "Teaching 1", LocationKind::Teaching, 200},
                       {"L_T2", "Teaching 2", LocationKind::Teaching, 200},
                       {"L_CANTEEN", "Canteen", LocationKind::Canteen, 100},
                       {"L_DORM", "Dormitory", LocationKind::Dormitory, 200}};
    scene.facilities = {{"F_CANTEEN", "L_CANTEEN", 2, 2}};
    return scene;
}

SimTime number(const std::string& text, SimTime minimum) {
    SimTime result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || result < minimum)
        throw ValidationError("invalid CLI time: " + text);
    return result;
}

const char* state_name(StudentState state) {
    switch (state) {
    case StudentState::AtLocation: return "AtLocation";
    case StudentState::Moving: return "Moving";
    case StudentState::Queued: return "Queued";
    case StudentState::InService: return "InService";
    case StudentState::Eating: return "Eating";
    case StudentState::Finished: return "Finished";
    }
    return "Unknown";
}
}  // namespace

namespace {
int run(const std::vector<std::string>& args) {
    try {
        std::filesystem::path file = "data/students/mvp.csv";
        std::string selected = "S001";
        bool all = false, student_given = false;
        SimTime duration = 20, wait = 0;
        for (std::size_t i = 1; i < args.size(); ++i) {
            const std::string option = args[i];
            if (option == "--help") {
                std::cout << "B-only demo: fixed MVP routes and independently scripted services.\n"
                          << "demo_b [--students FILE] [--student ID | --all] [--wait MINUTES] [--duration MINUTES]\n"
                          << "Defaults: data/students/mvp.csv, S001, wait=0, duration=20.\n"
                          << "A/C production modules and CSV reporting are not used by this demo.\n";
                return 0;
            }
            if (option == "--all") { all = true; continue; }
            if (i + 1 == args.size()) throw ValidationError("missing value for " + option);
            const std::string value = args[++i];
            if (option == "--students") file = std::filesystem::u8path(value);
            else if (option == "--student") { selected = value; student_given = true; }
            else if (option == "--wait") wait = number(value, 0);
            else if (option == "--duration") duration = number(value, 1);
            else throw ValidationError("unknown option: " + option);
        }

        if (all && student_given) throw ValidationError("use either --all or --student, not both");
        const auto population = campustwin::read_student_specs(file);
        std::vector<StudentSpec> chosen;
        if (all) chosen = population;
        else {
            const auto found = std::find_if(population.begin(), population.end(), [&](const StudentSpec& s) {
                return s.id == selected;
            });
            if (found == population.end()) throw ValidationError("student not found: " + selected);
            chosen.push_back(*found);
        }
        StudentEngine engine;
        engine.reset(demo_scenario(), chosen);
        const DemoRoutes routes;
        std::map<StudentId, StudentArrival> queued;
        std::map<StudentId, ServiceStarted> services;
        std::size_t arrival_count = 0;
        std::cout << "B-only demo: fixed routes + independent service scripts; no shared queue is simulated.\n"
                  << "loaded=" << population.size() << " selected=" << (all ? "all" : selected)
                  << " scripted_wait=" << wait << '\n';

        for (std::int64_t minute = 0; minute <= duration; ++minute) {
            const auto now = static_cast<SimTime>(minute);
            // C completion phase is BEFORE B.tick(now).
            for (auto it = services.begin(); it != services.end();) {
                const auto& service = it->second;
                if (minute != static_cast<std::int64_t>(service.at) + 2) { ++it; continue; }
                engine.on_service_completed({service.request_id, service.student_id, service.facility_id,
                                             service.window_id, now});
                std::cout << "event t=" << now << " ServiceCompleted student=" << service.student_id << '\n';
                it = services.erase(it);
            }
            for (const auto& arrival : engine.tick(now, routes)) {
                queued.emplace(arrival.student_id, arrival);
                ++arrival_count;
                std::cout << "event t=" << now << " StudentArrival student=" << arrival.student_id
                          << " request=" << arrival.request_id << '\n';
            }
            // Each student's service is a separate scripted test double, not a shared resource.
            for (auto it = queued.begin(); it != queued.end();) {
                const auto& arrival = it->second;
                if (minute != static_cast<std::int64_t>(arrival.at) + wait) { ++it; continue; }
                const ServiceStarted service{arrival.request_id, arrival.student_id, arrival.facility_id, 0, now, wait};
                engine.on_service_started(service);
                services.emplace(service.student_id, service);
                std::cout << "event t=" << now << " ServiceStarted student=" << service.student_id
                          << " wait=" << wait << '\n';
                it = queued.erase(it);
            }
            for (const auto& snapshot : engine.snapshots(now)) {
                std::cout << "tick " << now << " student=" << snapshot.student_id << " state=" << state_name(snapshot.state);
                if (snapshot.location_id) std::cout << " location=" << *snapshot.location_id;
                if (snapshot.move) {
                    std::cout << " from=" << snapshot.move->from << " to=" << snapshot.move->to
                              << " arrive_at=" << snapshot.move->arrive_at;
                }
                std::cout << '\n';
            }
        }
        const auto final = engine.snapshots(duration);
        const auto finished = std::count_if(final.begin(), final.end(), [](const StudentSnapshot& snapshot) {
            return snapshot.state == StudentState::Finished;
        });
        std::cout << "SUMMARY students=" << chosen.size() << " arrivals=" << arrival_count
                  << " finished=" << finished << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "demo_b: " << error.what() << '\n';
        return 1;
    }
}
}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
    try {
        std::vector<std::string> args;
        for (int i = 0; i < argc; ++i) args.push_back(std::filesystem::path(argv[i]).u8string());
        return run(args);
    } catch (const std::exception& error) {
        std::cerr << "demo_b: " << error.what() << '\n';
        return 1;
    }
}
#else
int main(int argc, char* argv[]) {
    return run(std::vector<std::string>(argv, argv + argc));
}
#endif
