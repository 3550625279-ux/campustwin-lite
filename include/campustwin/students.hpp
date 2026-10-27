#pragma once
#include "campustwin/contracts.hpp"
#include <unordered_map>

namespace campustwin {
std::vector<StudentSpec> read_student_specs(const std::filesystem::path& file);

class StudentEngine final : public IStudentEngine {
public:
    void reset(const CampusScenario& scenario,
               const std::vector<StudentSpec>& population) override;
    std::vector<StudentArrival> tick(SimTime now, const IRouteProvider& routes) override;
    void on_service_started(const ServiceStarted& event) override;
    void on_service_completed(const ServiceCompleted& event) override;
    std::vector<StudentSnapshot> snapshots(SimTime now) const override;
private:
    struct StudentRuntime {
        StudentSpec spec;
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
    };

    void require_initialized() const;
    StudentRuntime& find_student(const StudentId& id);

    // reset sorts by ID, so events and snapshots do not depend on CSV row order.
    std::vector<StudentRuntime> students_;
    std::unordered_map<StudentId, std::size_t> student_indices_;
    bool initialized_{false};
    SimTime last_tick_{-1};
    // Completions for t arrive before tick(t); snapshots of t-1 are then stale.
    bool completion_phase_started_{false};
};
}  // namespace campustwin
