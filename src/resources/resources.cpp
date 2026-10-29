#include "campustwin/resources.hpp"

#include <algorithm>
#include <limits>
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
            config.window_count > 100000 || config.service_minutes <= 0) {
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
