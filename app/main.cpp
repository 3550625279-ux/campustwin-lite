#include "campustwin/environment.hpp"
#include "campustwin/students.hpp"
#include "campustwin/resources.hpp"
#include "campustwin/reporting.hpp"
#include "campustwin/simulation.hpp"
#include <charconv>
#include <iomanip>
#include <iostream>
#include <locale>
#include <set>
#include <string>
#include <vector>

namespace {
int run(const std::vector<std::string>& args) {
    using namespace campustwin;
    std::filesystem::path scenario = "data/scenarios/mvp";
    std::filesystem::path population_file = "data/students/mvp.csv";
    std::filesystem::path output = "outputs/mvp";
    SimulationSettings settings;
    std::string route_from, route_to;
    std::set<std::string> seen;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto& option = args[i];
        if (option == "--help") {
            std::cout << "CampusTwin Lite: run the lunch simulation.\n"
                      << "campustwin [--scenario DIR] [--students FILE] [--duration MINUTES] [--output DIR]\n"
                      << "campustwin [--scenario DIR] --route FROM TO\n"
                      << "Defaults: data/scenarios/mvp, data/students/mvp.csv, 120, outputs/mvp.\n"
                      << "Existing result files are never overwritten.\n";
            return 0;
        }
        if (option != "--scenario" && option != "--students" && option != "--duration" && option != "--output" && option != "--route")
            throw ValidationError("unknown option: " + option);
        if (!seen.insert(option).second) throw ValidationError("duplicate option: " + option);
        if (++i == args.size() || args[i].empty()) throw ValidationError("missing value for " + option);
        if (option == "--route") {
            route_from = args[i];
            if (++i == args.size() || args[i].empty()) throw ValidationError("--route requires FROM and TO");
            route_to = args[i];
        }
        else if (option == "--scenario") scenario = std::filesystem::u8path(args[i]);
        else if (option == "--students") population_file = std::filesystem::u8path(args[i]);
        else if (option == "--output") output = std::filesystem::u8path(args[i]);
        else {
            const auto& value = args[i];
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), settings.duration_minutes);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || settings.duration_minutes < 1)
                throw ValidationError("duration must be a positive integer in range");
        }
    }
    CampusEnvironment environment;
    if (seen.count("--route") && (seen.count("--students") || seen.count("--duration") || seen.count("--output")))
        throw ValidationError("--route cannot be combined with simulation options");
    environment.load_directory(scenario);
    if (seen.count("--route")) {
        const auto route = environment.find_route(route_from, route_to);
        if (!route.reachable) throw ValidationError("route is unreachable: " + route_from + " -> " + route_to);
        std::cout.imbue(std::locale::classic());
        std::cout << "from,to,distance_m,path\n" << route_from << ',' << route_to << ','
                  << std::setprecision(17) << route.distance_m << ',';
        for (std::size_t i = 0; i < route.path.size(); ++i) {
            if (i) std::cout << '|';
            std::cout << route.path[i];
        }
        std::cout << '\n';
        return 0;
    }
    const auto population = read_student_specs(population_file);
    StudentEngine students;
    FacilityEngine facilities;
    CsvRunWriter writer(output);
    try {
        SimulationRunner{}.run(settings, environment, population, students, facilities, writer);
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string("run failed; any output may be incomplete: ") + error.what());
    }
    std::cout << "Simulation complete: " << population.size() << " students, "
              << settings.duration_minutes << " minutes; four CSV files written to " << output.u8string() << '\n';
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
        std::cerr << "campustwin: " << error.what() << '\n';
        return 1;
    }
}
#else
int main(int argc, char* argv[]) {
    try {
        return run(std::vector<std::string>(argv, argv + argc));
    } catch (const std::exception& error) {
        std::cerr << "campustwin: " << error.what() << '\n';
        return 1;
    }
}
#endif
