#include "campustwin/resources.hpp"
#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>

namespace campustwin {
namespace {
bool valid_id(const std::string& s) {
    if (s.empty()) return false;
    for (unsigned char ch : s)
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) return false;
    return true;
}
bool valid_request_id(const std::string& id) {
    const auto p = id.find(":lunch:");
    return p != std::string::npos && valid_id(id.substr(0,p)) &&
           id.substr(p+7) == "1";
}
void check_time(SimTime now) {
    if (now < 0) throw ValidationError("C: negative simulation time");
}
}
void FacilityEngine::require_initialized() const {
    if (!initialized_) throw ValidationError("C: reset must be called first");
}
void FacilityEngine::reset(const std::vector<FacilityConfig>& configs) {
    std::map<FacilityId, FacilityState> fresh;
    std::set<LocationId> used_locations;
    for (const auto& config : configs) {
        if (!valid_id(config.id) || !valid_id(config.location_id) ||
            config.window_count == 0 || config.service_minutes <= 0 ||
            config.window_count > 100000)
            throw ValidationError("C: invalid facility configuration");
        if (!used_locations.insert(config.location_id).second)
            throw ValidationError("C: multiple facilities at one location");
        FacilityState state;
        state.config = config;
        state.windows.resize(config.window_count);
        if (!fresh.emplace(config.id, std::move(state)).second)
            throw ValidationError("C: duplicate facility ID: " + config.id);
    }
    facilities_ = std::move(fresh);
    seen_requests_.clear(); active_students_.clear();
    last_complete_ = -1; current_tick_ = -1; phase_ = 0;
    initialized_ = true;
}
std::vector<ServiceCompleted> FacilityEngine::complete_due(SimTime now) {
    require_initialized(); check_time(now);
    if (phase_ != 0 || now != current_tick_ + 1)
        throw ValidationError("C: complete_due requires next consecutive minute");
    std::vector<ServiceCompleted> out;
    for (auto& [id, f] : facilities_) {
        for (std::size_t window = 0; window < f.windows.size(); ++window) {
            auto& slot = f.windows[window];
            if (!slot) continue;
            if (now > 0) ++f.busy_total;
            if (slot->end_at > now) continue;
            const auto& a = slot->request;
            out.push_back({a.request_id, a.student_id, id, window, now});
            active_students_.erase(a.student_id);
            ++f.completed;
            slot.reset();
        }
    }
    last_complete_ = now;
    current_tick_ = now;
    phase_ = 1;
    return out;
}
void FacilityEngine::enqueue(const std::vector<StudentArrival>& arrivals, SimTime now) {
    require_initialized(); check_time(now);
    if (phase_ != 1 || current_tick_ != now)
        throw ValidationError("C: enqueue must follow complete_due at the same minute");
    // Validate all requests before touching any queue.
    std::set<RequestId> batch_requests;
    std::set<StudentId> batch_students;
    for (const auto& a : arrivals) {
        if (!valid_request_id(a.request_id) || !valid_id(a.student_id) ||
            !valid_id(a.facility_id) || a.at != now)
            throw ValidationError("C: invalid arrival fields or timestamp");
        if (!facilities_.count(a.facility_id))
            throw ValidationError("C: unknown facility " + a.facility_id);
        if (seen_requests_.count(a.request_id) || !batch_requests.insert(a.request_id).second)
            throw ValidationError("C: duplicate request " + a.request_id);
        if (active_students_.count(a.student_id) || !batch_students.insert(a.student_id).second)
            throw ValidationError("C: student has an unfinished request: " + a.student_id);
    }
    auto ordered = arrivals;
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        if (a.student_id != b.student_id) return a.student_id < b.student_id;
        return a.request_id < b.request_id;
    });
    for (const auto& a : ordered) {
        facilities_.at(a.facility_id).queue.push_back(a);
        ++facilities_.at(a.facility_id).arrived;
        seen_requests_.insert(a.request_id);
        active_students_.insert(a.student_id);
    }
    phase_ = 2;
}
std::vector<ServiceStarted> FacilityEngine::start_ready(SimTime now) {
    require_initialized(); check_time(now);
    if (phase_ != 2 || current_tick_ != now)
        throw ValidationError("C: start_ready must follow enqueue at the same minute");
    std::vector<ServiceStarted> out;
    // Precheck time overflow before mutating anything.
    for (const auto& [id, f] : facilities_) {
        const auto available = std::count_if(f.windows.begin(), f.windows.end(),
                                             [](const auto& w){ return !w; });
        if (available > 0 && !f.queue.empty() &&
            now > std::numeric_limits<SimTime>::max() - f.config.service_minutes)
            throw ValidationError("C: service end time overflows SimTime: " + id);
    }
    for (auto& [id, f] : facilities_) {
        for (std::size_t w = 0; w < f.windows.size() && !f.queue.empty(); ++w) {
            if (f.windows[w]) continue;
            auto a = f.queue.front(); f.queue.pop_front();
            f.windows[w] = ActiveService{a, static_cast<SimTime>(now + f.config.service_minutes)};
            ++f.started;
            const auto wait = static_cast<SimTime>(now - a.at);
            f.wait_total += wait;
            out.push_back({a.request_id, a.student_id, id, w, now, wait});
        }
        f.max_queue = std::max(f.max_queue, f.queue.size());
    }
    phase_ = 0;
    return out;
}
std::vector<FacilitySnapshot> FacilityEngine::snapshots(SimTime now) const {
    require_initialized(); check_time(now);
    if (phase_ != 0 || current_tick_ != now)
        throw ValidationError("C: snapshots require completed tick");
    std::vector<FacilitySnapshot> out;
    for (const auto& [id, f] : facilities_) {
        std::size_t active = 0;
        for (const auto& slot : f.windows) if (slot) ++active;
        out.push_back({now, id, f.queue.size(), active, f.arrived, f.started,
                       f.completed, f.max_queue, f.wait_total, f.busy_total});
    }
    return out;
}
std::vector<FacilitySummary> FacilityEngine::summaries(SimTime now) const {
    require_initialized(); check_time(now);
    if (phase_ != 0 || current_tick_ != now)
        throw ValidationError("C: summaries require completed tick");
    std::vector<FacilitySummary> out;
    for (const auto& [id, f] : facilities_) {
        out.push_back({id, f.config.window_count, now, f.arrived, f.started,
                       f.completed, f.max_queue, f.wait_total, f.busy_total});
    }
    return out;
}
} // namespace campustwin
