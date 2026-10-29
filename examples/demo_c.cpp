#include "campustwin/resources.hpp"
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {
using namespace campustwin;

template <typename T>
T number(const std::string& value, T minimum) {
    T result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result < minimum)
        throw ValidationError("invalid integer: " + value);
    return result;
}

std::vector<std::vector<std::string>> read_csv(const std::filesystem::path& path,
                                                const std::string& header) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw ValidationError("cannot open " + path.u8string());
    std::string line;
    if (!std::getline(in, line)) throw ValidationError("missing CSV header: " + path.u8string());
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != header) throw ValidationError("unexpected CSV header: " + path.u8string());
    std::vector<std::vector<std::string>> rows;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::vector<std::string> row;
        std::size_t start = 0;
        for (;;) {
            const auto end = line.find(',', start);
            const auto field = line.substr(start, end == std::string::npos ? end : end - start);
            if (field.empty() || field.find_first_of("\"\r\n\t ") != std::string::npos)
                throw ValidationError("invalid CSV field: " + path.u8string());
            row.push_back(field);
            if (end == std::string::npos) break;
            start = end + 1;
        }
        if (row.size() != 4) throw ValidationError("expected four CSV columns: " + path.u8string());
        rows.push_back(std::move(row));
    }
    if (in.bad()) throw ValidationError("failed reading " + path.u8string());
    return rows;
}

int run(const std::vector<std::string>& args) {
    std::filesystem::path facilities_file = "data/fixtures/resource_facilities.csv";
    std::filesystem::path arrivals_file = "data/fixtures/resource_arrivals.csv";
    SimTime duration = 6;
    std::set<std::string> seen;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto& option = args[i];
        if (option == "--help") {
            std::cout << "C-only queue demo; service events are CSV on stdout, snapshots and summary on stderr.\n"
                      << "demo_c [--facilities FILE] [--arrivals FILE] [--duration MINUTES]\n"
                      << "Defaults: data/fixtures/resource_facilities.csv, "
                         "data/fixtures/resource_arrivals.csv, 6.\n";
            return 0;
        }
        if (option != "--facilities" && option != "--arrivals" && option != "--duration")
            throw ValidationError("unknown option: " + option);
        if (!seen.insert(option).second) throw ValidationError("duplicate option: " + option);
        if (++i == args.size() || args[i].empty()) throw ValidationError("missing value for " + option);
        if (option == "--facilities") facilities_file = std::filesystem::u8path(args[i]);
        else if (option == "--arrivals") arrivals_file = std::filesystem::u8path(args[i]);
        else duration = number<SimTime>(args[i], 0);
    }
    std::vector<FacilityConfig> configs;
    for (const auto& row : read_csv(facilities_file, "id,location_id,window_count,service_minutes"))
        configs.push_back({row[0], row[1], number<std::size_t>(row[2], 1), number<SimTime>(row[3], 1)});
    if (configs.empty()) throw ValidationError("at least one facility is required");
    std::map<SimTime, std::vector<StudentArrival>> arrivals;
    for (const auto& row : read_csv(arrivals_file, "request_id,student_id,facility_id,at")) {
        const auto at = number<SimTime>(row[3], 0);
        if (at > duration) throw ValidationError("arrival is later than demo duration");
        arrivals[at].push_back({row[0], row[1], row[2], at});
    }
    FacilityEngine engine;
    engine.reset(configs);
    std::cout << "at,type,request_id,student_id,facility_id,window_id,wait_minutes\n";
    for (SimTime now = 0;; ++now) {
        for (const auto& event : engine.complete_due(now))
            std::cout << now << ",ServiceCompleted," << event.request_id << ',' << event.student_id
                      << ',' << event.facility_id << ',' << event.window_id << ",\n";
        engine.enqueue(arrivals[now], now);
        for (const auto& event : engine.start_ready(now))
            std::cout << now << ",ServiceStarted," << event.request_id << ',' << event.student_id
                      << ',' << event.facility_id << ',' << event.window_id << ',' << event.wait_minutes << '\n';
        for (const auto& snapshot : engine.snapshots(now))
            std::cerr << "tick=" << now << " facility=" << snapshot.facility_id
                      << " queue=" << snapshot.queue_length << " in_service=" << snapshot.in_service
                      << " arrived=" << snapshot.arrived << " started=" << snapshot.started
                      << " completed=" << snapshot.completed << " max_queue=" << snapshot.max_queue_length << '\n';
        if (now == duration) break;
    }
    std::cerr.imbue(std::locale::classic());
    std::cerr << std::fixed << std::setprecision(6);
    for (const auto& summary : engine.summaries(duration)) {
        const double mean = summary.started ? double(summary.total_wait_minutes) / summary.started : 0.0;
        const double utilization = summary.window_count && duration > 0 ?
            double(summary.busy_window_minutes) / (double(summary.window_count) * duration) : 0.0;
        std::cerr << "summary facility=" << summary.facility_id << " arrived=" << summary.arrived
                  << " started=" << summary.started << " completed=" << summary.completed
                  << " max_queue=" << summary.max_queue_length << " total_wait=" << summary.total_wait_minutes
                  << " mean_wait=" << mean << " busy_window_minutes=" << summary.busy_window_minutes
                  << " utilization=" << utilization << '\n';
    }
    if (!std::cout) throw std::runtime_error("failed writing event CSV");
    return 0;
}
}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
    try {
        std::vector<std::string> args;
        for (int i = 0; i < argc; ++i) args.push_back(std::filesystem::path(argv[i]).u8string());
        return run(args);
    } catch (const std::exception& error) {
        std::cerr << "demo_c: " << error.what() << '\n';
        return 1;
    }
}
#else
int main(int argc, char* argv[]) {
    try {
        return run(std::vector<std::string>(argv, argv + argc));
    } catch (const std::exception& error) {
        std::cerr << "demo_c: " << error.what() << '\n';
        return 1;
    }
}
#endif
