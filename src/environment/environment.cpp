#include "campustwin/environment.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <locale>
#include <queue>
#include <set>
#include <sstream>

namespace campustwin {
namespace {
constexpr std::size_t max_locations = 10000;
constexpr std::size_t max_roads = 100000;
constexpr std::uintmax_t max_csv_bytes = 32 * 1024 * 1024;
constexpr std::uint64_t max_integer = 2147483647;
struct Context {
    std::vector<std::string> locations, roads, facilities;
};
struct CsvRow { std::size_t line; std::vector<std::string> fields; };

void require(bool condition, const std::string& context, const std::string& message) {
    if (!condition) throw ValidationError(context + ": " + message);
}
bool ascii_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}
bool valid_utf8(const std::string& text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first < 0x80) continue;
        unsigned count = 0; std::uint32_t value = 0, minimum = 0;
        if (first >= 0xC2 && first <= 0xDF) { count = 1; value = first & 0x1F; minimum = 0x80; }
        else if (first >= 0xE0 && first <= 0xEF) { count = 2; value = first & 0x0F; minimum = 0x800; }
        else if (first >= 0xF0 && first <= 0xF4) { count = 3; value = first & 0x07; minimum = 0x10000; }
        else return false;
        if (text.size() - i < count) return false;
        for (unsigned j = 0; j < count; ++j) {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xC0) != 0x80) return false;
            value = (value << 6) | (next & 0x3F);
        }
        if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return false;
    }
    return true;
}
void field(const std::string& text, const std::string& context) {
    require(!text.empty(), context, "empty field");
    require(valid_utf8(text), context, "invalid UTF-8");
    require(text.compare(0, 3, "\xEF\xBB\xBF") != 0, context, "UTF-8 BOM is unsupported");
    require(!ascii_space(static_cast<unsigned char>(text.front())) &&
            !ascii_space(static_cast<unsigned char>(text.back())), context, "surrounding whitespace");
    for (unsigned char c : text)
        require(c >= 0x20 && c != 0x7F && c != ',' && c != '"', context,
                "commas, quotes and control characters are unsupported");
}
void id(const std::string& text, const std::string& context) {
    require(!text.empty(), context, "ID must not be empty");
    for (unsigned char c : text)
        require((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-', context,
                "ID must contain only ASCII letters, digits, underscore or hyphen");
}
std::uint64_t integer(const std::string& text, const std::string& context) {
    require(!text.empty(), context, "expected positive decimal integer");
    std::uint64_t value = 0;
    for (unsigned char c : text) {
        require(c >= '0' && c <= '9', context, "expected positive decimal integer");
        const auto digit = static_cast<unsigned>(c - '0');
        require(value <= (max_integer - digit) / 10, context, "integer exceeds 2147483647");
        value = value * 10 + digit;
    }
    require(value > 0, context, "integer must be positive");
    return value;
}
double distance(const std::string& text, const std::string& context) {
    std::istringstream input(text); input.imbue(std::locale::classic());
    double value = 0; input >> std::noskipws >> value;
    require(input && input.peek() == std::char_traits<char>::eof() && std::isfinite(value) && value > 0,
            context, "distance_m must be finite, positive and fully numeric");
    return value;
}
LocationKind parse_kind(const std::string& text, const std::string& context) {
    if (text == "Teaching") return LocationKind::Teaching;
    if (text == "Canteen") return LocationKind::Canteen;
    if (text == "Dormitory") return LocationKind::Dormitory;
    if (text == "Library") return LocationKind::Library;
    throw ValidationError(context + ": unknown location kind " + text);
}
const char* kind_text(LocationKind kind) {
    switch (kind) {
        case LocationKind::Teaching: return "Teaching";
        case LocationKind::Canteen: return "Canteen";
        case LocationKind::Dormitory: return "Dormitory";
        case LocationKind::Library: return "Library";
    }
    throw ValidationError("invalid location kind");
}
std::string row_context(const std::filesystem::path& path, std::size_t line) {
    return path.u8string() + ":" + std::to_string(line);
}
std::vector<CsvRow> read_csv(const std::filesystem::path& path,
                             const std::string& header, std::size_t columns, std::size_t limit) {
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(path, ec);
    if (ec) throw std::runtime_error(path.u8string() + ": cannot read file size: " + ec.message());
    require(bytes <= max_csv_bytes, path.u8string(), "CSV exceeds 32 MiB limit");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error(path.u8string() + ": cannot open CSV");
    std::string line;
    require(static_cast<bool>(std::getline(input, line)), row_context(path, 1), "missing CSV header");
    if (!line.empty() && line.back() == '\r') line.pop_back();
    require(line == header, row_context(path, 1), "unexpected header or unsupported BOM; expected " + header);
    std::vector<CsvRow> result;
    std::size_t number = 1;
    while (std::getline(input, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto context = row_context(path, number);
        require(result.size() < limit, context, "too many rows");
        std::vector<std::string> values;
        std::size_t start = 0;
        for (;;) {
            const auto comma = line.find(',', start);
            values.push_back(line.substr(start, comma == std::string::npos ? comma : comma - start));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        require(values.size() == columns, context, "wrong column count");
        for (std::size_t i = 0; i < values.size(); ++i) field(values[i], context + " field " + std::to_string(i + 1));
        result.push_back({number, std::move(values)});
    }
    if (input.bad()) throw std::runtime_error(path.u8string() + ": CSV read failed");
    return result;
}
std::string context_at(const std::vector<std::string>* contexts, std::size_t index, const std::string& fallback) {
    return contexts ? (*contexts)[index] : fallback;
}
void validate(const CampusScenario& scenario, const Context* source = nullptr) {
    require(scenario.locations.size() <= max_locations, "scenario", "more than 10000 locations");
    require(scenario.roads.size() <= max_roads, "scenario", "more than 100000 roads");
    require(scenario.facilities.size() <= max_locations, "scenario", "more than 10000 facilities");
    std::map<LocationId, LocationKind> locations;
    for (std::size_t i = 0; i < scenario.locations.size(); ++i) {
        const auto& location = scenario.locations[i];
        const auto context = context_at(source ? &source->locations : nullptr, i, "location[" + location.id + "]");
        id(location.id, context + " id"); field(location.name, context + " name");
        kind_text(location.kind);
        require(location.capacity > 0 && location.capacity <= max_integer, context, "capacity must be 1..2147483647");
        require(locations.emplace(location.id, location.kind).second, context, "duplicate location ID " + location.id);
    }
    std::set<std::pair<LocationId, LocationId>> roads;
    for (std::size_t i = 0; i < scenario.roads.size(); ++i) {
        const auto& road = scenario.roads[i];
        const auto context = context_at(source ? &source->roads : nullptr, i, "road[" + road.from + "," + road.to + "]");
        require(locations.count(road.from) && locations.count(road.to), context, "unknown road endpoint");
        require(road.from != road.to, context, "self-loop road is unsupported");
        require(std::isfinite(road.distance_m) && road.distance_m > 0, context, "distance_m must be finite and positive");
        require(roads.emplace(std::min(road.from, road.to), std::max(road.from, road.to)).second,
                context, "duplicate undirected road");
    }
    std::set<FacilityId> facility_ids;
    std::set<LocationId> facility_locations;
    for (std::size_t i = 0; i < scenario.facilities.size(); ++i) {
        const auto& facility = scenario.facilities[i];
        const auto context = context_at(source ? &source->facilities : nullptr, i, "facility[" + facility.id + "]");
        id(facility.id, context + " id");
        require(facility_ids.insert(facility.id).second, context, "duplicate facility ID");
        const auto location = locations.find(facility.location_id);
        require(location != locations.end() && location->second == LocationKind::Canteen,
                context, "facility must refer to an existing Canteen");
        require(facility.id != facility.location_id, context, "facility ID must differ from its location ID");
        require(facility_locations.insert(facility.location_id).second, context, "multiple facilities at one location");
        require(facility.window_count > 0 && facility.window_count <= max_facility_windows,
                context, "window_count must be 1..100000");
        require(facility.service_minutes > 0, context, "service_minutes must be positive");
    }
}
bool same_edge(const Road& road, const LocationId& a, const LocationId& b) {
    return (road.from == a && road.to == b) || (road.from == b && road.to == a);
}
void write_file(const std::filesystem::path& path, const std::string& data) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error(path.u8string() + ": cannot open for writing");
    output << data; output.close();
    if (!output) throw std::runtime_error(path.u8string() + ": writing or closing CSV failed; output may be incomplete");
}
} // namespace

void CampusEnvironment::load_directory(const std::filesystem::path& directory) {
    CampusScenario next; Context source;
    const auto lp = directory / "locations.csv";
    for (const auto& row : read_csv(lp, "id,name,kind,capacity", 4, max_locations)) {
        const auto context = row_context(lp, row.line); const auto& f = row.fields;
        next.locations.push_back({f[0], f[1], parse_kind(f[2], context + " kind"),
                                  static_cast<std::size_t>(integer(f[3], context + " capacity"))});
        source.locations.push_back(context);
    }
    const auto rp = directory / "roads.csv";
    for (const auto& row : read_csv(rp, "from,to,distance_m", 3, max_roads)) {
        const auto context = row_context(rp, row.line); const auto& f = row.fields;
        next.roads.push_back({f[0], f[1], distance(f[2], context + " distance_m")});
        source.roads.push_back(context);
    }
    const auto fp = directory / "facilities.csv";
    for (const auto& row : read_csv(fp, "id,location_id,window_count,service_minutes", 4, max_locations)) {
        const auto context = row_context(fp, row.line); const auto& f = row.fields;
        next.facilities.push_back({f[0], f[1], static_cast<std::size_t>(integer(f[2], context + " window_count")),
                                   static_cast<SimTime>(integer(f[3], context + " service_minutes"))});
        source.facilities.push_back(context);
    }
    validate(next, &source);
    replace_scenario(std::move(next));
}
void CampusEnvironment::save_directory(const std::filesystem::path& directory) const {
    validate(scenario_);
    std::ostringstream locations, roads, facilities;
    locations.imbue(std::locale::classic()); roads.imbue(std::locale::classic()); facilities.imbue(std::locale::classic());
    locations << "id,name,kind,capacity\n";
    for (const auto& l : scenario_.locations) locations << l.id << ',' << l.name << ',' << kind_text(l.kind) << ',' << l.capacity << '\n';
    roads << "from,to,distance_m\n" << std::scientific << std::setprecision(std::numeric_limits<double>::max_digits10 - 1);
    for (const auto& r : scenario_.roads) roads << r.from << ',' << r.to << ',' << r.distance_m << '\n';
    facilities << "id,location_id,window_count,service_minutes\n";
    for (const auto& f : scenario_.facilities) facilities << f.id << ',' << f.location_id << ',' << f.window_count << ',' << f.service_minutes << '\n';
    require(locations.str().size() <= max_csv_bytes && roads.str().size() <= max_csv_bytes && facilities.str().size() <= max_csv_bytes, "save_directory", "serialized CSV exceeds 32 MiB limit");
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) throw std::runtime_error(directory.u8string() + ": cannot create scenario directory: " + ec.message());
    write_file(directory / "locations.csv", locations.str());
    write_file(directory / "roads.csv", roads.str());
    write_file(directory / "facilities.csv", facilities.str());
}
void CampusEnvironment::replace_scenario(CampusScenario next) {
    validate(next);
    decltype(graph_) graph;
    for (const auto& location : next.locations) graph.emplace(location.id, std::vector<std::pair<LocationId, double>>{});
    for (const auto& road : next.roads) {
        graph.at(road.from).emplace_back(road.to, road.distance_m);
        graph.at(road.to).emplace_back(road.from, road.distance_m);
    }
    for (auto& entry : graph) std::sort(entry.second.begin(), entry.second.end());
    // All parsing, validation and graph allocation finish before either live value changes.
    std::swap(scenario_, next); graph_.swap(graph);
}
const CampusScenario& CampusEnvironment::scenario() const { return scenario_; }
RouteInfo CampusEnvironment::find_route(const LocationId& from, const LocationId& to) const {
    require(graph_.count(from) && graph_.count(to), "route", "unknown source or destination ID");
    if (from == to) return {true, 0.0, {from}};
    using Item = std::pair<long double, LocationId>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pending;
    std::map<LocationId, long double> best;
    std::map<LocationId, LocationId> previous;
    best[from] = 0; pending.emplace(0, from);
    while (!pending.empty()) {
        const auto [total, current] = pending.top(); pending.pop();
        if (total != best.at(current)) continue;
        if (current == to) {
            require(std::isfinite(total) && total <= std::numeric_limits<double>::max(), "route", "shortest distance exceeds double range");
            std::vector<LocationId> path;
            for (auto node = to;; node = previous.at(node)) { path.push_back(node); if (node == from) break; }
            std::reverse(path.begin(), path.end());
            return {true, static_cast<double>(total), std::move(path)};
        }
        for (const auto& [next, length] : graph_.at(current)) {
            const long double candidate = total + static_cast<long double>(length);
            const auto found = best.find(next);
            if (found == best.end() || candidate < found->second) {
                best[next] = candidate; previous[next] = current; pending.emplace(candidate, next);
            }
        }
    }
    return {};
}
void CampusEnvironment::add_location(Location location) {
    require(!graph_.count(location.id), "add_location", "location ID already exists");
    auto next = scenario_; next.locations.push_back(std::move(location)); replace_scenario(std::move(next));
}
void CampusEnvironment::update_location(Location location) {
    auto next = scenario_;
    const auto found = std::find_if(next.locations.begin(), next.locations.end(), [&](const Location& l) { return l.id == location.id; });
    require(found != next.locations.end(), "update_location", "unknown location ID");
    *found = std::move(location); replace_scenario(std::move(next));
}
void CampusEnvironment::remove_location(const LocationId& location) {
    require(graph_.count(location), "remove_location", "unknown location ID");
    for (const auto& r : scenario_.roads) require(r.from != location && r.to != location, "remove_location", "referenced by road " + r.from + "--" + r.to);
    for (const auto& f : scenario_.facilities) require(f.location_id != location, "remove_location", "referenced by facility " + f.id);
    auto next = scenario_;
    next.locations.erase(std::remove_if(next.locations.begin(), next.locations.end(), [&](const Location& l) { return l.id == location; }), next.locations.end());
    replace_scenario(std::move(next));
}
void CampusEnvironment::set_road(Road road) {
    auto next = scenario_;
    const auto found = std::find_if(next.roads.begin(), next.roads.end(), [&](const Road& r) { return same_edge(r, road.from, road.to); });
    if (found == next.roads.end()) next.roads.push_back(std::move(road)); else *found = std::move(road);
    replace_scenario(std::move(next));
}
void CampusEnvironment::remove_road(const LocationId& from, const LocationId& to) {
    auto next = scenario_;
    const auto found = std::find_if(next.roads.begin(), next.roads.end(), [&](const Road& r) { return same_edge(r, from, to); });
    require(found != next.roads.end(), "remove_road", "road does not exist");
    next.roads.erase(found); replace_scenario(std::move(next));
}
void CampusEnvironment::set_facility(FacilityConfig facility) {
    auto next = scenario_;
    const auto found = std::find_if(next.facilities.begin(), next.facilities.end(), [&](const FacilityConfig& f) { return f.id == facility.id; });
    if (found == next.facilities.end()) next.facilities.push_back(std::move(facility)); else *found = std::move(facility);
    replace_scenario(std::move(next));
}
void CampusEnvironment::remove_facility(const FacilityId& facility) {
    auto next = scenario_;
    const auto found = std::find_if(next.facilities.begin(), next.facilities.end(), [&](const FacilityConfig& f) { return f.id == facility; });
    require(found != next.facilities.end(), "remove_facility", "facility does not exist");
    next.facilities.erase(found); replace_scenario(std::move(next));
}
} // namespace campustwin
