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
        CsvRunWriter again(output);
        rejects([&]{ again.begin({1}, {}); }, "existing output must not be overwritten");
        require(read(output / "events.csv") == events, "refused overwrite changed existing output");
        std::cout << "PASS: C CSV validation, retries and overwrite protection\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
