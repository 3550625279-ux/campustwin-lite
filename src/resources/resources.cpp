#include "campustwin/resources.hpp"
#include "campustwin/not_implemented.hpp"

namespace campustwin {
void FacilityEngine::reset(const std::vector<FacilityConfig>&) {
    not_implemented("C", "initialize facility queues and windows");
}
std::vector<ServiceCompleted> FacilityEngine::complete_due(SimTime) {
    not_implemented("C", "release due services and accumulate busy time");
}
void FacilityEngine::enqueue(const std::vector<StudentArrival>&, SimTime) {
    not_implemented("C", "validate and enqueue the complete arrival batch");
}
std::vector<ServiceStarted> FacilityEngine::start_ready(SimTime) {
    not_implemented("C", "assign idle windows from the FIFO queue");
}
std::vector<FacilitySnapshot> FacilityEngine::snapshots(SimTime) const {
    not_implemented("C", "export facility snapshots");
}
std::vector<FacilitySummary> FacilityEngine::summaries(SimTime) const {
    not_implemented("C", "export facility summaries");
}
}  // namespace campustwin
