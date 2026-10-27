#include "campustwin/students.hpp"

#include <algorithm>
#include <charconv>
#include <iostream>
#include <optional>
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

int main(int argc, char* argv[]) {
    try {
        std::filesystem::path file = "data/students/mvp.csv";
        std::string selected = "S001";
        SimTime duration = 20, wait = 0;
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--help") {
                std::cout << "B-only demo: fixed MVP routes and scripted service for ONE student.\n"
                          << "demo_b [--students FILE] [--student ID] [--wait MINUTES] [--duration MINUTES]\n"
                          << "Defaults: data/students/mvp.csv, S001, wait=0, duration=20.\n"
                          << "A/C production modules and CSV reporting are not used by this demo.\n";
                return 0;
            }
            if (i + 1 == argc) throw ValidationError("missing value for " + option);
            const std::string value = argv[++i];
            if (option == "--students") file = value;
            else if (option == "--student") selected = value;
            else if (option == "--wait") wait = number(value, 0);
            else if (option == "--duration") duration = number(value, 1);
            else throw ValidationError("unknown option: " + option);
        }

        const auto population = campustwin::read_student_specs(file);
        const auto chosen = std::find_if(population.begin(), population.end(), [&](const StudentSpec& s) {
            return s.id == selected;
        });
        if (chosen == population.end()) throw ValidationError("student not found: " + selected);
        StudentEngine engine;
        engine.reset(demo_scenario(), {*chosen});
        const DemoRoutes routes;
        std::optional<StudentArrival> queued;
        std::optional<ServiceStarted> service;
        std::size_t arrival_count = 0;
        std::cout << "B-only demo: fixed route provider + scripted single-student service.\n"
                  << "loaded=" << population.size() << " selected=" << selected
                  << " scripted_wait=" << wait << '\n';

        for (std::int64_t minute = 0; minute <= duration; ++minute) {
            const auto now = static_cast<SimTime>(minute);
            // C completion phase is BEFORE B.tick(now).
            if (service && minute == static_cast<std::int64_t>(service->at) + 2) {
                engine.on_service_completed({service->request_id, selected, service->facility_id,
                                             service->window_id, now});
                std::cout << "event t=" << now << " ServiceCompleted\n";
            }
            for (const auto& arrival : engine.tick(now, routes)) {
                queued = arrival;
                ++arrival_count;
                std::cout << "event t=" << now << " StudentArrival request=" << arrival.request_id << '\n';
            }
            // C start phase is AFTER B.tick(now). No queue algorithm is hidden here.
            if (queued && minute == static_cast<std::int64_t>(queued->at) + wait) {
                service = ServiceStarted{queued->request_id, selected, queued->facility_id, 0, now, wait};
                engine.on_service_started(*service);
                queued.reset();
                std::cout << "event t=" << now << " ServiceStarted wait=" << wait << '\n';
            }
            const auto snapshot = engine.snapshots(now).front();
            std::cout << "tick " << now << " student=" << selected << " state=" << state_name(snapshot.state);
            if (snapshot.location_id) std::cout << " location=" << *snapshot.location_id;
            if (snapshot.move) {
                std::cout << " from=" << snapshot.move->from << " to=" << snapshot.move->to
                          << " arrive_at=" << snapshot.move->arrive_at;
            }
            std::cout << '\n';
        }
        const auto final = engine.snapshots(duration).front();
        std::cout << "SUMMARY students=1 arrivals=" << arrival_count
                  << " finished=" << (final.state == StudentState::Finished ? 1 : 0) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "demo_b: " << error.what() << '\n';
        return 1;
    }
}
