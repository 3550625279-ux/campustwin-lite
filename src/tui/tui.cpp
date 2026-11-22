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
#endif

#include "campustwin/tui.hpp"
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
    }
    return "未知";
}

const char* kind_en(LocationKind kind) {
    switch (kind) {
    case LocationKind::Teaching: return "Teaching";
    case LocationKind::Canteen: return "Canteen";
    case LocationKind::Dormitory: return "Dormitory";
    case LocationKind::Library: return "Library";
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

const char* role_name(UserRole role) {
    switch (role) {
    case UserRole::Guest: return "guest";
    case UserRole::Student: return "student";
    case UserRole::Admin: return "admin";
    }
    return "guest";
}

const char* role_label_zh(UserRole role) {
    switch (role) {
    case UserRole::Guest: return "访客 (只读浏览/仿真/查询)";
    case UserRole::Student: return "普通用户 (仿真/实验/数据导出)";
    case UserRole::Admin: return "系统管理员 (全权限含场景编辑)";
    }
    return "访客";
}

RoleStore::RoleStore(std::filesystem::path file) : file_(std::move(file)) {
    std::error_code ec;
    if (file_.empty() || !std::filesystem::exists(file_, ec) || ec) return;
    std::ifstream in(file_, std::ios::binary);
    if (!in) return;
    std::string line;
    if (!std::getline(in, line)) return;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty()) continue;
        const auto comma = line.find(',');
        if (comma == std::string::npos) continue;
        const auto user = line.substr(0, comma);
        const auto role_str = line.substr(comma + 1);
        if (role_str == "admin") roles_[user] = UserRole::Admin;
        else if (role_str == "student") roles_[user] = UserRole::Student;
    }
}

UserRole RoleStore::role_of(const std::string& username) const {
    const auto found = roles_.find(username);
    if (found != roles_.end()) return found->second;
    if (username == "admin") return UserRole::Admin;
    return UserRole::Student;
}

void RoleStore::set_role(const std::string& username, UserRole role) {
    roles_[username] = role;
    save();
}

void RoleStore::save() const {
    if (file_.empty()) return;
    std::error_code ec;
    if (!file_.parent_path().empty()) {
        std::filesystem::create_directories(file_.parent_path(), ec);
    }
    std::ofstream out(file_, std::ios::binary | std::ios::trunc);
    if (!out) throw ValidationError("cannot write role file: " + file_.u8string());
    out << "username,role\n";
    for (const auto& [user, role] : roles_) {
        out << user << ',' << role_name(role) << '\n';
    }
}

SimTime RecordedRun::peak_congestion_minute() const {
    SimTime best_tick = 0;
    std::size_t best_score = 0;
    for (const auto& frame : frames) {
        std::size_t score = 0;
        for (const auto& f : frame.facilities) score += f.queue_length * 2 + f.in_service;
        for (const auto& s : frame.seats) score += s.queued * 2 + s.occupied;
        if (score > best_score) {
            best_score = score;
            best_tick = frame.at;
        }
    }
    return best_tick;
}

std::size_t RecordedRun::finished_students_at(SimTime tick) const {
    if (tick < 0 || static_cast<std::size_t>(tick) >= frames.size()) return 0;
    const auto& snaps = frames[static_cast<std::size_t>(tick)].students;
    return static_cast<std::size_t>(std::count_if(snaps.begin(), snaps.end(), [](const StudentSnapshot& s) {
        return s.state == StudentState::Finished;
    }));
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
    frame.students = students;
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
        if (fs::exists(candidate / "data/scenarios/mvp/locations.csv", ec) && !ec) {
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
      accounts_(account_file_),
      roles_(account_file_.parent_path() / "roles.csv") {
    if (options_.scenario_dir || options_.students_file || options_.schedule_file) {
        active_preset_title_ = "自定义命令行参数配置";
        scenario_path_ = options_.scenario_dir ? *options_.scenario_dir : (root_ / "data/scenarios/mvp");
        students_path_ = options_.students_file ? *options_.students_file : (root_ / "data/students/mvp.csv");
        schedule_path_ = options_.schedule_file;
        settings_.duration_minutes = options_.duration_minutes.value_or(schedule_path_ ? 160 : 120);
        policy_override_ = options_.policy_override;
        environment_.load_directory(scenario_path_);
        reload_population();
    } else {
        // Default to Stage-3 flagship scenario (campus_day) if present, else MVP.
        const auto list = presets();
        std::size_t idx = list.size() >= 5 ? 4 : 0;
        load_preset(idx);
        if (options_.duration_minutes) settings_.duration_minutes = *options_.duration_minutes;
        if (options_.policy_override) {
            policy_override_ = options_.policy_override;
            reload_population();
        }
    }
}

std::string ConsoleWorkbench::style(const char* ansi_code, const std::string& text) const {
    if (!color_) return text;
    return std::string(ansi_code) + text + ansi::reset;
}

std::vector<ConsoleWorkbench::Preset> ConsoleWorkbench::presets() const {
    return {
        {"mvp",
         "MVP 经典午餐手算基准",
         "4地点 / 20学生 / 1食堂2窗口 / 验证170人·分钟标准手算队列",
         "data/scenarios/mvp",
         "data/students/mvp.csv",
         std::nullopt,
         120},
        {"two_canteens",
         "双食堂扩容与步速分析",
         "5地点 / 40学生 / 远近双食堂 / 对比增加窗口与提升步速",
         "data/scenarios/two_canteens",
         "data/students/two_canteens.csv",
         std::nullopt,
         120},
        {"dynamic_campus",
         "阶段一：动态拥堵感知选食堂",
         "8地点 / 60学生 / 对比 fixed·nearest·fastest 与 t-1 冻结预测",
         "data/scenarios/dynamic_campus",
         "data/students/dynamic_campus.csv",
         std::nullopt,
         120},
        {"service_campus",
         "阶段二：异质窗口与餐品增广匹配",
         "11地点 / 60学生 / 3食堂9窗口 / 米饭·面食·现炒异质兼容分配",
         "data/scenarios/service_campus",
         "data/students/service_campus.csv",
         std::nullopt,
         120},
        {"campus_day",
         "阶段三：全天多活动日程与有限座位 (旗舰)",
         "16地点 / 38道路 / 36学生 / 216项课程·午餐·自习·归寝活动 / 6处座位池",
         "data/scenarios/campus_day",
         "data/students/campus_day.csv",
         std::filesystem::path("data/schedules/campus_day.csv"),
         160},
    };
}

void ConsoleWorkbench::load_preset(std::size_t index) {
    const auto list = presets();
    if (index >= list.size()) throw ValidationError("invalid preset index");
    const auto& p = list[index];
    scenario_path_ = root_ / p.scenario_rel;
    students_path_ = root_ / p.students_rel;
    schedule_path_ = p.schedule_rel ? std::optional<std::filesystem::path>(root_ / *p.schedule_rel) : std::nullopt;
    settings_.duration_minutes = p.default_duration;
    policy_override_.reset();
    environment_.load_directory(scenario_path_);
    scenario_dirty_ = false;
    active_preset_title_ = p.title;
    reload_population();
}

void ConsoleWorkbench::reload_population() {
    auto loaded = read_student_specs(students_path_);
    if (policy_override_) {
        for (auto& s : loaded) s.lunch_policy = *policy_override_;
    }
    if (schedule_path_) {
        loaded = read_schedules(*schedule_path_, loaded);
    }
    // Validate compatibility against current scenario early via a dry reset on StudentEngine.
    StudentEngine validator;
    validator.reset(environment_.scenario(), loaded);
    population_ = std::move(loaded);
    cached_run_.reset();
}

RecordedRun ConsoleWorkbench::execute_simulation(
    const CampusEnvironment& env,
    std::vector<StudentSpec> pop,
    SimTime duration,
    const std::optional<std::filesystem::path>& csv_output) const {
    SimulationSettings run_settings{duration};
    StudentEngine students;
    FacilityEngine facilities;
    SeatEngine seats;
    InMemoryRunObserver memory_writer;
    memory_writer.set_population(pop);

    if (csv_output) {
        const bool detailed = (!pop.empty() && !pop.front().activities.empty()) || !env.scenario().seat_pools.empty();
        CsvRunWriter file_writer(*csv_output, detailed);
        CompositeRunWriter composite;
        composite.add_writer(memory_writer);
        composite.add_writer(file_writer);
        SimulationRunner{}.run(run_settings, env, pop, students, facilities, composite, &seats);
    } else {
        SimulationRunner{}.run(run_settings, env, pop, students, facilities, memory_writer, &seats);
    }
    return memory_writer.take_run();
}

void ConsoleWorkbench::ensure_run_cached() {
    if (!cached_run_) {
        cached_run_ = execute_simulation(environment_, population_, settings_.duration_minutes);
    }
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

    const std::string user_badge = current_user_.empty()
        ? style(ansi::yellow, "[未登录 · 访客 Guest]")
        : (current_role_ == UserRole::Admin
               ? style(ansi::bold_green, "[管理员 Admin: " + current_user_ + "]")
               : style(ansi::bright_cyan, "[已登录 Student: " + current_user_ + "]"));

    out_ << "\n" << style(ansi::bold, "▸ 当前工作台状态") << "  " << user_badge << '\n';
    out_ << "  当前方案 : " << style(ansi::bold_white, active_preset_title_)
         << (scenario_dirty_ ? style(ansi::bold_yellow, "  [沙盒已修改·未保存*]") : "") << '\n';
    out_ << "  校园拓扑 : " << sc.locations.size() << " 个地点 | "
         << sc.roads.size() << " 条双向道路 | "
         << sc.facilities.size() << " 座食堂 (" << total_windows << " 个窗口) | "
         << sc.seat_pools.size() << " 处有限座位池\n";
    out_ << "  学生配置 : " << population_.size() << " 名学生 | "
         << (total_activities > 0 ? ("有序多活动日程 (" + std::to_string(total_activities) + " 项活动)") : "经典单次午餐与返程")
         << " | 时长 " << settings_.duration_minutes << " 分钟"
         << " | 策略: " << (policy_override_ ? policy_text(*policy_override_) : "按学生CSV配置") << '\n';
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
        out_ << "  仿真缓存 : " << style(ansi::dim, "待运行 (选择 [2] 或 [3] 自动执行毫秒级引擎计算)") << '\n';
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

int ConsoleWorkbench::run() {
    print_banner();
    while (true) {
        print_status_card();
        out_ << "\n" << style(ansi::bold, "▸ 主功能菜单 (输入编号回车)") << '\n'
             << "  " << style(ansi::bold_cyan, "[1]") << " 切换预设场景 / 配置仿真参数        "
             << style(ansi::dim, "(5套官方合成校园场景一键切换)") << '\n'
             << "  " << style(ansi::bold_cyan, "[2]") << " 运行仿真 · 实时逐分钟动态看板      "
             << style(ansi::dim, "(动画播放 / 峰值跳转 / 窗口与座位队列监控)") << '\n'
             << "  " << style(ansi::bold_cyan, "[3]") << " 学生个体轨迹 · 选食堂决策与日程透视 "
             << style(ansi::dim, "(单人全天甘特时间轴 / 候选食堂打分表)") << '\n'
             << "  " << style(ansi::bold_cyan, "[4]") << " 控制变量对照实验中心               "
             << style(ansi::dim, "(策略对比 / 专用vs通用窗口 / 扩容与封路实验)") << '\n'
             << "  " << style(ansi::bold_cyan, "[5]") << " 校园路网拓扑与 Dijkstra 最短路导航  "
             << style(ansi::dim, "(地点设施总览 / 任意两点路径与距离矩阵)") << '\n'
             << "  " << style(ansi::bold_cyan, "[6]") << " 校园场景沙盒在线编辑器             "
             << style(ansi::dim, "(增删改地点·道路·窗口·座位池, 需管理员权限)") << '\n'
             << "  " << style(ansi::bold_cyan, "[7]") << " 本地账号认证与角色权限中心         "
             << style(ansi::dim, "(PBKDF2-SHA256 加密存储 / 一键演示管理员)") << '\n'
             << "  " << style(ansi::bold_cyan, "[8]") << " 导出标准 CSV 结果 / 生成离线 HTML  "
             << style(ansi::dim, "(输出5/8份CSV契约文件或全阶段网页报告)") << '\n'
             << "  " << style(ansi::bold_yellow, "[0]") << " 退出系统 (quit)\n\n";

        std::string choice;
        if (!prompt_line("campustwin> ", choice)) {
            break;
        }
        if (choice.empty()) continue;
        try {
            if (choice == "0" || choice == "q" || choice == "quit" || choice == "exit") {
                if (scenario_dirty_) {
                    out_ << style(ansi::yellow, "提示: 当前沙盒场景存在未保存修改。再次输入 quit 确认退出，或去 [6] 保存。\n");
                    std::string confirm;
                    if (!prompt_line("确认退出? (y/N)> ", confirm)) break;
                    if (confirm != "y" && confirm != "Y" && confirm != "quit") continue;
                }
                out_ << style(ansi::bright_green, "已退出 CampusTwin Lite 交互工作台。\n");
                break;
            }
            if (choice == "1") menu_presets();
            else if (choice == "2") menu_simulation_playback();
            else if (choice == "3") menu_student_inspector();
            else if (choice == "4") menu_experiments();
            else if (choice == "5") menu_map_and_routes();
            else if (choice == "6") menu_scenario_editor();
            else if (choice == "7") menu_accounts();
            else if (choice == "8") menu_export();
            else {
                out_ << style(ansi::red, "未知菜单编号: " + choice + "，请输入 0-8。\n");
            }
        } catch (const std::exception& ex) {
            out_ << style(ansi::bold_red, std::string("操作异常拦截: ") + ex.what()) << '\n';
        }
    }
    return 0;
}

void ConsoleWorkbench::menu_presets() {
    const auto list = presets();
    out_ << "\n" << style(ansi::bold_cyan, "─── [1] 切换预设校园场景与仿真参数 ───") << '\n';
    for (std::size_t i = 0; i < list.size(); ++i) {
        out_ << "  " << style(ansi::bold_white, "[" + std::to_string(i + 1) + "] ")
             << pad_right(list[i].title, 36) << " " << style(ansi::dim, list[i].subtitle) << '\n';
    }
    out_ << "  " << style(ansi::bold_white, "[6] ") << "修改当前仿真时长 / 全局选食堂策略覆盖 (fixed | nearest | fastest)\n"
         << "  " << style(ansi::bold_white, "[7] ") << "手动输入自定义场景目录与学生/日程 CSV 路径\n"
         << "  " << style(ansi::dim, "[0] 返回主菜单") << '\n';

    std::string input;
    if (!prompt_line("选择预设或操作 [0-7]> ", input) || input.empty() || input == "0") return;
    int sel = 0;
    if (!parse_int(input, sel) || sel < 1 || sel > 7) {
        out_ << style(ansi::red, "无效选项。\n");
        return;
    }
    if (sel >= 1 && sel <= 5) {
        load_preset(static_cast<std::size_t>(sel - 1));
        out_ << style(ansi::bright_green, "✓ 已加载预设场景: " + active_preset_title_) << '\n';
        return;
    }
    if (sel == 6) {
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
        out_ << style(ansi::bright_green, "✓ 运行参数已更新。\n");
        return;
    }
    if (sel == 7) {
        std::string sc_dir, st_file, sch_file;
        if (!prompt_line("场景目录路径 (如 data/scenarios/campus_day)> ", sc_dir) || sc_dir.empty()) return;
        if (!prompt_line("学生 CSV 路径 (如 data/students/campus_day.csv)> ", st_file) || st_file.empty()) return;
        if (!prompt_line("日程 CSV 路径 (可选，直接回车表示不使用日程)> ", sch_file)) return;
        CampusEnvironment next_env;
        const auto resolved_sc = std::filesystem::exists(std::filesystem::u8path(sc_dir))
            ? std::filesystem::u8path(sc_dir)
            : (root_ / std::filesystem::u8path(sc_dir));
        const auto resolved_st = std::filesystem::exists(std::filesystem::u8path(st_file))
            ? std::filesystem::u8path(st_file)
            : (root_ / std::filesystem::u8path(st_file));
        next_env.load_directory(resolved_sc);
        auto next_pop = read_student_specs(resolved_st);
        std::optional<std::filesystem::path> resolved_sch;
        if (!sch_file.empty()) {
            resolved_sch = std::filesystem::exists(std::filesystem::u8path(sch_file))
                ? std::filesystem::u8path(sch_file)
                : (root_ / std::filesystem::u8path(sch_file));
            next_pop = read_schedules(*resolved_sch, next_pop);
        }
        StudentEngine check;
        check.reset(next_env.scenario(), next_pop);
        environment_ = std::move(next_env);
        scenario_path_ = resolved_sc;
        students_path_ = resolved_st;
        schedule_path_ = resolved_sch;
        population_ = std::move(next_pop);
        scenario_dirty_ = false;
        active_preset_title_ = "自定义场景 (" + resolved_sc.filename().u8string() + ")";
        cached_run_.reset();
        out_ << style(ansi::bright_green, "✓ 自定义配置加载并校验通过！\n");
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
        out_ << "  日程统计: 已完成活动 " << run.completed_activities_count() << "/" << run.activities.size()
             << " | 迟到课程数: " << style(ansi::yellow, std::to_string(run.late_classes_count()))
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

void ConsoleWorkbench::render_tick_dashboard(const RecordedRun& run, SimTime tick, bool clear_frame) const {
    if (tick < 0 || static_cast<std::size_t>(tick) >= run.frames.size()) return;
    const auto& frame = run.frames[static_cast<std::size_t>(tick)];
    if (clear_frame && color_) {
        out_ << "\x1b[2J\x1b[H";
    }

    const double progress = run.settings.duration_minutes > 0
        ? static_cast<double>(tick) / static_cast<double>(run.settings.duration_minutes)
        : 1.0;

    out_ << style(ansi::bold_cyan, "================================================================================") << '\n';
    out_ << "  " << style(ansi::bold_white, "实时仿真态势看板") << "  | 时钟 "
         << style(ansi::bold_yellow, "t = " + pad_left(std::to_string(tick), 3) + " / " + std::to_string(run.settings.duration_minutes) + " min")
         << "  " << make_bar(progress, 22, color_, ansi::bright_cyan) << " " << format_fixed(progress * 100.0, 1) << "%\n";
    out_ << style(ansi::bold_cyan, "================================================================================") << '\n';

    // 1. 8-State Conservation Summary
    std::map<StudentState, std::size_t> state_counts;
    std::map<LocationId, std::size_t> loc_counts;
    std::map<std::string, std::size_t> road_counts;
    for (const auto& s : frame.students) {
        ++state_counts[s.state];
        if (s.location_id) {
            ++loc_counts[*s.location_id];
        } else if (s.move) {
            ++road_counts[s.move->from + " -> " + s.move->to];
        }
    }

    out_ << "  " << style(ansi::bold, "▸ 学生状态守恒分布 (总人数 = " + std::to_string(frame.students.size()) + ")") << '\n';
    out_ << "    原地等待:" << pad_left(std::to_string(state_counts[StudentState::AtLocation]), 3)
         << " | 步行途中:" << style(ansi::bright_cyan, pad_left(std::to_string(state_counts[StudentState::Moving]), 3))
         << " | 窗口排队:" << style(ansi::bright_yellow, pad_left(std::to_string(state_counts[StudentState::Queued]), 3))
         << " | 正在取餐:" << style(ansi::magenta, pad_left(std::to_string(state_counts[StudentState::InService]), 3)) << '\n'
         << "    等候座位:" << style(ansi::bright_red, pad_left(std::to_string(state_counts[StudentState::WaitingSeat]), 3))
         << " | 正在就餐:" << style(ansi::bright_green, pad_left(std::to_string(state_counts[StudentState::Eating]), 3))
         << " | 课程/自习:" << style(ansi::cyan, pad_left(std::to_string(state_counts[StudentState::InActivity]), 3))
         << " | 全部完成:" << style(ansi::green, pad_left(std::to_string(state_counts[StudentState::Finished]), 3)) << '\n';

    // 2. Facility Windows & Queues
    std::map<FacilityId, std::size_t> win_cap;
    for (const auto& fc : run.scenario.facilities) win_cap[fc.id] = fc.window_count;

    out_ << "\n  " << style(ansi::bold, "▸ 食堂窗口负载与排队实时监控") << '\n';
    for (const auto& f : frame.facilities) {
        const auto cap = win_cap[f.facility_id];
        const double ratio = cap > 0 ? static_cast<double>(f.in_service) / static_cast<double>(cap) : 0.0;
        const std::string q_bar(std::min<std::size_t>(f.queue_length, 20), '|');
        out_ << "    " << pad_right(f.facility_id, 10)
             << " 窗口 " << make_bar(ratio, 8, color_, ansi::magenta) << " "
             << f.in_service << "/" << cap
             << " | 队列: " << pad_right(style(ansi::bright_yellow, q_bar) + (f.queue_length > 20 ? "+" : ""), 22)
             << " (" << pad_left(std::to_string(f.queue_length), 2) << "人, 峰值" << pad_left(std::to_string(f.max_queue_length), 2) << ")"
             << " | 完成:" << pad_left(std::to_string(f.completed), 2) << "/" << f.arrived << '\n';
    }

    // 3. Finite Seat Pools
    if (!frame.seats.empty()) {
        out_ << "\n  " << style(ansi::bold, "▸ 有限座位池实时监控 (食堂 & 图书馆)") << '\n';
        for (const auto& s : frame.seats) {
            const double ratio = s.capacity > 0 ? static_cast<double>(s.occupied) / static_cast<double>(s.capacity) : 0.0;
            const std::string q_bar(std::min<std::size_t>(s.queued, 16), '!');
            out_ << "    " << pad_right(s.location_id, 12)
                 << " 占座 " << make_bar(ratio, 8, color_, ansi::bright_green) << " "
                 << s.occupied << "/" << s.capacity
                 << " | 等座: " << pad_right(style(ansi::bright_red, q_bar), 17)
                 << " (" << pad_left(std::to_string(s.queued), 2) << "人, 峰值" << pad_left(std::to_string(s.max_queue), 2) << ")"
                 << " | 累计入座:" << pad_left(std::to_string(s.granted), 2) << '\n';
        }
    }

    // 4. Campus Location & Moving Traffic Hotspots
    out_ << "\n  " << style(ansi::bold, "▸ 校园驻留与在途分布") << '\n';
    out_ << "    驻留热点: ";
    std::vector<std::pair<LocationId, std::size_t>> sorted_locs(loc_counts.begin(), loc_counts.end());
    std::sort(sorted_locs.begin(), sorted_locs.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    if (sorted_locs.empty()) {
        out_ << "无驻留人员";
    } else {
        for (std::size_t i = 0; i < std::min<std::size_t>(sorted_locs.size(), 6); ++i) {
            if (i > 0) out_ << "  ";
            out_ << sorted_locs[i].first << "(" << sorted_locs[i].second << ")";
        }
        if (sorted_locs.size() > 6) out_ << " ...";
    }
    out_ << "\n    在途路段: ";
    if (road_counts.empty()) {
        out_ << style(ansi::dim, "当前分钟无人步行移动");
    } else {
        std::size_t shown = 0;
        for (const auto& [seg, cnt] : road_counts) {
            if (shown++ >= 4) {
                out_ << " ...";
                break;
            }
            if (shown > 1) out_ << " | ";
            out_ << style(ansi::bright_cyan, seg) << "(" << cnt << "人)";
        }
    }
    out_ << '\n';

    // 5. Live Minute Events
    out_ << "\n  " << style(ansi::bold, "▸ 本分钟关键离散事件流 (t = " + std::to_string(tick) + ")") << '\n';
    std::vector<std::string> logs;
    for (const auto& d : frame.decisions) {
        if (d.selected) {
            std::string detail = d.student_id + " 按[" +
                (d.policy == LunchPolicy::Fixed ? "fixed" : d.policy == LunchPolicy::Nearest ? "nearest" : "fastest") +
                "]选定 " + d.facility_id;
            if (d.estimated_total_minutes) {
                detail += " (预计总耗时 " + std::to_string(*d.estimated_total_minutes) + "m)";
            }
            logs.push_back(style(ansi::bright_cyan, "[决策] ") + detail);
        }
    }
    for (const auto& a : frame.arrivals) {
        logs.push_back(style(ansi::yellow, "[到达] ") + a.student_id + " 抵达 " + a.facility_id + " 请求餐品(" + a.meal_type + ")");
    }
    for (const auto& st : frame.started) {
        logs.push_back(style(ansi::magenta, "[取餐] ") + st.student_id + " 进入 " + st.facility_id +
                       "#W" + std::to_string(st.window_id) + " (排队等待 " + std::to_string(st.wait_minutes) + "m)");
    }
    for (const auto& c : frame.completed) {
        logs.push_back(style(ansi::green, "[出餐] ") + c.student_id + " 在 " + c.facility_id +
                       "#W" + std::to_string(c.window_id) + " 完成取餐");
    }
    for (const auto& g : frame.seat_grants) {
        logs.push_back(style(ansi::bright_green, "[入座] ") + g.request.student_id + " 获得 " + g.request.location_id +
                       " 座位 [占用至 t=" + std::to_string(g.until) + ", 等座 " + std::to_string(g.at - g.request.at) + "m]");
    }
    if (logs.empty()) {
        out_ << "    " << style(ansi::dim, "(本分钟无新触发的离散到达/分配事件，各状态计时器平稳推进)") << '\n';
    } else {
        const std::size_t limit = 8;
        for (std::size_t i = 0; i < std::min(logs.size(), limit); ++i) {
            out_ << "    • " << logs[i] << '\n';
        }
        if (logs.size() > limit) {
            out_ << "    " << style(ansi::dim, "... 以及其余 " + std::to_string(logs.size() - limit) + " 条并发事件") << '\n';
        }
    }
}

void ConsoleWorkbench::menu_simulation_playback() {
    const auto t0 = std::chrono::steady_clock::now();
    cached_run_ = execute_simulation(environment_, population_, settings_.duration_minutes);
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    out_ << style(ansi::bright_green, "\n✓ 引擎仿真执行完毕 (耗时 " + std::to_string(elapsed_ms) + " ms，每分钟守恒断言全部通过)") << '\n';
    print_run_summary(*cached_run_);

    SimTime cursor = cached_run_->peak_congestion_minute();
    out_ << "\n" << style(ansi::bold_cyan, "▸ 进入交互式时间轴播放器 (默认定位至拥堵峰值时刻 t=" + std::to_string(cursor) + ")") << '\n';

    while (true) {
        render_tick_dashboard(*cached_run_, cursor, false);
        out_ << "\n  " << style(ansi::dim, "控制指令: [回车/n]下一分  [b]上一分  [+/-]快进退10分  [k]跳至拥堵峰值  [g 分钟]跳转  [p 步数]动画连播  [s]汇总表  [q]返回") << '\n';
        std::string cmd;
        if (!prompt_line("playback(t=" + std::to_string(cursor) + ")> ", cmd)) break;
        if (cmd == "q" || cmd == "0" || cmd == "quit") break;
        if (cmd.empty() || cmd == "n") {
            cursor = std::min<SimTime>(cached_run_->settings.duration_minutes, cursor + 1);
            continue;
        }
        if (cmd == "b") {
            cursor = std::max<SimTime>(0, cursor - 1);
            continue;
        }
        if (cmd == "+") {
            cursor = std::min<SimTime>(cached_run_->settings.duration_minutes, cursor + 10);
            continue;
        }
        if (cmd == "-") {
            cursor = std::max<SimTime>(0, cursor - 10);
            continue;
        }
        if (cmd == "k") {
            cursor = cached_run_->peak_congestion_minute();
            continue;
        }
        if (cmd == "s") {
            print_run_summary(*cached_run_);
            continue;
        }
        if (cmd[0] == 'p') {
            std::istringstream iss(cmd.substr(1));
            int steps = 25;
            int delay_ms = 110;
            iss >> steps >> delay_ms;
            steps = std::max(1, std::min(steps, 300));
            delay_ms = std::max(0, std::min(delay_ms, 1000));
            for (int i = 0; i < steps && cursor < cached_run_->settings.duration_minutes; ++i) {
                ++cursor;
                render_tick_dashboard(*cached_run_, cursor, true);
                if (delay_ms > 0 && color_) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                }
            }
            continue;
        }
        std::string target_str = cmd;
        if (cmd.rfind("g ", 0) == 0) target_str = cmd.substr(2);
        SimTime target = 0;
        if (parse_int(target_str, target) && target >= 0 && target <= cached_run_->settings.duration_minutes) {
            cursor = target;
        } else {
            out_ << style(ansi::red, "无法识别的播放指令，请输入 n / b / + / - / k / p / s / q 或 0.." +
                                     std::to_string(cached_run_->settings.duration_minutes) + " 的分钟数。\n");
        }
    }
}

void ConsoleWorkbench::menu_student_inspector() {
    ensure_run_cached();
    const auto& run = *cached_run_;

    out_ << "\n" << style(ansi::bold_cyan, "─── [3] 学生个体全生命周期轨迹、选食堂决策与日程透视 ───") << '\n';
    out_ << "  " << pad_right("学号", 8) << pad_right("初始地点", 12) << pad_right("餐品需求", 10)
         << pad_right("策略", 10) << pad_right("步速(m/m)", 11) << pad_right("选定食堂", 10) << "状态概览\n";
    out_ << "  " << std::string(80, '-') << '\n';

    const auto& final_snaps = run.frames.back().students;
    std::map<StudentId, StudentSnapshot> final_by_id;
    for (const auto& s : final_snaps) final_by_id[s.student_id] = s;

    const std::size_t preview_count = std::min<std::size_t>(run.population.size(), 12);
    for (std::size_t i = 0; i < preview_count; ++i) {
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
    if (run.population.size() > preview_count) {
        out_ << "  " << style(ansi::dim, "... 共 " + std::to_string(run.population.size()) + " 名学生 (如 S001 .. " +
                              run.population.back().id + ")") << '\n';
    }

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
             << " | 策略: " << policy_text(spec.lunch_policy) << '\n';

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
                if (r.started_at) {
                    const auto diff = *r.started_at - r.activity.start_minute;
                    delay_str = diff > 0 ? style(ansi::bold_yellow, "+" + std::to_string(diff) + "m") : "0m";
                }
                const std::string seat_w = r.seat_wait_minutes > 0
                    ? style(ansi::bright_red, "+" + std::to_string(r.seat_wait_minutes) + "m")
                    : "0m";
                out_ << "     " << pad_right(std::to_string(i + 1) + "." + r.activity.id, 14)
                     << pad_right(activity_zh(r.activity.kind), 6)
                     << pad_right(r.destination.empty() ? "-" : r.destination, 12)
                     << pad_left(std::to_string(r.activity.start_minute), 6)
                     << pad_left(opt_t(r.departed_at), 6)
                     << pad_left(opt_t(r.arrived_at), 6)
                     << pad_left(opt_t(r.started_at), 6)
                     << pad_left(opt_t(r.completed_at), 6)
                     << pad_left(delay_str, 8)
                     << pad_left(seat_w, 8)
                     << "  " << join_path(r.path, "->") << '\n';
            }
        }

        // 3. Visual State Strip (sampled to 60 columns)
        out_ << "\n  " << style(ansi::bold, "3) 状态时间轴色带 (0 .. " + std::to_string(run.settings.duration_minutes) + " min)") << '\n';
        out_ << "     图例: [.]等待 [>]移动 [Q]排队 [S]取餐 [W]等座 [E]就餐 [A]上课/自习 [_]已完成\n     [";
        const std::size_t cols = std::min<std::size_t>(64, run.frames.size());
        for (std::size_t c = 0; c < cols; ++c) {
            const std::size_t f_idx = (c * (run.frames.size() - 1)) / std::max<std::size_t>(1, cols - 1);
            const auto& snaps = run.frames[f_idx].students;
            const auto it = std::find_if(snaps.begin(), snaps.end(), [&](const StudentSnapshot& s) {
                return s.student_id == sid;
            });
            if (it == snaps.end()) {
                out_ << ' ';
                continue;
            }
            switch (it->state) {
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
}

void ConsoleWorkbench::menu_experiments() {
    out_ << "\n" << style(ansi::bold_cyan, "─── [4] 控制变量对照实验中心 (内存直接驱动真实 C++ 引擎) ───") << '\n'
         << "  " << style(ansi::bold_white, "[1] ") << "阶段一实验：选食堂策略对照 (fixed vs nearest vs fastest · 60人动态场景)\n"
         << "  " << style(ansi::bold_white, "[2] ") << "阶段二实验：异质窗口分工对照 (专用窗口 Dedicated vs 通用窗口 Flexible · 60人3食堂)\n"
         << "  " << style(ansi::bold_white, "[3] ") << "阶段三实验：全天日程瓶颈与封路对照 (基准 / 食堂座位×2 / 图书馆座位×2 / 封3条路)\n"
         << "  " << style(ansi::bold_white, "[4] ") << "基础敏感性：窗口扩容与步速翻倍对照 (基准 / 近食堂+1窗口 / 全员步速×2 · 40人双食堂)\n"
         << "  " << style(ansi::dim, "[0] 返回主菜单") << '\n';

    std::string input;
    if (!prompt_line("选择要运行的对照实验 [0-4]> ", input) || input.empty() || input == "0") return;
    if (input == "1") run_experiment_policies();
    else if (input == "2") run_experiment_heterogeneous_windows();
    else if (input == "3") run_experiment_schedule_bottlenecks();
    else if (input == "4") run_experiment_lunch_sensitivity();
    else out_ << style(ansi::red, "无效实验编号。\n");
}

void ConsoleWorkbench::run_experiment_policies() {
    CampusEnvironment env;
    env.load_directory(root_ / "data/scenarios/dynamic_campus");
    const auto base_pop = read_student_specs(root_ / "data/students/dynamic_campus.csv");

    struct Row {
        std::string name;
        std::size_t completed{0};
        std::size_t max_q{0};
        std::int64_t total_wait{0};
        double mean_wait{0.0};
        std::map<FacilityId, std::size_t> split;
    };
    std::vector<Row> rows;
    std::int64_t max_wait_seen = 1;

    for (const auto& [pol, label] : std::vector<std::pair<LunchPolicy, std::string>>{
             {LunchPolicy::Fixed, "fixed (预设固定)"},
             {LunchPolicy::Nearest, "nearest (唯距离最近)"},
             {LunchPolicy::Fastest, "fastest (拥堵感知最快)"}}) {
        auto pop = base_pop;
        for (auto& s : pop) s.lunch_policy = pol;
        const auto run = execute_simulation(env, pop, 120);
        Row r;
        r.name = label;
        std::size_t started_sum = 0;
        for (const auto& f : run.summaries) {
            r.completed += f.completed;
            r.max_q = std::max(r.max_q, f.max_queue_length);
            r.total_wait += f.total_wait_minutes;
            started_sum += f.started;
            r.split[f.facility_id] = f.arrived;
        }
        r.mean_wait = started_sum ? static_cast<double>(r.total_wait) / static_cast<double>(started_sum) : 0.0;
        max_wait_seen = std::max(max_wait_seen, r.total_wait);
        rows.push_back(std::move(r));
    }

    out_ << "\n" << style(ansi::bold_green, "★ 实验一结果：三种选食堂策略对照 (dynamic_campus, 60人, 120分钟)") << '\n';
    out_ << "  " << pad_right("策略方案", 24) << pad_left("完成取餐", 10) << pad_left("单食堂峰值队列", 16)
         << pad_left("总等待(m)", 11) << pad_left("平均等待", 10) << "  食堂分流 (F_NEAR : F_FAR) & 等待对比\n";
    out_ << "  " << std::string(96, '-') << '\n';
    for (const auto& r : rows) {
        const double ratio = static_cast<double>(r.total_wait) / static_cast<double>(max_wait_seen);
        const auto near_cnt = r.split.count("F_NEAR") ? r.split.at("F_NEAR") : 0;
        const auto far_cnt = r.split.count("F_FAR") ? r.split.at("F_FAR") : 0;
        out_ << "  " << pad_right(r.name, 24)
             << pad_left(std::to_string(r.completed) + "/60", 10)
             << pad_left(std::to_string(r.max_q), 16)
             << pad_left(std::to_string(r.total_wait), 11)
             << pad_left(format_fixed(r.mean_wait, 2) + "m", 10)
             << "  " << pad_right(std::to_string(near_cnt) + " : " + std::to_string(far_cnt), 9)
             << make_bar(ratio, 14, color_, ansi::bright_yellow) << '\n';
    }
    out_ << "  " << style(ansi::bright_cyan, "结论洞察: nearest 使近距离食堂严重拥堵；fastest 利用 t-1 冻结队列快照主动将后续学生分流至远食堂，显著削峰降等。") << '\n';
}

void ConsoleWorkbench::run_experiment_heterogeneous_windows() {
    CampusEnvironment env_ded;
    env_ded.load_directory(root_ / "data/scenarios/service_campus");
    const auto pop = read_student_specs(root_ / "data/students/service_campus.csv");

    auto sc_flex = env_ded.scenario();
    const std::vector<std::pair<std::string, SimTime>> meals{{"rice", 2}, {"noodle", 3}, {"stirfry", 5}};
    for (auto& fac : sc_flex.facilities) {
        fac.window_services.clear();
        for (std::size_t w = 0; w < fac.window_count; ++w) {
            for (const auto& [m, dur] : meals) {
                fac.window_services.push_back({w, m, dur});
            }
        }
    }
    CampusEnvironment env_flex;
    env_flex.replace_scenario(std::move(sc_flex));

    out_ << "\n" << style(ansi::bold_green, "★ 实验二结果：专用窗口 vs 通用窗口对照 (service_campus, 60人, 3食堂×3窗口, 60分钟)") << '\n';
    out_ << "  " << pad_right("窗口配置方案", 26) << pad_left("完成取餐", 10) << pad_left("全校峰值总队列", 16)
         << pad_left("总等待(m)", 11) << pad_left("平均等待", 10) << "  等待时长直观对比\n";
    out_ << "  " << std::string(90, '-') << '\n';

    struct Item { std::string label; const CampusEnvironment* env; };
    std::vector<std::tuple<std::string, std::size_t, std::size_t, std::int64_t, double>> results;
    std::int64_t max_w = 1;
    for (const auto& item : std::vector<Item>{{"专用窗口 (Dedicated 1窗1品)", &env_ded}, {"通用窗口 (Flexible 窗窗全品)", &env_flex}}) {
        const auto run = execute_simulation(*item.env, pop, 60);
        std::size_t comp = 0, started = 0, max_total_q = 0;
        std::int64_t wait_sum = 0;
        for (const auto& f : run.summaries) {
            comp += f.completed;
            started += f.started;
            wait_sum += f.total_wait_minutes;
        }
        for (const auto& frame : run.frames) {
            std::size_t tq = 0;
            for (const auto& fs : frame.facilities) tq += fs.queue_length;
            max_total_q = std::max(max_total_q, tq);
        }
        const double mw = started ? static_cast<double>(wait_sum) / static_cast<double>(started) : 0.0;
        max_w = std::max(max_w, wait_sum);
        results.emplace_back(item.label, comp, max_total_q, wait_sum, mw);
    }
    for (const auto& [label, comp, mq, ws, mw] : results) {
        out_ << "  " << pad_right(label, 26)
             << pad_left(std::to_string(comp) + "/60", 10)
             << pad_left(std::to_string(mq), 16)
             << pad_left(std::to_string(ws), 11)
             << pad_left(format_fixed(mw, 2) + "m", 10)
             << "  " << make_bar(static_cast<double>(ws) / static_cast<double>(max_w), 16, color_, ansi::magenta) << '\n';
    }
    out_ << "  " << style(ansi::bright_cyan, "结论洞察: 窗口总数与单品制作耗时完全不变时，通用窗口配合二分图增广匹配消除了“面食排队而米饭窗空闲”的结构性闲置。") << '\n';
}

void ConsoleWorkbench::run_experiment_schedule_bottlenecks() {
    CampusEnvironment base_env;
    base_env.load_directory(root_ / "data/scenarios/campus_day");
    auto pop = read_student_specs(root_ / "data/students/campus_day.csv");
    pop = read_schedules(root_ / "data/schedules/campus_day.csv", pop);

    auto make_seat_scaled = [&](const std::set<LocationId>& targets) {
        auto sc = base_env.scenario();
        for (auto& pool : sc.seat_pools) {
            if (targets.count(pool.location_id)) pool.capacity *= 2;
        }
        CampusEnvironment e;
        e.replace_scenario(std::move(sc));
        return e;
    };

    auto sc_detour = base_env.scenario();
    const std::set<std::pair<LocationId, LocationId>> closed{
        {"L_NEAR", "L_T1"}, {"L_LAB", "L_WEST"}, {"L_FAR", "L_LIBRARY_E"}};
    sc_detour.roads.erase(
        std::remove_if(sc_detour.roads.begin(), sc_detour.roads.end(), [&](const Road& r) {
            const auto key = std::minmax(r.from, r.to);
            return closed.count({key.first, key.second}) > 0;
        }),
        sc_detour.roads.end());
    CampusEnvironment env_detour;
    env_detour.replace_scenario(std::move(sc_detour));

    const auto env_dining = make_seat_scaled({"L_NEAR", "L_FAR", "L_WEST"});
    const auto env_study = make_seat_scaled({"L_LIBRARY", "L_LIBRARY_E", "L_LIBRARY_W"});

    struct Variant { std::string name; const CampusEnvironment* env; };
    const std::vector<Variant> variants{
        {"1. 基准场景 (Baseline)", &base_env},
        {"2. 仅食堂座位翻倍 (×2)", &env_dining},
        {"3. 仅图书馆座位翻倍 (×2)", &env_study},
        {"4. 封闭三条主干道路", &env_detour},
    };

    out_ << "\n" << style(ansi::bold_green, "★ 实验三结果：全天多活动日程瓶颈定位与封路冲击 (36人, 216项活动, 160分钟)") << '\n';
    out_ << "  " << pad_right("实验方案", 26) << pad_left("完成活动", 10) << pad_left("迟到课程", 10)
         << pad_left("课程总延迟", 12) << pad_left("总等座时长", 12) << pad_left("总行程(m)", 11)
         << pad_left("全员结束", 10) << "  延迟对比\n";
    out_ << "  " << std::string(106, '-') << '\n';

    for (const auto& v : variants) {
        const auto run = execute_simulation(*v.env, pop, 160);
        double total_dist = 0.0;
        for (const auto& a : run.activities) total_dist += a.distance_m;
        const auto delay = run.total_class_delay_minutes();
        const auto seat_w = run.total_observed_seat_wait();
        const auto end_at = run.all_finished_minute();
        out_ << "  " << pad_right(v.name, 26)
             << pad_left(std::to_string(run.completed_activities_count()) + "/216", 10)
             << pad_left(std::to_string(run.late_classes_count()), 10)
             << pad_left(std::to_string(delay) + "m", 12)
             << pad_left(std::to_string(seat_w) + "m", 12)
             << pad_left(format_fixed(total_dist, 0), 11)
             << pad_left(end_at ? ("t=" + std::to_string(*end_at) + "m") : "未完", 10)
             << "  " << make_bar(static_cast<double>(delay) / 1100.0, 12, color_, ansi::bright_cyan) << '\n';
    }
    out_ << "  " << style(ansi::bright_cyan, "结论洞察: 图书馆座位翻倍使总等座从 885m 骤降至 357m，全员结束从第124分钟提前到第94分钟（核心瓶颈在自习座位而非食堂座位）；") << '\n'
         << "  " << style(ansi::bright_cyan, "          封闭3条道路使总步行距离从 30240m 增至 34800m，虽然错峰使等座略降，但课程总延迟升至 1055m！") << '\n';
}

void ConsoleWorkbench::run_experiment_lunch_sensitivity() {
    CampusEnvironment base_env;
    base_env.load_directory(root_ / "data/scenarios/two_canteens");
    const auto base_pop = read_student_specs(root_ / "data/students/two_canteens.csv");

    auto sc_more = base_env.scenario();
    for (auto& f : sc_more.facilities) {
        if (f.id == "F_NEAR") f.window_count = 3;
    }
    CampusEnvironment env_more;
    env_more.replace_scenario(std::move(sc_more));

    auto fast_pop = base_pop;
    for (auto& s : fast_pop) s.walk_m_per_min *= 2.0;

    struct Setup { std::string name; const CampusEnvironment* env; const std::vector<StudentSpec>* pop; };
    const std::vector<Setup> setups{
        {"基准 (F_NEAR=2窗, 原步速)", &base_env, &base_pop},
        {"扩容 (F_NEAR=3窗, 原步速)", &env_more, &base_pop},
        {"提速 (F_NEAR=2窗, 步速×2)", &base_env, &fast_pop},
    };

    out_ << "\n" << style(ansi::bold_green, "★ 实验四结果：窗口扩容 vs 步行提速对照 (two_canteens, 40人, 120分钟)") << '\n';
    out_ << "  " << pad_right("对照方案", 28) << pad_left("F_NEAR等待", 12) << pad_left("F_NEAR峰值", 12)
         << pad_left("F_FAR等待", 12) << pad_left("F_FAR峰值", 12) << pad_left("全员结束时刻", 14) << '\n';
    out_ << "  " << std::string(92, '-') << '\n';
    for (const auto& st : setups) {
        const auto run = execute_simulation(*st.env, *st.pop, 120);
        std::map<FacilityId, FacilitySummary> by_id;
        for (const auto& s : run.summaries) by_id[s.facility_id] = s;
        const auto done_at = run.all_finished_minute();
        out_ << "  " << pad_right(st.name, 28)
             << pad_left(std::to_string(by_id["F_NEAR"].total_wait_minutes) + "m", 12)
             << pad_left(std::to_string(by_id["F_NEAR"].max_queue_length), 12)
             << pad_left(std::to_string(by_id["F_FAR"].total_wait_minutes) + "m", 12)
             << pad_left(std::to_string(by_id["F_FAR"].max_queue_length), 12)
             << pad_left(done_at ? ("t=" + std::to_string(*done_at) + "m") : "-", 14) << '\n';
    }
    out_ << "  " << style(ansi::bright_cyan, "结论洞察: F_NEAR 增加 1 个窗口将等待从 160m 降至 94m 且完全不影响 F_FAR；而盲目提高步速会使人群更集中到达，反而推高排队峰值！") << '\n';
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
        if (!prompt_line("\n输入 '起点ID 终点ID' 查询最短路，或输入 'matrix' 查看教学楼->食堂距离矩阵 (0返回)> ", line)) break;
        if (line.empty() || line == "0" || line == "q") break;
        if (line == "matrix") {
            std::vector<LocationId> origins, targets;
            for (const auto& l : sc.locations) {
                if (l.kind == LocationKind::Teaching || l.kind == LocationKind::Dormitory) origins.push_back(l.id);
                if (l.kind == LocationKind::Canteen || l.kind == LocationKind::Library) targets.push_back(l.id);
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

void ConsoleWorkbench::menu_scenario_editor() {
    out_ << "\n" << style(ansi::bold_cyan, "─── [6] 校园场景沙盒在线编辑器 (RBAC 权限保护) ───") << '\n';
    if (current_role_ != UserRole::Admin) {
        out_ << style(ansi::bold_yellow, "⚠ 权限拒绝：场景编辑属于受保护管理操作，需要管理员 (Admin) 权限！") << '\n'
             << "  当前身份: " << role_label_zh(current_role_) << '\n'
             << "  提示: 请先前往主菜单 " << style(ansi::bold_white, "[7] 本地账号认证与角色权限中心")
             << " 使用“一键演示管理员登录”或注册管理员账号。\n";
        return;
    }

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
        if (!prompt_line("editor(admin)> ", cmd) || cmd.empty() || cmd == "0" || cmd == "q") break;
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
                if (!prompt_line("地点类型 (Teaching / Canteen / Dormitory / Library)> ", kind_str)) continue;
                if (!prompt_line("描述容量 (正整数, 如 150)> ", cap_str)) continue;
                Location loc;
                loc.id = id;
                loc.name = name;
                if (kind_str == "Teaching") loc.kind = LocationKind::Teaching;
                else if (kind_str == "Canteen") loc.kind = LocationKind::Canteen;
                else if (kind_str == "Dormitory") loc.kind = LocationKind::Dormitory;
                else if (kind_str == "Library") loc.kind = LocationKind::Library;
                else throw ValidationError("地点类型必须为 Teaching, Canteen, Dormitory 或 Library");
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
                std::string out_dir;
                if (!prompt_line("输入保存目标目录 (默认 outputs/edited_scenario)> ", out_dir)) continue;
                if (out_dir.empty()) out_dir = (root_ / "outputs/edited_scenario").u8string();
                const auto target = std::filesystem::u8path(out_dir);
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

void ConsoleWorkbench::menu_accounts() {
    out_ << "\n" << style(ansi::bold_cyan, "─── [7] 本地账号认证与角色权限管理 (AccountStore + RBAC) ───") << '\n'
         << "  安全机制 : PBKDF2-HMAC-SHA256 (200,000 次迭代, 128-bit 随机盐, 常量时间摘要比对, 临时文件原子写入)\n"
         << "  当前会话 : " << (current_user_.empty() ? "未登录" : current_user_) << " | 角色: " << role_label_zh(current_role_) << '\n'
         << "\n  " << style(ansi::bold_white, "[1] ") << "一键体验演示管理员登录 (自动初始化并登录本地 admin 账号，适合现场答辩快速演示)\n"
         << "  " << style(ansi::bold_white, "[2] ") << "注册新本地账号 (register_user)\n"
         << "  " << style(ansi::bold_white, "[3] ") << "使用账号密码登录 (login)\n"
         << "  " << style(ansi::bold_white, "[4] ") << "注销当前登录回到访客模式 (logout)\n"
         << "  " << style(ansi::dim, "[0] 返回主菜单") << '\n';

    std::string choice;
    if (!prompt_line("auth> ", choice) || choice.empty() || choice == "0") return;
    if (choice == "1") {
        const std::string demo_user = "admin";
        const std::string demo_pass = "Campus2026!";
        if (!accounts_.contains(demo_user)) {
            out_ << style(ansi::dim, "正在通过 PBKDF2-HMAC-SHA256 (200,000 iterations) 初始化本地演示管理员...\n");
            accounts_.register_user(demo_user, demo_pass);
            roles_.set_role(demo_user, UserRole::Admin);
        }
        if (accounts_.login(demo_user, demo_pass)) {
            current_user_ = demo_user;
            current_role_ = UserRole::Admin;
            roles_.set_role(demo_user, UserRole::Admin);
            out_ << style(ansi::bold_green, "✓ 已通过 PBKDF2 校验登录为管理员 [admin]！现已解锁 [6] 场景编辑器与 [8] 数据导出。\n");
        } else {
            out_ << style(ansi::yellow, "本地已存在自定义密码的 admin 账号，请使用 [3] 输入密码登录。\n");
        }
    } else if (choice == "2") {
        std::string user, pass, role_sel;
        if (!prompt_line("设置用户名 (3-32位字母/数字/下划线)> ", user)) return;
        if (!prompt_line("设置密码 (至少8位可打印字符)> ", pass)) return;
        if (!prompt_line("选择角色 [1=普通用户 Student, 2=管理员 Admin] (默认 2)> ", role_sel)) return;
        const UserRole r = (role_sel == "1") ? UserRole::Student : UserRole::Admin;
        accounts_.register_user(user, pass);
        roles_.set_role(user, r);
        current_user_ = user;
        current_role_ = r;
        out_ << style(ansi::bold_green, "✓ 注册成功并自动登录为 " + user + " (" + role_label_zh(r) + ")！\n");
    } else if (choice == "3") {
        std::string user, pass;
        if (!prompt_line("用户名> ", user)) return;
        if (!prompt_line("密码> ", pass)) return;
        if (!accounts_.login(user, pass)) {
            throw ValidationError("用户名或密码错误 (PBKDF2 摘要校验未通过)");
        }
        current_user_ = user;
        current_role_ = roles_.role_of(user);
        out_ << style(ansi::bold_green, "✓ 登录成功: " + current_user_ + " (" + role_label_zh(current_role_) + ")\n");
    } else if (choice == "4") {
        current_user_.clear();
        current_role_ = UserRole::Guest;
        out_ << style(ansi::bright_cyan, "已注销，切换回访客只读模式。\n");
    }
}

void ConsoleWorkbench::menu_export() {
    out_ << "\n" << style(ansi::bold_cyan, "─── [8] 导出标准 CSV 数据集 / 生成离线 HTML 总报告 ───") << '\n';
    if (current_role_ == UserRole::Guest) {
        out_ << style(ansi::yellow, "提示: 文件导出需要登录账号 (Student 或 Admin)。请先在 [7] 登录（支持一键演示登录）。\n");
        return;
    }

    const auto now_stamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto suggested = root_ / "outputs" / ("ui_export_" + std::to_string(now_stamp));

    out_ << "  [1] 运行当前场景并导出标准 CSV 契约文件 (CompositeRunWriter 同步写内存与磁盘)\n"
         << "  [2] 查看全阶段离线 HTML 实验报告生成命令指引\n"
         << "  [0] 返回主菜单\n";
    std::string sel;
    if (!prompt_line("export> ", sel) || sel.empty() || sel == "0") return;
    if (sel == "1") {
        std::string dir_str;
        if (!prompt_line("输出目录 (直接回车使用 " + suggested.u8string() + ")> ", dir_str)) return;
        const auto out_dir = dir_str.empty() ? suggested : std::filesystem::u8path(dir_str);
        cached_run_ = execute_simulation(environment_, population_, settings_.duration_minutes, out_dir);
        const bool detailed = (!population_.empty() && !population_.front().activities.empty()) ||
                              !environment_.scenario().seat_pools.empty();
        out_ << style(ansi::bold_green, "✓ 已成功写出 " + std::string(detailed ? "8" : "5") + " 份标准 CSV 文件至: " + out_dir.u8string()) << '\n';
    } else if (sel == "2") {
        out_ << "\n  可在终端执行以下命令一键生成五合一离线交互 HTML 实验总报告:\n"
             << "    " << style(ansi::bright_cyan, "python tools/run_all_stages.py --executable build/campustwin.exe --output outputs/all_stages") << '\n'
             << "    " << style(ansi::bright_cyan, "Start-Process outputs/all_stages/report.html") << '\n';
    }
}

}  // namespace campustwin
