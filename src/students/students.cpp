#include "campustwin/students.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <set>
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
const std::string student_header_with_meal = student_header_with_policy + ",meal_type";

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
    check_id(spec.meal_type, context + " meal_type");
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

MoveInfo plan_move(const RouteInfo& route, const LocationId& from,
                   const LocationId& to, SimTime now, double speed,
                   const StudentId& student) {
    const auto context = "student " + student + " route " + from + " -> " + to;
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
    const bool has_meal = line == student_header_with_meal;
    const bool has_policy = has_meal || line == student_header_with_policy;
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
        const auto fields = split_row(line, context, has_meal ? 9 : has_policy ? 8 : 7);
        StudentSpec spec{fields[0], fields[1], fields[2], fields[3],
                         parse_time(fields[4], 0, context + " departure_minute"),
                         parse_speed(fields[5], context + " walk_m_per_min"),
                         parse_time(fields[6], 1, context + " meal_minutes")};
        if (has_policy) spec.lunch_policy = parse_policy(fields[7], context);
        if (has_meal) spec.meal_type = fields[8];
        check_spec(spec, context);
        if (!ids.insert(spec.id).second) invalid(context, "duplicate student ID: " + spec.id);
        result.push_back(std::move(spec));
    }
    if (input.bad()) throw std::runtime_error("error reading student file: " + file.u8string());
    return result;
}

std::vector<StudentSpec> read_schedules(const std::filesystem::path& file,
                                      const std::vector<StudentSpec>& population) {
    std::ifstream input(file);
    if (!input) throw ValidationError("cannot open schedule file: " + file.u8string());
    std::string line;
    if (!std::getline(input, line)) invalid("schedule", "missing header");
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::string header = "student_id,sequence,activity_id,kind,location_id,start_minute,duration_minutes,requires_seat";
    const bool has_meal_threshold = line == header + ",fast_meal_from";
    if (line != header && !has_meal_threshold)
        invalid("schedule", "unexpected schedule CSV header");
    auto result = population;
    std::map<StudentId, std::map<SimTime, ActivitySpec>> schedules;
    for (auto& spec : result) {
        if (!schedules.emplace(spec.id, std::map<SimTime, ActivitySpec>{}).second)
            invalid("schedule", "duplicate student");
        spec.activities.clear();
    }
    std::size_t number = 1;
    while (std::getline(input, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto context = file.u8string() + ":" + std::to_string(number);
        const auto f = split_row(line, context, has_meal_threshold ? 9 : 8);
        if (!schedules.count(f[0])) invalid(context, "unknown schedule student");
        ActivitySpec a;
        a.id = f[2]; check_id(a.id, context + " activity_id");
        if (f[3] == "class") a.kind = ActivityKind::Class;
        else if (f[3] == "study") a.kind = ActivityKind::Study;
        else if (f[3] == "rest") a.kind = ActivityKind::Rest;
        else if (f[3] == "lunch") a.kind = ActivityKind::Lunch;
        else if (f[3] == "sport") a.kind = ActivityKind::Sport;
        else invalid(context, "kind must be class, study, rest, lunch or sport");
        a.location_id = f[4];
        a.after_previous = a.kind == ActivityKind::Rest && f[5].empty();
        if (!a.after_previous) a.start_minute = parse_time(f[5], 0, context + " start_minute");
        a.duration_minutes = parse_time(f[6], 1, context + " duration_minutes");
        if (f[7] != "0" && f[7] != "1") invalid(context, "requires_seat must be 0 or 1");
        a.requires_seat = f[7] == "1";
        if (has_meal_threshold && !f[8].empty()) {
            if (a.kind != ActivityKind::Lunch) invalid(context, "fast_meal_from applies only to lunch");
            a.fast_meal_from = parse_time(f[8], 0, context + " fast_meal_from");
        }
        const auto sequence = parse_time(f[1], 1, context + " sequence");
        if (!schedules.at(f[0]).emplace(sequence, std::move(a)).second)
            invalid(context, "duplicate schedule sequence");
    }
    if (input.bad()) invalid("schedule", "read failed");
    for (auto& spec : result) {
        const auto& rows = schedules.at(spec.id);
        if (rows.empty()) invalid("schedule " + spec.id, "every student needs a schedule");
        std::int64_t expected = 1;
        for (const auto& row : rows) {
            if (row.first != expected++) invalid("schedule " + spec.id, "sequence must be contiguous from 1");
            spec.activities.push_back(row.second);
        }
    }
    return result;
}

std::vector<StudentSpec> read_scheduled_students(const std::filesystem::path& students,
        const std::optional<std::filesystem::path>& schedule) {
    auto population = read_student_specs(students);
    if (schedule) return read_schedules(*schedule, population);
    if (students.parent_path().filename() == "students") {
        const auto paired = students.parent_path().parent_path() / "schedules" / students.filename();
        if (std::filesystem::exists(paired)) return read_schedules(paired, population);
    }
    invalid(students.u8string(), "missing activity schedule; provide --schedule FILE");
}

void StudentEngine::reset(const CampusScenario& scenario,
                          const std::vector<StudentSpec>& population) {
    std::unordered_map<LocationId, LocationKind> locations;
    for (const auto& location : scenario.locations) {
        check_id(location.id, "scenario location");
        if (!locations.emplace(location.id, location.kind).second)
            invalid("scenario", "duplicate location ID: " + location.id);
    }
    std::unordered_map<LocationId, std::size_t> seat_capacities;
    for (const auto& pool : scenario.seat_pools) {
        if (!locations.count(pool.location_id) || pool.capacity == 0 || pool.capacity > max_facility_windows ||
            !seat_capacities.emplace(pool.location_id, pool.capacity).second)
            invalid("student engine", "invalid seat pool");
    }
    std::unordered_map<FacilityId, FacilityConfig> facilities;
    std::unordered_set<LocationId> facility_locations;
    for (const auto& facility : scenario.facilities) {
        check_id(facility.id, "scenario facility");
        const auto location = locations.find(facility.location_id);
        if (location == locations.end() || location->second != LocationKind::Canteen)
            invalid("facility " + facility.id, "must refer to an existing canteen location");
        if (facility.window_count == 0 || facility.window_count > max_facility_windows || facility.service_minutes <= 0)
            invalid("facility " + facility.id, "window count and service duration must be positive");
        std::set<std::pair<std::size_t, std::string>> offerings;
        std::set<std::size_t> covered_windows;
        for (const auto& service : facility.window_services) {
            if (service.meal_type != "*") check_id(service.meal_type, "facility meal_type");
            if (service.window_id >= facility.window_count || service.service_minutes <= 0 ||
                !offerings.emplace(service.window_id, service.meal_type).second)
                invalid("facility " + facility.id, "invalid or duplicate window service");
            covered_windows.insert(service.window_id);
        }
        if (!facility.window_services.empty() && covered_windows.size() != facility.window_count)
            invalid("facility " + facility.id, "every window must offer a service");
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
        if (spec.lunch_policy == LunchPolicy::Fixed && !facility_serves(facility->second, spec.meal_type))
            invalid(context, "fixed lunch facility cannot serve requested meal");
        if (spec.activities.empty()) invalid(context, "a nonempty activity schedule is required");
        StudentRuntime student;
        student.spec = spec;
        std::set<std::string> activity_ids;
        std::size_t lunches = 0;
        SimTime previous = -1;
        for (const auto& activity : spec.activities) {
            check_id(activity.id, context + " activity ID");
            if (!activity_ids.insert(activity.id).second || activity.start_minute < 0 ||
                (!activity.after_previous && activity.start_minute < previous) || activity.duration_minutes <= 0)
                invalid(context, "invalid activity ID, duration or start order");
            if (activity.after_previous && activity.kind != ActivityKind::Rest)
                invalid(context, "only rest activities can have no scheduled start");
            if (activity.fast_meal_from && (*activity.fast_meal_from < 0 || activity.kind != ActivityKind::Lunch))
                invalid(context, "fast_meal_from must be a nonnegative minute on lunch");
            checked_add(activity.start_minute, activity.duration_minutes, context + " activity");
            if (!activity.after_previous) previous = activity.start_minute;
            if (activity.kind == ActivityKind::Lunch) {
                ++lunches;
                if (!activity.location_id.empty() || activity.requires_seat)
                    invalid(context, "lunch location must be empty and requires_seat must be 0 (automatic)");
            } else {
                const auto found = locations.find(activity.location_id);
                const bool valid_kind = found != locations.end() &&
                    ((activity.kind == ActivityKind::Class && found->second == LocationKind::Teaching) ||
                     (activity.kind == ActivityKind::Study && found->second == LocationKind::Library) ||
                     (activity.kind == ActivityKind::Sport && found->second == LocationKind::Sports) ||
                     (activity.kind == ActivityKind::Rest && found->second == LocationKind::Dormitory));
                if (!valid_kind) invalid(context, "activity location kind does not match");
                if (activity.requires_seat && !seat_capacities.count(activity.location_id))
                    invalid(context, "activity requires a configured seat pool");
            }
            ActivityRecord record;
            record.student_id = spec.id; record.activity = activity; record.destination = activity.location_id;
            student.records.push_back(std::move(record));
        }
        if (lunches != 1) invalid(context, "schedule must contain exactly one lunch");
        if (spec.lunch_policy == LunchPolicy::Fixed) {
            student.selected_facility = facility->second.id;
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
    seat_requests_.clear();
    seat_capacities_.swap(seat_capacities);
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

void StudentEngine::finish_activity(StudentRuntime& student, SimTime now, bool missed) {
    auto& record = student.records.at(student.activity_index);
    if (missed) {
        record.missed_at = record.activity.start_minute + record.activity.duration_minutes;
        if (student.seat_request) record.seat_wait_minutes = now - student.seat_request->at;
    } else {
        record.completed_at = now;
    }
    ++student.activity_index;
    student.activity_until.reset();
    student.seat_request.reset(); student.occupied_seat.reset();
    student.state = student.activity_index == student.records.size() ? StudentState::Finished : StudentState::AtLocation;
}

const LunchDecision& StudentEngine::choose_canteen(const StudentRuntime& student, SimTime now,
        const IRouteProvider& routes, const IFacilityAdvisor* advisor,
        const FacilityStatuses& status_by_id, std::vector<LunchDecision>& decisions) const {
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
        if (config.window_services.empty()) decision.service_minutes = config.service_minutes;
        if (advisor) {
            const auto& status = status_by_id.at(config.id);
            decision.observed_at = status.observed_at;
            decision.queue_length = status.queue_length;
        }
        if (!facility_serves(config, student.spec.meal_type)) {
            decision.reason = "meal_unavailable";
            decisions.push_back(std::move(decision));
            continue;
        }
        const auto route = routes.find_route(*student.location, config.location_id);
        if (!route.reachable) {
            decision.reason = "unreachable";
            decisions.push_back(std::move(decision));
            continue;
        }
        const bool same_location = *student.location == config.location_id;
        if (!std::isfinite(route.distance_m) ||
            (same_location ? route.distance_m != 0.0 : route.distance_m <= 0.0))
            invalid("student " + student.spec.id, "expected a finite positive route distance");
        const double minutes = same_location ? 0.0 : std::max(1.0, std::ceil(route.distance_m / student.spec.walk_m_per_min));
        if (!std::isfinite(minutes) || minutes > std::numeric_limits<SimTime>::max())
            invalid("student " + student.spec.id, "travel duration is outside the supported range");
        const auto travel = static_cast<SimTime>(minutes);
        const auto arrival = travel == 0 ? now : checked_add(now, travel, "student travel");
        decision.distance_m = route.distance_m;
        decision.path = route.path;
        decision.travel_minutes = travel;
        if (advisor) {
            const auto estimate = advisor->estimate_service(config.id, arrival, student.spec.meal_type);
            if (!estimate) invalid("student decisions", "advisor rejected a configured meal");
            decision.estimated_wait_minutes = estimate->wait_minutes;
            decision.service_minutes = estimate->service_minutes;
            if (*decision.estimated_wait_minutes < 0 ||
                *decision.service_minutes <= 0 ||
                *decision.estimated_wait_minutes > std::numeric_limits<std::int64_t>::max() -
                    static_cast<std::int64_t>(travel) - *decision.service_minutes)
                invalid("student decisions", "invalid or overflowing wait estimate");
            decision.estimated_total_minutes = static_cast<std::int64_t>(travel) +
                *decision.estimated_wait_minutes + *decision.service_minutes;
        }
        decision.reason = "not_selected";
        decisions.push_back(std::move(decision));
        const auto index = decisions.size() - 1;
        if (!best ||
            (student.spec.lunch_policy == LunchPolicy::Nearest &&
             *decisions[index].distance_m < *decisions[*best].distance_m) ||
            (student.spec.lunch_policy == LunchPolicy::Fastest &&
             *decisions[index].estimated_total_minutes < *decisions[*best].estimated_total_minutes))
            best = index;
    }
    if (!best) invalid("student " + student.spec.id, "no eligible reachable lunch facility");
    auto& chosen = decisions[*best];
    chosen.selected = true;
    chosen.reason = student.spec.lunch_policy == LunchPolicy::Fixed ? "fixed_assignment" :
        student.spec.lunch_policy == LunchPolicy::Nearest ? "shortest_distance" : "earliest_estimated_meal";
    return chosen;
}

void StudentEngine::request_seat(StudentRuntime& student, SimTime now, const RequestId& id) {
    const auto& record = student.records.at(student.activity_index);
    const auto& activity = record.activity;
    const auto fixed_until = activity.kind == ActivityKind::Class
        ? std::optional<SimTime>{activity.start_minute + activity.duration_minutes} : std::nullopt;
    const auto duration = fixed_until ? *fixed_until - now : record.effective_meal_minutes.value_or(activity.duration_minutes);
    checked_add(now, duration, "seat occupancy");
    student.seat_request = SeatRequest{id, student.spec.id, *student.location, now, duration, fixed_until};
    student.state = StudentState::WaitingSeat;
}

void StudentEngine::arrive_for_lunch(StudentRuntime& student, SimTime now,
                                     std::vector<StudentArrival>& arrivals) {
    student.records.at(student.activity_index).arrived_at = now;
    student.state = StudentState::Queued;
    arrivals.push_back({student.request_id, student.spec.id, student.selected_facility, now,
                        student.spec.meal_type});
}

void StudentEngine::start_due_activity(StudentRuntime& student, SimTime now, const IRouteProvider& routes,
        const IFacilityAdvisor* advisor, const FacilityStatuses& statuses,
        std::vector<LunchDecision>& decisions, std::vector<StudentArrival>& arrivals) {
    auto& record = student.records.at(student.activity_index);
    const auto& activity = record.activity;
    if (activity.kind == ActivityKind::Lunch) {
        if (now < activity.start_minute) return;
        const auto& choice = choose_canteen(student, now, routes, advisor, statuses, decisions);
        student.selected_facility = choice.facility_id;
        record.destination = choice.destination;
        record.departed_at = record.started_at = now;
        record.distance_m = *choice.distance_m;
        record.path = choice.path;
        if (*choice.travel_minutes == 0) {
            arrive_for_lunch(student, now, arrivals);
            return;
        }
        student.move = MoveInfo{*student.location, choice.destination, now,
            checked_add(now, *choice.travel_minutes, "student travel"), *choice.distance_m};
    } else if (*student.location != record.destination) {
        const auto route = routes.find_route(*student.location, record.destination);
        const auto move = plan_move(route, *student.location, record.destination, now,
                                    student.spec.walk_m_per_min, student.spec.id);
        if (!activity.after_previous && now < activity.start_minute - (move.arrive_at - now)) return;
        record.departed_at = now;
        record.distance_m = move.distance_m;
        record.path = route.path;
        student.move = move;
    } else {
        if (!record.arrived_at) {
            record.arrived_at = record.departed_at = now;
            record.path = {*student.location};
        }
        if (!activity.after_previous && now < activity.start_minute) return;
        if (activity.requires_seat) {
            request_seat(student, now, student.spec.id + ":activity:" + activity.id);
        } else {
            record.started_at = now;
            student.activity_until = activity.kind == ActivityKind::Class
                ? activity.start_minute + activity.duration_minutes
                : checked_add(now, activity.duration_minutes, "activity end");
            student.state = StudentState::InActivity;
        }
        return;
    }
    student.location.reset();
    student.state = StudentState::Moving;
}

void StudentEngine::advance_student(StudentRuntime& student, SimTime now, const IRouteProvider& routes,
        const IFacilityAdvisor* advisor, const FacilityStatuses& statuses,
        std::vector<LunchDecision>& decisions, std::vector<StudentArrival>& arrivals) {
    // 只有到达普通活动地点或完成当前活动时，才在同一分钟继续检查日程。
    while (student.state != StudentState::Finished) {
        auto& record = student.records.at(student.activity_index);
        const auto& activity = record.activity;
        if (activity.kind == ActivityKind::Class && now >= activity.start_minute + activity.duration_minutes &&
            (student.state == StudentState::AtLocation || student.state == StudentState::WaitingSeat)) {
            finish_activity(student, now, true);
            continue;
        }
        switch (student.state) {
        case StudentState::AtLocation:
            start_due_activity(student, now, routes, advisor, statuses, decisions, arrivals);
            return;

        case StudentState::Moving:
            if (now != student.move->arrive_at) return;
            student.location = student.move->to;
            student.move.reset();
            if (activity.kind == ActivityKind::Lunch) {
                arrive_for_lunch(student, now, arrivals);
                return;
            }
            record.arrived_at = now;
            student.state = StudentState::AtLocation;
            continue;

        case StudentState::Eating:
        case StudentState::InActivity:
            if (now != *student.activity_until) return;
            finish_activity(student, now);
            continue;

        case StudentState::Queued:
        case StudentState::InService:
        case StudentState::WaitingSeat:
        case StudentState::Finished:
            return;
        }
    }
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
    std::vector<SeatRequest> next_seat_requests;
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
        advance_student(student, now, routes, advisor, status_by_id, next_decisions, arrivals);
        // 取餐通知在 tick 前到达，自习申请在 tick 内产生。两者都在申请当分钟提交一次。
        if (student.state == StudentState::WaitingSeat && student.seat_request->at == now)
            next_seat_requests.push_back(*student.seat_request);
    }
    students_.swap(next);
    decisions_.swap(next_decisions);
    seat_requests_.swap(next_seat_requests);
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
    if (student.state != StudentState::Queued)
        invalid("service start", "student must be waiting for this request");
    const auto arrived = student.records.at(student.activity_index).arrived_at;
    if (!arrived || event.at < *arrived || event.wait_minutes != event.at - *arrived)
        invalid("service start", "invalid arrival or waiting duration");
    const auto config = std::find_if(facility_configs_.begin(), facility_configs_.end(),
        [&](const auto& f) { return f.id == student.selected_facility; });
    if (config == facility_configs_.end()) invalid("service start", "unknown facility");
    const auto duration = window_service_time(*config, event.window_id, student.spec.meal_type);
    if (!duration || (!event.meal_type.empty() && event.meal_type != student.spec.meal_type))
        invalid("service start", "window or meal does not match requested service");
    const auto end = checked_add(event.at, *duration, "service start");
    if ((event.expected_end_at && *event.expected_end_at != end) ||
        (!event.expected_end_at && !config->window_services.empty()))
        invalid("service start", "service end time does not match assigned window");
    student.service_minutes = *duration;
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
    auto& record = student.records.at(student.activity_index);
    const auto& activity = record.activity;
    const bool fast = activity.fast_meal_from && event.at >= *activity.fast_meal_from;
    const auto duration = fast ? activity.duration_minutes / 2 + activity.duration_minutes % 2 : activity.duration_minutes;
    const auto eating_end_if_seated_now = checked_add(event.at, duration, "meal completion");
    record.meal_received_at = event.at;
    record.effective_meal_minutes = duration;
    if (seat_capacities_.count(*student.location)) {
        request_seat(student, event.at, student.request_id + ":seat");
    } else {
        student.activity_until = eating_end_if_seated_now;
        student.state = StudentState::Eating;
    }
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
        result.push_back({now, student.spec.id, student.state, student.location, student.move, selected, student.occupied_seat});
    }
    return result;
}

std::vector<LunchDecision> StudentEngine::decisions(SimTime now) const {
    require_initialized();
    if (last_tick_ < 0 || now != last_tick_ || completion_phase_started_)
        invalid("student decisions", "read decisions for the completed current tick");
    return decisions_;
}

std::vector<SeatRequest> StudentEngine::seat_requests(SimTime now) const {
    require_initialized();
    if (now != last_tick_ || completion_phase_started_) invalid("seat requests", "read after current tick");
    return seat_requests_;
}

void StudentEngine::on_seat_granted(const SeatGranted& grant) {
    require_initialized();
    if (last_tick_ < 0 || grant.at != last_tick_ || completion_phase_started_)
        invalid("seat grant", "notify grants after current tick");
    auto& student = find_student(grant.request.student_id);
    if (student.state != StudentState::WaitingSeat || !student.seat_request)
        invalid("seat grant", "student is not waiting for a seat");
    const auto& request = *student.seat_request;
    if (grant.request.request_id != request.request_id || grant.request.location_id != request.location_id ||
        grant.request.at != request.at || grant.request.duration_minutes != request.duration_minutes ||
        grant.request.fixed_until != request.fixed_until || grant.at < request.at || grant.until <= grant.at ||
        grant.until != (request.fixed_until ? *request.fixed_until : checked_add(grant.at, request.duration_minutes, "seat grant")))
        invalid("seat grant", "request or occupancy time mismatch");
    student.occupied_seat = request.location_id;
    auto& record = student.records.at(student.activity_index);
    const bool lunch = record.activity.kind == ActivityKind::Lunch;
    student.activity_until = grant.until;
    student.state = lunch ? StudentState::Eating : StudentState::InActivity;
    record.seat_wait_minutes = grant.at - request.at;
    if (!lunch) record.started_at = grant.at;
}

std::vector<ActivityRecord> StudentEngine::activity_records() const {
    require_initialized();
    std::vector<ActivityRecord> records;
    for (const auto& student : students_) {
        auto current = student.records;
        for (auto& record : current) {
            const auto& activity = record.activity;
            if (activity.kind == ActivityKind::Class && !record.started_at &&
                activity.start_minute + activity.duration_minutes <= last_tick_)
                record.missed_at = activity.start_minute + activity.duration_minutes;
        }
        if (!current.empty() && student.state == StudentState::WaitingSeat && student.seat_request)
            current.at(student.activity_index).seat_wait_minutes = last_tick_ - student.seat_request->at;
        records.insert(records.end(), current.begin(), current.end());
    }
    return records;
}
}  // namespace campustwin
