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
    std::map<StudentId, FacilityId> destinations;
    for (const auto& student : population)
        destinations.emplace(student.id, student.lunch_facility);

    if (students.size() != population.size() || facilities.size() != locations.size())
        throw ValidationError("simulation: missing or extra snapshot");

    std::map<FacilityId, std::size_t> queued, in_service;
    std::set<StudentId> seen_students;
    for (const auto& student : students) {
        const auto destination = destinations.find(student.student_id);
        if (student.at != now || destination == destinations.end() ||
            !seen_students.insert(student.student_id).second)
            throw ValidationError("simulation: invalid or duplicate student snapshot");
        if (student.state == StudentState::Moving) {
            if (student.location_id || !student.move)
                throw ValidationError("simulation: invalid moving student snapshot");
        } else if (!student.location_id || student.move) {
            throw ValidationError("simulation: invalid stationary student snapshot");
        }
        if (student.state == StudentState::Queued || student.state == StudentState::InService) {
            const auto location = locations.find(destination->second);
            if (location == locations.end() || *student.location_id != location->second)
                throw ValidationError("simulation: student is at the wrong facility");
            if (student.state == StudentState::Queued) ++queued[destination->second];
            else ++in_service[destination->second];
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
}  // namespace

void SimulationRunner::run(const SimulationSettings& settings,
                           const ICampusEnvironment& environment,
                           const std::vector<StudentSpec>& population,
                           IStudentEngine& students,
                           IFacilityEngine& facilities,
                           IRunWriter& writer) const {
    if (settings.duration_minutes < 1)
        throw ValidationError("simulation: duration must be at least one minute");

    const auto& scenario = environment.scenario();
    students.reset(scenario, population);
    facilities.reset(scenario.facilities);
    writer.begin(settings, scenario);

    for (SimTime now = 0;; ++now) {
        const auto completed = facilities.complete_due(now);
        for (const auto& event : completed) students.on_service_completed(event);
        const auto arrivals = students.tick(now, environment);
        facilities.enqueue(arrivals, now);
        const auto started = facilities.start_ready(now);
        for (const auto& event : started) students.on_service_started(event);
        const auto student_snapshots = students.snapshots(now);
        const auto facility_snapshots = facilities.snapshots(now);
        check_snapshots(now, scenario, population, student_snapshots, facility_snapshots);
        writer.write_tick(now, completed, arrivals, started, student_snapshots, facility_snapshots);
        if (now == settings.duration_minutes) break;
    }
    writer.finish(facilities.summaries(settings.duration_minutes));
}
}  // namespace campustwin
