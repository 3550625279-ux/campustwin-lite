#pragma once
#include "campustwin/contracts.hpp"

namespace campustwin {
class FacilityEngine final : public IFacilityEngine {
public:
    void reset(const std::vector<FacilityConfig>& facilities) override;
    std::vector<ServiceCompleted> complete_due(SimTime now) override;
    void enqueue(const std::vector<StudentArrival>& arrivals, SimTime now) override;
    std::vector<ServiceStarted> start_ready(SimTime now) override;
    std::vector<FacilitySnapshot> snapshots(SimTime now) const override;
    std::vector<FacilitySummary> summaries(SimTime now) const override;
private:
    // TODO(C): own FIFO queues, windows, request histories, and counters here.
};
}  // namespace campustwin
