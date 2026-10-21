#pragma once
#include "campustwin/contracts.hpp"

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
    // TODO(B): own students, schedules, active requests, and movements here.
};
}  // namespace campustwin
