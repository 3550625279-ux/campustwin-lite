#include "campustwin/resources.hpp"
#include "campustwin/students.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace campustwin;
namespace {
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
void rejects(const std::function<void()>& fn, const std::string& message) {
    try { fn(); } catch (const ValidationError&) { return; }
    throw std::runtime_error("expected validation error: " + message);
}
StudentArrival arrival(const char* name, SimTime at, const char* meal) {
    return {std::string(name) + ":lunch:1", name, "F", at, meal};
}
FacilityConfig config() {
    return {"F", "C", 2, 2, {{0, "*", 2}, {1, "rice", 3}}};
}
const ServiceStarted& started(const std::vector<ServiceStarted>& events, const std::string& name) {
    const auto it = std::find_if(events.begin(), events.end(), [&](const auto& x) { return x.student_id == name; });
    if (it == events.end()) throw std::runtime_error("missing service for " + name);
    return *it;
}
void matching_and_forecast() {
    FacilityEngine engine;
    engine.reset({config()});
    auto initial = engine.decision_snapshot();
    const auto empty = initial->estimate_service("F", 0, "noodle");
    require(empty && empty->wait_minutes == 0 && empty->service_minutes == 2, "empty forecast");
    engine.complete_due(0);
    engine.enqueue({arrival("S001", 0, "rice"), arrival("S002", 0, "noodle"),
                    arrival("S003", 0, "noodle"), arrival("S004", 0, "rice")}, 0);
    const auto events = engine.start_ready(0);
    require(events.size() == 2, "matching should use both windows");
    require(started(events, "S001").window_id == 1 && started(events, "S001").expected_end_at == 3,
            "rice should move to specialist, completing after 3 minutes");
    require(started(events, "S002").window_id == 0 && started(events, "S002").expected_end_at == 2,
            "noodle should use flexible window");
    const auto frozen = engine.decision_snapshot();
    const auto prediction = frozen->estimate_service("F", 1, "noodle");
    require(prediction && prediction->wait_minutes == 3 && prediction->service_minutes == 2,
            "forecast must replay compatible queue and differing durations");
    engine.complete_due(1); engine.enqueue({arrival("S005", 1, "noodle")}, 1);
    require(engine.start_ready(1).empty(), "windows should still be busy");
    engine.complete_due(2); engine.enqueue({}, 2);
    require(started(engine.start_ready(2), "S003").window_id == 0, "oldest eligible request should start");
    engine.complete_due(3); engine.enqueue({}, 3);
    require(started(engine.start_ready(3), "S004").window_id == 1, "rice should use specialist");
    engine.complete_due(4); engine.enqueue({}, 4);
    require(started(engine.start_ready(4), "S005").wait_minutes == prediction->wait_minutes,
            "forecast should equal actual absent future arrivals");
    require(frozen->estimate_service("F", 1, "noodle")->wait_minutes == 3, "frozen forecast changed");
}
void validation_and_atomicity() {
    FacilityEngine engine;
    FacilityConfig dedicated{"F", "C", 2, 2, {{0,"rice",2},{1,"noodle",3}}};
    engine.reset({dedicated});
    require(!engine.decision_snapshot()->estimate_service("F", 0, "stirfry"), "unsupported meal forecast");
    engine.complete_due(0);
    rejects([&]{ engine.enqueue({arrival("A",0,"rice"),arrival("B",0,"stirfry")},0); }, "unsupported meal");
    engine.enqueue({arrival("A",0,"rice")},0);
    require(engine.start_ready(0).size() == 1, "invalid batch must not mutate queue");
    auto duplicate = dedicated; duplicate.window_services.push_back({0,"rice",5});
    rejects([&]{ engine.reset({duplicate}); }, "duplicate window meal");
    auto missing = dedicated; missing.window_services.pop_back();
    rejects([&]{ engine.reset({missing}); }, "missing window configuration");
    require(engine.snapshots(0).at(0).started == 1, "failed reset must preserve state");
    FacilityConfig overrides{"F","C",1,9,{{0,"*",4},{0,"noodle",1}}};
    require(window_service_time(overrides,0,"noodle") == 1 &&
            window_service_time(overrides,0,"rice") == 4, "specific meal must override wildcard");
    engine.reset({overrides}); engine.complete_due(0);
    engine.enqueue({arrival("A",0,"noodle")},0);
    require(started(engine.start_ready(0),"A").expected_end_at == 1, "override duration in real service");
}
// Independent exhaustive oracle: all three-window eligibility graphs and
// three-student meal sequences. Check maximum throughput and FIFO priority.
void exhaustive_matching() {
    const char* meals[] = {"rice", "noodle", "stirfry"};
    for (int a = 1; a < 8; ++a) for (int b = 1; b < 8; ++b) for (int c = 1; c < 8; ++c) {
        const int masks[] = {a,b,c};
        FacilityConfig f{"F","C",3,2};
        for (int w = 0; w < 3; ++w) for (int m = 0; m < 3; ++m)
            if (masks[w] & (1 << m)) f.window_services.push_back({static_cast<std::size_t>(w),meals[m],w+1});
        for (int sequence = 0; sequence < 27; ++sequence) {
            int types[] = {sequence % 3, (sequence / 3) % 3, (sequence / 9) % 3};
            bool supported = true;
            for (const auto type : types) if (!((a|b|c) & (1 << type))) supported = false;
            if (!supported) continue;
            int best_count = -1, best_priority = -1;
            for (int x = -1; x < 3; ++x) for (int y = -1; y < 3; ++y) for (int z = -1; z < 3; ++z) {
                int assignment[] = {x,y,z};
                int used = 0, count = 0, priority = 0; bool valid = true;
                for (int i = 0; i < 3; ++i) if (assignment[i] >= 0) {
                    const int bit = 1 << assignment[i];
                    if ((used & bit) || !(masks[assignment[i]] & (1 << types[i]))) valid = false;
                    used |= bit; ++count; priority |= 1 << (2-i);
                }
                if (valid && (count > best_count || (count == best_count && priority > best_priority))) {
                    best_count = count; best_priority = priority;
                }
            }
            FacilityEngine engine; engine.reset({f}); engine.complete_due(0);
            engine.enqueue({arrival("S001",0,meals[types[0]]),arrival("S002",0,meals[types[1]]),
                            arrival("S003",0,meals[types[2]])},0);
            const auto result = engine.start_ready(0);
            int priority = 0;
            for (const auto& event : result) priority |= 1 << (2 - (event.student_id.back() - '1'));
            require(static_cast<int>(result.size()) == best_count && priority == best_priority,
                    "matching differs from exhaustive eligibility oracle");
        }
    }
}
// With no later arrivals, replay a frozen forecast against a separate real
// facility engine over different eligibility graphs and arrival times.
void forecast_matches_replay() {
    const char* meals[] = {"rice", "noodle", "stirfry"};
    for (int seed = 0; seed < 120; ++seed) {
        const int masks[] = {1 + seed % 7, 1 + (seed * 3 + 1) % 7, 1 + (seed * 5 + 2) % 7};
        const int supported = masks[0] | masks[1] | masks[2];
        FacilityConfig f{"F","C",3,2};
        for (int w = 0; w < 3; ++w) for (int m = 0; m < 3; ++m)
            if (masks[w] & (1 << m)) f.window_services.push_back({static_cast<std::size_t>(w),meals[m],1 + (w + m) % 4});
        FacilityEngine initial; initial.reset({f}); initial.complete_due(0);
        std::vector<StudentArrival> arrivals;
        for (int i = 0; i < 6; ++i) {
            int meal = (seed + i) % 3;
            while (!(supported & (1 << meal))) meal = (meal + 1) % 3;
            const auto id = "S00" + std::to_string(i+1);
            arrivals.push_back({id+":lunch:1",id,"F",0,meals[meal]});
        }
        initial.enqueue(arrivals,0); initial.start_ready(0);
        const auto frozen = initial.decision_snapshot();
        for (int meal = 0; meal < 3; ++meal) if (supported & (1 << meal)) {
            const SimTime at = 1 + seed % 5;
            const auto prediction = frozen->estimate_service("F",at,meals[meal]);
            require(prediction.has_value(), "supported forecast missing");
            auto replay = initial;
            bool found = false;
            for (SimTime now = 1; now < 100 && !found; ++now) {
                replay.complete_due(now);
                replay.enqueue(now == at ? std::vector<StudentArrival>{arrival("Z001",now,meals[meal])}
                                         : std::vector<StudentArrival>{}, now);
                for (const auto& event : replay.start_ready(now)) if (event.student_id == "Z001") {
                    require(event.wait_minutes == prediction->wait_minutes && event.expected_end_at &&
                            *event.expected_end_at - event.at == prediction->service_minutes,
                            "forecast disagrees with real replay");
                    found = true;
                }
            }
            require(found, "real replay did not serve forecast student");
        }
    }
}
struct Routes final : IRouteProvider {
    RouteInfo find_route(const LocationId& from, const LocationId& to) const override {
        return {true, 60.0, {from,to}};
    }
};
void student_uses_assigned_duration() {
    CampusScenario scenario;
    scenario.locations = {{"T","Teaching",LocationKind::Teaching,5},
                          {"C","Canteen",LocationKind::Canteen,5},
                          {"D","Dorm",LocationKind::Dormitory,5}};
    scenario.facilities = {config()};
    StudentSpec spec{"S001","T","F","D",0,60,5,LunchPolicy::Fixed,"rice"};
    StudentEngine students; students.reset(scenario,{spec}); Routes routes;
    students.tick(0,routes);
    const auto arrived = students.tick(1,routes);
    require(arrived.size()==1 && arrived[0].meal_type=="rice", "meal must travel with request");
    rejects([&]{ students.on_service_started({"S001:lunch:1","S001","F",1,1,0,3,"rice"}); }, "wrong end");
    students.on_service_started({"S001:lunch:1","S001","F",1,1,0,4,"rice"});
    students.tick(2,routes); students.tick(3,routes);
    rejects([&]{ students.on_service_completed({"S001:lunch:1","S001","F",0,4}); }, "wrong window");
    students.on_service_completed({"S001:lunch:1","S001","F",1,4});
    students.tick(4,routes);
    require(students.snapshots(4).at(0).state == StudentState::Eating, "completion enters eating");
}
}
int main() {
    try { matching_and_forecast(); validation_and_atomicity(); exhaustive_matching(); forecast_matches_replay(); student_uses_assigned_duration(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    std::cout << "complex service tests passed\n"; return 0;
}
