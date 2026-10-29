#include "campustwin/accounts.hpp"
#include "campustwin/environment.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <string>

namespace {
using namespace campustwin;
void help() {
    std::cout << "CampusTwin Lite - A subsystem (C++17)\n"
              << "register USER / login USER  (password is prompted, visible locally)\n"
              << "logout / help / quit\n"
              << "After login:\n"
              << "  list | new | load \"DIR\" | save \"DIR\"\n"
              << "  location-add ID \"NAME\" KIND CAPACITY\n"
              << "  location-update ID \"NAME\" KIND CAPACITY\n"
              << "  location-remove ID\n"
              << "  road-set FROM TO METERS | road-remove FROM TO\n"
              << "  facility-set ID LOCATION WINDOWS SERVICE_MINUTES\n"
              << "  facility-remove ID | route FROM TO\n"
              << "KIND: Teaching / Canteen / Dormitory / Library\n"
              << "Use double quotes around names and paths containing spaces.\n"
              << "new/load/quit reject unsaved edits: save them first.\n";
}
std::string word(std::istringstream& in) {
    in >> std::ws;
    std::string value;
    if (in.peek() != '"') {
        if (!(in >> value)) throw ValidationError("missing argument; type help");
        return value;
    }
    in.get();
    // Quoted Windows paths keep their backslashes; CSV names cannot contain quotes.
    char c = 0;
    bool closed = false;
    while (in.get(c)) {
        if (c == '"') { closed = true; break; }
        value += c;
    }
    if (!closed) throw ValidationError("missing closing double quote");
    const auto next = in.peek();
    if (next != std::char_traits<char>::eof() && next != ' ' && next != '\t')
        throw ValidationError("expected a space after closing double quote");
    return value;
}
void end(std::istringstream& in) {
    in >> std::ws;
    if (!in.eof()) throw ValidationError("too many arguments; type help");
}
std::size_t count(const std::string& value) {
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw ValidationError("expected a nonnegative integer: " + value);
    std::size_t used = 0;
    unsigned long long n = 0;
    try { n = std::stoull(value, &used); }
    catch (const std::exception&) { throw ValidationError("integer out of range: " + value); }
    if (used != value.size() || n > std::numeric_limits<std::size_t>::max())
        throw ValidationError("integer out of range: " + value);
    return static_cast<std::size_t>(n);
}
double meters(const std::string& value) {
    std::istringstream in(value);
    in.imbue(std::locale::classic());
    double result = 0;
    if (!(in >> std::noskipws >> result) || !in.eof() || !std::isfinite(result))
        throw ValidationError("invalid distance: " + value);
    return result;
}
LocationKind kind(const std::string& value) {
    if (value == "Teaching") return LocationKind::Teaching;
    if (value == "Canteen") return LocationKind::Canteen;
    if (value == "Dormitory") return LocationKind::Dormitory;
    if (value == "Library") return LocationKind::Library;
    throw ValidationError("unknown location kind: " + value);
}
const char* kind_name(LocationKind value) {
    switch (value) {
    case LocationKind::Teaching: return "Teaching";
    case LocationKind::Canteen: return "Canteen";
    case LocationKind::Dormitory: return "Dormitory";
    case LocationKind::Library: return "Library";
    }
    throw ValidationError("unknown location kind");
}
void list(const CampusScenario& s) {
    std::cout << "Locations=" << s.locations.size() << " Roads=" << s.roads.size()
              << " Facilities=" << s.facilities.size() << '\n';
    for (const auto& x : s.locations)
        std::cout << "LOCATION " << x.id << ' ' << std::quoted(x.name) << ' '
                  << kind_name(x.kind) << " capacity=" << x.capacity << '\n';
    for (const auto& x : s.roads)
        std::cout << "ROAD " << x.from << " <-> " << x.to << " meters=" << x.distance_m << '\n';
    for (const auto& x : s.facilities)
        std::cout << "FACILITY " << x.id << " location=" << x.location_id
                  << " windows=" << x.window_count << " service_minutes=" << x.service_minutes << '\n';
}
}

int run_demo(int argc, char** argv) {
    try {
        std::filesystem::path scenario_dir = "data/scenarios/mvp";
        std::filesystem::path account_file = "data/private/accounts.db";
        bool autoload = true;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help") { help(); return 0; }
            if (arg == "--empty") { autoload = false; continue; }
            if ((arg == "--scenario" || arg == "--accounts") && i + 1 < argc) {
                if (arg == "--scenario") scenario_dir = std::filesystem::u8path(argv[++i]);
                else account_file = std::filesystem::u8path(argv[++i]);
            } else throw ValidationError("unknown or incomplete option: " + arg);
        }
        CampusEnvironment environment;
        if (autoload) environment.load_directory(scenario_dir);
        AccountStore accounts(account_file);
        std::string user;
        bool dirty = false;
        bool had_error = false;
        help();
        std::cout << std::setprecision(15);
        for (std::string line; std::cout << (user.empty() ? "guest" : user)
                                       << (dirty ? "*" : "") << "> ", std::getline(std::cin, line);) {
            try {
                std::istringstream in(line);
                std::string command;
                if (!(in >> command)) continue;
                if (command == "help") { end(in); help(); continue; }
                if (command == "quit") {
                    end(in);
                    if (dirty) throw ValidationError("unsaved edits; use save before quit");
                    return had_error ? 1 : 0;
                }
                if (command == "register" || command == "login") {
                    const auto name = word(in); end(in);
                    std::cout << "Password (visible in this local teaching demo): " << std::flush;
                    std::string password;
                    if (!std::getline(std::cin, password)) throw ValidationError("password input ended");
                    if (command == "register") {
                        accounts.register_user(name, password);
                        std::cout << "Registered. Use login to enter.\n";
                    } else {
                        if (!accounts.login(name, password)) throw ValidationError("invalid username or password");
                        user = name;
                        std::cout << "Logged in as " << user << '\n';
                    }
                    continue;
                }
                if (command == "logout") { end(in); user.clear(); std::cout << "Logged out.\n"; continue; }
                if (user.empty()) throw ValidationError("login required");
                if (command == "list") { end(in); list(environment.scenario()); }
                else if (command == "new") {
                    end(in);
                    if (dirty) throw ValidationError("unsaved edits; use save before new");
                    environment.replace_scenario({}); dirty = true;
                    std::cout << "Created an empty scenario.\n";
                } else if (command == "load" || command == "save") {
                    const auto dir = std::filesystem::u8path(word(in)); end(in);
                    if (command == "load") {
                        if (dirty) throw ValidationError("unsaved edits; use save before load");
                        environment.load_directory(dir);
                        std::cout << "Loaded " << dir.u8string() << '\n';
                    } else {
                        environment.save_directory(dir);
                        std::cout << "Saved " << dir.u8string() << '\n';
                    }
                    dirty = false;
                } else if (command == "location-add" || command == "location-update") {
                    Location x;
                    x.id = word(in); x.name = word(in); x.kind = kind(word(in)); x.capacity = count(word(in)); end(in);
                    if (command == "location-add") environment.add_location(x);
                    else environment.update_location(x);
                    dirty = true; std::cout << "Location updated.\n";
                } else if (command == "location-remove") {
                    const auto id = word(in); end(in); environment.remove_location(id);
                    dirty = true; std::cout << "Location removed.\n";
                } else if (command == "road-set") {
                    Road x; x.from = word(in); x.to = word(in); x.distance_m = meters(word(in)); end(in);
                    environment.set_road(x); dirty = true; std::cout << "Road updated.\n";
                } else if (command == "road-remove") {
                    const auto from = word(in), to = word(in); end(in);
                    environment.remove_road(from, to); dirty = true; std::cout << "Road removed.\n";
                } else if (command == "facility-set") {
                    FacilityConfig x;
                    x.id = word(in); x.location_id = word(in); x.window_count = count(word(in));
                    const auto duration = count(word(in)); end(in);
                    if (duration > static_cast<std::size_t>(std::numeric_limits<SimTime>::max()))
                        throw ValidationError("service_minutes exceeds SimTime range");
                    x.service_minutes = static_cast<SimTime>(duration);
                    environment.set_facility(x); dirty = true; std::cout << "Facility updated.\n";
                } else if (command == "facility-remove") {
                    const auto id = word(in); end(in); environment.remove_facility(id);
                    dirty = true; std::cout << "Facility removed.\n";
                } else if (command == "route") {
                    const auto from = word(in), to = word(in); end(in);
                    const auto route = environment.find_route(from, to);
                    if (!route.reachable) std::cout << "UNREACHABLE\n";
                    else {
                        std::cout << "distance_m=" << route.distance_m << " path=";
                        for (std::size_t i = 0; i < route.path.size(); ++i) {
                            if (i) std::cout << " -> ";
                            std::cout << route.path[i];
                        }
                        std::cout << '\n';
                    }
                } else throw ValidationError("unknown command; type help");
            } catch (const std::exception& error) {
                had_error = true;
                std::cerr << "ERROR: " << error.what() << '\n';
            }
        }
        if (dirty) { std::cerr << "ERROR: input ended with unsaved edits\n"; return 1; }
        return had_error ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}


// Windows supplies UTF-16 command-line arguments. Convert once at the boundary.
// Interactive text and scene files use UTF-8 on every platform.
#ifdef _WIN32
int wmain(int argc, wchar_t** wide_argv) {
    std::vector<std::string> utf8;
    for (int i = 0; i < argc; ++i) utf8.push_back(std::filesystem::path(wide_argv[i]).u8string());
    std::vector<char*> argv;
    for (auto& value : utf8) argv.push_back(value.data());
    return run_demo(argc, argv.data());
}
#else
int main(int argc, char** argv) { return run_demo(argc, argv); }
#endif
