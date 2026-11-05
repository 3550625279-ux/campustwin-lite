#pragma once
#include "campustwin/contracts.hpp"
#include <deque>
#include <map>
#include <set>

namespace campustwin {
// C owns resource occupancy. B owns the activity which consumes it.
class SeatEngine final : public ISeatEngine {
public:
    void reset(const std::vector<SeatPoolConfig>& pools) override;
    void release_due(SimTime now) override;
    void enqueue(const std::vector<SeatRequest>& requests, SimTime now) override;
    std::vector<SeatGranted> start_ready(SimTime now) override;
    std::vector<SeatSnapshot> snapshots(SimTime now) const override;
private:
    struct Pool {
        SeatSnapshot stats;
        std::deque<SeatRequest> queue;
        std::vector<SeatGranted> active;
    };
    std::map<LocationId, Pool> pools_;
    std::set<RequestId> seen_;
    std::set<StudentId> present_;
    SimTime now_{-1};
    int phase_{-1}; // reset, release, enqueue, grant
};
} // namespace campustwin
