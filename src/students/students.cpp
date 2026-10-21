#include "campustwin/students.hpp"
#include "campustwin/not_implemented.hpp"

namespace campustwin {
std::vector<StudentSpec> read_student_specs(const std::filesystem::path&) {
    not_implemented("B", "parse and validate student CSV");
}
void StudentEngine::reset(const CampusScenario&, const std::vector<StudentSpec>&) {
    not_implemented("B", "initialize student states");
}
std::vector<StudentArrival> StudentEngine::tick(SimTime, const IRouteProvider&) {
    not_implemented("B", "advance schedules and movement");
}
void StudentEngine::on_service_started(const ServiceStarted&) {
    not_implemented("B", "Queued -> InService");
}
void StudentEngine::on_service_completed(const ServiceCompleted&) {
    not_implemented("B", "InService -> Eating");
}
std::vector<StudentSnapshot> StudentEngine::snapshots(SimTime) const {
    not_implemented("B", "export ordered student snapshots");
}
}  // namespace campustwin
