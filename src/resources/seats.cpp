#include "campustwin/seats.hpp"
#include <algorithm>
#include <limits>
#include <tuple>

namespace campustwin {
void SeatEngine::reset(const std::vector<SeatPoolConfig>& configs) {
    std::map<LocationId, Pool> next;
    for (const auto& config : configs) {
        if (config.location_id.empty() || config.capacity == 0 || config.capacity > max_facility_windows)
            throw ValidationError("seat pool: location and capacity 1..100000 required");
        Pool pool;
        pool.stats.location_id = config.location_id;
        pool.stats.capacity = config.capacity;
        if (!next.emplace(config.location_id, std::move(pool)).second)
            throw ValidationError("seat pool: duplicate location");
    }
    pools_.swap(next); seen_.clear(); present_.clear(); now_ = -1; phase_ = 0;
}
void SeatEngine::release_due(SimTime now) {
    if ((phase_ != 0 && phase_ != 3) || now < 0 ||
        static_cast<std::int64_t>(now) != static_cast<std::int64_t>(now_) + 1)
        throw ValidationError("seats: release every consecutive minute before enqueue");
    for (auto& entry : pools_) {
        auto& pool = entry.second;
        // All prior grants occupied [t-1,t). No grant can end before this tick.
        if (now > 0) pool.stats.busy_seat_minutes += static_cast<std::int64_t>(pool.active.size());
        pool.active.erase(std::remove_if(pool.active.begin(), pool.active.end(), [&](const auto& grant) {
            if (grant.until != now) return false;
            present_.erase(grant.request.student_id);
            ++pool.stats.released;
            return true;
        }), pool.active.end());
    }
    now_ = now; phase_ = 1;
}
void SeatEngine::enqueue(const std::vector<SeatRequest>& requests, SimTime now) {
    if (phase_ != 1 || now != now_) throw ValidationError("seats: enqueue must follow release");
    auto ids = seen_;
    auto people = present_;
    auto sorted = requests;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return std::tie(a.student_id, a.request_id) < std::tie(b.student_id, b.request_id);
    });
    for (const auto& r : sorted) {
        if (r.at != now || r.request_id.empty() || r.student_id.empty() || r.duration_minutes <= 0 ||
            static_cast<std::int64_t>(now) + r.duration_minutes > std::numeric_limits<SimTime>::max() ||
            !pools_.count(r.location_id) || !ids.insert(r.request_id).second || !people.insert(r.student_id).second)
            throw ValidationError("seats: invalid, overflowing or duplicate request");
    }
    for (const auto& r : sorted) {
        auto& pool = pools_.at(r.location_id);
        pool.queue.push_back(r); ++pool.stats.arrived;
        pool.stats.max_queue = std::max(pool.stats.max_queue, pool.queue.size());
    }
    seen_.swap(ids); present_.swap(people); phase_ = 2;
}
std::vector<SeatGranted> SeatEngine::start_ready(SimTime now) {
    if (phase_ != 2 || now != now_) throw ValidationError("seats: grants must follow enqueue");
    // Check before mutating: a long-waiting request may now overflow at grant time.
    for (const auto& entry : pools_) {
        const auto& pool = entry.second;
        const auto count = std::min(pool.queue.size(), pool.stats.capacity - pool.active.size());
        for (std::size_t i = 0; i < count; ++i)
            if (static_cast<std::int64_t>(now) + pool.queue[i].duration_minutes > std::numeric_limits<SimTime>::max())
                throw ValidationError("seats: occupancy end time overflow");
    }
    std::vector<SeatGranted> result;
    for (auto& entry : pools_) {
        auto& pool = entry.second;
        while (!pool.queue.empty() && pool.active.size() < pool.stats.capacity) {
            auto r = pool.queue.front(); pool.queue.pop_front();
            SeatGranted g{r, now, static_cast<SimTime>(now + r.duration_minutes)};
            pool.active.push_back(g); result.push_back(g);
            ++pool.stats.granted;
            pool.stats.total_wait_minutes += now - r.at;
        }
    }
    phase_ = 3;
    return result;
}
std::vector<SeatSnapshot> SeatEngine::snapshots(SimTime now) const {
    if (phase_ != 3 || now != now_) throw ValidationError("seats: read snapshots after grants");
    std::vector<SeatSnapshot> result;
    for (const auto& entry : pools_) {
        auto stats = entry.second.stats;
        stats.at = now; stats.queued = entry.second.queue.size(); stats.occupied = entry.second.active.size();
        result.push_back(stats);
    }
    return result;
}
} // namespace campustwin
