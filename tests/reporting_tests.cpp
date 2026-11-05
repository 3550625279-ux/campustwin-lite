#include "campustwin/reporting.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
using namespace campustwin;
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
template <typename F> void rejects(F action, const std::string& message) {
    try { action(); } catch (const ValidationError&) { return; }
    throw std::runtime_error("expected rejection: " + message);
}
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot read test output");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
struct TemporaryDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("campustwin-reporting-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    ~TemporaryDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
}  // namespace

int main() {
    try {
        TemporaryDirectory temporary;
        const auto output = temporary.path / "run";
        CsvRunWriter writer(output);
        writer.begin({1}, {});
        rejects([&]{ writer.begin({1}, {}); }, "begin twice");
        const StudentArrival event{"S001:lunch:1", "S001", "F", 0};
        const StudentSnapshot invalid{0, "S001", StudentState::Moving, LocationId("L"), {}};
        const StudentSnapshot valid{0, "S001", StudentState::Queued, LocationId("L"), {}};
        rejects([&]{ writer.write_tick(0, {}, {event}, {}, {invalid}, {}); }, "invalid snapshot");
        writer.write_tick(0, {}, {event}, {}, {valid}, {});
        LunchDecision decision;
        decision.at = 0; decision.student_id = "S001"; decision.facility_id = "F";
        decision.destination = "L"; decision.distance_m = 120; decision.path = {"L_T1", "L"};
        decision.queue_length = 0; decision.travel_minutes = 2; decision.estimated_wait_minutes = 0;
        decision.service_minutes = 2; decision.estimated_total_minutes = 4;
        decision.selected = true; decision.reason = "fixed_assignment";
        auto malformed = decision; malformed.student_id = "bad,field";
        rejects([&]{ writer.write_decisions(0, {malformed}); }, "invalid decision row");
        writer.write_decisions(0, {decision});
        rejects([&]{ writer.write_decisions(0, {decision}); }, "duplicate decision tick");
        rejects([&]{ writer.finish({}); }, "missing final tick");
        rejects([&]{ writer.write_tick(0, {}, {}, {}, {}, {}); }, "duplicate tick");
        writer.write_tick(1, {}, {}, {}, {}, {});
        writer.finish({{"F", 1, 1, 1, 0, 0, 1, 0, 0}});
        const auto events = read(output / "events.csv");
        require(events == "at,type,request_id,student_id,facility_id,window_id,wait_minutes\n"
                          "0,StudentArrival,S001:lunch:1,S001,F,,\n", "failed tick wrote partial event");
        require(read(output / "student_snapshots.csv").find("0,S001,Queued,L,,,,,\n") != std::string::npos,
                "stationary snapshot");
        require(read(output / "summary.csv").find("F,1,1,1,0,0,1,0,0.000000,0,0.000000\n") != std::string::npos,
                "summary formatting and zero division");
        require(read(output / "decisions.csv").find(
                    "0,S001,fixed,F,L,-1,0,120.000000,L_T1|L,2,0,2,4,1,fixed_assignment\n") != std::string::npos,
                "decision rows, zero values, and retry");
        CsvRunWriter again(output);
        rejects([&]{ again.begin({1}, {}); }, "existing output must not be overwritten");
        require(read(output / "events.csv") == events, "refused overwrite changed existing output");
        const auto only_decisions = temporary.path / "existing-decisions";
        std::filesystem::create_directories(only_decisions);
        { std::ofstream sentinel(only_decisions / "decisions.csv"); sentinel << "keep"; }
        CsvRunWriter protected_writer(only_decisions);
        rejects([&]{ protected_writer.begin({1}, {}); }, "existing decisions must not be overwritten");
        require(read(only_decisions / "decisions.csv") == "keep" &&
                !std::filesystem::exists(only_decisions / "events.csv"), "partial overwrite of outputs");
        std::cout << "PASS: C CSV validation, retries and overwrite protection\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
