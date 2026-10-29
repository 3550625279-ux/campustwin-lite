#pragma once
#include "campustwin/contracts.hpp"
#include <deque>
#include <map>
#include <optional>
#include <set>

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
    struct ActiveService { StudentArrival request; SimTime end_at; };
    struct FacilityState {
        FacilityConfig config;
        std::deque<StudentArrival> queue;
        std::vector<std::optional<ActiveService>> windows;
        std::size_t arrived{0}, started{0}, completed{0}, max_queue{0};
        std::int64_t wait_total{0}, busy_total{0};
    };
    std::map<FacilityId, FacilityState> facilities_;
    std::set<RequestId> seen_requests_;
    std::set<StudentId> active_students_;
    bool initialized_{false};
    SimTime last_complete_{-1};
    SimTime current_tick_{-1};
    int phase_{0}; // 0: complete_due, 1: enqueue, 2: start_ready, 3: snapshot
    void require_initialized() const;
};
} // namespace campustwin
