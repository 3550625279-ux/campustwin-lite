#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// Draft v0.1. Units and event semantics are defined in docs/04_API_CONTRACT.md.
namespace campustwin {

using SimTime = std::int32_t;  // integer minutes since 12:00
using LocationId = std::string;
using FacilityId = std::string;
using StudentId = std::string;
using RequestId = std::string;
// Shared input bound: A validates it before C allocates window state.
inline constexpr std::size_t max_facility_windows = 100000;

class ValidationError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class LocationKind { Teaching, Canteen, Dormitory, Library };
enum class StudentState { AtLocation, Moving, Queued, InService, Eating, Finished };
enum class LunchPolicy { Fixed, Nearest, Fastest };

struct Location {
    LocationId id;
    std::string name;
    LocationKind kind{LocationKind::Teaching};
    std::size_t capacity{1};  // descriptive in MVP; not enforced
};

struct Road {
    LocationId from;
    LocationId to;
    double distance_m{0.0};  // undirected edge, finite and > 0
};

// A row describes one food served by one window. "*" means every food;
// an explicit food row takes precedence over the wildcard for that window.
struct WindowServiceConfig {
    std::size_t window_id{0};
    std::string meal_type;
    SimTime service_minutes{0};
};

struct FacilityConfig {
    FacilityId id;
    LocationId location_id;
    std::size_t window_count{0};
    SimTime service_minutes{0};
    // Empty means legacy homogeneous windows, each serving every meal.
    std::vector<WindowServiceConfig> window_services{};
};

inline std::optional<SimTime> window_service_time(const FacilityConfig& facility,
                                                  std::size_t window,
                                                  const std::string& meal) {
    if (window >= facility.window_count) return std::nullopt;
    if (facility.window_services.empty()) return facility.service_minutes;
    std::optional<SimTime> wildcard;
    for (const auto& service : facility.window_services) {
        if (service.window_id != window) continue;
        if (service.meal_type == meal) return service.service_minutes;
        if (service.meal_type == "*") wildcard = service.service_minutes;
    }
    return wildcard;
}

inline bool facility_serves(const FacilityConfig& facility, const std::string& meal) {
    if (facility.window_services.empty()) return facility.window_count > 0;
    for (const auto& service : facility.window_services)
        if (service.meal_type == "*" || service.meal_type == meal) return true;
    return false;
}

struct CampusScenario {
    std::vector<Location> locations;
    std::vector<Road> roads;
    std::vector<FacilityConfig> facilities;
};

struct RouteInfo {
    bool reachable{false};
    double distance_m{0.0};  // ignored when !reachable
    std::vector<LocationId> path;
};

struct StudentSpec {
    StudentId id;
    LocationId initial_location;
    FacilityId lunch_facility;
    LocationId after_meal_location;
    SimTime departure_minute{0};
    double walk_m_per_min{60.0};
    SimTime meal_minutes{5};
    LunchPolicy lunch_policy{LunchPolicy::Fixed};
    std::string meal_type{"standard"};
};

struct SimulationSettings {
    SimTime duration_minutes{120};  // ticks 0..duration, inclusive
};

struct StudentArrival {
    RequestId request_id;
    StudentId student_id;
    FacilityId facility_id;
    SimTime at{0};
    std::string meal_type{"standard"};
};

struct ServiceStarted {
    RequestId request_id;
    StudentId student_id;
    FacilityId facility_id;
    std::size_t window_id{0};  // 0-based within a facility
    SimTime at{0};
    SimTime wait_minutes{0};
    // C reports the actual assigned window's end time. Empty supports old test doubles.
    std::optional<SimTime> expected_end_at{};
    std::string meal_type{};
};

struct ServiceCompleted {
    RequestId request_id;
    StudentId student_id;
    FacilityId facility_id;
    std::size_t window_id{0};
    SimTime at{0};
};

struct MoveInfo {
    LocationId from;
    LocationId to;
    SimTime depart_at{0};
    SimTime arrive_at{0};
    double distance_m{0.0};
};

struct StudentSnapshot {
    SimTime at{0};
    StudentId student_id;
    StudentState state{StudentState::AtLocation};
    std::optional<LocationId> location_id;  // absent iff Moving
    std::optional<MoveInfo> move;           // present iff Moving
    std::optional<FacilityId> lunch_facility{}; // actual choice, once made
};

// A frozen, end-of-minute view. At -1 it represents the initial empty state.
struct FacilityDecisionStatus {
    FacilityId facility_id;
    LocationId location_id;
    SimTime observed_at{-1};
    std::size_t queue_length{0};
    std::size_t in_service{0};
    std::size_t window_count{0};
    SimTime service_minutes{0};
};

struct ServiceEstimate {
    std::int64_t wait_minutes{0};
    SimTime service_minutes{0};
};

// One row per evaluated candidate; estimates assume no subsequent arrivals.
struct LunchDecision {
    SimTime at{0};
    StudentId student_id;
    LunchPolicy policy{LunchPolicy::Fixed};
    FacilityId facility_id;
    LocationId destination;
    SimTime observed_at{-1};
    std::optional<std::size_t> queue_length;
    std::optional<double> distance_m;
    std::vector<LocationId> path;
    std::optional<SimTime> travel_minutes;
    std::optional<std::int64_t> estimated_wait_minutes;
    std::optional<SimTime> service_minutes;
    std::optional<std::int64_t> estimated_total_minutes;
    bool selected{false};
    std::string reason;
};

struct FacilitySnapshot {
    SimTime at{0};
    FacilityId facility_id;
    std::size_t queue_length{0};
    std::size_t in_service{0};
    std::size_t arrived{0};
    std::size_t started{0};
    std::size_t completed{0};
    std::size_t max_queue_length{0};
    std::int64_t total_wait_minutes{0};  // sum over requests that started
    std::int64_t busy_window_minutes{0};
};

struct FacilitySummary {
    FacilityId facility_id;
    std::size_t window_count{0};
    SimTime observation_minutes{0};
    std::size_t arrived{0};
    std::size_t started{0};
    std::size_t completed{0};
    std::size_t max_queue_length{0};
    std::int64_t total_wait_minutes{0};
    std::int64_t busy_window_minutes{0};
};

class IRouteProvider {
public:
    virtual ~IRouteProvider() = default;
    virtual RouteInfo find_route(const LocationId& from,
                                 const LocationId& to) const = 0;
};

class ICampusEnvironment : public IRouteProvider {
public:
    ~ICampusEnvironment() override = default;
    virtual void load_directory(const std::filesystem::path& directory) = 0;
    virtual void save_directory(const std::filesystem::path& directory) const = 0;
    virtual void replace_scenario(CampusScenario scenario) = 0;
    virtual const CampusScenario& scenario() const = 0;
};

class IFacilityAdvisor {
public:
    virtual ~IFacilityAdvisor() = default;
    virtual std::vector<FacilityDecisionStatus> statuses() const = 0;
    // FCFS estimate using the frozen queue and window release times only.
    virtual std::int64_t estimate_wait(const FacilityId& facility, SimTime arrival_at) const = 0;
    virtual std::optional<ServiceEstimate> estimate_service(const FacilityId& facility,
            SimTime arrival_at, const std::string& meal_type) const {
        (void)meal_type;
        for (const auto& status : statuses()) if (status.facility_id == facility)
            return ServiceEstimate{estimate_wait(facility, arrival_at), status.service_minutes};
        throw ValidationError("unknown forecast facility " + facility);
    }
};

class IStudentEngine {
public:
    virtual ~IStudentEngine() = default;
    virtual void reset(const CampusScenario& scenario,
                       const std::vector<StudentSpec>& population) = 0;
    virtual std::vector<StudentArrival> tick(SimTime now,
                                            const IRouteProvider& routes) = 0;
    virtual std::vector<StudentArrival> tick(SimTime now, const IRouteProvider& routes,
                                            const IFacilityAdvisor* advisor) {
        (void)advisor;
        return tick(now, routes);
    }
    virtual std::vector<LunchDecision> decisions(SimTime now) const {
        (void)now;
        return {};
    }
    virtual void on_service_started(const ServiceStarted& event) = 0;
    virtual void on_service_completed(const ServiceCompleted& event) = 0;
    virtual std::vector<StudentSnapshot> snapshots(SimTime now) const = 0;
};

class IFacilityEngine {
public:
    virtual ~IFacilityEngine() = default;
    virtual void reset(const std::vector<FacilityConfig>& facilities) = 0;
    virtual std::unique_ptr<IFacilityAdvisor> decision_snapshot() const { return {}; }
    virtual std::vector<ServiceCompleted> complete_due(SimTime now) = 0;
    virtual void enqueue(const std::vector<StudentArrival>& arrivals,
                         SimTime now) = 0;
    virtual std::vector<ServiceStarted> start_ready(SimTime now) = 0;
    virtual std::vector<FacilitySnapshot> snapshots(SimTime now) const = 0;
    virtual std::vector<FacilitySummary> summaries(SimTime now) const = 0;
};

class IRunWriter {
public:
    virtual ~IRunWriter() = default;
    virtual void begin(const SimulationSettings& settings,
                       const CampusScenario& scenario) = 0;
    virtual void write_tick(
        SimTime now,
        const std::vector<ServiceCompleted>& completed,
        const std::vector<StudentArrival>& arrivals,
        const std::vector<ServiceStarted>& started,
        const std::vector<StudentSnapshot>& students,
        const std::vector<FacilitySnapshot>& facilities) = 0;
    virtual void write_decisions(SimTime now, const std::vector<LunchDecision>& decisions) {
        (void)now;
        (void)decisions;
    }
    virtual void finish(const std::vector<FacilitySummary>& facilities) = 0;
};

}  // namespace campustwin
