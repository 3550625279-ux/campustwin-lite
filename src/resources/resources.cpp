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
    std::vector<SimTime> available_at;
    mutable std::optional<std::int64_t> next_available_after_queue;
};

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
        const auto found = entries_.find(id);
        if (found == entries_.end()) throw ValidationError("C: unknown forecast facility " + id);
        const auto& facility = found->second;
        if (arrival_at < 0 || arrival_at < facility.status.observed_at)
            throw ValidationError("C: forecast arrival precedes snapshot");
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
        return std::max<std::int64_t>(0, *facility.next_available_after_queue - arrival_at);
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

    // Check every facility before mutating any queue/window. This prevents a
    // time overflow from leaving only part of the minute's assignments applied.
    for (const auto& [id, facility] : facilities_) {
        const auto available = std::count_if(
            facility.windows.begin(), facility.windows.end(),
            [](const auto& window) { return !window; });

        if (available > 0 && !facility.queue.empty() &&
            now > std::numeric_limits<SimTime>::max() -
                      facility.config.service_minutes) {
            throw ValidationError(
                "C: service end time overflows SimTime: " + id);
        }
    }

    for (auto& [id, facility] : facilities_) {
        for (std::size_t window = 0;
             window < facility.windows.size() && !facility.queue.empty();
             ++window) {
            if (facility.windows[window]) {
                continue;
            }

            auto arrival = facility.queue.front();
            facility.queue.pop_front();

            const SimTime end_at = static_cast<SimTime>(
                now + facility.config.service_minutes);
            facility.windows[window] = ActiveService{arrival, end_at};

            ++facility.started;
            const SimTime wait = static_cast<SimTime>(now - arrival.at);
            facility.wait_total += wait;
            out.push_back({arrival.request_id, arrival.student_id, id,
                           window, now, wait});
        }

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
