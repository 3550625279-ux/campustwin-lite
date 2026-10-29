#include "campustwin/reporting.hpp"
#include <algorithm>
#include <iomanip>
#include <locale>
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
    }
    throw ValidationError("C: unknown student state");
}
void ensure(const std::ofstream& o, const std::string& name) {
    if (!o) throw std::runtime_error("C: failed writing " + name);
}
}
void CsvRunWriter::begin(const SimulationSettings& settings, const CampusScenario&) {
    if (begun_) throw ValidationError("C: writer already started");
    if (settings.duration_minutes < 1) throw ValidationError("C: invalid duration");
    std::filesystem::create_directories(output_directory_);
    for (const auto* name : {"events.csv", "student_snapshots.csv", "facility_snapshots.csv", "summary.csv"}) {
        if (std::filesystem::exists(output_directory_ / name))
            throw ValidationError(std::string("C: refusing to overwrite ") + name);
    }
    events_.open(output_directory_ / "events.csv", std::ios::binary);
    students_.open(output_directory_ / "student_snapshots.csv", std::ios::binary);
    facilities_.open(output_directory_ / "facility_snapshots.csv", std::ios::binary);
    summary_.open(output_directory_ / "summary.csv", std::ios::binary);
    ensure(events_, "events.csv"); ensure(students_, "student_snapshots.csv");
    ensure(facilities_, "facility_snapshots.csv"); ensure(summary_, "summary.csv");
    events_ << "at,type,request_id,student_id,facility_id,window_id,wait_minutes\n";
    students_ << "at,student_id,state,location_id,from,to,depart_at,arrive_at,distance_m\n";
    facilities_ << "at,facility_id,queue_length,in_service,arrived,started,completed,max_queue_length,total_wait_minutes,busy_window_minutes\n";
    summary_ << "facility_id,window_count,observation_minutes,arrived,started,completed,max_queue_length,total_wait_minutes,mean_wait_minutes,busy_window_minutes,utilization\n";
    duration_ = settings.duration_minutes; next_tick_ = 0; begun_ = true;
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
void CsvRunWriter::finish(const std::vector<FacilitySummary>& facilities) {
    if (!begun_ || next_tick_ != static_cast<std::int64_t>(duration_) + 1)
        throw ValidationError("C: writer must receive every tick including final tick");
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
    events_.flush(); students_.flush(); facilities_.flush(); summary_.flush();
    ensure(events_, "events.csv"); ensure(students_, "student_snapshots.csv");
    ensure(facilities_, "facility_snapshots.csv"); ensure(summary_, "summary.csv");
    events_.close(); students_.close(); facilities_.close(); summary_.close();
    ensure(events_, "events.csv"); ensure(students_, "student_snapshots.csv");
    ensure(facilities_, "facility_snapshots.csv"); ensure(summary_, "summary.csv");
    begun_ = false;
}
} // namespace campustwin
