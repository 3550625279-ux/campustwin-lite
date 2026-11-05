#include "campustwin/reporting.hpp"
#include <algorithm>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>

namespace campustwin {
namespace {
void csv_check(const std::string& s) {
    if (s.find_first_of(",\r\n\"") != std::string::npos)
        throw ValidationError("C: CSV field contains separator or quote");
}
const char* state_name(StudentState state) {
    switch (state) {
    case StudentState::AtLocation: return "AtLocation";
    case StudentState::Moving: return "Moving";
    case StudentState::Queued: return "Queued";
    case StudentState::InService: return "InService";
    case StudentState::Eating: return "Eating";
    case StudentState::Finished: return "Finished";
    case StudentState::WaitingSeat: return "WaitingSeat";
    case StudentState::InActivity: return "InActivity";
    }
    throw ValidationError("C: unknown student state");
}
const char* activity_name(ActivityKind kind) {
    switch (kind) {
    case ActivityKind::Class: return "class";
    case ActivityKind::Study: return "study";
    case ActivityKind::Rest: return "rest";
    case ActivityKind::Lunch: return "lunch";
    }
    throw ValidationError("C: unknown activity kind");
}
const char* policy_name(LunchPolicy policy) {
    switch (policy) {
    case LunchPolicy::Fixed: return "fixed";
    case LunchPolicy::Nearest: return "nearest";
    case LunchPolicy::Fastest: return "fastest";
    }
    throw ValidationError("C: unknown lunch policy");
}
void ensure(const std::ofstream& o, const std::string& name) {
    if (!o) throw std::runtime_error("C: failed writing " + name);
}
}
void CsvRunWriter::begin(const SimulationSettings& settings, const CampusScenario& scenario) {
    if (begun_) throw ValidationError("C: writer already started");
    if (settings.duration_minutes < 1) throw ValidationError("C: invalid duration");
    std::filesystem::create_directories(output_directory_);
    activity_output_ = activity_output_ || !scenario.seat_pools.empty();
    std::vector<const char*> names{"events.csv", "student_snapshots.csv", "facility_snapshots.csv", "summary.csv", "decisions.csv"};
    if (activity_output_) names.insert(names.end(), {"activities.csv", "seat_events.csv", "seat_snapshots.csv"});
    for (const auto* name : names) {
        if (std::filesystem::exists(output_directory_ / name))
            throw ValidationError(std::string("C: refusing to overwrite ") + name);
    }
    events_.open(output_directory_ / "events.csv", std::ios::binary);
    students_.open(output_directory_ / "student_snapshots.csv", std::ios::binary);
    facilities_.open(output_directory_ / "facility_snapshots.csv", std::ios::binary);
    summary_.open(output_directory_ / "summary.csv", std::ios::binary);
    decisions_.open(output_directory_ / "decisions.csv", std::ios::binary);
    ensure(events_, "events.csv"); ensure(students_, "student_snapshots.csv");
    ensure(facilities_, "facility_snapshots.csv"); ensure(summary_, "summary.csv"); ensure(decisions_, "decisions.csv");
    events_ << "at,type,request_id,student_id,facility_id,window_id,wait_minutes\n";
    students_ << "at,student_id,state,location_id,from,to,depart_at,arrive_at,distance_m\n";
    facilities_ << "at,facility_id,queue_length,in_service,arrived,started,completed,max_queue_length,total_wait_minutes,busy_window_minutes\n";
    summary_ << "facility_id,window_count,observation_minutes,arrived,started,completed,max_queue_length,total_wait_minutes,mean_wait_minutes,busy_window_minutes,utilization\n";
    decisions_ << "at,student_id,policy,facility_id,destination,observed_at,queue_length,distance_m,route_path,travel_minutes,estimated_wait_minutes,service_minutes,estimated_total_minutes,selected,reason\n";
    if (activity_output_) {
        activities_.open(output_directory_ / "activities.csv", std::ios::binary);
        seat_events_.open(output_directory_ / "seat_events.csv", std::ios::binary);
        seat_snapshots_.open(output_directory_ / "seat_snapshots.csv", std::ios::binary);
        ensure(activities_, "activities.csv"); ensure(seat_events_, "seat_events.csv"); ensure(seat_snapshots_, "seat_snapshots.csv");
        activities_ << "student_id,sequence,activity_id,kind,location_id,planned_start,duration_minutes,requires_seat,departed_at,arrived_at,started_at,completed_at,start_delay_minutes,seat_wait_minutes,distance_m,route_path,status\n";
        seat_events_ << "at,type,request_id,student_id,location_id,requested_at,duration_minutes,until,wait_minutes\n";
        seat_snapshots_ << "at,location_id,capacity,queued,occupied,arrived,granted,released,max_queue,total_wait_minutes,busy_seat_minutes\n";
    }
    activities_written_ = false; last_seat_tick_ = -1;
    duration_ = settings.duration_minutes; next_tick_ = 0; last_decision_tick_ = -1; begun_ = true;
}
void CsvRunWriter::write_tick(SimTime now,
    const std::vector<ServiceCompleted>& completed,
    const std::vector<StudentArrival>& arrivals,
    const std::vector<ServiceStarted>& started,
    const std::vector<StudentSnapshot>& students,
    const std::vector<FacilitySnapshot>& facilities) {
    if (!begun_ || now != next_tick_ || now > duration_)
        throw ValidationError("C: writer tick not consecutive or out of range");
    // Validate and assemble every row before appending any of this tick. A
    // rejected snapshot can then be corrected and retried without duplicate events.
    std::ostringstream event_rows, student_rows, facility_rows;
    event_rows.imbue(std::locale::classic());
    student_rows.imbue(std::locale::classic());
    facility_rows.imbue(std::locale::classic());
    student_rows << std::fixed << std::setprecision(6);
    auto ends = completed;
    std::sort(ends.begin(), ends.end(), [](const auto& a, const auto& b) {
        return std::tie(a.facility_id,a.window_id) < std::tie(b.facility_id,b.window_id);
    });
    for (const auto& e : ends) {
        if (e.at != now) throw ValidationError("C: completed event timestamp mismatch");
        csv_check(e.request_id); csv_check(e.student_id); csv_check(e.facility_id);
        event_rows << now << ",ServiceCompleted," << e.request_id << ',' << e.student_id
                << ',' << e.facility_id << ',' << e.window_id << ",\n";
    }
    auto incoming = arrivals;
    std::sort(incoming.begin(), incoming.end(), [](const auto& a, const auto& b) {
        return std::tie(a.student_id,a.request_id) < std::tie(b.student_id,b.request_id);
    });
    for (const auto& a : incoming) {
        if (a.at != now) throw ValidationError("C: arrival event timestamp mismatch");
        csv_check(a.request_id); csv_check(a.student_id); csv_check(a.facility_id);
        event_rows << now << ",StudentArrival," << a.request_id << ',' << a.student_id
                << ',' << a.facility_id << ",,\n";
    }
    auto starts = started;
    std::sort(starts.begin(), starts.end(), [](const auto& a, const auto& b) {
        return std::tie(a.facility_id,a.window_id) < std::tie(b.facility_id,b.window_id);
    });
    for (const auto& e : starts) {
        if (e.at != now) throw ValidationError("C: started event timestamp mismatch");
        csv_check(e.request_id); csv_check(e.student_id); csv_check(e.facility_id);
        event_rows << now << ",ServiceStarted," << e.request_id << ',' << e.student_id
                << ',' << e.facility_id << ',' << e.window_id << ',' << e.wait_minutes << '\n';
    }
    auto people = students;
    std::sort(people.begin(), people.end(), [](const auto& a, const auto& b){ return a.student_id < b.student_id; });
    for (const auto& s : people) {
        if (s.at != now) throw ValidationError("C: student snapshot timestamp mismatch");
        csv_check(s.student_id);
        student_rows << now << ',' << s.student_id << ',' << state_name(s.state) << ',';
        if (s.state == StudentState::Moving) {
            if (s.location_id || !s.move) throw ValidationError("C: invalid moving snapshot");
            const auto& m = *s.move;
            csv_check(m.from); csv_check(m.to);
            student_rows << ',' << m.from << ',' << m.to << ',' << m.depart_at << ',' << m.arrive_at << ',' << m.distance_m;
        } else {
            if (!s.location_id || s.move) throw ValidationError("C: invalid stationary snapshot");
            csv_check(*s.location_id);
            student_rows << *s.location_id << ",,,,,";
        }
        student_rows << '\n';
    }
    auto fs = facilities;
    std::sort(fs.begin(), fs.end(), [](const auto& a, const auto& b){return a.facility_id < b.facility_id;});
    for (const auto& f : fs) {
        if (f.at != now) throw ValidationError("C: facility snapshot timestamp mismatch");
        csv_check(f.facility_id);
        facility_rows << now << ',' << f.facility_id << ',' << f.queue_length << ',' << f.in_service << ','
                    << f.arrived << ',' << f.started << ',' << f.completed << ',' << f.max_queue_length
                    << ',' << f.total_wait_minutes << ',' << f.busy_window_minutes << '\n';
    }
    events_ << event_rows.str(); students_ << student_rows.str(); facilities_ << facility_rows.str();
    ensure(events_, "events.csv"); ensure(students_, "student_snapshots.csv");
    ensure(facilities_, "facility_snapshots.csv");
    ++next_tick_;
}
void CsvRunWriter::write_decisions(SimTime now, const std::vector<LunchDecision>& decisions) {
    if (!begun_ || now != next_tick_ - 1 || now <= last_decision_tick_)
        throw ValidationError("C: decision rows must follow their tick exactly once");
    std::ostringstream rows;
    rows.imbue(std::locale::classic());
    rows << std::fixed << std::setprecision(6);
    auto ordered = decisions;
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        return std::tie(a.student_id, a.facility_id) < std::tie(b.student_id, b.facility_id);
    });
    for (const auto& d : ordered) {
        if (d.at != now) throw ValidationError("C: decision timestamp mismatch");
        csv_check(d.student_id); csv_check(d.facility_id); csv_check(d.destination); csv_check(d.reason);
        rows << now << ',' << d.student_id << ',' << policy_name(d.policy) << ',' << d.facility_id
             << ',' << d.destination << ',' << d.observed_at << ',';
        if (d.queue_length) rows << *d.queue_length;
        rows << ',';
        if (d.distance_m) rows << *d.distance_m;
        rows << ',';
        for (std::size_t i = 0; i < d.path.size(); ++i) {
            csv_check(d.path[i]);
            if (i) rows << '|';
            rows << d.path[i];
        }
        rows << ',';
        if (d.travel_minutes) rows << *d.travel_minutes;
        rows << ',';
        if (d.estimated_wait_minutes) rows << *d.estimated_wait_minutes;
        rows << ',';
        if (d.service_minutes) rows << *d.service_minutes;
        rows << ',';
        if (d.estimated_total_minutes) rows << *d.estimated_total_minutes;
        rows << ',' << (d.selected ? 1 : 0) << ',' << d.reason << '\n';
    }
    decisions_ << rows.str();
    ensure(decisions_, "decisions.csv");
    last_decision_tick_ = now;
}
void CsvRunWriter::finish(const std::vector<FacilitySummary>& facilities) {
    if (!begun_ || next_tick_ != static_cast<std::int64_t>(duration_) + 1)
        throw ValidationError("C: writer must receive every tick including final tick");
    if (activity_output_ && (!activities_written_ || last_seat_tick_ != duration_))
        throw ValidationError("C: missing final activity or seat records");
    std::ostringstream summary_rows;
    summary_rows.imbue(std::locale::classic());
    summary_rows << std::fixed << std::setprecision(6);
    auto ordered = facilities;
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b){return a.facility_id < b.facility_id;});
    for (const auto& f : ordered) {
        csv_check(f.facility_id);
        const double mean = f.started ? double(f.total_wait_minutes) / f.started : 0.0;
        const double util = f.window_count && f.observation_minutes > 0 ?
            double(f.busy_window_minutes) / (double(f.window_count) * f.observation_minutes) : 0.0;
        summary_rows << f.facility_id << ',' << f.window_count << ',' << f.observation_minutes
                 << ',' << f.arrived << ',' << f.started << ',' << f.completed << ','
                 << f.max_queue_length << ',' << f.total_wait_minutes << ',' << mean
                 << ',' << f.busy_window_minutes << ',' << util << '\n';
    }
    summary_ << summary_rows.str();
    events_.flush(); students_.flush(); facilities_.flush(); summary_.flush(); decisions_.flush();
    ensure(events_, "events.csv"); ensure(students_, "student_snapshots.csv");
    ensure(facilities_, "facility_snapshots.csv"); ensure(summary_, "summary.csv"); ensure(decisions_, "decisions.csv");
    events_.close(); students_.close(); facilities_.close(); summary_.close(); decisions_.close();
    ensure(events_, "events.csv"); ensure(students_, "student_snapshots.csv");
    ensure(facilities_, "facility_snapshots.csv"); ensure(summary_, "summary.csv"); ensure(decisions_, "decisions.csv");
    if (activity_output_) {
        activities_.close(); seat_events_.close(); seat_snapshots_.close();
        ensure(activities_, "activities.csv"); ensure(seat_events_, "seat_events.csv"); ensure(seat_snapshots_, "seat_snapshots.csv");
    }
    begun_ = false;
}

void CsvRunWriter::write_seats(SimTime now, const std::vector<SeatRequest>& requests,
                              const std::vector<SeatGranted>& grants, const std::vector<SeatSnapshot>& seats) {
    if (!activity_output_) return;
    if (!begun_ || now != next_tick_ - 1 || now != last_seat_tick_ + 1)
        throw ValidationError("C: seat rows must follow every tick once");
    std::ostringstream events, rows;
    events.imbue(std::locale::classic()); rows.imbue(std::locale::classic());
    auto event = [&](const SeatRequest& r, const char* type, std::optional<SimTime> until) {
        csv_check(r.request_id); csv_check(r.student_id); csv_check(r.location_id);
        events << now << ',' << type << ',' << r.request_id << ',' << r.student_id << ',' << r.location_id
               << ',' << r.at << ',' << r.duration_minutes << ',';
        if (until) events << *until;
        events << ',';
        if (until) events << now - r.at;
        events << '\n';
    };
    for (const auto& r : requests) {
        if (r.at != now) throw ValidationError("C: seat request timestamp mismatch");
        event(r, "SeatRequested", {});
    }
    for (const auto& g : grants) {
        if (g.at != now) throw ValidationError("C: seat grant timestamp mismatch");
        event(g.request, "SeatGranted", g.until);
    }
    for (const auto& s : seats) {
        if (s.at != now) throw ValidationError("C: seat snapshot timestamp mismatch");
        csv_check(s.location_id);
        rows << now << ',' << s.location_id << ',' << s.capacity << ',' << s.queued << ',' << s.occupied << ','
             << s.arrived << ',' << s.granted << ',' << s.released << ',' << s.max_queue << ','
             << s.total_wait_minutes << ',' << s.busy_seat_minutes << '\n';
    }
    seat_events_ << events.str(); seat_snapshots_ << rows.str();
    ensure(seat_events_, "seat_events.csv"); ensure(seat_snapshots_, "seat_snapshots.csv");
    last_seat_tick_ = now;
}

void CsvRunWriter::write_activities(const std::vector<ActivityRecord>& records) {
    if (!activity_output_) {
        if (!records.empty()) throw ValidationError("C: activity output must be enabled for schedules");
        return;
    }
    if (!begun_ || next_tick_ != static_cast<std::int64_t>(duration_) + 1 || activities_written_)
        throw ValidationError("C: write activity records once after the final tick");
    std::map<StudentId, std::size_t> sequences;
    std::ostringstream rows;
    rows.imbue(std::locale::classic()); rows << std::fixed << std::setprecision(6);
    for (const auto& r : records) {
        csv_check(r.student_id); csv_check(r.activity.id); csv_check(r.destination);
        rows << r.student_id << ',' << ++sequences[r.student_id] << ',' << r.activity.id << ','
             << activity_name(r.activity.kind) << ',' << r.destination << ',' << r.activity.start_minute << ','
             << r.activity.duration_minutes << ',' << (r.activity.requires_seat ? 1 : 0) << ',';
        auto optional = [&](const std::optional<SimTime>& t) { if (t) rows << *t; rows << ','; };
        optional(r.departed_at); optional(r.arrived_at); optional(r.started_at); optional(r.completed_at);
        if (r.started_at) rows << *r.started_at - r.activity.start_minute;
        rows << ',' << r.seat_wait_minutes << ',' << r.distance_m << ',';
        for (std::size_t i = 0; i < r.path.size(); ++i) { csv_check(r.path[i]); if (i) rows << '|'; rows << r.path[i]; }
        rows << ',' << (r.completed_at ? "completed" : r.started_at ? "active" : r.departed_at ? "pending" : "planned") << '\n';
    }
    activities_ << rows.str(); ensure(activities_, "activities.csv"); activities_written_ = true;
}
} // namespace campustwin
