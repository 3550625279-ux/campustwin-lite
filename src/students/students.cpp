#include "campustwin/students.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace campustwin {
namespace {
constexpr std::string_view student_header =
    "id,initial_location,lunch_facility,after_meal_location,"
    "departure_minute,walk_m_per_min,meal_minutes";
const std::string student_header_with_policy = std::string(student_header) + ",lunch_policy";

[[noreturn]] void invalid(const std::string& context, const std::string& message) {
    throw ValidationError(context + ": " + message);
}

void check_id(const std::string& value, const std::string& context) {
    const bool valid = !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
               (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
    });
    if (!valid) invalid(context, "expected a nonempty ASCII ID [A-Za-z0-9_-]");
}

void check_spec(const StudentSpec& spec, const std::string& context) {
    check_id(spec.id, context + " id");
    check_id(spec.initial_location, context + " initial_location");
    if (spec.lunch_policy == LunchPolicy::Fixed || !spec.lunch_facility.empty())
        check_id(spec.lunch_facility, context + " lunch_facility");
    if (spec.lunch_policy != LunchPolicy::Fixed && spec.lunch_policy != LunchPolicy::Nearest &&
        spec.lunch_policy != LunchPolicy::Fastest)
        invalid(context, "unknown lunch policy");
    check_id(spec.after_meal_location, context + " after_meal_location");
    if (spec.departure_minute < 0) invalid(context, "departure_minute must be nonnegative");
    if (!std::isfinite(spec.walk_m_per_min) || spec.walk_m_per_min <= 0.0)
        invalid(context, "walk_m_per_min must be finite and positive");
    if (spec.meal_minutes <= 0) invalid(context, "meal_minutes must be positive");
}

SimTime parse_time(const std::string& text, SimTime minimum, const std::string& context) {
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](unsigned char ch) {
            return ch >= '0' && ch <= '9';
        })) invalid(context, "expected an unsigned decimal integer");
    SimTime value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value < minimum)
        invalid(context, "time is outside the supported range");
    return value;
}

double parse_speed(const std::string& text, const std::string& context) {
    double value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        !std::isfinite(value) || value <= 0.0)
        invalid(context, "expected a finite positive decimal number");
    return value;
}

std::vector<std::string> split_row(const std::string& line, const std::string& context,
                                   std::size_t expected_columns) {
    if (line.find('"') != std::string::npos) invalid(context, "quoted CSV fields are not supported");
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const auto end = line.find(',', start);
        fields.push_back(line.substr(start, end == std::string::npos ? end : end - start));
        if (fields.back().find_first_of(" \t\r\n") != std::string::npos)
            invalid(context, "fields must not contain whitespace");
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (fields.size() != expected_columns) invalid(context, "incorrect number of columns");
    return fields;
}

LunchPolicy parse_policy(const std::string& value, const std::string& context) {
    if (value == "fixed") return LunchPolicy::Fixed;
    if (value == "nearest") return LunchPolicy::Nearest;
    if (value == "fastest") return LunchPolicy::Fastest;
    invalid(context, "lunch_policy must be fixed, nearest, or fastest");
}

SimTime checked_add(SimTime at, SimTime duration, const std::string& context) {
    const auto result = static_cast<std::int64_t>(at) + duration;
    if (at < 0 || duration <= 0 || result > std::numeric_limits<SimTime>::max())
        invalid(context, "time addition is outside the supported range");
    return static_cast<SimTime>(result);
}

MoveInfo plan_move(const IRouteProvider& routes, const LocationId& from,
                   const LocationId& to, SimTime now, double speed,
                   const StudentId& student) {
    const auto context = "student " + student + " route " + from + " -> " + to;
    const auto route = routes.find_route(from, to);
    if (!route.reachable) invalid(context, "destination is unreachable");
    if (!std::isfinite(route.distance_m) || route.distance_m <= 0.0)
        invalid(context, "expected a finite positive route distance");
    // Validate the floating-point result before converting to the integer time type.
    const double minutes = std::max(1.0, std::ceil(route.distance_m / speed));
    if (!std::isfinite(minutes) || minutes > std::numeric_limits<SimTime>::max())
        invalid(context, "travel duration is outside the supported range");
    return {from, to, now, checked_add(now, static_cast<SimTime>(minutes), context), route.distance_m};
}
}  // namespace

std::vector<StudentSpec> read_student_specs(const std::filesystem::path& file) {
    std::ifstream input(file);
    if (!input) throw std::runtime_error("cannot open student file: " + file.u8string());
    std::string line;
    if (!std::getline(input, line)) invalid(file.u8string() + ":1", "missing header");
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const bool has_policy = line == student_header_with_policy;
    if (!has_policy && line != student_header)
        invalid(file.u8string() + ":1", "unexpected student CSV header");

    std::vector<StudentSpec> result;
    std::unordered_set<StudentId> ids;
    std::size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto context = file.u8string() + ":" + std::to_string(line_number);
        const auto fields = split_row(line, context, has_policy ? 8 : 7);
        StudentSpec spec{fields[0], fields[1], fields[2], fields[3],
                         parse_time(fields[4], 0, context + " departure_minute"),
                         parse_speed(fields[5], context + " walk_m_per_min"),
                         parse_time(fields[6], 1, context + " meal_minutes")};
        if (has_policy) spec.lunch_policy = parse_policy(fields[7], context);
        check_spec(spec, context);
        if (!ids.insert(spec.id).second) invalid(context, "duplicate student ID: " + spec.id);
        result.push_back(std::move(spec));
    }
    if (input.bad()) throw std::runtime_error("error reading student file: " + file.u8string());
    return result;
}

void StudentEngine::reset(const CampusScenario& scenario,
                          const std::vector<StudentSpec>& population) {
    std::unordered_map<LocationId, LocationKind> locations;
    for (const auto& location : scenario.locations) {
        check_id(location.id, "scenario location");
        if (!locations.emplace(location.id, location.kind).second)
            invalid("scenario", "duplicate location ID: " + location.id);
    }
    std::unordered_map<FacilityId, FacilityConfig> facilities;
    std::unordered_set<LocationId> facility_locations;
    for (const auto& facility : scenario.facilities) {
        check_id(facility.id, "scenario facility");
        const auto location = locations.find(facility.location_id);
        if (location == locations.end() || location->second != LocationKind::Canteen)
            invalid("facility " + facility.id, "must refer to an existing canteen location");
        if (facility.window_count == 0 || facility.service_minutes <= 0)
            invalid("facility " + facility.id, "window count and service duration must be positive");
        if (!facilities.emplace(facility.id, facility).second ||
            !facility_locations.insert(facility.location_id).second)
            invalid("scenario", "duplicate facility ID or canteen location");
    }

    // Construct a replacement first: invalid reset input leaves the previous run intact.
    std::vector<StudentRuntime> next;
    next.reserve(population.size());
    for (const auto& spec : population) {
        const auto context = "student " + spec.id;
        check_spec(spec, context);
        if (locations.count(spec.initial_location) == 0 ||
            locations.count(spec.after_meal_location) == 0)
            invalid(context, "initial or after-meal location is unknown");
        const auto facility = facilities.find(spec.lunch_facility);
        if (!spec.lunch_facility.empty() && facility == facilities.end())
            invalid(context, "lunch facility is unknown");
        if (spec.lunch_policy == LunchPolicy::Fixed && facility == facilities.end())
            invalid(context, "fixed lunch facility is unknown");
        if (spec.lunch_policy == LunchPolicy::Fixed &&
            (spec.initial_location == facility->second.location_id ||
             spec.after_meal_location == facility->second.location_id))
            invalid(context, "both lunch trips must have distinct endpoints");
        if (spec.lunch_policy != LunchPolicy::Fixed &&
            std::none_of(facilities.begin(), facilities.end(), [&](const auto& item) {
                return item.second.location_id != spec.initial_location &&
                       item.second.location_id != spec.after_meal_location;
            })) invalid(context, "no eligible lunch facility");
        StudentRuntime student;
        student.spec = spec;
        if (spec.lunch_policy == LunchPolicy::Fixed) {
            student.selected_facility = facility->second.id;
            student.canteen_location = facility->second.location_id;
            student.service_minutes = facility->second.service_minutes;
            student.window_count = facility->second.window_count;
        }
        student.location = spec.initial_location;
        student.request_id = spec.id + ":lunch:1";
        next.push_back(std::move(student));
    }
    std::sort(next.begin(), next.end(), [](const StudentRuntime& a, const StudentRuntime& b) {
        return a.spec.id < b.spec.id;
    });
    std::unordered_map<StudentId, std::size_t> indices;
    for (std::size_t i = 0; i < next.size(); ++i) {
        if (!indices.emplace(next[i].spec.id, i).second)
            invalid("population", "duplicate student ID: " + next[i].spec.id);
    }
    std::vector<FacilityConfig> ordered;
    for (const auto& item : facilities) ordered.push_back(item.second);
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    students_.swap(next);
    student_indices_.swap(indices);
    facility_configs_.swap(ordered);
    decisions_.clear();
    initialized_ = true;
    last_tick_ = -1;
    completion_phase_started_ = false;
}

void StudentEngine::require_initialized() const {
    if (!initialized_) invalid("student engine", "call reset before using the engine");
}

StudentEngine::StudentRuntime& StudentEngine::find_student(const StudentId& id) {
    const auto found = student_indices_.find(id);
    if (found == student_indices_.end()) invalid("service event", "unknown student: " + id);
    return students_[found->second];
}

std::vector<StudentArrival> StudentEngine::tick(SimTime now, const IRouteProvider& routes) {
    return tick(now, routes, nullptr);
}

std::vector<StudentArrival> StudentEngine::tick(SimTime now, const IRouteProvider& routes,
                                                const IFacilityAdvisor* advisor) {
    require_initialized();
    if (now < 0 || static_cast<std::int64_t>(now) != static_cast<std::int64_t>(last_tick_) + 1)
        invalid("student tick", "ticks must start at 0 and advance by exactly one minute");

    // Stage the tick, so a failed route cannot consume only part of its arrivals.
    auto next = students_;
    std::vector<StudentArrival> arrivals;
    std::vector<LunchDecision> next_decisions;
    std::map<FacilityId, FacilityDecisionStatus> status_by_id;
    if (advisor) {
        for (const auto& status : advisor->statuses()) {
            const auto found = std::find_if(facility_configs_.begin(), facility_configs_.end(),
                [&](const auto& config) { return config.id == status.facility_id; });
            if (found == facility_configs_.end() || status.location_id != found->location_id ||
                status.window_count != found->window_count || status.service_minutes != found->service_minutes ||
                status.in_service > status.window_count || status.observed_at != now - 1 ||
                !status_by_id.emplace(status.facility_id, status).second)
                invalid("student decisions", "invalid or stale facility snapshot");
        }
        if (status_by_id.size() != facility_configs_.size())
            invalid("student decisions", "incomplete facility snapshot");
    }
    for (auto& student : next) {
        switch (student.state) {
        case StudentState::AtLocation:
            if (now == student.spec.departure_minute) {
                if (student.spec.lunch_policy == LunchPolicy::Fastest && !advisor)
                    invalid("student decisions", "fastest policy requires a facility snapshot");
                std::optional<std::size_t> best;
                for (const auto& config : facility_configs_) {
                    if (student.spec.lunch_policy == LunchPolicy::Fixed &&
                        config.id != student.spec.lunch_facility) continue;
                    LunchDecision decision;
                    decision.at = now;
                    decision.student_id = student.spec.id;
                    decision.policy = student.spec.lunch_policy;
                    decision.facility_id = config.id;
                    decision.destination = config.location_id;
                    decision.service_minutes = config.service_minutes;
                    if (advisor) {
                        const auto& status = status_by_id.at(config.id);
                        decision.observed_at = status.observed_at;
                        decision.queue_length = status.queue_length;
                    }
                    if (*student.location == config.location_id ||
                        student.spec.after_meal_location == config.location_id) {
                        decision.reason = "invalid_endpoint";
                        next_decisions.push_back(std::move(decision));
                        continue;
                    }
                    const auto route = routes.find_route(*student.location, config.location_id);
                    if (!route.reachable) {
                        decision.reason = "unreachable";
                        next_decisions.push_back(std::move(decision));
                        continue;
                    }
                    if (!std::isfinite(route.distance_m) || route.distance_m <= 0.0)
                        invalid("student " + student.spec.id, "expected a finite positive route distance");
                    const double minutes = std::max(1.0, std::ceil(route.distance_m / student.spec.walk_m_per_min));
                    if (!std::isfinite(minutes) || minutes > std::numeric_limits<SimTime>::max())
                        invalid("student " + student.spec.id, "travel duration is outside the supported range");
                    const auto travel = static_cast<SimTime>(minutes);
                    const auto arrival = checked_add(now, travel, "student travel");
                    decision.distance_m = route.distance_m;
                    decision.path = route.path;
                    decision.travel_minutes = travel;
                    if (advisor) {
                        decision.estimated_wait_minutes = advisor->estimate_wait(config.id, arrival);
                        if (*decision.estimated_wait_minutes < 0 ||
                            *decision.estimated_wait_minutes > std::numeric_limits<std::int64_t>::max() -
                                static_cast<std::int64_t>(travel) - config.service_minutes)
                            invalid("student decisions", "invalid or overflowing wait estimate");
                        decision.estimated_total_minutes = static_cast<std::int64_t>(travel) +
                            *decision.estimated_wait_minutes + config.service_minutes;
                    }
                    decision.reason = "not_selected";
                    next_decisions.push_back(std::move(decision));
                    const auto index = next_decisions.size() - 1;
                    if (!best ||
                        (student.spec.lunch_policy == LunchPolicy::Nearest &&
                         *next_decisions[index].distance_m < *next_decisions[*best].distance_m) ||
                        (student.spec.lunch_policy == LunchPolicy::Fastest &&
                         *next_decisions[index].estimated_total_minutes < *next_decisions[*best].estimated_total_minutes))
                        best = index;
                }
                if (!best) invalid("student " + student.spec.id, "no eligible reachable lunch facility");
                auto& chosen = next_decisions[*best];
                chosen.selected = true;
                chosen.reason = student.spec.lunch_policy == LunchPolicy::Fixed ? "fixed_assignment" :
                    student.spec.lunch_policy == LunchPolicy::Nearest ? "shortest_distance" : "earliest_estimated_meal";
                const auto found = std::find_if(facility_configs_.begin(), facility_configs_.end(),
                    [&](const auto& config) { return config.id == chosen.facility_id; });
                student.selected_facility = found->id;
                student.canteen_location = found->location_id;
                student.service_minutes = found->service_minutes;
                student.window_count = found->window_count;
                student.move = MoveInfo{*student.location, found->location_id, now,
                    checked_add(now, *chosen.travel_minutes, "student travel"), *chosen.distance_m};
                student.location.reset();
                student.state = StudentState::Moving;
            }
            break;
        case StudentState::Moving:
            if (now == student.move->arrive_at) {
                student.location = student.move->to;
                student.move.reset();
                if (student.returning) {
                    student.state = StudentState::Finished;
                } else {
                    student.state = StudentState::Queued;
                    student.arrived_at = now;
                    arrivals.push_back({student.request_id, student.spec.id,
                                        student.selected_facility, now});
                }
            }
            break;
        case StudentState::Eating:
            if (now == *student.eating_until) {
                student.move = plan_move(routes, *student.location, student.spec.after_meal_location,
                                         now, student.spec.walk_m_per_min, student.spec.id);
                student.location.reset();
                student.returning = true;
                student.state = StudentState::Moving;
            }
            break;
        case StudentState::Queued:
        case StudentState::InService:
        case StudentState::Finished:
            break;
        }
    }
    students_.swap(next);
    decisions_.swap(next_decisions);
    last_tick_ = now;
    completion_phase_started_ = false;
    return arrivals;
}

void StudentEngine::on_service_started(const ServiceStarted& event) {
    require_initialized();
    if (last_tick_ < 0 || event.at != last_tick_ || completion_phase_started_)
        invalid("service start", "notify starts after tick(t), before the next completion phase");
    auto& student = find_student(event.student_id);
    if (event.request_id != student.request_id || event.facility_id != student.selected_facility)
        invalid("service start", "request or facility does not match this student");
    if (student.state != StudentState::Queued || !student.arrived_at)
        invalid("service start", "student must be waiting for this request");
    if (event.window_id >= student.window_count || event.at < *student.arrived_at ||
        event.wait_minutes < 0 || event.wait_minutes != event.at - *student.arrived_at)
        invalid("service start", "invalid window, timestamp or waiting duration");
    checked_add(event.at, student.service_minutes, "service start");
    student.service = event;
    student.state = StudentState::InService;
}

void StudentEngine::on_service_completed(const ServiceCompleted& event) {
    require_initialized();
    if (event.at < 0 || static_cast<std::int64_t>(event.at) !=
                            static_cast<std::int64_t>(last_tick_) + 1)
        invalid("service completion", "notify completions before tick(t)");
    auto& student = find_student(event.student_id);
    if (event.request_id != student.request_id || event.facility_id != student.selected_facility)
        invalid("service completion", "request or facility does not match this student");
    if (student.state != StudentState::InService || !student.service)
        invalid("service completion", "student must be in service for this request");
    const auto expected_at = checked_add(student.service->at, student.service_minutes, "service completion");
    if (event.window_id != student.service->window_id || event.at != expected_at)
        invalid("service completion", "window or completion time does not match the started service");
    const auto eating_until = checked_add(event.at, student.spec.meal_minutes, "meal completion");
    student.eating_until = eating_until;
    student.state = StudentState::Eating;
    completion_phase_started_ = true;
}

std::vector<StudentSnapshot> StudentEngine::snapshots(SimTime now) const {
    require_initialized();
    if (last_tick_ < 0 || now != last_tick_ || completion_phase_started_)
        invalid("student snapshots", "read the completed current tick, after its service events");
    std::vector<StudentSnapshot> result;
    result.reserve(students_.size());
    for (const auto& student : students_) {
        const auto selected = student.selected_facility.empty() ? std::optional<FacilityId>{} :
            std::optional<FacilityId>{student.selected_facility};
        result.push_back({now, student.spec.id, student.state, student.location, student.move, selected});
    }
    return result;
}

std::vector<LunchDecision> StudentEngine::decisions(SimTime now) const {
    require_initialized();
    if (last_tick_ < 0 || now != last_tick_ || completion_phase_started_)
        invalid("student decisions", "read decisions for the completed current tick");
    return decisions_;
}
}  // namespace campustwin
