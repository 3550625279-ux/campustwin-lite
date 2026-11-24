#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#include <termios.h>
#endif

#include "campustwin/tui.hpp"
#include "run_cache.hpp"
#include "campustwin/environment.hpp"
#include "campustwin/reporting.hpp"
#include "campustwin/resources.hpp"
#include "campustwin/seats.hpp"
#include "campustwin/simulation.hpp"
#include "campustwin/students.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <thread>
#include <tuple>

namespace campustwin {
namespace {

constexpr SimTime flagship_dismissal = 210;  // 11:30, measured from 08:00.

namespace ansi {
constexpr const char* reset = "\x1b[0m";
constexpr const char* bold = "\x1b[1m";
constexpr const char* dim = "\x1b[2m";
constexpr const char* cyan = "\x1b[36m";
constexpr const char* bright_cyan = "\x1b[96m";
constexpr const char* green = "\x1b[32m";
constexpr const char* bright_green = "\x1b[92m";
constexpr const char* yellow = "\x1b[33m";
constexpr const char* bright_yellow = "\x1b[93m";
constexpr const char* red = "\x1b[31m";
constexpr const char* bright_red = "\x1b[91m";
constexpr const char* magenta = "\x1b[35m";
constexpr const char* blue = "\x1b[34m";
constexpr const char* bold_cyan = "\x1b[1;36m";
constexpr const char* bold_green = "\x1b[1;32m";
constexpr const char* bold_yellow = "\x1b[1;33m";
constexpr const char* bold_red = "\x1b[1;31m";
constexpr const char* bold_white = "\x1b[1;97m";
}  // namespace ansi

bool setup_console_utf8_and_vt(bool requested_color, const std::ostream& out) {
    const bool is_std_cout = (&out == &std::cout);
    if (std::getenv("NO_COLOR") != nullptr) {
        requested_color = false;
    }
#ifdef _WIN32
    if (is_std_cout) {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        if (!_isatty(_fileno(stdout))) {
            return false;
        }
        HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
        if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
            return false;
        }
        DWORD mode = 0;
        if (!GetConsoleMode(handle, &mode)) {
            return false;
        }
        if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) == 0) {
            if (!SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
                return false;
            }
        }
        return requested_color;
    }
    return false;
#else
    if (is_std_cout && isatty(fileno(stdout))) {
        return requested_color;
    }
    return false;
#endif
}

// Computes terminal column width ignoring ANSI CSI escape sequences; CJK/fullwidth = 2 cols, ASCII = 1 col.
std::size_t display_width(std::string_view text) {
    std::size_t cols = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == 0x1B && i + 1 < text.size() && text[i + 1] == '[') {
            i += 2;
            while (i < text.size()) {
                const auto ch = static_cast<unsigned char>(text[i++]);
                if (ch >= 0x40 && ch <= 0x7E) break;
            }
            continue;
        }
        if (c < 0x80) {
            if (c >= 0x20 && c != 0x7F) ++cols;
            ++i;
            continue;
        }
        std::uint32_t cp = 0;
        std::size_t extra = 0;
        if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            extra = 3;
        } else {
            ++i;
            ++cols;
            continue;
        }
        if (i + extra >= text.size()) {
            i = text.size();
            ++cols;
            break;
        }
        for (std::size_t j = 1; j <= extra; ++j) {
            cp = (cp << 6) | (static_cast<unsigned char>(text[i + j]) & 0x3F);
        }
        i += extra + 1;
        const bool wide =
            (cp >= 0x1100 && cp <= 0x115F) ||
            (cp >= 0x2E80 && cp <= 0xA4CF && cp != 0x303F) ||
            (cp >= 0xAC00 && cp <= 0xD7A3) ||
            (cp >= 0xF900 && cp <= 0xFAFF) ||
            (cp >= 0xFE10 && cp <= 0xFE6F) ||
            (cp >= 0xFF00 && cp <= 0xFF60) ||
            (cp >= 0xFFE0 && cp <= 0xFFE6) ||
            (cp >= 0x1F300 && cp <= 0x1FAFF) ||
            (cp >= 0x20000 && cp <= 0x3FFFD);
        cols += wide ? 2 : 1;
    }
    return cols;
}

std::string pad_right(const std::string& text, std::size_t width) {
    const auto w = display_width(text);
    if (w >= width) return text;
    return text + std::string(width - w, ' ');
}

std::string pad_left(const std::string& text, std::size_t width) {
    const auto w = display_width(text);
    if (w >= width) return text;
    return std::string(width - w, ' ') + text;
}

void print_wrapped_tokens(std::ostream& out, const std::string& prefix,
                          const std::string& cont_indent, const std::vector<std::string>& items,
                          const std::string& sep, std::size_t max_cols = 96) {
    out << prefix;
    std::size_t cur_w = display_width(prefix);
    const std::size_t indent_w = display_width(cont_indent);
    const std::size_t sep_w = display_width(sep);
    for (std::size_t i = 0; i < items.size(); ++i) {
        const std::size_t item_w = display_width(items[i]);
        if (i > 0) {
            if (cur_w + sep_w + item_w > max_cols) {
                out << '\n' << cont_indent;
                cur_w = indent_w;
            } else {
                out << sep;
                cur_w += sep_w;
            }
        }
        out << items[i];
        cur_w += item_w;
    }
    out << '\n';
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    std::size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) {
        ++start;
    }
    return s.substr(start);
}

std::string format_fixed(double value, int decimals = 1) {
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::fixed << std::setprecision(decimals) << value;
    return oss.str();
}

const char* kind_zh(LocationKind kind) {
    switch (kind) {
    case LocationKind::Teaching: return "教学楼";
    case LocationKind::Canteen: return "食堂";
    case LocationKind::Dormitory: return "宿舍";
    case LocationKind::Library: return "图书馆";
    case LocationKind::Sports: return "体育场";
    }
    return "未知";
}

const char* kind_en(LocationKind kind) {
    switch (kind) {
    case LocationKind::Teaching: return "Teaching";
    case LocationKind::Canteen: return "Canteen";
    case LocationKind::Dormitory: return "Dormitory";
    case LocationKind::Library: return "Library";
    case LocationKind::Sports: return "Sports";
    }
    return "Unknown";
}

const char* policy_text(LunchPolicy policy) {
    switch (policy) {
    case LunchPolicy::Fixed: return "fixed (固定食堂)";
    case LunchPolicy::Nearest: return "nearest (最短距离)";
    case LunchPolicy::Fastest: return "fastest (预计最快)";
    }
    return "unknown";
}

const char* activity_zh(ActivityKind kind) {
    switch (kind) {
    case ActivityKind::Class: return "课程";
    case ActivityKind::Study: return "自习";
    case ActivityKind::Rest: return "休息";
    case ActivityKind::Lunch: return "午餐";
    case ActivityKind::Sport: return "运动";
    }
    return "未知";
}

const char* state_zh(StudentState state) {
    switch (state) {
    case StudentState::AtLocation: return "原地等待";
    case StudentState::Moving: return "步行途中";
    case StudentState::Queued: return "窗口排队";
    case StudentState::InService: return "正在取餐";
    case StudentState::WaitingSeat: return "等候座位";
    case StudentState::Eating: return "正在就餐";
    case StudentState::InActivity: return "进行活动";
    case StudentState::Finished: return "全部完成";
    }
    return "未知";
}

constexpr std::pair<StudentState, const char*> ordered_student_states[] = {
    {StudentState::AtLocation, ansi::dim},
    {StudentState::Moving, ansi::bright_cyan},
    {StudentState::Queued, ansi::bright_yellow},
    {StudentState::InService, ansi::magenta},
    {StudentState::WaitingSeat, ansi::bright_red},
    {StudentState::Eating, ansi::bright_green},
    {StudentState::InActivity, ansi::cyan},
    {StudentState::Finished, ansi::green},
};

std::string make_bar(double ratio, std::size_t width, bool use_color, const char* fill_ansi = ansi::bright_cyan) {
    const double clamped = std::max(0.0, std::min(1.0, ratio));
    const auto filled = static_cast<std::size_t>(std::llround(clamped * static_cast<double>(width)));
    const std::string left(filled, '#');
    const std::string right(width - filled, '.');
    if (!use_color) {
        return "[" + left + right + "]";
    }
    return std::string("[") + fill_ansi + left + ansi::reset + ansi::dim + right + ansi::reset + "]";
}

std::string join_path(const std::vector<LocationId>& path, const char* sep = " -> ") {
    std::ostringstream oss;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (i > 0) oss << sep;
        oss << path[i];
    }
    return oss.str();
}

template <typename Int>
bool parse_int(const std::string& text, Int& out) {
    const auto s = trim(text);
    if (s.empty()) return false;
    const auto parsed = std::from_chars(s.data(), s.data() + s.size(), out);
    return parsed.ec == std::errc{} && parsed.ptr == s.data() + s.size();
}

}  // namespace

SimTime RecordedRun::peak_congestion_minute() const {
    SimTime best_tick = 0;
    std::size_t most_waiting = 0;
    for (const auto& frame : frames) {
        std::size_t waiting = 0;
        for (const auto& f : frame.facilities) waiting += f.queue_length;
        for (const auto& s : frame.seats) waiting += s.queued;
        if (waiting > most_waiting) {
            most_waiting = waiting;
            best_tick = frame.at;
        }
    }
    return best_tick;
}

std::size_t RecordedRun::finished_students_at(SimTime tick) const {
    if (tick < 0 || static_cast<std::size_t>(tick) >= frames.size()) return 0;
    return frames[static_cast<std::size_t>(tick)].finished_students;
}

namespace {
StudentSnapshot snapshot_at(const std::vector<StudentSnapshot>& history, SimTime tick) {
    const auto upper = std::upper_bound(history.begin(), history.end(), tick,
        [](SimTime at, const StudentSnapshot& snapshot) { return at < snapshot.at; });
    auto snapshot = *std::prev(upper);
    snapshot.at = tick;
    return snapshot;
}

bool same_state(const StudentSnapshot& a, const StudentSnapshot& b) {
    if (a.state != b.state || a.location_id != b.location_id ||
        a.lunch_facility != b.lunch_facility || a.occupied_seat != b.occupied_seat ||
        a.move.has_value() != b.move.has_value()) return false;
    if (!a.move) return true;
    const auto& x = *a.move;
    const auto& y = *b.move;
    return std::tie(x.from, x.to, x.depart_at, x.arrive_at, x.distance_m) ==
           std::tie(y.from, y.to, y.depart_at, y.arrive_at, y.distance_m);
}
}  // namespace

std::vector<StudentSnapshot> RecordedRun::students_at(SimTime tick) const {
    std::vector<StudentSnapshot> result;
    if (tick < 0 || static_cast<std::size_t>(tick) >= frames.size()) return result;
    result.reserve(student_history.size());
    for (const auto& history : student_history) result.push_back(snapshot_at(history, tick));
    return result;
}

std::optional<StudentSnapshot> RecordedRun::student_at(const StudentId& id, SimTime tick) const {
    if (tick < 0 || static_cast<std::size_t>(tick) >= frames.size()) return std::nullopt;
    const auto it = std::lower_bound(student_history.begin(), student_history.end(), id,
        [](const auto& history, const StudentId& sid) { return history.front().student_id < sid; });
    if (it == student_history.end() || it->front().student_id != id) return std::nullopt;
    return snapshot_at(*it, tick);
}

std::size_t RecordedRun::completed_activities_count() const {
    return static_cast<std::size_t>(std::count_if(activities.begin(), activities.end(), [](const ActivityRecord& r) {
        return r.completed_at.has_value();
    }));
}

std::size_t RecordedRun::late_classes_count() const {
    return static_cast<std::size_t>(std::count_if(activities.begin(), activities.end(), [](const ActivityRecord& r) {
        return r.activity.kind == ActivityKind::Class && r.started_at &&
               *r.started_at > r.activity.start_minute;
    }));
}

std::int64_t RecordedRun::total_class_delay_minutes() const {
    std::int64_t total = 0;
    for (const auto& r : activities) {
        if (r.activity.kind == ActivityKind::Class && r.started_at &&
            *r.started_at > r.activity.start_minute) {
            total += static_cast<std::int64_t>(*r.started_at) - r.activity.start_minute;
        }
    }
    return total;
}

std::int64_t RecordedRun::total_observed_seat_wait() const {
    std::int64_t total = 0;
    for (const auto& r : activities) {
        total += r.seat_wait_minutes;
    }
    if (activities.empty() && !frames.empty() && !frames.back().seats.empty()) {
        for (const auto& s : frames.back().seats) total += s.total_wait_minutes;
    }
    return total;
}

std::optional<SimTime> RecordedRun::all_finished_minute() const {
    if (population.empty()) return 0;
    for (const auto& frame : frames) {
        if (finished_students_at(frame.at) == population.size()) {
            return frame.at;
        }
    }
    return std::nullopt;
}

void InMemoryRunObserver::begin(const SimulationSettings& settings, const CampusScenario& scenario) {
    if (active_) throw ValidationError("InMemoryRunObserver: run already active");
    run_.settings = settings;
    run_.scenario = scenario;
    run_.frames.clear();
    run_.student_history.clear();
    run_.frames.reserve(static_cast<std::size_t>(settings.duration_minutes) + 1);
    run_.activities.clear();
    run_.summaries.clear();
    active_ = true;
}

void InMemoryRunObserver::write_tick(SimTime now,
                                     const std::vector<ServiceCompleted>& completed,
                                     const std::vector<StudentArrival>& arrivals,
                                     const std::vector<ServiceStarted>& started,
                                     const std::vector<StudentSnapshot>& students,
                                     const std::vector<FacilitySnapshot>& facilities) {
    if (!active_ || static_cast<std::size_t>(now) != run_.frames.size()) {
        throw ValidationError("InMemoryRunObserver: out-of-order tick");
    }
    TickFrame frame;
    frame.at = now;
    frame.completed = completed;
    frame.arrivals = arrivals;
    frame.started = started;
    if (now == 0) run_.student_history.resize(students.size());
    if (students.size() != run_.student_history.size())
        throw ValidationError("InMemoryRunObserver: student count changed");
    for (std::size_t i = 0; i < students.size(); ++i) {
        const auto& student = students[i];
        auto& history = run_.student_history[i];
        if ((i > 0 && students[i - 1].student_id >= student.student_id) ||
            (!history.empty() && history.back().student_id != student.student_id))
            throw ValidationError("InMemoryRunObserver: unexpected student order");
        if (history.empty() || !same_state(history.back(), student)) history.push_back(student);
        if (student.state == StudentState::Finished) ++frame.finished_students;
    }
    frame.facilities = facilities;
    run_.frames.push_back(std::move(frame));
}

void InMemoryRunObserver::write_decisions(SimTime now, const std::vector<LunchDecision>& decisions) {
    if (!active_ || run_.frames.empty() || run_.frames.back().at != now) {
        throw ValidationError("InMemoryRunObserver: decision tick mismatch");
    }
    run_.frames.back().decisions = decisions;
}

void InMemoryRunObserver::write_seats(SimTime now,
                                      const std::vector<SeatRequest>& requests,
                                      const std::vector<SeatGranted>& grants,
                                      const std::vector<SeatSnapshot>& seats) {
    if (!active_ || run_.frames.empty() || run_.frames.back().at != now) {
        throw ValidationError("InMemoryRunObserver: seat tick mismatch");
    }
    run_.frames.back().seat_requests = requests;
    run_.frames.back().seat_grants = grants;
    run_.frames.back().seats = seats;
}

void InMemoryRunObserver::write_activities(const std::vector<ActivityRecord>& records) {
    if (!active_) throw ValidationError("InMemoryRunObserver: not active");
    run_.activities = records;
}

void InMemoryRunObserver::finish(const std::vector<FacilitySummary>& facilities) {
    if (!active_) throw ValidationError("InMemoryRunObserver: not active");
    run_.summaries = facilities;
    active_ = false;
}

void InMemoryRunObserver::set_population(std::vector<StudentSpec> population) {
    run_.population = std::move(population);
}

void CompositeRunWriter::add_writer(IRunWriter& writer) {
    writers_.push_back(&writer);
}

void CompositeRunWriter::begin(const SimulationSettings& settings, const CampusScenario& scenario) {
    for (auto* w : writers_) w->begin(settings, scenario);
}

void CompositeRunWriter::write_tick(SimTime now,
                                    const std::vector<ServiceCompleted>& completed,
                                    const std::vector<StudentArrival>& arrivals,
                                    const std::vector<ServiceStarted>& started,
                                    const std::vector<StudentSnapshot>& students,
                                    const std::vector<FacilitySnapshot>& facilities) {
    for (auto* w : writers_) w->write_tick(now, completed, arrivals, started, students, facilities);
}

void CompositeRunWriter::write_decisions(SimTime now, const std::vector<LunchDecision>& decisions) {
    for (auto* w : writers_) w->write_decisions(now, decisions);
}

void CompositeRunWriter::write_seats(SimTime now,
                                     const std::vector<SeatRequest>& requests,
                                     const std::vector<SeatGranted>& grants,
                                     const std::vector<SeatSnapshot>& seats) {
    for (auto* w : writers_) w->write_seats(now, requests, grants, seats);
}

void CompositeRunWriter::write_activities(const std::vector<ActivityRecord>& records) {
    for (auto* w : writers_) w->write_activities(records);
}

void CompositeRunWriter::finish(const std::vector<FacilitySummary>& facilities) {
    for (auto* w : writers_) w->finish(facilities);
}

std::filesystem::path detect_data_root(const std::filesystem::path& executable_path) {
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;
    std::error_code ec;
    const auto cwd = fs::current_path(ec);
    if (!ec && !cwd.empty()) {
        candidates.push_back(cwd);
        candidates.push_back(cwd.parent_path());
        candidates.push_back(cwd.parent_path().parent_path());
    }
    if (!executable_path.empty()) {
        const auto exe_abs = fs::absolute(executable_path, ec);
        if (!ec) {
            const auto dir = exe_abs.parent_path();
            candidates.push_back(dir);
            candidates.push_back(dir.parent_path());
            candidates.push_back(dir.parent_path().parent_path());
        }
    }
    for (const auto& candidate : candidates) {
        if (candidate.empty()) continue;
        if (fs::exists(candidate / "data/scenarios/campus_flagship/locations.csv", ec) && !ec) {
            return candidate;
        }
    }
    return cwd.empty() ? fs::path(".") : cwd;
}

ConsoleWorkbench::ConsoleWorkbench(WorkbenchOptions options, std::istream& in, std::ostream& out)
    : options_(std::move(options)),
      in_(in),
      out_(out),
      color_(setup_console_utf8_and_vt(options_.enable_color, out)),
      root_(options_.data_root.empty() ? detect_data_root() : options_.data_root),
      account_file_(options_.accounts_file ? *options_.accounts_file : (root_ / "data/private/accounts.db")),
      accounts_(account_file_) {}

void ConsoleWorkbench::load_initial_scenario() {
    cached_run_.reset();
    lunch_plan_.reset();
    scenario_dirty_ = false;
    playback_speed_ = 1;
    if (options_.scenario_dir || options_.students_file || options_.schedule_file) {
        active_preset_title_ = "自定义命令行参数配置";
        scenario_path_ = options_.scenario_dir ? *options_.scenario_dir : (root_ / "data/scenarios/campus_flagship");
        students_path_ = options_.students_file ? *options_.students_file : (root_ / "data/students/campus_flagship.csv");
        schedule_path_ = options_.schedule_file;
        settings_.duration_minutes = options_.duration_minutes.value_or(schedule_path_ ? 160 : 120);
        policy_override_ = options_.policy_override;
        environment_.load_directory(scenario_path_);
        reload_population();
    } else {
        load_flagship(LunchPlan::Staggered40);
    }
}

std::string ConsoleWorkbench::style(const char* ansi_code, const std::string& text) const {
    if (!color_) return text;
    return std::string(ansi_code) + text + ansi::reset;
}

const char* ConsoleWorkbench::lunch_plan_name(LunchPlan plan) {
    switch (plan) {
    case LunchPlan::Concentrated: return "11:30 集中 (0 分钟)";
    case LunchPlan::Staggered20: return "20 分钟错峰";
    case LunchPlan::Staggered40: return "40 分钟错峰";
    case LunchPlan::Staggered60: return "60 分钟错峰";
    }
    throw ValidationError("未知午餐方案");
}

void ConsoleWorkbench::apply_lunch_plan(std::vector<StudentSpec>& population, LunchPlan plan) {
    std::sort(population.begin(), population.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    // Flagship IDs have a fixed width, so text order is also numeric order.
    for (std::size_t rank = 0; rank < population.size(); ++rank) {
        const auto offset = rank * static_cast<std::size_t>(plan) / population.size();
        for (auto& activity : population[rank].activities) {
            if (activity.kind == ActivityKind::Lunch) activity.start_minute = flagship_dismissal + static_cast<SimTime>(offset);
        }
    }
}

bool ConsoleWorkbench::choose_flagship() {
    out_ << "\n选择旗舰校园午餐实验 · 9000 人 · 08:00–18:00\n"
         << "  [1] 0 分钟 · 11:30 集中出发\n"
         << "  [2] 20 分钟错峰（11:30–11:49）\n"
         << "  [3] 40 分钟错峰（11:30–12:09，默认）\n"
         << "  [4] 60 分钟错峰（11:30–12:29）\n"
         << "  [0] 返回主菜单\n";

    std::string input;
    while (prompt_line("午餐方案 [1–4]> ", input)) {
        if (input == "0") return false;
        constexpr LunchPlan plans[] = {LunchPlan::Concentrated, LunchPlan::Staggered20,
                                      LunchPlan::Staggered40, LunchPlan::Staggered60};
        int selection = 3;
        if ((input.empty() || parse_int(input, selection)) && selection >= 1 && selection <= 4) {
            load_flagship(plans[selection - 1]);
            return true;
        }
        out_ << "请输入 1、2、3、4，或 0。\n";
    }
    return false;
}

void ConsoleWorkbench::load_flagship(LunchPlan plan) {
    const auto scene = root_ / "data/scenarios/campus_flagship";
    const auto people = root_ / "data/students/campus_flagship.csv";
    const auto schedule = root_ / "data/schedules/campus_flagship.csv";
    CampusEnvironment next;
    next.load_directory(scene);
    auto population = read_scheduled_students(people, schedule);
    apply_lunch_plan(population, plan);
    if (options_.policy_override) {
        for (auto& student : population) student.lunch_policy = *options_.policy_override;
    }
    StudentEngine validator;
    validator.reset(next.scenario(), population);

    environment_ = std::move(next);
    population_ = std::move(population);
    scenario_path_ = scene;
    students_path_ = people;
    schedule_path_ = schedule;
    settings_.duration_minutes = options_.duration_minutes.value_or(600);
    policy_override_ = options_.policy_override;
    lunch_plan_ = plan;
    scenario_dirty_ = false;
    active_preset_title_ = std::string("旗舰校园 · ") + lunch_plan_name(plan);
    cached_run_.reset();
}

void ConsoleWorkbench::reload_population() {
    auto loaded = read_scheduled_students(students_path_, schedule_path_);
    if (lunch_plan_) apply_lunch_plan(loaded, *lunch_plan_);
    if (policy_override_) {
        for (auto& s : loaded) s.lunch_policy = *policy_override_;
    }
    // Validate compatibility against current scenario early via a dry reset on StudentEngine.
    StudentEngine validator;
    validator.reset(environment_.scenario(), loaded);
    population_ = std::move(loaded);
    cached_run_.reset();
}

std::filesystem::path ConsoleWorkbench::user_directory() const {
    // A reversible encoding keeps case-sensitive account names distinct on Windows.
    constexpr char hex[] = "0123456789abcdef";
    std::string folder = "u-";
    for (unsigned char c : current_user_) {
        folder.push_back(hex[c >> 4]);
        folder.push_back(hex[c & 15]);
    }
    auto account_root = account_file_;
    account_root += ".users";
    return account_root / folder;
}

RecordedRun ConsoleWorkbench::load_or_simulate(
    const CampusEnvironment& env, const std::vector<StudentSpec>& population, SimTime duration) {
    const auto key = make_run_cache_key(user_directory() / "runs", env.scenario(), population, duration);
    if (auto saved = load_run_cache(key, env.scenario(), population, duration)) {
        out_ << "已读取本地仿真结果。\n";
        return std::move(*saved);
    }
    out_ << "正在计算仿真，共 " << population.size() << " 名学生…\n" << std::flush;
    StudentEngine students;
    FacilityEngine facilities;
    SeatEngine seats;
    InMemoryRunObserver observer;
    observer.set_population(population);
    SimulationRunner{}.run({duration}, env, population, students, facilities, observer, &seats);
    auto run = observer.take_run();
    try {
        save_run_cache(key, run);
    } catch (const std::exception& error) {
        out_ << "本地结果保存失败：" << error.what() << '\n';
    }
    return run;
}

void ConsoleWorkbench::ensure_run_cached() {
    if (!cached_run_)
        cached_run_ = load_or_simulate(environment_, population_, settings_.duration_minutes);
}

void ConsoleWorkbench::print_banner() const {
    out_ << '\n'
         << style(ansi::bold_cyan, "================================================================================") << '\n'
         << "  " << style(ansi::bold_white, "CampusTwin Lite") << "  "
         << style(ansi::bright_cyan, "校园出行、活动与服务数字孪生交互平台 (C++17 CLI Workbench)") << '\n'
         << "  " << style(ansi::dim, "架构: Environment(A) + StudentEngine(B) + Facility/Seat(C) + SimulationRunner") << '\n'
         << style(ansi::bold_cyan, "================================================================================") << '\n';
}

void ConsoleWorkbench::print_status_card() const {
    const auto& sc = environment_.scenario();
    std::size_t total_windows = 0;
    for (const auto& f : sc.facilities) total_windows += f.window_count;
    std::size_t total_activities = 0;
    for (const auto& s : population_) total_activities += s.activities.size();

    const auto user_badge = style(ansi::bright_cyan, "[当前账号 " + current_user_ + "]");
    out_ << "\n" << style(ansi::bold, "▸ 当前工作台状态") << "  " << user_badge << '\n';
    out_ << "  当前方案 : " << style(ansi::bold_white, active_preset_title_)
         << (scenario_dirty_ ? style(ansi::bold_yellow, "  [沙盒已修改·未保存*]") : "") << '\n';
    out_ << "  校园拓扑 : " << sc.locations.size() << " 个地点 | "
         << sc.roads.size() << " 条双向道路 | "
         << sc.facilities.size() << " 座食堂 (" << total_windows << " 个窗口) | "
         << sc.seat_pools.size() << " 处有限座位池\n";
    out_ << "  学生配置 : " << population_.size() << " 名学生 | "
         << std::to_string(total_activities) << " 项日程"
         << " | 时长 " << settings_.duration_minutes << " 分钟"
         << " | 回放 " << playback_speed_ << " 倍 | 策略: " << (policy_override_ ? policy_text(*policy_override_) : "按学生CSV配置") << '\n';
    if (cached_run_) {
        const auto done = cached_run_->finished_students_at(cached_run_->settings.duration_minutes);
        out_ << "  仿真缓存 : " << style(ansi::bright_green, "已就绪")
             << " (完成学生 " << done << "/" << cached_run_->population.size();
        if (!cached_run_->activities.empty()) {
            out_ << ", 完成活动 " << cached_run_->completed_activities_count() << "/" << cached_run_->activities.size()
                 << ", 峰值拥堵 t=" << cached_run_->peak_congestion_minute() << "m";
        }
        out_ << ")\n";
    } else {
        out_ << "  仿真缓存 : " << style(ansi::dim, "待运行（进入仿真或学生档案时计算）") << '\n';
    }
}

bool ConsoleWorkbench::prompt_line(const std::string& label, std::string& line) {
    out_ << style(ansi::bold_cyan, label) << std::flush;
    if (!std::getline(in_, line)) {
        out_ << '\n';
        return false;
    }
    line = trim(line);
    return true;
}

bool ConsoleWorkbench::prompt_password(std::string& password) {
    out_ << "密码> " << std::flush;
#ifdef _WIN32
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    const bool hidden = &in_ == &std::cin && GetConsoleMode(input, &mode) &&
                        SetConsoleMode(input, mode & ~ENABLE_ECHO_INPUT);
#else
    termios mode{};
    bool hidden = false;
    if (&in_ == &std::cin && tcgetattr(STDIN_FILENO, &mode) == 0) {
        auto changed = mode;
        changed.c_lflag &= ~ECHO;
        hidden = tcsetattr(STDIN_FILENO, TCSANOW, &changed) == 0;
    }
#endif
    const bool read = static_cast<bool>(std::getline(in_, password));
#ifdef _WIN32
    if (hidden) SetConsoleMode(input, mode);
#else
    if (hidden) tcsetattr(STDIN_FILENO, TCSANOW, &mode);
#endif
    out_ << '\n';
    return read;
}

bool ConsoleWorkbench::authenticate() {
    while (true) {
        out_ << "\nCampusTwin Lite · 本地账号\n"
             << "  [1] 登录\n  [2] 注册\n  [0] 关闭程序\n";
        std::string choice;
        if (!prompt_line("选择> ", choice) || choice == "0") return false;
        if (choice != "1" && choice != "2") {
            out_ << "请输入 1、2 或 0。\n";
            continue;
        }
        std::string user, password;
        if (choice == "2") out_ << "用户名 3–32 位字母、数字或下划线，密码至少 8 位。\n";
        if (!prompt_line("用户名> ", user) || !prompt_password(password)) return false;
        try {
            if (choice == "2") accounts_.register_user(user, password);
            else if (!accounts_.login(user, password)) {
                out_ << "用户名或密码错误。\n";
                continue;
            }
            current_user_ = std::move(user);
            out_ << (choice == "2" ? "注册成功，已登录 " : "已登录 ") << current_user_ << "。\n";
            return true;
        } catch (const std::exception& error) {
            out_ << "账号操作失败：" << error.what() << '\n';
        }
    }
}

int ConsoleWorkbench::run() {
    while (authenticate()) {
        load_initial_scenario();
        print_banner();
        if (!run_session()) break;

        cached_run_.reset();
        current_user_.clear();
        out_ << "已退出登录。下次登录该账号可继续使用本地结果。\n";
    }
    return 0;
}

bool ConsoleWorkbench::run_session() {
    while (true) {
        print_status_card();
        out_ << "\n" << style(ansi::bold, "▸ 主功能菜单 (输入编号回车)") << '\n'
             << "  " << style(ansi::bold_cyan, "[1]") << " 场景与回放设置        "
             << style(ansi::dim, "(旗舰校园 / 自定义场景)") << '\n'
             << "  " << style(ansi::bold_cyan, "[2]") << " 运行仿真 · 实时逐分钟动态看板      "
             << style(ansi::dim, "(动画播放 / 峰值跳转 / 窗口与座位队列监控)") << '\n'
             << "  " << style(ansi::bold_cyan, "[3]") << " 学生个体轨迹 · 选食堂决策与日程透视 "
             << style(ansi::dim, "(单人全天甘特时间轴 / 候选食堂打分表)") << '\n'
             << "  " << style(ansi::bold_cyan, "[4]") << " 午餐实验                           "
             << style(ansi::dim, "(单方案回放 / 总结果对照)") << '\n'
             << "  " << style(ansi::bold_cyan, "[5]") << " 校园路网拓扑与 Dijkstra 最短路导航  "
             << style(ansi::dim, "(地点设施总览 / 任意两点路径与距离矩阵)") << '\n'
             << "  " << style(ansi::bold_cyan, "[6]") << " 校园场景沙盒在线编辑器             "
             << style(ansi::dim, "(修改并保存个人场景)") << '\n'
             << "  " << style(ansi::bold_cyan, "[7]") << " 退出登录，返回登录/注册页" << '\n'
             << "  " << style(ansi::bold_yellow, "[0]") << " 关闭程序 (quit)\n\n";

        std::string choice;
        if (!prompt_line("campustwin> ", choice)) {
            break;
        }
        if (choice.empty()) continue;
        try {
            if (choice == "7" || choice == "0" || choice == "q" || choice == "quit" || choice == "exit") {
                if (scenario_dirty_) {
                    out_ << style(ansi::yellow, "当前场景有未保存的修改，可在 [6] 保存。\n");
                    std::string confirm;
                    const auto label = choice == "7"
                        ? "放弃修改并退出登录？(y/N)> " : "放弃修改并关闭程序？(y/N)> ";
                    if (!prompt_line(label, confirm)) break;
                    if (confirm != "y" && confirm != "Y" && confirm != "quit") continue;
                }
                if (choice == "7") return true;
                out_ << style(ansi::bright_green, "已退出 CampusTwin Lite 交互工作台。\n");
                break;
            }
            if (choice == "1") menu_scenarios();
            else if (choice == "2") menu_simulation_playback();
            else if (choice == "3") menu_student_inspector();
            else if (choice == "4") menu_lunch_timing_experiment();
            else if (choice == "5") menu_map_and_routes();
            else if (choice == "6") menu_scenario_editor();
            else {
                out_ << style(ansi::red, "未知菜单编号: " + choice + "，请选择上面的功能。\n");
            }
        } catch (const std::exception& ex) {
            out_ << style(ansi::bold_red, std::string("操作异常拦截: ") + ex.what()) << '\n';
        }
    }
    return false;
}

void ConsoleWorkbench::menu_scenarios() {
    out_ << "\n" << style(ansi::bold_cyan, "─── 校园场景与参数 ───") << '\n'
         << "  [1] 恢复旗舰默认场景\n"
         << "  [2] 自定义场景 · 选择文件夹中的 CSV\n"
         << "  [3] 仿真时长 / 选食堂策略 / 回放速度\n"
         << "  [0] 返回主菜单\n";

    std::string input;
    if (!prompt_line("选择 [0–3]> ", input) || input.empty() || input == "0") return;
    if (input == "1") {
        load_flagship(LunchPlan::Staggered40);
        out_ << style(ansi::bright_green, "已加载 " + active_preset_title_ + "。\n");
        return;
    }
    if (input == "3") {
        std::string dur_str;
        if (!prompt_line("输入仿真时长(分钟，直接回车保持 " + std::to_string(settings_.duration_minutes) + ")> ", dur_str)) return;
        if (!dur_str.empty()) {
            SimTime d = 0;
            if (!parse_int(dur_str, d) || d < 1) throw ValidationError("仿真时长必须为正整数");
            settings_.duration_minutes = d;
            cached_run_.reset();
        }
        std::string pol_str;
        if (!prompt_line("全局策略覆盖 (default / fixed / nearest / fastest，回车保持当前)> ", pol_str)) return;
        if (!pol_str.empty()) {
            if (pol_str == "default") policy_override_.reset();
            else if (pol_str == "fixed") policy_override_ = LunchPolicy::Fixed;
            else if (pol_str == "nearest") policy_override_ = LunchPolicy::Nearest;
            else if (pol_str == "fastest") policy_override_ = LunchPolicy::Fastest;
            else throw ValidationError("策略必须为 default, fixed, nearest 或 fastest");
            reload_population();
        }
        std::string speed;
        if (!prompt_line("回放倍率 1–120（回车保持 " + std::to_string(playback_speed_) + " 倍）> ", speed)) return;
        if (!speed.empty()) {
            int rate = 0;
            if (!parse_int(speed, rate) || rate < 1 || rate > 120)
                throw ValidationError("回放倍率须为 1–120 的整数");
            playback_speed_ = rate;
        }
        out_ << style(ansi::bright_green, "✓ 运行参数已更新。\n");
        return;
    }
    if (input == "2") {
        namespace fs = std::filesystem;
        const auto import_dir = fs::absolute(user_directory() / "imports");
        fs::create_directories(import_dir);
        std::vector<fs::path> scenes;
        for (const auto& entry : fs::directory_iterator(import_dir)) {
            if (!entry.is_directory()) continue;
            const auto& dir = entry.path();
            if (fs::is_regular_file(dir / "locations.csv") && fs::is_regular_file(dir / "roads.csv") &&
                fs::is_regular_file(dir / "facilities.csv") && fs::is_regular_file(dir / "students.csv") &&
                fs::is_regular_file(dir / "schedule.csv")) {
                scenes.push_back(dir);
            }
        }
        std::sort(scenes.begin(), scenes.end());

        out_ << "\n─── 选择自定义场景 ───\n"
             << "  个人导入目录：" << import_dir.u8string() << '\n';
        if (scenes.empty()) {
            out_ << "  暂无可选场景。请把一套 CSV 放入个人导入目录下的一个子文件夹。\n"
                 << "  所需文件：locations.csv、roads.csv、facilities.csv、students.csv、schedule.csv\n";
        }
        for (std::size_t i = 0; i < scenes.size(); ++i) {
            out_ << "  [" << i + 1 << "] " << scenes[i].filename().u8string() << '\n';
        }
        out_ << "  [0] 返回主菜单\n";
        std::size_t selection = 0;
        while (true) {
            if (!prompt_line("选择场景编号> ", input) || input.empty() || input == "0") return;
            if (parse_int(input, selection) && selection > 0 && selection <= scenes.size()) break;
            out_ << "请输入列表中的场景编号，或输入 0 返回。\n";
        }

        const auto& resolved_sc = scenes[selection - 1];
        const auto resolved_st = resolved_sc / "students.csv";
        const auto resolved_sch = resolved_sc / "schedule.csv";
        CampusEnvironment next_env;
        next_env.load_directory(resolved_sc);
        auto next_pop = read_scheduled_students(resolved_st, resolved_sch);
        StudentEngine check;
        check.reset(next_env.scenario(), next_pop);
        environment_ = std::move(next_env);
        scenario_path_ = resolved_sc;
        students_path_ = resolved_st;
        schedule_path_ = resolved_sch;
        lunch_plan_.reset();
        population_ = std::move(next_pop);
        policy_override_.reset();
        scenario_dirty_ = false;
        active_preset_title_ = "自定义场景 (" + resolved_sc.filename().u8string() + ")";
        cached_run_.reset();
        out_ << style(ansi::bright_green, "已加载 " + active_preset_title_ + "。\n");
    }
}

void ConsoleWorkbench::print_run_summary(const RecordedRun& run) const {
    out_ << "\n" << style(ansi::bold_cyan, "─── 仿真全局汇总报告 (0 .. " + std::to_string(run.settings.duration_minutes) + " 分钟) ───") << '\n';
    const auto finished = run.finished_students_at(run.settings.duration_minutes);
    const auto all_done_at = run.all_finished_minute();
    out_ << "  总学生数: " << style(ansi::bold_white, std::to_string(run.population.size()))
         << " | 截止完成人数: " << style(finished == run.population.size() ? ansi::bold_green : ansi::bold_yellow,
                                          std::to_string(finished) + "/" + std::to_string(run.population.size()))
         << " | 全部结束时刻: " << (all_done_at ? ("第 " + std::to_string(*all_done_at) + " 分钟") : "截止前未全员结束")
         << " | 拥堵峰值时刻: " << style(ansi::bold_yellow, "t=" + std::to_string(run.peak_congestion_minute()) + "m") << '\n';

    if (!run.activities.empty()) {
        const auto missed = std::count_if(run.activities.begin(), run.activities.end(),
                                          [](const auto& activity) { return activity.missed_at.has_value(); });
        out_ << "  日程统计: 已完成活动 " << run.completed_activities_count() << "/" << run.activities.size()
             << " | 迟到课程数: " << style(ansi::yellow, std::to_string(run.late_classes_count()))
             << " | 缺课 " << missed
             << " | 课程总延迟: " << run.total_class_delay_minutes() << " 分钟"
             << " | 累计等座总时长: " << run.total_observed_seat_wait() << " 分钟\n";
    }

    out_ << "\n  " << style(ansi::bold, "[食堂窗口服务统计]") << '\n';
    out_ << "  " << pad_right("食堂ID", 12) << pad_left("窗口数", 8) << pad_left("到达", 8)
         << pad_left("开始", 8) << pad_left("完成", 8) << pad_left("峰值队列", 10)
         << pad_left("总等待(m)", 11) << pad_left("平均等待", 10) << "  窗口利用率\n";
    out_ << "  " << std::string(88, '-') << '\n';
    for (const auto& f : run.summaries) {
        const double mean_wait = f.started ? static_cast<double>(f.total_wait_minutes) / static_cast<double>(f.started) : 0.0;
        const double util = (f.window_count > 0 && f.observation_minutes > 0)
            ? static_cast<double>(f.busy_window_minutes) / (static_cast<double>(f.window_count) * f.observation_minutes)
            : 0.0;
        out_ << "  " << pad_right(f.facility_id, 12)
             << pad_left(std::to_string(f.window_count), 8)
             << pad_left(std::to_string(f.arrived), 8)
             << pad_left(std::to_string(f.started), 8)
             << pad_left(std::to_string(f.completed), 8)
             << pad_left(std::to_string(f.max_queue_length), 10)
             << pad_left(std::to_string(f.total_wait_minutes), 11)
             << pad_left(format_fixed(mean_wait, 2) + "m", 10)
             << "  " << make_bar(util, 10, color_, ansi::bright_green) << " " << format_fixed(util * 100.0, 1) << "%\n";
    }

    if (!run.frames.empty() && !run.frames.back().seats.empty()) {
        out_ << "\n  " << style(ansi::bold, "[有限座位池(食堂/图书馆)统计]") << '\n';
        out_ << "  " << pad_right("地点ID", 14) << pad_left("座位数", 8) << pad_left("累计请求", 10)
             << pad_left("已入座", 8) << pad_left("峰值等座", 10) << pad_left("入座者等待", 12) << "  座位利用率\n";
        out_ << "  " << std::string(84, '-') << '\n';
        for (const auto& s : run.frames.back().seats) {
            const double util = (s.capacity > 0 && run.settings.duration_minutes > 0)
                ? static_cast<double>(s.busy_seat_minutes) / (static_cast<double>(s.capacity) * run.settings.duration_minutes)
                : 0.0;
            out_ << "  " << pad_right(s.location_id, 14)
                 << pad_left(std::to_string(s.capacity), 8)
                 << pad_left(std::to_string(s.arrived), 10)
                 << pad_left(std::to_string(s.granted), 8)
                 << pad_left(std::to_string(s.max_queue), 10)
                 << pad_left(std::to_string(s.total_wait_minutes) + "m", 12)
                 << "  " << make_bar(util, 10, color_, ansi::bright_cyan) << " " << format_fixed(util * 100.0, 1) << "%\n";
        }
    }
}

void ConsoleWorkbench::render_tick_dashboard(const RecordedRun& run, SimTime tick, PlaybackView view,
                                             const StudentId& selected_student) const {
    if (tick < 0 || static_cast<std::size_t>(tick) >= run.frames.size()) return;
    const auto& frame = run.frames[static_cast<std::size_t>(tick)];
    const auto students = run.students_at(tick);
    out_ << '\n' << style(ansi::bold_white, "实时仿真态势看板") << "  "
         << style(ansi::bold_yellow, "第 " + std::to_string(tick) + " / " +
                                    std::to_string(run.settings.duration_minutes) + " 分钟")
         << "  ·  " << students.size() << " 人\n";
    if (active_preset_title_.rfind("旗舰校园", 0) == 0) {
        const auto minute = 480 + static_cast<std::int64_t>(tick);
        out_ << "  校园时间 " << std::setw(2) << std::setfill('0') << (minute / 60) % 24
             << ':' << std::setw(2) << minute % 60 << std::setfill(' ') << '\n';
    }
    render_tick_overview(run, frame, students);
    const auto event_details = render_tick_event_summary(run, frame, students, view == PlaybackView::Scene);
    if (view != PlaybackView::Overview) {
        render_tick_details(run, frame, students, view, selected_student);
    }
    if (!event_details.empty()) {
        out_ << "\n  " << style(ansi::bold, "本分钟事件明细") << '\n';
        for (const auto& event : event_details) out_ << "    " << event << '\n';
    }
}

void ConsoleWorkbench::render_tick_overview(const RecordedRun& run, const TickFrame& frame,
                                             const std::vector<StudentSnapshot>& students) const {
    std::map<StudentState, std::size_t> counts;
    for (const auto& student : students) ++counts[student.state];

    out_ << "  " << style(ansi::bold, "学生");
    std::size_t index = 0;
    for (const auto& [state, color] : ordered_student_states) {
        if (index == 4) out_ << "\n      ";
        out_ << (index % 4 == 0 ? "  " : " · ") << state_zh(state) << ' '
             << style(color, std::to_string(counts[state]));
        ++index;
    }
    out_ << '\n';

    struct QueueSummary {
        std::string name;
        const char* resource;
        std::size_t waiting;
        std::size_t occupied;
        std::size_t capacity;
        std::size_t previous_waiting;
    };

    const auto location_name = [&](const LocationId& id) {
        const auto it = std::find_if(run.scenario.locations.begin(), run.scenario.locations.end(),
            [&](const Location& location) { return location.id == id; });
        return it != run.scenario.locations.end() && !it->name.empty() ? it->name : id;
    };

    std::map<FacilityId, std::size_t> previous_window_queues;
    std::map<LocationId, std::size_t> previous_seat_queues;
    if (frame.at > 0) {
        const auto& previous = run.frames[static_cast<std::size_t>(frame.at - 1)];
        for (const auto& item : previous.facilities) previous_window_queues[item.facility_id] = item.queue_length;
        for (const auto& item : previous.seats) previous_seat_queues[item.location_id] = item.queued;
    }

    std::vector<QueueSummary> queues;
    for (const auto& facility : frame.facilities) {
        if (facility.queue_length == 0) continue;
        const auto config = std::find_if(run.scenario.facilities.begin(), run.scenario.facilities.end(),
            [&](const FacilityConfig& item) { return item.id == facility.facility_id; });
        const bool has_config = config != run.scenario.facilities.end();
        queues.push_back({has_config ? location_name(config->location_id) : facility.facility_id,
                          "窗口", facility.queue_length, facility.in_service,
                          has_config ? config->window_count : 0, previous_window_queues[facility.facility_id]});
    }
    for (const auto& seat : frame.seats) {
        if (seat.queued == 0) continue;
        queues.push_back({location_name(seat.location_id), "座位", seat.queued,
                          seat.occupied, seat.capacity, previous_seat_queues[seat.location_id]});
    }

    std::stable_sort(queues.begin(), queues.end(), [](const QueueSummary& left, const QueueSummary& right) {
        return left.waiting > right.waiting;
    });
    constexpr std::size_t visible_queues = 2;
    if (queues.empty()) {
        out_ << "  " << style(ansi::bold, "拥堵") << "  无人排队或等座\n";
    }
    for (std::size_t i = 0; i < std::min(visible_queues, queues.size()); ++i) {
        const auto& queue = queues[i];
        out_ << (i == 0 ? "  " + style(ansi::bold, "拥堵") + "  " : "        ")
             << queue.name << "  " << queue.resource << ' ' << queue.occupied << '/' << queue.capacity
             << " · " << style(ansi::bright_yellow, std::to_string(queue.waiting) + " 人等待");
        if (frame.at > 0 && queue.waiting != queue.previous_waiting) {
            const auto delta = static_cast<std::int64_t>(queue.waiting) -
                               static_cast<std::int64_t>(queue.previous_waiting);
            out_ << "（较上分钟 " << (delta > 0 ? "+" : "") << delta << "）";
        }
        out_ << '\n';
    }
    if (queues.size() > visible_queues) {
        out_ << "        另有 " << queues.size() - visible_queues << " 个队列有人等待\n";
    }
}

void ConsoleWorkbench::render_tick_details(const RecordedRun& run, const TickFrame& frame,
                                            const std::vector<StudentSnapshot>& students, PlaybackView view,
                                           const StudentId& selected_student) const {
    const SimTime tick = frame.at;

    // Pre-index student specs, facility configs, location names, and historical events up to `tick`
    std::map<StudentId, const StudentSpec*> spec_by_id;
    for (const auto& sp : run.population) spec_by_id[sp.id] = &sp;

    std::map<FacilityId, const FacilityConfig*> fac_cfg;
    for (const auto& fc : run.scenario.facilities) fac_cfg[fc.id] = &fc;

    std::map<LocationId, std::string> loc_names;
    for (const auto& loc : run.scenario.locations) loc_names[loc.id] = loc.name;

    std::map<StudentId, StudentArrival> latest_arrival;
    std::map<StudentId, ServiceStarted> latest_service_start;
    std::map<StudentId, ServiceCompleted> latest_service_done;
    std::map<StudentId, SeatRequest> latest_seat_req;
    std::map<StudentId, SeatGranted> latest_seat_grant;

    for (SimTime t = 0; t <= tick && static_cast<std::size_t>(t) < run.frames.size(); ++t) {
        const auto& f = run.frames[static_cast<std::size_t>(t)];
        for (const auto& a : f.arrivals) latest_arrival[a.student_id] = a;
        for (const auto& st : f.started) latest_service_start[st.student_id] = st;
        for (const auto& c : f.completed) latest_service_done[c.student_id] = c;
        for (const auto& sr : f.seat_requests) latest_seat_req[sr.student_id] = sr;
        for (const auto& sg : f.seat_grants) latest_seat_grant[sg.request.student_id] = sg;
    }

    std::map<LocationId, std::size_t> loc_counts;
    std::map<LocationId, std::map<StudentState, std::vector<StudentId>>> loc_state_students;
    struct MovingStudentDetail {
        StudentId id;
        MoveInfo move;
    };
    std::map<std::string, std::vector<MovingStudentDetail>> road_students;

    // Also pre-compute facility active windows & FIFO queue at `tick`, and seat pool occupants & queue at `tick`
    struct WindowOccupant {
        StudentId student_id;
        std::string meal_type;
        SimTime start_at{0};
        std::optional<SimTime> end_at;
        SimTime wait_minutes{0};
    };
    std::map<FacilityId, std::map<std::size_t, WindowOccupant>> active_windows;
    struct QueuedStudentInfo {
        StudentId student_id;
        std::string meal_type;
        SimTime arrived_at{0};
    };
    std::map<FacilityId, std::vector<QueuedStudentInfo>> facility_queues;

    struct SeatOccupantInfo {
        StudentId student_id;
        StudentState state{StudentState::Eating};
        SimTime granted_at{0};
        SimTime until{0};
    };
    std::map<LocationId, std::vector<SeatOccupantInfo>> seat_occupants;
    struct SeatWaiterInfo {
        StudentId student_id;
        SimTime requested_at{0};
        SimTime duration_minutes{0};
    };
    std::map<LocationId, std::vector<SeatWaiterInfo>> seat_waiters;

    for (const auto& s : students) {
        const auto* sp = spec_by_id.count(s.student_id) ? spec_by_id.at(s.student_id) : nullptr;
        const std::string meal = sp ? sp->meal_type : "standard";

        if (s.location_id) {
            ++loc_counts[*s.location_id];
            loc_state_students[*s.location_id][s.state].push_back(s.student_id);
        } else if (s.move) {
            const std::string seg = s.move->from + " -> " + s.move->to;
            road_students[seg].push_back(MovingStudentDetail{s.student_id, *s.move});
        }

        if (s.state == StudentState::InService) {
            const auto it = latest_service_start.find(s.student_id);
            if (it != latest_service_start.end()) {
                std::optional<SimTime> end_at = it->second.expected_end_at;
                const std::string mtype = !it->second.meal_type.empty() ? it->second.meal_type : meal;
                if (!end_at && fac_cfg.count(it->second.facility_id)) {
                    if (const auto srv = window_service_time(*fac_cfg.at(it->second.facility_id), it->second.window_id, mtype)) {
                        end_at = it->second.at + *srv;
                    }
                }
                active_windows[it->second.facility_id][it->second.window_id] = WindowOccupant{
                    s.student_id, mtype, it->second.at, end_at, it->second.wait_minutes
                };
            }
        } else if (s.state == StudentState::Queued) {
            const auto it = latest_arrival.find(s.student_id);
            const FacilityId fid = it != latest_arrival.end()
                ? it->second.facility_id
                : (s.lunch_facility ? *s.lunch_facility : (sp ? sp->lunch_facility : "?"));
            const SimTime arr_at = it != latest_arrival.end() ? it->second.at : tick;
            const std::string mtype = it != latest_arrival.end() ? it->second.meal_type : meal;
            facility_queues[fid].push_back(QueuedStudentInfo{s.student_id, mtype, arr_at});
        }

        if (s.occupied_seat) {
            const auto it = latest_seat_grant.find(s.student_id);
            const SimTime g_at = it != latest_seat_grant.end() ? it->second.at : tick;
            const SimTime g_until = it != latest_seat_grant.end() ? it->second.until : tick;
            seat_occupants[*s.occupied_seat].push_back(SeatOccupantInfo{s.student_id, s.state, g_at, g_until});
        } else if (s.state == StudentState::WaitingSeat && s.location_id) {
            const auto it = latest_seat_req.find(s.student_id);
            const SimTime req_at = it != latest_seat_req.end() ? it->second.at : tick;
            const SimTime dur = it != latest_seat_req.end() ? it->second.duration_minutes : (sp ? sp->meal_minutes : 5);
            seat_waiters[*s.location_id].push_back(SeatWaiterInfo{s.student_id, req_at, dur});
        }

    }

    for (auto& [fid, q] : facility_queues) {
        std::sort(q.begin(), q.end(), [](const QueuedStudentInfo& a, const QueuedStudentInfo& b) {
            return a.arrived_at != b.arrived_at ? a.arrived_at < b.arrived_at : a.student_id < b.student_id;
        });
    }
    for (auto& [lid, q] : seat_waiters) {
        std::sort(q.begin(), q.end(), [](const SeatWaiterInfo& a, const SeatWaiterInfo& b) {
            return a.requested_at != b.requested_at ? a.requested_at < b.requested_at : a.student_id < b.student_id;
        });
    }

    if (view == PlaybackView::Scene) {
        out_ << '\n' << "  " << style(ansi::bold_cyan, "场景明细") << '\n';
        // Facility Windows & Complete Queues
        std::map<FacilityId, std::size_t> win_cap;
        for (const auto& fc : run.scenario.facilities) win_cap[fc.id] = fc.window_count;

        out_ << "\n  " << style(ansi::bold, "食堂窗口与排队") << '\n';
        for (const auto& f : frame.facilities) {
            const auto cap = win_cap[f.facility_id];
            const double ratio = cap > 0 ? static_cast<double>(f.in_service) / static_cast<double>(cap) : 0.0;
            const std::string q_bar(std::min<std::size_t>(f.queue_length, 20), '|');
            out_ << "    " << style(ansi::bold_white, pad_right(f.facility_id, 10))
                 << " 窗口 " << make_bar(ratio, 8, color_, ansi::magenta) << " "
                 << f.in_service << "/" << cap
                 << " | 队列: " << pad_right(style(ansi::bright_yellow, q_bar) + (f.queue_length > 20 ? "+" : ""), 22)
                 << " (" << pad_left(std::to_string(f.queue_length), 2) << "人, 峰值" << pad_left(std::to_string(f.max_queue_length), 2) << ")"
                 << " | 到达:" << pad_left(std::to_string(f.arrived), 2)
                 << " 开始:" << pad_left(std::to_string(f.started), 2)
                 << " 完成:" << pad_left(std::to_string(f.completed), 2) << '\n';

            // Per-window live service status
            std::vector<std::string> win_tokens;
            const auto& win_map = active_windows[f.facility_id];
            for (std::size_t w = 0; w < std::min<std::size_t>(cap, 12); ++w) {
                const auto wit = win_map.find(w);
                if (wit != win_map.end()) {
                    const auto& occ = wit->second;
                    std::string token = "#W" + std::to_string(w) + ":" +
                        style(ansi::magenta, occ.student_id + "[" + occ.meal_type + ",t=" + std::to_string(occ.start_at) +
                              ".." + (occ.end_at ? std::to_string(*occ.end_at) : "?") + "m]");
                    win_tokens.push_back(token);
                } else {
                    win_tokens.push_back("#W" + std::to_string(w) + ":" + style(ansi::dim, "[空闲]"));
                }
            }
            if (cap > 12) win_tokens.push_back("另有 " + std::to_string(cap - 12) + " 个窗口");
            print_wrapped_tokens(out_, "      ├─ 窗口预览: ", "      │            ", win_tokens, "  ");

            // Queue preview
            const auto& q_list = facility_queues[f.facility_id];
            if (q_list.empty()) {
                out_ << "      └─ 排队序列: " << style(ansi::dim, "(当前无排队等待人员)") << '\n';
            } else {
                std::vector<std::string> q_tokens;
                q_tokens.reserve(q_list.size());
                for (std::size_t qi = 0; qi < std::min<std::size_t>(q_list.size(), 8); ++qi) {
                    const auto& qs = q_list[qi];
                    q_tokens.push_back(std::to_string(qi + 1) + "." + qs.student_id + "(" + qs.meal_type +
                                       ",t=" + std::to_string(qs.arrived_at) + "到,已等" + std::to_string(tick - qs.arrived_at) + "m)");
                }
                print_wrapped_tokens(out_, "      └─ 排队序列(" + std::to_string(q_list.size()) + "人): ",
                                     "         ", q_tokens, " -> ");
            }
        }

        // 3. Finite seat pools and current queue previews
        if (!frame.seats.empty()) {
            out_ << "\n  " << style(ansi::bold, "座位占用与排队") << '\n';
            for (const auto& s : frame.seats) {
                const double ratio = s.capacity > 0 ? static_cast<double>(s.occupied) / static_cast<double>(s.capacity) : 0.0;
                const std::string q_bar(std::min<std::size_t>(s.queued, 16), '!');
                out_ << "    " << style(ansi::bold_white, pad_right(s.location_id, 12))
                     << " 占座 " << make_bar(ratio, 8, color_, ansi::bright_green) << " "
                     << s.occupied << "/" << s.capacity
                     << " | 等座: " << pad_right(style(ansi::bright_red, q_bar), 17)
                     << " (" << pad_left(std::to_string(s.queued), 2) << "人, 峰值" << pad_left(std::to_string(s.max_queue), 2) << ")"
                     << " | 累计请求:" << pad_left(std::to_string(s.arrived), 2)
                     << " 已入座:" << pad_left(std::to_string(s.granted), 2)
                     << " 已释座:" << pad_left(std::to_string(s.released), 2);
                if (s.expired) out_ << " 已过期:" << s.expired;
                out_ << '\n';

                const auto& occ_list = seat_occupants[s.location_id];
                if (occ_list.empty()) {
                    out_ << "      ├─ 当前占座: " << style(ansi::dim, "(无人在座)") << '\n';
                } else {
                    std::vector<std::string> occ_tokens;
                    for (std::size_t i = 0; i < std::min<std::size_t>(occ_list.size(), 8); ++i) {
                        const auto& o = occ_list[i];
                        occ_tokens.push_back(o.student_id + "[" + (o.state == StudentState::Eating ? "就餐" : "活动") +
                                             ",t=" + std::to_string(o.granted_at) + ".." + std::to_string(o.until) + "m]");
                    }
                    print_wrapped_tokens(out_, "      ├─ 当前占座(" + std::to_string(occ_list.size()) + "人): ",
                                         "      │            ", occ_tokens, ", ");
                }

                const auto& wait_list = seat_waiters[s.location_id];
                if (wait_list.empty()) {
                    out_ << "      └─ 等座队列: " << style(ansi::dim, "(当前无人排队等座)") << '\n';
                } else {
                    std::vector<std::string> w_tokens;
                    for (std::size_t wi = 0; wi < std::min<std::size_t>(wait_list.size(), 8); ++wi) {
                        const auto& ws = wait_list[wi];
                        w_tokens.push_back(std::to_string(wi + 1) + "." + ws.student_id +
                                           "(已等" + std::to_string(tick - ws.requested_at) + "m,需" + std::to_string(ws.duration_minutes) + "m)");
                    }
                    print_wrapped_tokens(out_, "      └─ 等座队列(" + std::to_string(wait_list.size()) + "人): ",
                                         "         ", w_tokens, " -> ");
                }
            }
        }

        // 4. Campus location counts and a preview of movement by route.
        out_ << "\n  " << style(ansi::bold, "地点与在途") << '\n';
        std::vector<std::pair<LocationId, std::size_t>> sorted_locs(loc_counts.begin(), loc_counts.end());
        std::sort(sorted_locs.begin(), sorted_locs.end(), [](const auto& a, const auto& b) {
            return a.second != b.second ? a.second > b.second : a.first < b.first;
        });
        if (sorted_locs.empty()) {
            out_ << "    驻留地点: 当前全体学生均在步行途中\n";
        } else {
            for (const auto& [lid, cnt] : sorted_locs) {
                const std::string lname = loc_names.count(lid) ? ("(" + loc_names.at(lid) + ")") : "";
                std::vector<std::string> state_parts;
                for (const auto& [st, col] : ordered_student_states) {
                    const auto& ids = loc_state_students[lid][st];
                    if (ids.empty()) continue;
                    std::ostringstream oss;
                    oss << style(col, std::string(state_zh(st)) + std::to_string(ids.size()) + "人") << "[";
                    for (std::size_t i = 0; i < std::min<std::size_t>(ids.size(), 8); ++i) {
                        if (i > 0) oss << ",";
                        oss << ids[i];
                    }
                    if (ids.size() > 8) oss << ",…";
                    oss << "]";
                    state_parts.push_back(oss.str());
                }
                const std::string prefix = "    • 驻留 " + style(ansi::bold_white, pad_right(lid + lname, 22)) +
                                           " 共 " + pad_left(std::to_string(cnt), 2) + "人 : ";
                print_wrapped_tokens(out_, prefix, "           ", state_parts, " | ");
            }
        }
        std::vector<std::string> empty_locs;
        for (const auto& loc : run.scenario.locations) {
            if (loc_counts.count(loc.id) == 0) empty_locs.push_back(loc.id + "(0)");
        }
        if (!empty_locs.empty()) {
            print_wrapped_tokens(out_, "    • 空置地点: ", "                ", empty_locs, ", ");
        }

        if (road_students.empty()) {
            out_ << "    • 在途路段: " << style(ansi::dim, "当前分钟无人步行移动") << '\n';
        } else {
            for (const auto& [seg, movers] : road_students) {
                std::vector<std::string> mover_tokens;
                mover_tokens.reserve(movers.size());
                for (std::size_t i = 0; i < std::min<std::size_t>(movers.size(), 8); ++i) {
                    const auto& m = movers[i];
                    mover_tokens.push_back(m.id + "(t=" + std::to_string(m.move.depart_at) + "->" +
                                           std::to_string(m.move.arrive_at) + "m," +
                                           format_fixed(m.move.distance_m, 0) + "m,剩" +
                                           std::to_string(std::max<SimTime>(0, m.move.arrive_at - tick)) + "m)");
                }
                const std::string prefix = "    • 在途 " + style(ansi::bright_cyan, pad_right(seg, 22)) +
                                           " 共 " + pad_left(std::to_string(movers.size()), 2) + "人 : ";
                print_wrapped_tokens(out_, prefix, "           ", mover_tokens, ", ");
            }
        }
        return;
    }

    out_ << "\n  " << style(ansi::bold_cyan, "学生明细")
         << (selected_student.empty() ? (students.size() <= 24 ? " · 全员" : " · 预览") : " · " + selected_student) << '\n';
    out_ << "    " << pad_right("学号", 8) << pad_right("当前状态", 10)
         << pad_right("当前位置或去向", 24) << "当前进度\n";

    // Map queue rank for quick lookup
    std::map<StudentId, std::pair<FacilityId, std::size_t>> fac_queue_rank;
    for (const auto& [fid, q] : facility_queues) {
        for (std::size_t i = 0; i < q.size(); ++i) fac_queue_rank[q[i].student_id] = {fid, i + 1};
    }
    std::map<StudentId, std::pair<LocationId, std::size_t>> seat_queue_rank;
    for (const auto& [lid, q] : seat_waiters) {
        for (std::size_t i = 0; i < q.size(); ++i) seat_queue_rank[q[i].student_id] = {lid, i + 1};
    }

    bool found = false;
    std::size_t displayed = 0;
    for (const auto& s : students) {
        if (selected_student.empty() && displayed++ == 24) {
            out_ << "    仅预览前 24 人。使用 v 学号查看任意学生。\n";
            break;
        }
        if (!selected_student.empty() && s.student_id != selected_student) continue;
        found = true;
        const auto* sp = spec_by_id.count(s.student_id) ? spec_by_id.at(s.student_id) : nullptr;
        const std::string pol_short = !sp ? "?" :
            (sp->lunch_policy == LunchPolicy::Fixed ? "fixed" :
             sp->lunch_policy == LunchPolicy::Nearest ? "nearest" : "fastest");
        const std::string fac_choice = s.lunch_facility.value_or(sp ? sp->lunch_facility : "-");
        const std::string meal_str = sp ? sp->meal_type : "standard";
        const std::string food_cfg = fac_choice + "·" + meal_str + "·" + pol_short;

        std::string pos_str = s.location_id ? ("@ " + *s.location_id)
                                            : (s.move ? (s.move->from + " -> " + s.move->to) : "-");

        const char* st_color = ansi::dim;
        switch (s.state) {
        case StudentState::AtLocation: st_color = ansi::dim; break;
        case StudentState::Moving: st_color = ansi::bright_cyan; break;
        case StudentState::Queued: st_color = ansi::bright_yellow; break;
        case StudentState::InService: st_color = ansi::magenta; break;
        case StudentState::WaitingSeat: st_color = ansi::bright_red; break;
        case StudentState::Eating: st_color = ansi::bright_green; break;
        case StudentState::InActivity: st_color = ansi::cyan; break;
        case StudentState::Finished: st_color = ansi::green; break;
        }

        std::string detail;
        if (s.state == StudentState::Moving && s.move) {
            detail = "步行移动 [t=" + std::to_string(s.move->depart_at) + "->" + std::to_string(s.move->arrive_at) +
                     "m, 距离" + format_fixed(s.move->distance_m, 0) + "m, 剩" +
                     std::to_string(std::max<SimTime>(0, s.move->arrive_at - tick)) + "m]";
        } else if (s.state == StudentState::Queued) {
            const auto qit = fac_queue_rank.find(s.student_id);
            const auto ait = latest_arrival.find(s.student_id);
            const SimTime arr_t = ait != latest_arrival.end() ? ait->second.at : tick;
            detail = "在 " + (qit != fac_queue_rank.end() ? qit->second.first : fac_choice) +
                     " 排队第 " + (qit != fac_queue_rank.end() ? std::to_string(qit->second.second) : "?") +
                     " 位 [t=" + std::to_string(arr_t) + "m到达, 已等" + std::to_string(tick - arr_t) + "m]";
        } else if (s.state == StudentState::InService) {
            const auto sit = latest_service_start.find(s.student_id);
            if (sit != latest_service_start.end()) {
                std::optional<SimTime> end_at = sit->second.expected_end_at;
                if (!end_at && fac_cfg.count(sit->second.facility_id)) {
                    end_at = window_service_time(*fac_cfg.at(sit->second.facility_id), sit->second.window_id, meal_str);
                    if (end_at) *end_at += sit->second.at;
                }
                detail = "占用窗口 " + sit->second.facility_id + "#W" + std::to_string(sit->second.window_id) +
                         " [t=" + std::to_string(sit->second.at) + ".." + (end_at ? std::to_string(*end_at) : "?") +
                         "m, 曾排" + std::to_string(sit->second.wait_minutes) + "m]";
            }
        } else if (s.state == StudentState::WaitingSeat) {
            const auto sqit = seat_queue_rank.find(s.student_id);
            const auto srit = latest_seat_req.find(s.student_id);
            const SimTime req_t = srit != latest_seat_req.end() ? srit->second.at : tick;
            const SimTime dur_t = srit != latest_seat_req.end() ? srit->second.duration_minutes : 0;
            detail = "等座 " + s.location_id.value_or("?") + " 第 " +
                     (sqit != seat_queue_rank.end() ? std::to_string(sqit->second.second) : "?") +
                     " 位 [t=" + std::to_string(req_t) + "m申请, 已等" + std::to_string(tick - req_t) +
                     "m, 需" + std::to_string(dur_t) + "m]";
        } else if (s.state == StudentState::Eating) {
            if (s.occupied_seat) {
                const auto sgit = latest_seat_grant.find(s.student_id);
                if (sgit != latest_seat_grant.end()) {
                    detail = "占座就餐 @" + *s.occupied_seat + " [t=" + std::to_string(sgit->second.at) +
                             ".." + std::to_string(sgit->second.until) + "m, 剩" +
                             std::to_string(std::max<SimTime>(0, sgit->second.until - tick)) + "m]";
                }
            } else {
                const auto cit = latest_service_done.find(s.student_id);
                const SimTime start_e = cit != latest_service_done.end() ? cit->second.at : tick;
                SimTime duration = sp ? sp->meal_minutes : 5;
                for (const auto& activity : run.activities) {
                    if (activity.student_id == s.student_id && activity.effective_meal_minutes) {
                        duration = *activity.effective_meal_minutes;
                        break;
                    }
                }
                const SimTime end_e = start_e + duration;
                detail = "食堂就餐 @" + s.location_id.value_or("?") + " [t=" + std::to_string(start_e) +
                         ".." + std::to_string(end_e) + "m, 剩" + std::to_string(std::max<SimTime>(0, end_e - tick)) + "m]";
            }
        } else if (s.state == StudentState::InActivity) {
            for (const auto& r : run.activities) {
                if (r.student_id == s.student_id && r.started_at && *r.started_at <= tick &&
                    (!r.completed_at || tick < *r.completed_at)) {
                    const SimTime delay = *r.started_at - r.activity.start_minute;
                    detail = std::string(activity_zh(r.activity.kind)) + ":" + r.activity.id +
                             " [t=" + std::to_string(*r.started_at) + ".." +
                             (r.completed_at ? std::to_string(*r.completed_at) : "?") + "m, " +
                             (r.activity.after_previous ? "随前序" :
                                 delay > 0 ? ("延后+" + std::to_string(delay) + "m") : "准点") + "]";
                    break;
                }
            }
            if (detail.empty()) detail = "执行日程活动 @" + s.location_id.value_or("?");
        } else if (s.state == StudentState::AtLocation) {
            detail = "等待下一项日程";
        } else if (s.state == StudentState::Finished) {
            detail = "全部日程已完成";
        }

        out_ << "    " << pad_right(s.student_id, 8) << ' '
             << pad_right(style(st_color, state_zh(s.state)), 9) << ' '
             << pad_right(pos_str, 23) << ' ' << detail << '\n';
        if (!selected_student.empty()) out_ << "    午餐配置  " << food_cfg << '\n';
    }
    if (!found && !selected_student.empty()) {
        out_ << "    " << style(ansi::yellow, "未找到学生 " + selected_student) << '\n';
    }
}

std::vector<std::string> ConsoleWorkbench::render_tick_event_summary(const RecordedRun& run, const TickFrame& frame,
                                                                    const std::vector<StudentSnapshot>& students,
                                                                    bool include_details) const {
    const SimTime tick = frame.at;
    std::size_t decisions = 0, departures = 0, arrivals = 0, service_starts = 0, meals_served = 0;
    std::size_t seat_requests = 0, seat_grants = 0, activity_starts = 0, activity_ends = 0, finished = 0;
    std::vector<std::string> logs;
    const auto record = [&](std::size_t& count, const auto& describe) {
        ++count;
        if (include_details) logs.push_back(describe());
    };

    for (const auto& event : frame.completed) {
        record(meals_served, [&] {
            return style(ansi::green, "[出餐] ") + event.student_id + " 在 " + event.facility_id +
                   "#W" + std::to_string(event.window_id) + " 完成取餐，释放窗口";
        });
    }
    for (const auto& decision : frame.decisions) {
        if (!decision.selected) continue;
        record(decisions, [&] {
            std::string detail = decision.student_id + " 按 " + policy_text(decision.policy) +
                                 " 选定 " + decision.facility_id;
            if (decision.distance_m && decision.travel_minutes) {
                detail += "（路程 " + format_fixed(*decision.distance_m, 0) + " 米，步行 " +
                          std::to_string(*decision.travel_minutes) + " 分钟";
                if (decision.estimated_wait_minutes) {
                    detail += "，预计等窗 " + std::to_string(*decision.estimated_wait_minutes) + " 分钟";
                }
                if (decision.service_minutes) detail += "，取餐 " + std::to_string(*decision.service_minutes) + " 分钟";
                if (decision.estimated_total_minutes) {
                    detail += "，预计共 " + std::to_string(*decision.estimated_total_minutes) + " 分钟";
                }
                detail += "）";
            }
            return style(ansi::bright_cyan, "[决策] ") + detail;
        });
    }
    for (const auto& student : students) {
        if (student.state != StudentState::Moving || !student.move || student.move->depart_at != tick) continue;
        record(departures, [&] {
            return style(ansi::cyan, "[出发] ") + student.student_id + " 从 " + student.move->from +
                   " 前往 " + student.move->to + "（" + format_fixed(student.move->distance_m, 0) +
                   " 米，预计第 " + std::to_string(student.move->arrive_at) + " 分钟抵达）";
        });
    }
    for (const auto& event : frame.arrivals) {
        record(arrivals, [&] {
            return style(ansi::yellow, "[到达] ") + event.student_id + " 到达 " + event.facility_id +
                   " 并入队（" + event.meal_type + "）";
        });
    }
    for (const auto& event : frame.started) {
        record(service_starts, [&] {
            const std::string end = event.expected_end_at
                ? "，预计第 " + std::to_string(*event.expected_end_at) + " 分钟出餐" : "";
            return style(ansi::magenta, "[取餐] ") + event.student_id + " 进入 " + event.facility_id +
                   "#W" + std::to_string(event.window_id) + "（已排队 " +
                   std::to_string(event.wait_minutes) + " 分钟" + end + "）";
        });
    }
    for (const auto& event : frame.seat_requests) {
        record(seat_requests, [&] {
            return style(ansi::bright_yellow, "[申座] ") + event.student_id + " 在 " + event.location_id +
                   " 申请座位（计划占用 " + std::to_string(event.duration_minutes) + " 分钟）";
        });
    }
    for (const auto& event : frame.seat_grants) {
        record(seat_grants, [&] {
            return style(ansi::bright_green, "[入座] ") + event.request.student_id + " 在 " +
                   event.request.location_id + " 入座（占用至第 " + std::to_string(event.until) +
                   " 分钟，已等座 " + std::to_string(event.at - event.request.at) + " 分钟）";
        });
    }
    for (const auto& activity : run.activities) {
        if (activity.started_at == tick && activity.activity.kind != ActivityKind::Lunch) {
            record(activity_starts, [&] {
                const SimTime delay = tick - activity.activity.start_minute;
                const std::string timing = activity.activity.after_previous ? "随前序" :
                    delay > 0 ? "延后 " + std::to_string(delay) + " 分钟" : "准点";
                return style(ansi::cyan, "[开始活动] ") + activity.student_id + " 开始 " +
                       activity_zh(activity.activity.kind) + "（" + activity.activity.id + " @" +
                       activity.destination + "，" + timing + "）";
            });
        }
        if (activity.completed_at == tick) {
            record(activity_ends, [&] {
                return style(ansi::green, "[完成活动] ") + activity.student_id + " 完成 " +
                       activity_zh(activity.activity.kind) + "（" + activity.activity.id + " @" +
                       activity.destination + "）";
            });
        }
        if (activity.missed_at == tick) {
            record(activity_ends, [&] {
                return style(ansi::yellow, "[缺课] ") + activity.student_id + " 未赶上 " +
                       activity.activity.id + " @" + activity.destination;
            });
        }
    }
    const auto completed_before = run.finished_students_at(tick - 1);
    finished = frame.finished_students - completed_before;

    const std::pair<const char*, std::size_t> counts[] = {
        {"选食堂", decisions}, {"出发", departures}, {"到达食堂", arrivals},
        {"开始取餐", service_starts}, {"出餐", meals_served}, {"申座", seat_requests}, {"入座", seat_grants},
        {"开始活动", activity_starts}, {"完成活动", activity_ends}, {"完成日程", finished}
    };
    std::vector<std::string> items;
    for (const auto& [label, count] : counts) {
        if (count != 0) items.push_back(std::string(label) + ' ' + std::to_string(count));
    }
    if (items.empty()) items.push_back("本分钟无新事件");
    print_wrapped_tokens(out_, "  " + style(ansi::bold, "事件") + "  ", "        ", items, " · ", 78);
    return logs;
}

void ConsoleWorkbench::menu_simulation_playback() {
    ensure_run_cached();
    const auto& run = *cached_run_;
    const auto duration = run.settings.duration_minutes;
    const auto peak = run.peak_congestion_minute();
    SimTime cursor = population_.size() > 1000 ? 0 : peak;
    auto view = PlaybackView::Overview;
    StudentId selected_student;

    const auto draw = [&](bool refresh) {
        if (refresh && color_) out_ << "\x1b[2J\x1b[H";
        render_tick_dashboard(run, cursor, view, selected_student);
        out_.flush();
    };

    draw(false);
    while (true) {
        const auto scene_hint = view == PlaybackView::Scene ? "[r]收起场景" : "[r]场景";
        const auto student_hint = view == PlaybackView::Students ? "[v]收起学生" : "[v]学生";
        out_ << "\n  " << style(ansi::dim, "[回车/n]下一分  [b]上一分  [+/-]跳10分  [g 分钟]跳转  [k]峰值")
             << "\n  " << style(ansi::dim, std::string(scene_hint) + "  " + student_hint +
                          "  [v 学号]查学生  [p 步数]按倍率连播  [s]全程汇总  [q]返回") << '\n';
        std::string cmd;
        if (!prompt_line("playback(t=" + std::to_string(cursor) + ")> ", cmd)) break;
        if (cmd == "q" || cmd == "0" || cmd == "quit") break;

        if (cmd == "r") {
            view = view == PlaybackView::Scene ? PlaybackView::Overview : PlaybackView::Scene;
            selected_student.clear();
            draw(true);
            continue;
        }
        if (cmd == "v" || cmd.rfind("v ", 0) == 0) {
            view = cmd == "v" && view == PlaybackView::Students ? PlaybackView::Overview : PlaybackView::Students;
            selected_student = cmd == "v" ? StudentId{} : trim(cmd.substr(2));
            draw(true);
            continue;
        }
        if (cmd == "s") {
            print_run_summary(run);
            continue;
        }
        if (!cmd.empty() && cmd.front() == 'p') {
            std::istringstream iss(cmd.substr(1));
            int steps = playback_speed_;
            int delay_ms = 60000 / playback_speed_;
            iss >> steps >> delay_ms;
            steps = std::clamp(steps, 1, 300);
            delay_ms = std::clamp(delay_ms, 0, 60000);
            view = PlaybackView::Overview;
            selected_student.clear();
            if (cursor == duration) draw(true);
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < steps && cursor < duration; ++i) {
                if (delay_ms > 0) {
                    std::this_thread::sleep_until(start + std::chrono::milliseconds((i + 1) * delay_ms));
                }
                ++cursor;
                draw(true);
            }
            continue;
        }

        if (cmd.empty() || cmd == "n") {
            cursor += std::min<SimTime>(duration - cursor, 1);
        } else if (cmd == "b") {
            cursor = std::max<SimTime>(0, cursor - 1);
        } else if (cmd == "+") {
            cursor += std::min<SimTime>(duration - cursor, 10);
        } else if (cmd == "-") {
            cursor = std::max<SimTime>(0, cursor - 10);
        } else if (cmd == "k") {
            cursor = peak;
        } else {
            const std::string target_str = cmd.rfind("g ", 0) == 0 ? trim(cmd.substr(2)) : cmd;
            SimTime target = 0;
            if (!parse_int(target_str, target) || target < 0 || target > duration) {
                out_ << style(ansi::red, "无法识别的回放指令。请输入上方指令或使用 g 跳转至指定分钟。\n");
                continue;
            }
            cursor = target;
        }
        view = PlaybackView::Overview;
        selected_student.clear();
        draw(true);
    }
}

void ConsoleWorkbench::menu_student_inspector() {
    ensure_run_cached();
    const auto& run = *cached_run_;

    out_ << "\n" << style(ansi::bold_cyan, "─── [3] 学生个体全生命周期轨迹、选食堂决策与日程透视 (全员名册共 " +
                          std::to_string(run.population.size()) + " 人) ───") << '\n';
    out_ << "  " << pad_right("学号", 8) << pad_right("初始地点", 12) << pad_right("餐品需求", 10)
         << pad_right("策略", 10) << pad_right("步速(m/m)", 11) << pad_right("选定食堂", 10) << "结束状态\n";
    out_ << "  " << std::string(80, '-') << '\n';

    const auto final_snaps = run.students_at(run.settings.duration_minutes);
    std::map<StudentId, StudentSnapshot> final_by_id;
    for (const auto& s : final_snaps) final_by_id[s.student_id] = s;

    for (std::size_t i = 0; i < std::min<std::size_t>(run.population.size(), 24); ++i) {
        const auto& sp = run.population[i];
        const auto& fs = final_by_id.at(sp.id);
        const std::string pol = sp.lunch_policy == LunchPolicy::Fixed ? "fixed" :
            sp.lunch_policy == LunchPolicy::Nearest ? "nearest" : "fastest";
        out_ << "  " << pad_right(sp.id, 8)
             << pad_right(sp.initial_location, 12)
             << pad_right(sp.meal_type, 10)
             << pad_right(pol, 10)
             << pad_right(format_fixed(sp.walk_m_per_min, 1), 11)
             << pad_right(fs.lunch_facility.value_or(sp.lunch_facility), 10)
             << state_zh(fs.state) << '\n';
    }

    if (run.population.size() > 24)
        out_ << "  名册预览 24 人，可输入任意学号查看。\n";
    if (run.population.empty()) return;
    while (true) {
        std::string sid;
        if (!prompt_line("\n输入要透视的学生学号 (回车默认 " + run.population.front().id + "，输入 0 返回)> ", sid)) break;
        if (sid == "0" || sid == "q") break;
        if (sid.empty()) sid = run.population.front().id;

        const auto spec_it = std::find_if(run.population.begin(), run.population.end(),
                                          [&](const StudentSpec& s) { return s.id == sid; });
        if (spec_it == run.population.end()) {
            out_ << style(ansi::red, "未找到学生 ID: " + sid + "\n");
            continue;
        }
        const auto& spec = *spec_it;
        out_ << "\n" << style(ansi::bold_white, "▶ 学生档案: " + spec.id)
             << " | 起点: " << spec.initial_location
             << " | 步行速度: " << format_fixed(spec.walk_m_per_min, 1) << " m/min"
             << " | 餐品偏好: " << style(ansi::bright_yellow, spec.meal_type)
             << " | 回放 " << playback_speed_ << " 倍 | 策略: " << policy_text(spec.lunch_policy) << '\n';

        // 1. Candidate Canteen Decisions
        std::vector<LunchDecision> student_decisions;
        for (const auto& frame : run.frames) {
            for (const auto& d : frame.decisions) {
                if (d.student_id == sid) student_decisions.push_back(d);
            }
        }
        if (!student_decisions.empty()) {
            out_ << "\n  " << style(ansi::bold, "1) 出发时刻选食堂评估明细 (决策时刻 t=" + std::to_string(student_decisions.front().at) +
                                                ", 观察快照 t=" + std::to_string(student_decisions.front().observed_at) + ")") << '\n';
            out_ << "     " << pad_right("候选食堂", 10) << pad_left("距离(m)", 9) << pad_left("路途(m)", 9)
                 << pad_left("冻结队列", 10) << pad_left("预计等窗", 10) << pad_left("服务(m)", 9)
                 << pad_left("预计总计", 10) << "  结果与原因\n";
            out_ << "     " << std::string(86, '-') << '\n';
            for (const auto& d : student_decisions) {
                const std::string mark = d.selected
                    ? style(ansi::bold_green, "★ 选中 (" + d.reason + ")")
                    : style(ansi::dim, "  未选 (" + d.reason + ")");
                out_ << "     " << pad_right(d.facility_id, 10)
                     << pad_left(d.distance_m ? format_fixed(*d.distance_m, 0) : "-", 9)
                     << pad_left(d.travel_minutes ? std::to_string(*d.travel_minutes) : "-", 9)
                     << pad_left(d.queue_length ? std::to_string(*d.queue_length) : "-", 10)
                     << pad_left(d.estimated_wait_minutes ? std::to_string(*d.estimated_wait_minutes) : "-", 10)
                     << pad_left(d.service_minutes ? std::to_string(*d.service_minutes) : "-", 9)
                     << pad_left(d.estimated_total_minutes ? std::to_string(*d.estimated_total_minutes) : "-", 10)
                     << "  " << mark << '\n';
                if (d.selected && !d.path.empty()) {
                    out_ << "     " << style(ansi::bright_cyan, "最优路径: " + join_path(d.path)) << '\n';
                }
            }
        }

        // 2. Schedule Activities Table
        std::vector<ActivityRecord> student_acts;
        for (const auto& a : run.activities) {
            if (a.student_id == sid) student_acts.push_back(a);
        }
        if (!student_acts.empty()) {
            out_ << "\n  " << style(ansi::bold, "2) 全天有序日程执行与延迟传播表") << '\n';
            out_ << "     " << pad_right("序号·活动", 14) << pad_right("类型", 6) << pad_right("目的地", 12)
                 << pad_left("计划", 6) << pad_left("出发", 6) << pad_left("到达", 6)
                 << pad_left("开始", 6) << pad_left("结束", 6) << pad_left("延迟", 8)
                 << pad_left("等座", 8) << "  实际路线\n";
            out_ << "     " << std::string(94, '-') << '\n';
            for (std::size_t i = 0; i < student_acts.size(); ++i) {
                const auto& r = student_acts[i];
                const auto opt_t = [](const std::optional<SimTime>& v) { return v ? std::to_string(*v) : "-"; };
                std::string delay_str = "-";
                if (r.started_at && !r.activity.after_previous) {
                    const auto diff = *r.started_at - r.activity.start_minute;
                    delay_str = diff > 0 ? style(ansi::bold_yellow, "+" + std::to_string(diff) + "m") : "0m";
                }
                const std::string seat_w = r.seat_wait_minutes > 0
                    ? style(ansi::bright_red, "+" + std::to_string(r.seat_wait_minutes) + "m")
                    : "0m";
                out_ << "     " << pad_right(std::to_string(i + 1) + "." + r.activity.id, 14)
                     << pad_right(activity_zh(r.activity.kind), 6)
                     << pad_right(r.destination.empty() ? "-" : r.destination, 12)
                     << pad_left(r.activity.after_previous ? "随前序" : std::to_string(r.activity.start_minute), 6)
                     << pad_left(opt_t(r.departed_at), 6)
                     << pad_left(opt_t(r.arrived_at), 6)
                     << pad_left(opt_t(r.started_at), 6)
                     << pad_left(r.missed_at ? "缺课" : opt_t(r.completed_at), 6)
                     << pad_left(delay_str, 8)
                     << pad_left(seat_w, 8)
                     << "  " << join_path(r.path, "->");
                if (r.effective_meal_minutes) out_ << " | 实际用餐 " << *r.effective_meal_minutes << "m";
                out_ << '\n';
            }
        }

        // 3. 1:1 Minute-by-Minute Full-Resolution Visual State Strip (60 min per row)
        out_ << "\n  " << style(ansi::bold, "3) 逐分钟 1:1 全分辨率状态时间轴 (0 .. " + std::to_string(run.settings.duration_minutes) + " min)") << '\n';
        out_ << "     图例: [.]等待 [>]移动 [Q]排队 [S]取餐 [W]等座 [E]就餐 [A]日程活动 [_]已完成\n";

        std::vector<StudentSnapshot> timeline;
        timeline.reserve(run.frames.size());
        for (const auto& f : run.frames) {
            if (auto snapshot = run.student_at(sid, f.at)) timeline.push_back(std::move(*snapshot));
        }

        const std::size_t row_span = 60;
        for (std::size_t base = 0; base < timeline.size(); base += row_span) {
            const std::size_t end_idx = std::min(timeline.size(), base + row_span);
            out_ << "     t=" << pad_left(std::to_string(base), 3) << ".."
                 << pad_right(std::to_string(end_idx - 1) + "m", 4) << " [";
            for (std::size_t t = base; t < end_idx; ++t) {
                switch (timeline[t].state) {
                case StudentState::AtLocation: out_ << style(ansi::dim, "."); break;
                case StudentState::Moving: out_ << style(ansi::bright_cyan, ">"); break;
                case StudentState::Queued: out_ << style(ansi::bright_yellow, "Q"); break;
                case StudentState::InService: out_ << style(ansi::magenta, "S"); break;
                case StudentState::WaitingSeat: out_ << style(ansi::bright_red, "W"); break;
                case StudentState::Eating: out_ << style(ansi::bright_green, "E"); break;
                case StudentState::InActivity: out_ << style(ansi::cyan, "A"); break;
                case StudentState::Finished: out_ << style(ansi::green, "_"); break;
                }
            }
            out_ << "]\n";
        }

        // 4. Complete Contiguous State Intervals Breakdown
        if (!timeline.empty()) {
            out_ << "\n  " << style(ansi::bold, "4) 全生命周期连续状态区间分解表 (每一分钟状态去向全记录)") << '\n';
            std::size_t seg_start = 0;
            const auto same_segment = [](const StudentSnapshot& a, const StudentSnapshot& b) {
                if (a.state != b.state || a.location_id != b.location_id || a.occupied_seat != b.occupied_seat) return false;
                if (a.move.has_value() != b.move.has_value()) return false;
                if (a.move && (a.move->from != b.move->from || a.move->to != b.move->to || a.move->depart_at != b.move->depart_at)) return false;
                return true;
            };
            for (std::size_t t = 1; t <= timeline.size(); ++t) {
                if (t == timeline.size() || !same_segment(timeline[seg_start], timeline[t])) {
                    const auto& snap = timeline[seg_start];
                    const std::size_t seg_end = t - 1;
                    const std::size_t dur = seg_end - seg_start + 1;
                    std::string where = snap.location_id ? ("@ " + *snap.location_id)
                        : (snap.move ? (snap.move->from + " -> " + snap.move->to + " (" + format_fixed(snap.move->distance_m, 0) + "m)") : "-");
                    if (snap.occupied_seat) where += " [占座:" + *snap.occupied_seat + "]";
                    out_ << "     • t = " << pad_left(std::to_string(seg_start), 3) << " .. "
                         << pad_left(std::to_string(seg_end), 3) << "m ("
                         << pad_left(std::to_string(dur), 3) << "分钟) : "
                         << pad_right(state_zh(snap.state), 10) << " | " << where << '\n';
                    seg_start = t;
                }
            }
        }
    }
}

namespace {
struct StudentLunchOutcome {
    std::optional<SimTime> window_wait;
    std::optional<SimTime> served_at;
    std::optional<SimTime> eating_at;
    std::vector<ActivityRecord> activities;
};

struct ActivityTimingCounts {
    std::size_t total{0}, late{0}, missed{0}, pending{0}, late_arrival{0}, waited{0};

    void add(const ActivityRecord& record) {
        ++total;
        if (record.missed_at) ++missed;
        else if (!record.started_at) ++pending;
        else if (*record.started_at > record.activity.start_minute) ++late;
        if (record.arrived_at && *record.arrived_at > record.activity.start_minute) ++late_arrival;
        if (record.seat_wait_minutes > 0) ++waited;
    }
};

struct LunchBatchTiming {
    StudentId first_id, last_id;
    std::size_t finished{0};
    std::int64_t dismissal_to_finish{0};
};

struct LunchTimingResult {
    std::string label;
    std::map<StudentId, StudentLunchOutcome> students;
    SimTime peak_at{0};
    std::size_t peak_queue{0};
    std::optional<double> mean_wait;
    std::optional<SimTime> p90_wait;
    std::size_t started{0}, served{0}, finished{0};
    std::size_t lunch_finished{0}, fast_meals{0};
    std::int64_t release_to_finish{0}, dismissal_to_finish{0};
    ActivityTimingCounts classes, study, sport;
    std::vector<LunchBatchTiming> batches;
};

std::string campus_clock(const std::optional<SimTime>& at) {
    if (!at) return "-";
    const auto minute = 480 + static_cast<std::int64_t>(*at);
    std::ostringstream text;
    text << std::setfill('0') << std::setw(2) << minute / 60 << ':' << std::setw(2) << minute % 60;
    return text.str();
}

std::string minutes_text(const std::optional<SimTime>& minutes) {
    return minutes ? std::to_string(*minutes) : "-";
}

LunchTimingResult summarize_lunch_timing(std::string label, RecordedRun run) {
    LunchTimingResult result;
    result.label = std::move(label);
    result.finished = run.finished_students_at(run.settings.duration_minutes);
    const auto batch_count = std::min<std::size_t>(6, run.population.size());
    result.batches.resize(batch_count);
    std::map<StudentId, std::size_t> batch_by_id;
    for (std::size_t rank = 0; rank < run.population.size(); ++rank) {
        const auto batch_index = rank * batch_count / run.population.size();
        const auto& id = run.population[rank].id;
        batch_by_id.emplace(id, batch_index);
        auto& batch = result.batches[batch_index];
        if (batch.first_id.empty()) batch.first_id = id;
        batch.last_id = id;
    }
    std::vector<SimTime> waits;
    std::int64_t total_wait = 0;
    std::set<LocationId> dining_pools;
    for (const auto& facility : run.scenario.facilities) {
        for (const auto& pool : run.scenario.seat_pools) {
            if (facility.location_id == pool.location_id) dining_pools.insert(pool.location_id);
        }
    }
    for (const auto& frame : run.frames) {
        std::size_t waiting = 0;
        for (const auto& facility : frame.facilities) waiting += facility.queue_length;
        if (waiting > result.peak_queue) {
            result.peak_queue = waiting;
            result.peak_at = frame.at;
        }
        for (const auto& event : frame.started) {
            result.students[event.student_id].window_wait = event.wait_minutes;
            total_wait += event.wait_minutes;
            waits.push_back(event.wait_minutes);
        }
        for (const auto& event : frame.completed) {
            auto& person = result.students[event.student_id];
            person.served_at = event.at;
            ++result.served;
        }
        for (const auto& grant : frame.seat_grants) {
            if (dining_pools.count(grant.request.location_id))
                result.students[grant.request.student_id].eating_at = grant.at;
        }
    }

    for (auto& activity : run.activities) {
        auto& person = result.students[activity.student_id];
        if (activity.activity.kind == ActivityKind::Lunch) {
            if (!dining_pools.count(activity.destination)) person.eating_at = person.served_at;
            if (activity.meal_received_at && activity.activity.fast_meal_from &&
                *activity.meal_received_at >= *activity.activity.fast_meal_from) ++result.fast_meals;
            if (activity.completed_at) {
                ++result.lunch_finished;
                result.release_to_finish += *activity.completed_at - activity.activity.start_minute;
                result.dismissal_to_finish += *activity.completed_at - flagship_dismissal;
                auto& batch = result.batches[batch_by_id.at(activity.student_id)];
                ++batch.finished;
                batch.dismissal_to_finish += *activity.completed_at - flagship_dismissal;
            }
        }
        if (activity.activity.kind == ActivityKind::Class && activity.activity.id == "afternoon") result.classes.add(activity);
        if (activity.activity.kind == ActivityKind::Study) result.study.add(activity);
        if (activity.activity.kind == ActivityKind::Sport) result.sport.add(activity);
        person.activities.push_back(std::move(activity));
    }
    result.started = waits.size();
    if (!waits.empty()) {
        result.mean_wait = static_cast<double>(total_wait) / waits.size();
        std::sort(waits.begin(), waits.end());
        result.p90_wait = waits[waits.size() - waits.size() / 10 - 1];
    }
    return result;
}
}  // namespace

void ConsoleWorkbench::menu_lunch_timing_experiment() {
    out_ << "\n午餐实验\n"
         << "  [1] 单方案展示 · 选择一种方案并打开回放\n"
         << "  [2] 总结果对照 · 比较 0 / 20 / 40 / 60 分钟四种方案\n"
         << "  [0] 返回主菜单\n";
    std::string choice;
    if (!prompt_line("选择> ", choice) || choice.empty() || choice == "0") return;
    if (choice == "1") {
        if (scenario_dirty_) {
            std::string confirm;
            if (!prompt_line("切换到旗舰实验会放弃当前场景的未保存修改，继续？(y/N)> ", confirm) ||
                (confirm != "y" && confirm != "Y")) return;
        }
        if (choose_flagship()) {
            out_ << "当前实验：" << active_preset_title_ << '\n';
            menu_simulation_playback();
        }
    } else if (choice == "2") {
        show_lunch_timing_comparison();
    } else {
        out_ << "请输入 1、2 或 0。\n";
    }
}

void ConsoleWorkbench::show_lunch_timing_comparison() {
    out_ << "\n午餐错峰对照 · 旗舰校园\n"
         << "11:30 统一下课，按学号分批放行。四组共用同一份课表和校园设施。\n"
         << "计算完成后可按学号对比四组完整日程。\n";
    CampusEnvironment campus;
    campus.load_directory(root_ / "data/scenarios/campus_flagship");
    const auto student_file = root_ / "data/students/campus_flagship.csv";
    const auto schedule_dir = root_ / "data/schedules";
    const auto baseline = read_scheduled_students(student_file, schedule_dir / "campus_flagship.csv");

    constexpr SimTime duration = 600;
    std::vector<LunchTimingResult> results;
    const auto run_variant = [&](const std::string& label, std::vector<StudentSpec> population) {
        out_ << "正在计算 " << label << "（" << population.size() << " 人）…\n" << std::flush;
        results.push_back(summarize_lunch_timing(label,
            load_or_simulate(campus, population, duration)));
    };
    for (auto plan : {LunchPlan::Concentrated, LunchPlan::Staggered20, LunchPlan::Staggered40, LunchPlan::Staggered60}) {
        auto population = baseline;
        apply_lunch_plan(population, plan);
        run_variant(lunch_plan_name(plan), std::move(population));
    }
    const auto mean = [](std::int64_t sum, std::size_t count) {
        return count ? format_fixed(static_cast<double>(sum) / count, 1) : "-";
    };
    out_ << "\n各方案汇总 · 截止 18:00\n";
    for (const auto& row : results) {
        out_ << "  " << row.label << " | 全校窗口队列峰值 " << row.peak_queue
             << " 人 @" << campus_clock(row.peak_queue ? std::optional<SimTime>(row.peak_at) : std::nullopt)
             << " | 等窗均值/P90 "
             << (row.mean_wait ? format_fixed(*row.mean_wait, 1) : "-") << '/'
             << minutes_text(row.p90_wait) << " 分（已开始取餐 " << row.started << " 人）\n"
             << "    已出餐 " << row.served << " 人 | 吃完午餐 " << row.lunch_finished
             << " 人 | 13:00 起快速用餐 " << row.fast_meals
             << " 人 | 放行至吃完平均 " << mean(row.release_to_finish, row.lunch_finished)
             << " 分 | 下课至吃完平均 " << mean(row.dismissal_to_finish, row.lunch_finished) << " 分\n"
             << "    下午课 " << row.classes.total << " 人：迟到 " << row.classes.late
             << "，缺课 " << row.classes.missed << "，待开始 " << row.classes.pending << '\n'
             << "    自习 " << row.study.total << " 人：推迟开始 " << row.study.late << "，晚到 "
             << row.study.late_arrival << "，等位 " << row.study.waited << "，未开始 " << row.study.pending << '\n'
             << "    运动 " << row.sport.total << " 人：推迟开始 " << row.sport.late << "，晚到 "
             << row.sport.late_arrival << "，等位 " << row.sport.waited << "，未开始 " << row.sport.pending
             << " | 完成全天 " << row.finished << '\n';
    }
    out_ << "\n按学号段对照 · 下课至吃完平均分钟（已吃完学生）\n"
         << "  学号段              0分钟    20分钟    40分钟    60分钟\n";
    for (std::size_t i = 0; i < results.front().batches.size(); ++i) {
        const auto& group = results.front().batches[i];
        out_ << "  " << pad_right(group.first_id + "–" + group.last_id, 16);
        for (const auto& row : results) {
            const auto& batch = row.batches[i];
            out_ << pad_left(mean(batch.dismissal_to_finish, batch.finished), 10);
        }
        out_ << '\n';
    }

    while (true) {
        std::string id;
        if (!prompt_line("\n输入学号对照（如 S0001，回车或 0 返回）> ", id) || id.empty() || id == "0") return;
        if (baseline.empty() || !results.front().students.count(id)) {
            out_ << "未找到学生 " << id << '\n';
            continue;
        }
        out_ << "\n学生 " << id << " 的四种日程\n";
        for (const auto& row : results) {
            const auto& person = row.students.at(id);
            out_ << "  " << row.label << " | 等窗 " << minutes_text(person.window_wait)
                 << " 分 | 出餐 " << campus_clock(person.served_at)
                 << " | 开始吃饭 " << campus_clock(person.eating_at) << '\n';
            out_ << "    " << pad_right("活动", 13) << pad_right("地点", 7) << pad_right("计划", 15)
                 << pad_right("出发", 8) << pad_right("到达", 8) << pad_right("开始", 8)
                 << pad_right("完成", 8) << "等座(分)\n";
            for (const auto& activity : person.activities) {
                const bool lunch = activity.activity.kind == ActivityKind::Lunch;
                const auto& spec = activity.activity;
                std::string planned = spec.after_previous ? "随前序" : campus_clock(spec.start_minute);
                if (spec.kind == ActivityKind::Class) planned += "–" + campus_clock(spec.start_minute + spec.duration_minutes);
                out_ << "    " << pad_right(activity.activity.id, 13)
                     << pad_right(activity.destination.empty() ? "-" : activity.destination, 7)
                     << pad_right(planned, 15)
                     << pad_right(campus_clock(activity.departed_at), 8)
                     << pad_right(campus_clock(activity.arrived_at), 8)
                     << pad_right(campus_clock(lunch ? person.eating_at : activity.started_at), 8)
                     << pad_right(activity.missed_at ? "缺课" : campus_clock(activity.completed_at), 8)
                     << activity.seat_wait_minutes;
                if (activity.effective_meal_minutes) out_ << " | 实际用餐 " << *activity.effective_meal_minutes << " 分钟";
                out_ << '\n';
            }
        }
    }
}

void ConsoleWorkbench::menu_map_and_routes() {
    const auto& sc = environment_.scenario();
    std::map<LocationId, std::size_t> degree;
    for (const auto& r : sc.roads) {
        ++degree[r.from];
        ++degree[r.to];
    }
    std::map<LocationId, std::size_t> seat_map;
    for (const auto& p : sc.seat_pools) seat_map[p.location_id] = p.capacity;

    out_ << "\n" << style(ansi::bold_cyan, "─── [5] 校园路网拓扑总览与 Dijkstra 最短路导航 ───") << '\n';
    out_ << "  " << pad_right("地点ID", 14) << pad_right("名称", 20) << pad_right("类型", 10)
         << pad_left("描述容量", 10) << pad_left("有限座位池", 12) << pad_left("相连道路数", 12) << '\n';
    out_ << "  " << std::string(80, '-') << '\n';
    for (const auto& loc : sc.locations) {
        const std::string sp = seat_map.count(loc.id) ? std::to_string(seat_map[loc.id]) + " 座" : "不限";
        out_ << "  " << pad_right(loc.id, 14) << pad_right(loc.name, 20) << pad_right(kind_zh(loc.kind), 10)
             << pad_left(std::to_string(loc.capacity), 10) << pad_left(sp, 12)
             << pad_left(std::to_string(degree[loc.id]), 12) << '\n';
    }

    out_ << "\n  " << style(ansi::bold, "[食堂与窗口服务能力]") << '\n';
    for (const auto& f : sc.facilities) {
        out_ << "  • " << style(ansi::bold_white, f.id) << " @ " << f.location_id
             << " | 窗口数=" << f.window_count << " | 默认耗时=" << f.service_minutes << "m";
        if (f.window_services.empty()) {
            out_ << " (同质通用窗口)\n";
        } else {
            out_ << " | 异质窗口明细: ";
            for (std::size_t i = 0; i < f.window_services.size(); ++i) {
                if (i > 0) out_ << ", ";
                const auto& ws = f.window_services[i];
                out_ << "W" << ws.window_id << ":" << ws.meal_type << "(" << ws.service_minutes << "m)";
            }
            out_ << '\n';
        }
    }

    while (true) {
        std::string line;
        if (!prompt_line("\n输入 '起点ID 终点ID' 查询最短路，或输入 'matrix' 查看校园距离矩阵 (0返回)> ", line)) break;
        if (line.empty() || line == "0" || line == "q") break;
        if (line == "matrix") {
            std::vector<LocationId> origins, targets;
            for (const auto& l : sc.locations) {
                if (l.kind == LocationKind::Teaching || l.kind == LocationKind::Dormitory) origins.push_back(l.id);
                if (l.kind == LocationKind::Canteen || l.kind == LocationKind::Library || l.kind == LocationKind::Sports)
                    targets.push_back(l.id);
            }
            out_ << "\n  " << pad_right("起点 \\ 终点", 14);
            for (const auto& t : targets) out_ << pad_left(t, 12);
            out_ << '\n' << "  " << std::string(14 + targets.size() * 12, '-') << '\n';
            for (const auto& o : origins) {
                out_ << "  " << pad_right(o, 14);
                for (const auto& t : targets) {
                    const auto rt = environment_.find_route(o, t);
                    out_ << pad_left(rt.reachable ? (format_fixed(rt.distance_m, 0) + "m") : "不可达", 12);
                }
                out_ << '\n';
            }
            continue;
        }
        std::istringstream iss(line);
        std::string from, to;
        if (!(iss >> from >> to)) {
            out_ << style(ansi::yellow, "格式示例: L_T1 L_FAR  或输入 matrix\n");
            continue;
        }
        const auto route = environment_.find_route(from, to);
        if (!route.reachable) {
            out_ << style(ansi::bold_red, "✗ 路线不可达: " + from + " -> " + to) << '\n';
        } else {
            out_ << style(ansi::bold_green, "✓ 最短距离: " + format_fixed(route.distance_m, 1) + " 米")
                 << " | 节点跳数: " << (route.path.size() > 0 ? route.path.size() - 1 : 0) << '\n'
                 << "  拓扑路径: " << style(ansi::bright_cyan, join_path(route.path, " ==> ")) << '\n'
                 << "  预计步行: 慢速(50m/m)=" << static_cast<int>(std::ceil(route.distance_m / 50.0)) << "分 | "
                 << "常速(60m/m)=" << static_cast<int>(std::ceil(route.distance_m / 60.0)) << "分 | "
                 << "快速(80m/m)=" << static_cast<int>(std::ceil(route.distance_m / 80.0)) << "分\n";
        }
    }
}

namespace {
std::filesystem::path new_output_directory(const std::filesystem::path& parent, const std::string& prefix) {
    std::filesystem::create_directories(parent);
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto candidate = parent / (prefix + "-" + std::to_string(stamp) + "-" + std::to_string(attempt));
        if (std::filesystem::create_directory(candidate)) return candidate;
    }
    throw ValidationError("无法创建个人输出目录");
}
}  // namespace

void ConsoleWorkbench::menu_scenario_editor() {
    out_ << "\n校园场景编辑 · 保存到个人目录\n";
    while (true) {
        out_ << "\n  当前沙盒: " << environment_.scenario().locations.size() << " 地点 | "
             << environment_.scenario().roads.size() << " 道路 | "
             << environment_.scenario().facilities.size() << " 食堂 | "
             << environment_.scenario().seat_pools.size() << " 座位池"
             << (scenario_dirty_ ? style(ansi::bold_yellow, " [已修改*]") : "") << '\n'
             << "  [1] 新增或更新无向道路 (set_road)      [2] 拆除/封闭道路 (remove_road)\n"
             << "  [3] 修改食堂窗口数与默认耗时           [4] 修改或新增地点有限座位池容量\n"
             << "  [5] 新增地点 (add_location)            [6] 删除无引用地点 (remove_location)\n"
             << "  [7] 保存沙盒场景至新目录               [8] 放弃沙盒修改并重载原磁盘场景\n"
             << "  [0] 返回主菜单 (修改立即对 [2] 仿真看板生效)\n";

        std::string cmd;
        if (!prompt_line("editor> ", cmd) || cmd.empty() || cmd == "0" || cmd == "q") break;
        try {
            if (cmd == "1") {
                std::string line;
                if (!prompt_line("输入 '起点ID 终点ID 距离米数' (如 L_T1 L_FAR 160)> ", line)) continue;
                std::istringstream iss(line);
                Road r;
                if (!(iss >> r.from >> r.to >> r.distance_m)) throw ValidationError("输入格式有误");
                environment_.set_road(r);
                scenario_dirty_ = true;
                cached_run_.reset();
                out_ << style(ansi::bright_green, "✓ 已设置无向道路 " + r.from + " <-> " + r.to + " (" + format_fixed(r.distance_m, 1) + "m)\n");
            } else if (cmd == "2") {
                std::string line;
                if (!prompt_line("输入要封闭/删除的道路两端 '起点ID 终点ID'> ", line)) continue;
                std::istringstream iss(line);
                std::string a, b;
                if (!(iss >> a >> b)) throw ValidationError("输入格式有误");
                environment_.remove_road(a, b);
                scenario_dirty_ = true;
                cached_run_.reset();
                out_ << style(ansi::bright_green, "✓ 已封闭道路 " + a + " <-> " + b + "（可前往 [2] 观察绕行效果）\n");
            } else if (cmd == "3") {
                std::string line;
                if (!prompt_line("输入 '食堂ID 绑定地点ID 窗口数 默认服务分钟' (如 F_NEAR L_NEAR 4 2)> ", line)) continue;
                std::istringstream iss(line);
                FacilityConfig fc;
                if (!(iss >> fc.id >> fc.location_id >> fc.window_count >> fc.service_minutes)) {
                    throw ValidationError("输入格式有误");
                }
                environment_.set_facility(fc);
                scenario_dirty_ = true;
                cached_run_.reset();
                out_ << style(ansi::bright_green, "✓ 已更新食堂配置 " + fc.id + " (同质窗口数=" + std::to_string(fc.window_count) + ")\n");
            } else if (cmd == "4") {
                std::string line;
                if (!prompt_line("输入 '地点ID 座位池容量' (如 L_LIBRARY 8)> ", line)) continue;
                std::istringstream iss(line);
                SeatPoolConfig sp;
                if (!(iss >> sp.location_id >> sp.capacity)) throw ValidationError("输入格式有误");
                auto next = environment_.scenario();
                auto found = std::find_if(next.seat_pools.begin(), next.seat_pools.end(),
                                          [&](const SeatPoolConfig& p) { return p.location_id == sp.location_id; });
                if (found == next.seat_pools.end()) next.seat_pools.push_back(sp);
                else found->capacity = sp.capacity;
                environment_.replace_scenario(std::move(next));
                scenario_dirty_ = true;
                cached_run_.reset();
                out_ << style(ansi::bright_green, "✓ 已设置地点 " + sp.location_id + " 有限座位容量为 " + std::to_string(sp.capacity) + "\n");
            } else if (cmd == "5") {
                std::string id, name, kind_str, cap_str;
                if (!prompt_line("新地点 ID (如 L_GYM)> ", id) || id.empty()) continue;
                if (!prompt_line("地点名称 (如 West_Annex)> ", name) || name.empty()) continue;
                if (!prompt_line("地点类型 (Teaching / Canteen / Dormitory / Library / Sports)> ", kind_str)) continue;
                if (!prompt_line("描述容量 (正整数, 如 150)> ", cap_str)) continue;
                Location loc;
                loc.id = id;
                loc.name = name;
                if (kind_str == "Teaching") loc.kind = LocationKind::Teaching;
                else if (kind_str == "Canteen") loc.kind = LocationKind::Canteen;
                else if (kind_str == "Dormitory") loc.kind = LocationKind::Dormitory;
                else if (kind_str == "Library") loc.kind = LocationKind::Library;
                else if (kind_str == "Sports") loc.kind = LocationKind::Sports;
                else throw ValidationError("地点类型必须为 Teaching, Canteen, Dormitory, Library 或 Sports");
                std::size_t cap = 0;
                if (!parse_int(cap_str, cap) || cap == 0) throw ValidationError("容量必须为正整数");
                loc.capacity = cap;
                environment_.add_location(std::move(loc));
                scenario_dirty_ = true;
                cached_run_.reset();
                out_ << style(ansi::bright_green, "✓ 已新增地点 " + id + " (" + kind_en(loc.kind) + ")\n");
            } else if (cmd == "6") {
                std::string id;
                if (!prompt_line("输入要删除的地点 ID> ", id) || id.empty()) continue;
                environment_.remove_location(id);
                scenario_dirty_ = true;
                cached_run_.reset();
                out_ << style(ansi::bright_green, "✓ 已删除地点 " + id + "\n");
            } else if (cmd == "7") {
                const auto target = new_output_directory(user_directory() / "scenarios", "campus");
                environment_.save_directory(target);
                scenario_dirty_ = false;
                out_ << style(ansi::bright_green, "✓ 场景已原子校验并保存至: " + target.u8string() + "\n");
            } else if (cmd == "8") {
                environment_.load_directory(scenario_path_);
                scenario_dirty_ = false;
                reload_population();
                out_ << style(ansi::bright_green, "✓ 已放弃未保存修改，恢复原磁盘场景。\n");
            }
        } catch (const std::exception& ex) {
            out_ << style(ansi::bold_red, std::string("校验拒绝 (原场景保持完好): ") + ex.what()) << '\n';
        }
    }
}

}  // namespace campustwin
