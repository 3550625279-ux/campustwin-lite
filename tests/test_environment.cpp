#include "campustwin/environment.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace campustwin;

namespace {
int checks = 0;
void check(bool ok, const char* expression, int line) {
    ++checks;
    if (!ok) throw std::runtime_error("line " + std::to_string(line) + ": " + expression);
}
#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

std::string signature(const CampusScenario& scenario) {
    std::ostringstream out;
    out.precision(std::numeric_limits<double>::max_digits10);
    for (const auto& item : scenario.locations)
        out << item.id << '|' << item.name << '|' << static_cast<int>(item.kind)
            << '|' << item.capacity << '\n';
    out << "roads\n";
    for (const auto& item : scenario.roads)
        out << item.from << '|' << item.to << '|' << item.distance_m << '\n';
    out << "facilities\n";
    for (const auto& item : scenario.facilities)
        out << item.id << '|' << item.location_id << '|' << item.window_count
            << '|' << item.service_minutes << '\n';
    return out.str();
}

void expect_validation(const std::function<void()>& action) {
    bool caught = false;
    try { action(); }
    catch (const ValidationError&) { caught = true; }
    CHECK(caught);
}

void expect_io_error(const std::function<void()>& action) {
    bool caught = false;
    try { action(); }
    catch (const std::exception&) { caught = true; }
    CHECK(caught);
}

struct Scratch {
    fs::path directory = fs::temp_directory_path() /
        ("campustwin_environment_test_" + std::to_string(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    Scratch() { fs::create_directories(directory); }
    ~Scratch() { std::error_code ignored; fs::remove_all(directory, ignored); }
};

void write(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("test could not open " + path.string());
    out << text;
    out.close();
    if (!out) throw std::runtime_error("test could not write " + path.string());
}

const std::string locations =
    "id,name,kind,capacity\nA,Classroom,Teaching,200\nB,Canteen,Canteen,100\n";
const std::string roads = "from,to,distance_m\nA,B,12.5\n";
const std::string facilities =
    "id,location_id,window_count,service_minutes\nF,B,2,2\n";

void csv_directory(const fs::path& path, const std::string& a = locations,
                   const std::string& b = roads, const std::string& c = facilities) {
    fs::create_directories(path);
    write(path / "locations.csv", a);
    write(path / "roads.csv", b);
    write(path / "facilities.csv", c);
}

void test_routes_and_validation(const fs::path& root) {
    CampusEnvironment env;
    env.load_directory(root / "data/scenarios/mvp");
    CHECK(env.scenario().locations.size() == 5);
    CHECK(env.scenario().roads.size() == 6);
    CHECK(env.scenario().facilities.size() == 1);
    const ICampusEnvironment& shared = env;
    const IRouteProvider& routes_interface = shared;
    auto route = routes_interface.find_route("L_T1", "L_CANTEEN");
    CHECK(route.reachable);
    CHECK(std::abs(route.distance_m - 120) < 1e-9);
    CHECK(route.path.front() == "L_T1" && route.path.back() == "L_CANTEEN");
    route = env.find_route("L_T2", "L_DORM");
    CHECK(route.reachable && std::abs(route.distance_m - 300) < 1e-9);
    CHECK(route.path == std::vector<LocationId>({"L_T2", "L_CANTEEN", "L_DORM"}));
    route = env.find_route("L_DORM", "L_T2");
    CHECK(route.reachable && std::abs(route.distance_m - 300) < 1e-9);
    route = env.find_route("L_T1", "L_T1");
    CHECK(route.reachable && route.distance_m == 0);
    CHECK(route.path == std::vector<LocationId>({"L_T1"}));
    expect_validation([&] { env.find_route("missing", "L_T1"); });
    expect_validation([&] { env.find_route("missing", "missing"); });

    auto scenario = env.scenario();
    scenario.locations.push_back({"ISLAND", "Isolated", LocationKind::Library, 1});
    env.replace_scenario(scenario);
    route = env.find_route("ISLAND", "L_T1");
    CHECK(!route.reachable && route.path.empty());
    const std::string old = signature(env.scenario());
    auto rejected = [&](const std::function<void(CampusScenario&)>& edit) {
        auto candidate = env.scenario();
        edit(candidate);
        expect_validation([&] { env.replace_scenario(candidate); });
        CHECK(signature(env.scenario()) == old);
        CHECK(std::abs(env.find_route("L_T2", "L_DORM").distance_m - 300) < 1e-9);
    };
    rejected([](auto& s) { s.locations.push_back(s.locations.front()); });
    rejected([](auto& s) { s.locations.front().id = "bad id"; });
    rejected([](auto& s) { s.locations.front().capacity = 0; });
    rejected([](auto& s) { s.locations.front().name = "bad,name"; });
    rejected([](auto& s) { s.locations.front().kind = static_cast<LocationKind>(99); });
    rejected([](auto& s) { s.roads.front().to = "missing"; });
    rejected([](auto& s) { s.roads.front().to = s.roads.front().from; });
    rejected([](auto& s) { const auto r = s.roads.front(); s.roads.push_back({r.to,r.from,r.distance_m}); });
    rejected([](auto& s) { s.roads.front().distance_m = 0; });
    rejected([](auto& s) { s.roads.front().distance_m = -1; });
    rejected([](auto& s) { s.roads.front().distance_m = std::numeric_limits<double>::quiet_NaN(); });
    rejected([](auto& s) { s.roads.front().distance_m = std::numeric_limits<double>::infinity(); });
    rejected([](auto& s) { s.facilities.front().location_id = "missing"; });
    rejected([](auto& s) { s.facilities.front().location_id = "L_T1"; });
    rejected([](auto& s) { s.facilities.front().window_count = 0; });
    rejected([](auto& s) { s.facilities.front().service_minutes = 0; });
    rejected([](auto& s) { s.facilities.front().service_minutes = -1; });
    rejected([](auto& s) { s.facilities.push_back(s.facilities.front()); });
    rejected([](auto& s) { auto f = s.facilities.front(); f.id = "F_OTHER"; s.facilities.push_back(f); });

    CampusScenario ties;
    ties.locations = {{"A","A",LocationKind::Teaching,1}, {"B","B",LocationKind::Library,1},
                      {"C","C",LocationKind::Library,1}, {"D","D",LocationKind::Library,1}};
    ties.roads = {{"A","B",1}, {"A","C",1}, {"B","D",1}, {"C","D",1}};
    env.replace_scenario(ties);
    route = env.find_route("A", "D");
    CHECK(route.reachable && route.distance_m == 2);
    CHECK(route.path.size() == 3);
    for (int i = 0; i < 5; ++i) CHECK(env.find_route("A", "D").path == route.path);
}

void test_extreme_distances(const fs::path& scratch) {
    CampusEnvironment env;
    CampusScenario graph;
    graph.locations = {{"A","A",LocationKind::Teaching,1}, {"B","B",LocationKind::Library,1},
                       {"C","C",LocationKind::Library,1}};
    const double huge = std::numeric_limits<double>::max() * 0.75;
    graph.roads = {{"A","B",huge}, {"B","C",huge}};
    env.replace_scenario(graph);
    expect_validation([&] { env.find_route("A", "C"); });
    graph.roads.push_back({"A","C",1});
    env.replace_scenario(graph);
    CHECK(env.find_route("A", "C").distance_m == 1);
    graph.roads = {{"A","B",1e-300}, {"B","C",1e-300}};
    env.replace_scenario(graph);
    const auto tiny = env.find_route("A", "C");
    CHECK(tiny.reachable && tiny.distance_m > 0 && tiny.distance_m == 2e-300);
    const double denormal = std::numeric_limits<double>::denorm_min();
    if constexpr (std::numeric_limits<double>::has_denorm != std::denorm_absent) {
        graph.roads = {{"A","B",denormal}};
        env.replace_scenario(graph);
        env.save_directory(scratch / "denormal");
        CampusEnvironment restored;
        restored.load_directory(scratch / "denormal");
        CHECK(restored.find_route("A", "B").distance_m == denormal);
    }
}

void test_editing() {
    CampusEnvironment env;
    env.add_location({"A",u8"\u6559\u5b66\u697c",LocationKind::Teaching,100});
    env.add_location({"B","Canteen",LocationKind::Canteen,100});
    env.add_location({"C","Dormitory",LocationKind::Dormitory,100});
    expect_validation([&] { env.add_location({"A","Duplicate",LocationKind::Library,1}); });
    env.set_road({"A","B",20});
    env.set_road({"B","C",30});
    CHECK(env.find_route("A", "C").distance_m == 50);
    env.set_road({"B","A",25});
    CHECK(env.scenario().roads.size() == 2);
    CHECK(env.find_route("A", "C").distance_m == 55);
    env.set_facility({"F","B",1,2});
    env.set_facility({"F","B",2,3});
    CHECK(env.scenario().facilities.size() == 1);
    CHECK(env.scenario().facilities.front().window_count == 2);
    CHECK(env.scenario().facilities.front().service_minutes == 3);
    const std::string old = signature(env.scenario());
    expect_validation([&] { env.remove_location("B"); });
    CHECK(signature(env.scenario()) == old);
    expect_validation([&] { env.update_location({"B","Library",LocationKind::Library,100}); });
    CHECK(signature(env.scenario()) == old);
    expect_validation([&] { env.set_road({"A","C",-5}); });
    CHECK(signature(env.scenario()) == old);
    env.update_location({"A",u8"\u65b0\u6559\u5b66\u697c",LocationKind::Teaching,120});
    CHECK(env.scenario().locations.front().capacity == 120);
    env.remove_road("A", "B");
    CHECK(!env.find_route("A", "C").reachable);
    env.remove_road("C", "B");
    const std::string facility_dependency = signature(env.scenario());
    expect_validation([&] { env.remove_location("B"); });
    CHECK(signature(env.scenario()) == facility_dependency);
    env.remove_facility("F");
    env.remove_location("B");
    CHECK(env.scenario().locations.size() == 2);
    CHECK(env.scenario().roads.empty() && env.scenario().facilities.empty());
}

void test_files(const fs::path& root, const fs::path& scratch) {
    CampusEnvironment env;
    env.load_directory(root / "data/scenarios/mvp");
    auto candidate = env.scenario();
    candidate.locations.front().name = u8"\u6559\u5b66\u697c A";
    candidate.roads.front().distance_m = 120.123456789;
    env.replace_scenario(candidate);
    env.save_directory(scratch / "saved");
    CampusEnvironment restored;
    restored.load_directory(scratch / "saved");
    CHECK(restored.scenario().locations.front().name == candidate.locations.front().name);
    CHECK(restored.scenario().locations.size() == candidate.locations.size());
    CHECK(restored.scenario().roads.size() == candidate.roads.size());
    CHECK(restored.scenario().facilities.size() == candidate.facilities.size());
    CHECK(std::abs(restored.find_route("L_T1","L_CANTEEN").distance_m - 120.123456789) < 1e-9);
    CHECK(restored.scenario().facilities.front().window_count == 2);
    const std::string old = signature(env.scenario());
    const fs::path fixture = scratch / "csv";
    auto rejected = [&](const std::string& a, const std::string& b, const std::string& c) {
        csv_directory(fixture, a, b, c);
        expect_validation([&] { env.load_directory(fixture); });
        CHECK(signature(env.scenario()) == old);
    };
    rejected("bad,name,kind,capacity\nA,A,Teaching,1\n", roads, facilities);
    rejected("id,name,kind,capacity\nA,A,Teaching\n", roads, facilities);
    rejected("id,name,kind,capacity\nA,A,Teaching,1,extra\n", roads, facilities);
    rejected("\xEF\xBB\xBF" + locations, roads, facilities);
    rejected("id,name,kind,capacity\nA,\"A\",Teaching,1\n", roads, facilities);
    rejected("id,name,kind,capacity\nA, A,Teaching,1\n", roads, facilities);
    rejected("id,name,kind,capacity\nA,A,Teaching,-1\n", roads, facilities);
    rejected("id,name,kind,capacity\nA,A,Teaching,1x\n", roads, facilities);
    rejected("id,name,kind,capacity\nA,A,Unknown,1\n", roads, facilities);
    rejected(locations, "from,to,distance_m\nA,B,nan\n", facilities);
    rejected(locations, "from,to,distance_m\nA,B,inf\n", facilities);
    rejected(locations, "from,to,distance_m\nA,B,1e9999\n", facilities);
    rejected(locations, "from,to,distance_m\nA,B,0\n", facilities);
    rejected(locations, "from,to,distance_m\nA,B,12junk\n", facilities);
    rejected(locations, "from,to,distance_m\nA,B,12.5 \n", facilities);
    rejected(locations, roads, "id,location_id,window_count,service_minutes\nF,B,-1,2\n");
    rejected(locations, roads, "id,location_id,window_count,service_minutes\nF,B,2,2147483648\n");
    rejected(locations, roads, "id,location_id,window_count,service_minutes\nF,B,184467440737095516160,2\n");
    csv_directory(fixture);
    fs::remove(fixture / "facilities.csv");
    expect_io_error([&] { env.load_directory(fixture); });
    CHECK(signature(env.scenario()) == old);
    write(scratch / "not_a_directory", "x");
    expect_io_error([&] { env.save_directory(scratch / "not_a_directory"); });
    CHECK(signature(env.scenario()) == old);
    csv_directory(fixture,
        "id,name,kind,capacity\r\n\r\nA,Classroom,Teaching,200\r\nB,Canteen,Canteen,100\r\n\r\n",
        "from,to,distance_m\r\n\r\nA,B,12.5\r\n",
        "id,location_id,window_count,service_minutes\r\nF,B,2,2\r\n\r\n");
    env.load_directory(fixture);
    CHECK(env.scenario().locations.size() == 2);
    CHECK(env.find_route("A", "B").distance_m == 12.5);
}
}  // namespace

int test_main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: test_environment <repository-root>");
        Scratch scratch;
        const fs::path root = fs::u8path(argv[1]);
        test_routes_and_validation(root);
        test_extreme_distances(scratch.directory);
        test_editing();
        test_files(root, scratch.directory);
        std::cout << "PASS: environment (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: environment: " << error.what() << '\n';
        return 1;
    }
}



#ifdef _WIN32
// Windows passes its native UTF-16 arguments to wmain. Convert once before
// calling the portable test body, whose paths are explicitly read as UTF-8.
int wmain(int argc, wchar_t** argv) {
    try {
        std::vector<std::string> utf8_arguments;
        utf8_arguments.reserve(static_cast<std::size_t>(argc));
        for (int i = 0; i < argc; ++i)
            utf8_arguments.push_back(fs::path(argv[i]).u8string());
        std::vector<char*> arguments;
        arguments.reserve(static_cast<std::size_t>(argc) + 1);
        for (auto& argument : utf8_arguments) arguments.push_back(argument.data());
        arguments.push_back(nullptr);
        return test_main(argc, arguments.data());
    } catch (const std::exception& error) {
        std::cerr << "FAIL: environment argument conversion: " << error.what() << '\n';
        return 1;
    }
}
#else
int main(int argc, char** argv) { return test_main(argc, argv); }
#endif
