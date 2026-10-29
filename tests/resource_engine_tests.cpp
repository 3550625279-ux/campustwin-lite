#include "campustwin/resources.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace campustwin;
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
template <typename F> void rejects(F action, const std::string& message) {
    try { action(); } catch (const ValidationError&) { return; }
    throw std::runtime_error("expected rejection: " + message);
}
StudentArrival arrival(const char* student, SimTime at = 0) {
    return {std::string(student) + ":lunch:1", student, "F", at};
}

void fifo_and_statistics() {
    FacilityEngine engine;
    engine.reset({{"F", "L", 1, 2}});
    std::vector<std::string> events;
    for (SimTime now = 0; now <= 6; ++now) {
        for (const auto& e : engine.complete_due(now))
            events.push_back("end:" + e.student_id + ":" + std::to_string(now));
        if (now == 0) engine.enqueue({arrival("S002"), arrival("S001")}, now);
        else if (now == 2) engine.enqueue({arrival("S003", now)}, now);
        else engine.enqueue({}, now);
        for (const auto& e : engine.start_ready(now))
            events.push_back("start:" + e.student_id + ":" + std::to_string(now) + ":" + std::to_string(e.wait_minutes));
        const auto snapshot = engine.snapshots(now).at(0);
        require(snapshot.arrived == snapshot.queue_length + snapshot.in_service + snapshot.completed,
                "request conservation");
        require(snapshot.started == snapshot.in_service + snapshot.completed, "service conservation");
    }
    require(events == std::vector<std::string>{"start:S001:0:0", "end:S001:2", "start:S002:2:2",
                                               "end:S002:4", "start:S003:4:2", "end:S003:6"},
            "FIFO, phase order or service duration");
    const auto summary = engine.summaries(6).at(0);
    require(summary.arrived == 3 && summary.started == 3 && summary.completed == 3 &&
            summary.max_queue_length == 1 && summary.total_wait_minutes == 4 &&
            summary.busy_window_minutes == 6 && summary.observation_minutes == 6,
            "single-window summary");
}

void validation_and_rollback() {
    FacilityEngine engine;
    rejects([&]{ engine.complete_due(0); }, "uninitialized");
    engine.reset({{"F", "L", 2, 2}});
    rejects([&]{ engine.enqueue({}, 0); }, "wrong phase");
    engine.complete_due(0);
    rejects([&]{ engine.complete_due(0); }, "duplicate phase");
    auto wrong = arrival("S002");
    wrong.request_id = "S001:lunch:1";
    rejects([&]{ engine.enqueue({arrival("S001"), wrong}, 0); }, "request belongs to another student");
    rejects([&]{ engine.enqueue({arrival("S001"), arrival("S001")}, 0); }, "duplicate within batch");
    auto missing = arrival("S002");
    missing.facility_id = "UNKNOWN";
    rejects([&]{ engine.enqueue({arrival("S001"), missing}, 0); }, "unknown facility");
    engine.enqueue({arrival("S003"), arrival("S002"), arrival("S001")}, 0);
    const auto starts = engine.start_ready(0);
    require(starts.size() == 2 && starts[0].student_id == "S001" && starts[0].window_id == 0 &&
            starts[1].student_id == "S002" && starts[1].window_id == 1, "ordering and window IDs");
    const auto snapshot = engine.snapshots(0).at(0);
    require(snapshot.arrived == 3 && snapshot.queue_length == 1 && snapshot.max_queue_length == 1,
            "failed batches must not change state");
    rejects([&]{ engine.reset({{"F", "L", 0, 2}}); }, "invalid reset");
    rejects([&]{ engine.reset({{"F", "L", max_facility_windows + 1, 2}}); }, "window limit");
    require(engine.snapshots(0).at(0).arrived == 3, "invalid reset must preserve state");
    rejects([&]{ engine.reset({{"F", "L", 1, 2}, {"G", "L", 1, 2}}); }, "duplicate facility location");
    engine.complete_due(1);
    rejects([&]{ engine.enqueue({arrival("S001", 1)}, 1); }, "previous request duplicated");
    engine.enqueue({}, 1);
    require(engine.start_ready(1).empty(), "service cannot finish early");
    require(engine.complete_due(2).size() == 2, "both windows complete at t=2");
    engine.enqueue({}, 2);
    const auto next = engine.start_ready(2);
    require(next.size() == 1 && next[0].student_id == "S003" && next[0].window_id == 0 &&
            next[0].wait_minutes == 2, "old queue proceeds on lowest window");
    const auto cutoff = engine.snapshots(2).at(0);
    require(cutoff.arrived == 3 && cutoff.completed == 2 && cutoff.in_service == 1 &&
            cutoff.busy_window_minutes == 4, "unfinished service at cutoff");
}

void simultaneous_arrivals() {
    FacilityEngine engine;
    engine.reset({{"F", "L", 1, 2}});
    std::vector<SimTime> waits;
    for (SimTime now = 0; now <= 6; ++now) {
        engine.complete_due(now);
        if (now == 0) engine.enqueue({arrival("S003"), arrival("S001"), arrival("S002")}, now);
        else engine.enqueue({}, now);
        for (const auto& start : engine.start_ready(now)) waits.push_back(start.wait_minutes);
    }
    const auto summary = engine.summaries(6).at(0);
    require(waits == std::vector<SimTime>{0, 2, 4} && summary.max_queue_length == 2 &&
            summary.total_wait_minutes == 6 && summary.completed == 3, "simultaneous arrivals");
}

void time_overflow() {
    FacilityEngine engine;
    engine.reset({{"F", "L", 1, std::numeric_limits<SimTime>::max()}});
    engine.complete_due(0); engine.enqueue({}, 0); engine.start_ready(0);
    engine.complete_due(1); engine.enqueue({arrival("S001", 1)}, 1);
    rejects([&]{ engine.start_ready(1); }, "service end overflow");
    rejects([&]{ engine.start_ready(1); }, "overflow must not consume queued request");
}
}  // namespace

int main() {
    try {
        fifo_and_statistics(); validation_and_rollback(); simultaneous_arrivals(); time_overflow();
        std::cout << "PASS: C queue, event order, validation and statistics\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
