#include "campustwin/simulation.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace campustwin;
using Log = std::vector<std::string>;
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
struct Environment final : ICampusEnvironment {
    CampusScenario data;
    Environment() { data.facilities.push_back({"F", "L", 1, 1}); }
    void load_directory(const std::filesystem::path&) override {}
    void save_directory(const std::filesystem::path&) const override {}
    void replace_scenario(CampusScenario value) override { data = std::move(value); }
    const CampusScenario& scenario() const override { return data; }
    RouteInfo find_route(const LocationId&, const LocationId&) const override { return {}; }
};
struct Students final : IStudentEngine {
    Log& log;
    StudentState state = StudentState::Queued;
    explicit Students(Log& value) : log(value) {}
    void reset(const CampusScenario&, const std::vector<StudentSpec>&) override { log.push_back("B.reset"); }
    std::vector<StudentArrival> tick(SimTime now, const IRouteProvider&) override {
        log.push_back("B.tick");
        return now == 0 ? std::vector<StudentArrival>{{"S:lunch:1", "S", "F", 0}} : std::vector<StudentArrival>{};
    }
    void on_service_started(const ServiceStarted&) override { log.push_back("B.started"); state = StudentState::InService; }
    void on_service_completed(const ServiceCompleted&) override { log.push_back("B.completed"); state = StudentState::Eating; }
    std::vector<StudentSnapshot> snapshots(SimTime now) const override {
        log.push_back("B.snapshots");
        return {{now, "S", state, LocationId("L"), {}}};
    }
};
struct Facilities final : IFacilityEngine {
    Log& log;
    bool inconsistent;
    Facilities(Log& value, bool bad) : log(value), inconsistent(bad) {}
    void reset(const std::vector<FacilityConfig>&) override { log.push_back("C.reset"); }
    std::vector<ServiceCompleted> complete_due(SimTime now) override {
        log.push_back("C.complete");
        return now == 1 ? std::vector<ServiceCompleted>{{"S:lunch:1", "S", "F", 0, 1}} : std::vector<ServiceCompleted>{};
    }
    void enqueue(const std::vector<StudentArrival>&, SimTime) override { log.push_back("C.enqueue"); }
    std::vector<ServiceStarted> start_ready(SimTime now) override {
        log.push_back("C.start");
        return now == 0 ? std::vector<ServiceStarted>{{"S:lunch:1", "S", "F", 0, 0, 0}} : std::vector<ServiceStarted>{};
    }
    std::vector<FacilitySnapshot> snapshots(SimTime now) const override {
        log.push_back("C.snapshots");
        const bool active = now == 0 || inconsistent;
        return {{now, "F", 0, active ? 1u : 0u, 1, 1, active ? 0u : 1u, 0, 0, now}};
    }
    std::vector<FacilitySummary> summaries(SimTime) const override { log.push_back("C.summaries"); return {}; }
};
struct Writer final : IRunWriter {
    Log& log;
    int ticks = 0;
    bool finished = false;
    explicit Writer(Log& value) : log(value) {}
    void begin(const SimulationSettings&, const CampusScenario&) override { log.push_back("W.begin"); }
    void write_tick(SimTime, const std::vector<ServiceCompleted>&, const std::vector<StudentArrival>&,
                    const std::vector<ServiceStarted>&, const std::vector<StudentSnapshot>&,
                    const std::vector<FacilitySnapshot>&) override { ++ticks; log.push_back("W.tick"); }
    void finish(const std::vector<FacilitySummary>&) override { finished = true; log.push_back("W.finish"); }
};
void test(bool inconsistent) {
    Log log;
    Environment environment;
    Students students(log);
    Facilities facilities(log, inconsistent);
    Writer writer(log);
    const std::vector<StudentSpec> population{{"S", "A", "F", "B", 0, 60, 1}};
    bool rejected = false;
    try { SimulationRunner{}.run({1}, environment, population, students, facilities, writer); }
    catch (const ValidationError&) { rejected = true; }
    if (inconsistent) {
        require(rejected && writer.ticks == 1 && !writer.finished,
                "inconsistent cross-module state must not be written or finished");
    } else {
        require(!rejected && writer.ticks == 2 && writer.finished, "inclusive final tick");
        require(log == Log{"B.reset", "C.reset", "W.begin",
                           "C.complete", "B.tick", "C.enqueue", "C.start", "B.started", "B.snapshots", "C.snapshots", "W.tick",
                           "C.complete", "B.completed", "B.tick", "C.enqueue", "C.start", "B.snapshots", "C.snapshots", "W.tick",
                           "C.summaries", "W.finish"}, "phase order");
    }
}
}  // namespace
int main() {
    try {
        test(false); test(true);
        std::cout << "PASS: runner phases and cross-module consistency\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
