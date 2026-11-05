#include "campustwin/simulation.hpp"
#include <map>
#include <set>

namespace campustwin {
namespace {

// Students and facilities own their own state. Check that their snapshots
// agree, without duplicating either engine's routing or queue algorithm.
void check_snapshots(SimTime now, const CampusScenario& scenario,
                     const std::vector<StudentSpec>& population,
                     const std::vector<StudentSnapshot>& students,
                     const std::vector<FacilitySnapshot>& facilities) {
    std::map<FacilityId, LocationId> locations;
    for (const auto& facility : scenario.facilities)
        locations.emplace(facility.id, facility.location_id);
    std::map<StudentId, StudentSpec> specs;
    for (const auto& student : population)
        specs.emplace(student.id, student);

    if (students.size() != population.size() || facilities.size() != locations.size())
        throw ValidationError("simulation: missing or extra snapshot");

    std::map<FacilityId, std::size_t> queued, in_service;
    std::set<StudentId> seen_students;
    for (const auto& student : students) {
        const auto spec = specs.find(student.student_id);
        if (student.at != now || spec == specs.end() ||
            !seen_students.insert(student.student_id).second)
            throw ValidationError("simulation: invalid or duplicate student snapshot");
        const auto destination = student.lunch_facility ? *student.lunch_facility : spec->second.lunch_facility;
        if (spec->second.lunch_policy == LunchPolicy::Fixed && destination != spec->second.lunch_facility)
            throw ValidationError("simulation: fixed student changed facility");
        if (student.lunch_facility && locations.count(*student.lunch_facility) == 0)
            throw ValidationError("simulation: student selected an unknown facility");
        if (spec->second.lunch_policy != LunchPolicy::Fixed &&
            ((spec->second.activities.empty() && student.state != StudentState::AtLocation) ||
             student.state == StudentState::Queued || student.state == StudentState::InService) && !student.lunch_facility)
            throw ValidationError("simulation: missing dynamic facility choice");
        if (student.state == StudentState::Moving) {
            if (student.location_id || !student.move)
                throw ValidationError("simulation: invalid moving student snapshot");
        } else if (!student.location_id || student.move) {
            throw ValidationError("simulation: invalid stationary student snapshot");
        }
        if (student.state == StudentState::Queued || student.state == StudentState::InService) {
            const auto location = locations.find(destination);
            if (location == locations.end() || *student.location_id != location->second)
                throw ValidationError("simulation: student is at the wrong facility");
            if (student.state == StudentState::Queued) ++queued[destination];
            else ++in_service[destination];
        }
    }

    std::set<FacilityId> seen_facilities;
    for (const auto& facility : facilities) {
        if (facility.at != now || locations.count(facility.facility_id) == 0 ||
            !seen_facilities.insert(facility.facility_id).second ||
            facility.completed > facility.started || facility.started > facility.arrived ||
            facility.queue_length != facility.arrived - facility.started ||
            facility.in_service != facility.started - facility.completed ||
            facility.max_queue_length < facility.queue_length ||
            facility.total_wait_minutes < 0 || facility.busy_window_minutes < 0 ||
            facility.queue_length != queued[facility.facility_id] ||
            facility.in_service != in_service[facility.facility_id])
            throw ValidationError("simulation: inconsistent facility snapshot: " + facility.facility_id);
    }
}

void check_seats(SimTime now, const CampusScenario& scenario,
                 const std::vector<StudentSnapshot>& students, const std::vector<SeatSnapshot>& seats) {
    std::map<LocationId, std::size_t> capacities, waiting, occupied;
    for (const auto& pool : scenario.seat_pools) capacities.emplace(pool.location_id, pool.capacity);
    for (const auto& student : students) {
        if (student.state == StudentState::WaitingSeat) {
            if (!student.location_id || !capacities.count(*student.location_id) || student.occupied_seat)
                throw ValidationError("simulation: invalid seat waiter");
            ++waiting[*student.location_id];
        }
        if (student.occupied_seat) {
            if (student.location_id != student.occupied_seat || !capacities.count(*student.occupied_seat) ||
                (student.state != StudentState::Eating && student.state != StudentState::InActivity))
                throw ValidationError("simulation: invalid seat occupant");
            ++occupied[*student.occupied_seat];
        }
    }
    if (capacities.size() != seats.size()) throw ValidationError("simulation: incomplete seat snapshots");
    std::set<LocationId> seen;
    for (const auto& s : seats) {
        if (!seen.insert(s.location_id).second || !capacities.count(s.location_id) || s.at != now ||
            capacities.at(s.location_id) != s.capacity || s.occupied > s.capacity ||
            s.released > s.granted || s.granted > s.arrived || s.queued != s.arrived - s.granted ||
            s.occupied != s.granted - s.released || s.queued != waiting[s.location_id] ||
            s.occupied != occupied[s.location_id] || s.total_wait_minutes < 0 || s.busy_seat_minutes < 0)
            throw ValidationError("simulation: inconsistent seat snapshot");
    }
}
}  // namespace

void SimulationRunner::run(const SimulationSettings& settings,
                           const ICampusEnvironment& environment,
                           const std::vector<StudentSpec>& population,
                           IStudentEngine& students,
                           IFacilityEngine& facilities,
                           IRunWriter& writer, ISeatEngine* seats) const {
    if (settings.duration_minutes < 1)
        throw ValidationError("simulation: duration must be at least one minute");

    const auto& scenario = environment.scenario();
    if (!seats && !scenario.seat_pools.empty()) throw ValidationError("simulation: seat engine required");
    students.reset(scenario, population);
    facilities.reset(scenario.facilities);
    if (seats) seats->reset(scenario.seat_pools);
    writer.begin(settings, scenario);

    for (SimTime now = 0;; ++now) {
        // Freeze the end of t-1 before completing anything at t. All students
        // deciding in this tick use the same observation; t=0 uses reset state.
        const auto advisor = facilities.decision_snapshot();
        const auto completed = facilities.complete_due(now);
        for (const auto& event : completed) students.on_service_completed(event);
        if (seats) seats->release_due(now);
        const auto arrivals = students.tick(now, environment, advisor.get());
        facilities.enqueue(arrivals, now);
        const auto started = facilities.start_ready(now);
        for (const auto& event : started) students.on_service_started(event);
        const auto requests = students.seat_requests(now);
        std::vector<SeatGranted> grants;
        std::vector<SeatSnapshot> seat_snapshots;
        if (seats) {
            seats->enqueue(requests, now);
            grants = seats->start_ready(now);
            for (const auto& grant : grants) students.on_seat_granted(grant);
            seat_snapshots = seats->snapshots(now);
        } else if (!requests.empty()) throw ValidationError("simulation: missing seat engine");
        const auto student_snapshots = students.snapshots(now);
        const auto facility_snapshots = facilities.snapshots(now);
        check_snapshots(now, scenario, population, student_snapshots, facility_snapshots);
        check_seats(now, scenario, student_snapshots, seat_snapshots);
        writer.write_tick(now, completed, arrivals, started, student_snapshots, facility_snapshots);
        writer.write_decisions(now, students.decisions(now));
        writer.write_seats(now, requests, grants, seat_snapshots);
        if (now == settings.duration_minutes) break;
    }
    writer.write_activities(students.activity_records());
    writer.finish(facilities.summaries(settings.duration_minutes));
}
}  // namespace campustwin
