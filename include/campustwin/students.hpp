#pragma once
#include "campustwin/contracts.hpp"
#include <unordered_map>

namespace campustwin {
std::vector<StudentSpec> read_student_specs(const std::filesystem::path& file);
// Returns a replacement; requires a nonempty schedule for every student.
std::vector<StudentSpec> read_schedules(const std::filesystem::path& file,
                                      const std::vector<StudentSpec>& population);

class StudentEngine final : public IStudentEngine {
public:
    void reset(const CampusScenario& scenario,
               const std::vector<StudentSpec>& population) override;
    std::vector<StudentArrival> tick(SimTime now, const IRouteProvider& routes) override;
    std::vector<StudentArrival> tick(SimTime now, const IRouteProvider& routes,
                                    const IFacilityAdvisor* advisor) override;
    std::vector<LunchDecision> decisions(SimTime now) const override;
    void on_service_started(const ServiceStarted& event) override;
    void on_service_completed(const ServiceCompleted& event) override;
    std::vector<StudentSnapshot> snapshots(SimTime now) const override;
    std::vector<SeatRequest> seat_requests(SimTime now) const override;
    void on_seat_granted(const SeatGranted& grant) override;
    std::vector<ActivityRecord> activity_records() const override;
private:
    struct StudentRuntime {
        StudentSpec spec;
        FacilityId selected_facility;
        LocationId canteen_location;
        SimTime service_minutes{0};
        std::size_t window_count{0};
        StudentState state{StudentState::AtLocation};
        std::optional<LocationId> location;
        std::optional<MoveInfo> move;
        bool returning{false};
        RequestId request_id;
        std::optional<SimTime> arrived_at;
        std::optional<ServiceStarted> service;
        std::optional<SimTime> eating_until;
        std::size_t activity_index{0};
        std::vector<ActivityRecord> records;
        std::optional<SimTime> activity_until;
        std::optional<SeatRequest> seat_request;
        bool seat_enqueued{false};
        std::optional<LocationId> occupied_seat;
    };

    void require_initialized() const;
    StudentRuntime& find_student(const StudentId& id);
    void finish_activity(StudentRuntime& student, SimTime now);
    void start_activity(StudentRuntime& student, SimTime now);
    bool advance_schedule(StudentRuntime& student, SimTime now, const IRouteProvider& routes);

    // reset sorts by ID, so events and snapshots do not depend on CSV row order.
    std::vector<StudentRuntime> students_;
    std::unordered_map<StudentId, std::size_t> student_indices_;
    std::vector<FacilityConfig> facility_configs_;
    std::vector<LunchDecision> decisions_;
    std::vector<SeatRequest> seat_requests_;
    std::unordered_map<LocationId, std::size_t> seat_capacities_;
    bool initialized_{false};
    SimTime last_tick_{-1};
    // Completions for t arrive before tick(t); snapshots of t-1 are then stale.
    bool completion_phase_started_{false};
};
}  // namespace campustwin
