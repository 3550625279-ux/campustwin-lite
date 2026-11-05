#include "campustwin/resources.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace campustwin {
namespace {

bool valid_id(const std::string& value) {
    if (value.empty()) {
        return false;
    }

    for (unsigned char ch : value) {
        const bool is_letter = (ch >= 'A' && ch <= 'Z') ||
                               (ch >= 'a' && ch <= 'z');
        const bool is_digit = ch >= '0' && ch <= '9';
        if (!is_letter && !is_digit && ch != '_' && ch != '-') {
            return false;
        }
    }
    return true;
}

// MVP request IDs must match the student: "student_id:lunch:1".
bool valid_request_id(const RequestId& request_id,
                      const StudentId& student_id) {
    return valid_id(student_id) &&
           request_id == student_id + ":lunch:1";
}

void check_time(SimTime now) {
    if (now < 0) {
        throw ValidationError("C: negative simulation time");
    }
}

struct FrozenFacility {
    FacilityDecisionStatus status;
    FacilityConfig config;
    std::deque<StudentArrival> queue;
    std::vector<SimTime> available_at;
    mutable std::optional<std::int64_t> next_available_after_queue;
};

// Match waiting requests in FIFO order to available, compatible windows.
// An augmenting path may move an earlier request to another compatible window,
// but never removes it. This avoids wasting a specialist window when a
// flexible window can serve someone else. Window order breaks ties.
std::vector<std::optional<std::size_t>> assign_windows(
        const FacilityConfig& config, const std::deque<StudentArrival>& queue,
        const std::vector<bool>& free) {
    std::vector<std::optional<std::size_t>> assigned(config.window_count);
    if (config.window_services.empty()) {
        std::size_t next = 0;
        for (std::size_t w = 0; w < free.size() && next < queue.size(); ++w)
            if (free[w]) assigned[w] = next++;
        return assigned;
    }
    const auto none = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> request_window(queue.size(), none);
    std::size_t matched = 0;
    const auto capacity = static_cast<std::size_t>(std::count(free.begin(), free.end(), true));
    for (std::size_t root = 0; root < queue.size() && matched < capacity; ++root) {
        std::vector<std::size_t> parent(config.window_count, none), pending{root};
        std::size_t cursor = 0, found = none;
        while (cursor < pending.size() && found == none) {
            const auto request = pending[cursor++];
            for (std::size_t w = 0; w < free.size(); ++w) {
                if (!free[w] || parent[w] != none ||
                    !window_service_time(config, w, queue[request].meal_type)) continue;
                parent[w] = request;
                if (!assigned[w]) { found = w; break; }
                pending.push_back(*assigned[w]);
            }
        }
        if (found == none) continue;
        while (found != none) {
            const auto request = parent[found];
            const auto previous = request_window[request];
            assigned[found] = request;
            request_window[request] = found;
            found = previous;
        }
        ++matched;
    }
    return assigned;
}

class FrozenAdvisor final : public IFacilityAdvisor {
public:
    explicit FrozenAdvisor(std::map<FacilityId, FrozenFacility> entries)
        : entries_(std::move(entries)) {}

    std::vector<FacilityDecisionStatus> statuses() const override {
        std::vector<FacilityDecisionStatus> out;
        out.reserve(entries_.size());
        for (const auto& item : entries_) out.push_back(item.second.status);
        return out;
    }

    std::int64_t estimate_wait(const FacilityId& id, SimTime arrival_at) const override {
        const auto estimate = estimate_service(id, arrival_at, "standard");
        if (!estimate) throw ValidationError("C: facility does not serve standard meal " + id);
        return estimate->wait_minutes;
    }

    std::optional<ServiceEstimate> estimate_service(const FacilityId& id, SimTime arrival_at,
                                                     const std::string& meal) const override {
        const auto found = entries_.find(id);
        if (found == entries_.end()) throw ValidationError("C: unknown forecast facility " + id);
        const auto& facility = found->second;
        if (arrival_at < 0 || arrival_at < facility.status.observed_at)
            throw ValidationError("C: forecast arrival precedes snapshot");
        if (!facility_serves(facility.config, meal)) return std::nullopt;
        if (!facility.config.window_services.empty()) {
            auto queue = facility.queue;
            std::vector<std::int64_t> available(facility.available_at.begin(), facility.available_at.end());
            std::int64_t now = std::max<std::int64_t>(0, static_cast<std::int64_t>(facility.status.observed_at) + 1);
            bool added = false;
            for (;;) {
                if (!added && now >= arrival_at) {
                    queue.push_back({"forecast", "forecast", id, arrival_at, meal});
                    added = true;
                }
                std::vector<bool> free(available.size());
                for (std::size_t w = 0; w < available.size(); ++w) free[w] = available[w] <= now;
                const auto assignments = assign_windows(facility.config, queue, free);
                std::vector<bool> taken(queue.size(), false);
                for (std::size_t w = 0; w < assignments.size(); ++w) {
                    if (!assignments[w]) continue;
                    const auto q = *assignments[w];
                    const auto duration = *window_service_time(facility.config, w, queue[q].meal_type);
                    if (queue[q].request_id == "forecast") return ServiceEstimate{now - arrival_at, duration};
                    if (now > std::numeric_limits<std::int64_t>::max() - duration)
                        throw ValidationError("C: forecast time overflow");
                    available[w] = now + duration;
                    taken[q] = true;
                }
                std::deque<StudentArrival> remaining;
                for (std::size_t q = 0; q < queue.size(); ++q) if (!taken[q]) remaining.push_back(queue[q]);
                queue.swap(remaining);
                auto next = std::numeric_limits<std::int64_t>::max();
                if (!added && arrival_at > now) next = arrival_at;
                for (const auto time : available) if (time > now) next = std::min(next, time);
                if (next == std::numeric_limits<std::int64_t>::max())
                    throw ValidationError("C: forecast cannot schedule queued requests");
                now = next;
            }
        }
        if (!facility.next_available_after_queue) {
            std::vector<std::int64_t> times(facility.available_at.begin(), facility.available_at.end());
            std::priority_queue<std::int64_t, std::vector<std::int64_t>, std::greater<std::int64_t>>
                available(std::greater<std::int64_t>{}, std::move(times));
            if (available.empty()) throw ValidationError("C: forecast has no windows");
            // Existing queued students are ahead of the hypothetical student.
            // The immutable result is reused for every decision on this snapshot.
            for (std::size_t i = 0; i < facility.status.queue_length; ++i) {
                const auto start = available.top(); available.pop();
                if (start > std::numeric_limits<std::int64_t>::max() - facility.status.service_minutes)
                    throw ValidationError("C: forecast time overflow");
                available.push(start + facility.status.service_minutes);
            }
            facility.next_available_after_queue = available.top();
        }
        return ServiceEstimate{std::max<std::int64_t>(0, *facility.next_available_after_queue - arrival_at),
                               facility.config.service_minutes};
    }

private:
    std::map<FacilityId, FrozenFacility> entries_;
};

}  // namespace

void FacilityEngine::require_initialized() const {
    if (!initialized_) {
        throw ValidationError("C: reset must be called first");
    }
}

void FacilityEngine::reset(const std::vector<FacilityConfig>& configs) {
    // Build a fresh state first, so invalid configuration does not partially
    // overwrite an already valid engine state.
    std::map<FacilityId, FacilityState> fresh;
    std::set<LocationId> used_locations;

    for (const auto& config : configs) {
        if (!valid_id(config.id) || !valid_id(config.location_id) ||
            config.id == config.location_id || config.window_count == 0 ||
            config.window_count > max_facility_windows || config.service_minutes <= 0) {
            throw ValidationError("C: invalid facility configuration");
        }

        if (!used_locations.insert(config.location_id).second) {
            throw ValidationError("C: multiple facilities at one location");
        }

        FacilityState state;
        state.config = config;
        std::set<std::pair<std::size_t, std::string>> offerings;
        std::set<std::size_t> covered;
        for (const auto& service : config.window_services) {
            if (service.window_id >= config.window_count ||
                (service.meal_type != "*" && !valid_id(service.meal_type)) || service.service_minutes <= 0 ||
                !offerings.emplace(service.window_id, service.meal_type).second)
                throw ValidationError("C: invalid or duplicate window service configuration");
            covered.insert(service.window_id);
        }
        if (!config.window_services.empty() && covered.size() != config.window_count)
            throw ValidationError("C: every window must offer a service");
        state.windows.resize(config.window_count);

        if (!fresh.emplace(config.id, std::move(state)).second) {
            throw ValidationError("C: duplicate facility ID: " + config.id);
        }
    }

    facilities_ = std::move(fresh);
    seen_requests_.clear();
    active_students_.clear();
    last_complete_ = -1;
    current_tick_ = -1;
    phase_ = 0;
    initialized_ = true;
}

std::unique_ptr<IFacilityAdvisor> FacilityEngine::decision_snapshot() const {
    require_initialized();
    if (phase_ != 0) throw ValidationError("C: decision snapshot requires a completed tick");
    std::map<FacilityId, FrozenFacility> frozen;
    for (const auto& [id, facility] : facilities_) {
        FrozenFacility item;
        item.config = facility.config;
        item.queue = facility.queue;
        item.status = {id, facility.config.location_id, current_tick_, facility.queue.size(),
                       0, facility.config.window_count, facility.config.service_minutes};
        for (const auto& slot : facility.windows) {
            if (slot) {
                ++item.status.in_service;
                item.available_at.push_back(slot->end_at);
            } else {
                item.available_at.push_back(std::max<SimTime>(0, current_tick_));
            }
        }
        frozen.emplace(id, std::move(item));
    }
    return std::make_unique<FrozenAdvisor>(std::move(frozen));
}

std::vector<ServiceCompleted> FacilityEngine::complete_due(SimTime now) {
    require_initialized();
    check_time(now);

    // Avoid evaluating current_tick_ + 1 if the signed time type is already
    // at its maximum value.
    if (phase_ != 0 || current_tick_ == std::numeric_limits<SimTime>::max() ||
        now != current_tick_ + 1) {
        throw ValidationError(
            "C: complete_due requires next consecutive minute");
    }

    std::vector<ServiceCompleted> out;
    for (auto& [id, facility] : facilities_) {
        for (std::size_t window = 0; window < facility.windows.size(); ++window) {
            auto& slot = facility.windows[window];
            if (!slot) {
                continue;
            }

            // At t > 0, an occupied window was busy during [t-1, t).
            if (now > 0) {
                ++facility.busy_total;
            }

            if (slot->end_at > now) {
                continue;
            }

            const auto& request = slot->request;
            out.push_back({request.request_id, request.student_id, id, window, now});
            active_students_.erase(request.student_id);
            ++facility.completed;
            slot.reset();
        }
    }

    last_complete_ = now;
    current_tick_ = now;
    phase_ = 1;
    return out;
}

void FacilityEngine::enqueue(const std::vector<StudentArrival>& arrivals,
                             SimTime now) {
    require_initialized();
    check_time(now);
    if (phase_ != 1 || current_tick_ != now) {
        throw ValidationError(
            "C: enqueue must follow complete_due at the same minute");
    }

    // Validate the whole batch before modifying any queue or counter.
    std::set<RequestId> batch_requests;
    std::set<StudentId> batch_students;
    for (const auto& arrival : arrivals) {
        if (!valid_id(arrival.student_id) ||
            !valid_request_id(arrival.request_id, arrival.student_id) ||
            !valid_id(arrival.facility_id) || arrival.at != now) {
            throw ValidationError(
                "C: invalid arrival fields, request ID, or timestamp");
        }

        if (facilities_.count(arrival.facility_id) == 0) {
            throw ValidationError("C: unknown facility " + arrival.facility_id);
        }
        if (!valid_id(arrival.meal_type) ||
            !facility_serves(facilities_.at(arrival.facility_id).config, arrival.meal_type))
            throw ValidationError("C: facility cannot serve requested meal " + arrival.meal_type);

        if (seen_requests_.count(arrival.request_id) != 0 ||
            !batch_requests.insert(arrival.request_id).second) {
            throw ValidationError("C: duplicate request " + arrival.request_id);
        }

        if (active_students_.count(arrival.student_id) != 0 ||
            !batch_students.insert(arrival.student_id).second) {
            throw ValidationError(
                "C: student has an unfinished request: " + arrival.student_id);
        }
    }

    auto ordered = arrivals;
    std::sort(ordered.begin(), ordered.end(),
              [](const StudentArrival& left, const StudentArrival& right) {
                  if (left.student_id != right.student_id) {
                      return left.student_id < right.student_id;
                  }
                  return left.request_id < right.request_id;
              });

    for (const auto& arrival : ordered) {
        auto& facility = facilities_.at(arrival.facility_id);
        facility.queue.push_back(arrival);
        ++facility.arrived;
        seen_requests_.insert(arrival.request_id);
        active_students_.insert(arrival.student_id);
    }

    phase_ = 2;
}

std::vector<ServiceStarted> FacilityEngine::start_ready(SimTime now) {
    require_initialized();
    check_time(now);
    if (phase_ != 2 || current_tick_ != now) {
        throw ValidationError(
            "C: start_ready must follow enqueue at the same minute");
    }

    std::vector<ServiceStarted> out;

    // Prepare and validate all assignments before mutating any facility.
    std::map<FacilityId, std::vector<std::optional<std::size_t>>> planned;
    for (const auto& [id, facility] : facilities_) {
        std::vector<bool> free;
        for (const auto& slot : facility.windows) free.push_back(!slot);
        auto assignments = assign_windows(facility.config, facility.queue, free);
        for (std::size_t w = 0; w < assignments.size(); ++w) if (assignments[w]) {
            const auto duration = *window_service_time(facility.config, w, facility.queue[*assignments[w]].meal_type);
            if (now > std::numeric_limits<SimTime>::max() - duration)
                throw ValidationError("C: service end time overflows SimTime: " + id);
        }
        planned.emplace(id, std::move(assignments));
    }
    for (auto& [id, facility] : facilities_) {
        std::vector<bool> taken(facility.queue.size(), false);
        const auto& assignments = planned.at(id);
        for (std::size_t window = 0; window < assignments.size(); ++window) {
            if (!assignments[window]) continue;
            const auto index = *assignments[window];
            const auto arrival = facility.queue[index];
            taken[index] = true;
            const auto duration = *window_service_time(facility.config, window, arrival.meal_type);
            const SimTime end_at = static_cast<SimTime>(now + duration);
            facility.windows[window] = ActiveService{arrival, end_at};

            ++facility.started;
            const SimTime wait = static_cast<SimTime>(now - arrival.at);
            facility.wait_total += wait;
            out.push_back({arrival.request_id, arrival.student_id, id,
                           window, now, wait, end_at, arrival.meal_type});
        }
        std::deque<StudentArrival> remaining;
        for (std::size_t q = 0; q < facility.queue.size(); ++q) if (!taken[q]) remaining.push_back(facility.queue[q]);
        facility.queue.swap(remaining);

        // Contract definition: maximum queue length is measured after all
        // currently available windows have been assigned.
        facility.max_queue = std::max(facility.max_queue,
                                      facility.queue.size());
    }

    phase_ = 0;
    return out;
}

std::vector<FacilitySnapshot> FacilityEngine::snapshots(SimTime now) const {
    require_initialized();
    check_time(now);
    if (phase_ != 0 || current_tick_ != now) {
        throw ValidationError("C: snapshots require completed tick");
    }

    std::vector<FacilitySnapshot> out;
    for (const auto& [id, facility] : facilities_) {
        std::size_t active = 0;
        for (const auto& slot : facility.windows) {
            if (slot) {
                ++active;
            }
        }

        out.push_back({now, id, facility.queue.size(), active,
                       facility.arrived, facility.started, facility.completed,
                       facility.max_queue, facility.wait_total,
                       facility.busy_total});
    }
    return out;
}

std::vector<FacilitySummary> FacilityEngine::summaries(SimTime now) const {
    require_initialized();
    check_time(now);
    if (phase_ != 0 || current_tick_ != now) {
        throw ValidationError("C: summaries require completed tick");
    }

    std::vector<FacilitySummary> out;
    for (const auto& [id, facility] : facilities_) {
        out.push_back({id, facility.config.window_count, now,
                       facility.arrived, facility.started, facility.completed,
                       facility.max_queue, facility.wait_total,
                       facility.busy_total});
    }
    return out;
}

}  // namespace campustwin
