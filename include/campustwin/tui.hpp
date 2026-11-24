#pragma once

#include "campustwin/accounts.hpp"
#include "campustwin/contracts.hpp"
#include "campustwin/environment.hpp"
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace campustwin {

struct TickFrame {
    SimTime at{0};
    std::vector<ServiceCompleted> completed;
    std::vector<StudentArrival> arrivals;
    std::vector<ServiceStarted> started;
    std::size_t finished_students{0};
    std::vector<FacilitySnapshot> facilities;
    std::vector<LunchDecision> decisions;
    std::vector<SeatRequest> seat_requests;
    std::vector<SeatGranted> seat_grants;
    std::vector<SeatSnapshot> seats;
};

struct RecordedRun {
    SimulationSettings settings;
    CampusScenario scenario;
    std::vector<StudentSpec> population;
    std::vector<TickFrame> frames;
    // Each trace stores the first snapshot and subsequent changes in minute order.
    std::vector<std::vector<StudentSnapshot>> student_history;
    std::vector<StudentSnapshot> students_at(SimTime tick) const;
    std::optional<StudentSnapshot> student_at(const StudentId& id, SimTime tick) const;
    std::vector<ActivityRecord> activities;
    std::vector<FacilitySummary> summaries;

    SimTime peak_congestion_minute() const;
    std::size_t finished_students_at(SimTime tick) const;
    std::size_t completed_activities_count() const;
    std::size_t late_classes_count() const;
    std::int64_t total_class_delay_minutes() const;
    std::int64_t total_observed_seat_wait() const;
    std::optional<SimTime> all_finished_minute() const;
};

// Polymorphic IRunWriter that captures minute-by-minute simulation telemetry in memory for TUI playback.
class InMemoryRunObserver final : public IRunWriter {
public:
    void begin(const SimulationSettings& settings, const CampusScenario& scenario) override;
    void write_tick(SimTime now,
                    const std::vector<ServiceCompleted>& completed,
                    const std::vector<StudentArrival>& arrivals,
                    const std::vector<ServiceStarted>& started,
                    const std::vector<StudentSnapshot>& students,
                    const std::vector<FacilitySnapshot>& facilities) override;
    void write_decisions(SimTime now, const std::vector<LunchDecision>& decisions) override;
    void write_seats(SimTime now,
                     const std::vector<SeatRequest>& requests,
                     const std::vector<SeatGranted>& grants,
                     const std::vector<SeatSnapshot>& seats) override;
    void write_activities(const std::vector<ActivityRecord>& records) override;
    void finish(const std::vector<FacilitySummary>& facilities) override;

    void set_population(std::vector<StudentSpec> population);
    const RecordedRun& recorded_run() const { return run_; }
    RecordedRun take_run() { return std::move(run_); }

private:
    RecordedRun run_;
    bool active_{false};
};

// Composite IRunWriter multiplexing simulation output to multiple writers (e.g. memory + CSV files).
class CompositeRunWriter final : public IRunWriter {
public:
    void add_writer(IRunWriter& writer);
    void begin(const SimulationSettings& settings, const CampusScenario& scenario) override;
    void write_tick(SimTime now,
                    const std::vector<ServiceCompleted>& completed,
                    const std::vector<StudentArrival>& arrivals,
                    const std::vector<ServiceStarted>& started,
                    const std::vector<StudentSnapshot>& students,
                    const std::vector<FacilitySnapshot>& facilities) override;
    void write_decisions(SimTime now, const std::vector<LunchDecision>& decisions) override;
    void write_seats(SimTime now,
                     const std::vector<SeatRequest>& requests,
                     const std::vector<SeatGranted>& grants,
                     const std::vector<SeatSnapshot>& seats) override;
    void write_activities(const std::vector<ActivityRecord>& records) override;
    void finish(const std::vector<FacilitySummary>& facilities) override;

private:
    std::vector<IRunWriter*> writers_;
};

struct WorkbenchOptions {
    std::filesystem::path data_root;
    std::optional<std::filesystem::path> scenario_dir;
    std::optional<std::filesystem::path> students_file;
    std::optional<std::filesystem::path> schedule_file;
    std::optional<SimTime> duration_minutes;
    std::optional<LunchPolicy> policy_override;
    std::optional<std::filesystem::path> accounts_file;
    bool enable_color{true};
};

std::filesystem::path detect_data_root(const std::filesystem::path& executable_path = {});

class ConsoleWorkbench {
public:
    ConsoleWorkbench(WorkbenchOptions options, std::istream& in = std::cin, std::ostream& out = std::cout);
    int run();

private:
    enum class PlaybackView { Overview, Scene, Students };
    enum class LunchPlan { Concentrated = 0, Staggered20 = 20, Staggered40 = 40, Staggered60 = 60 };

    WorkbenchOptions options_;
    std::istream& in_;
    std::ostream& out_;
    bool color_{true};

    std::filesystem::path root_;
    std::string active_preset_title_;
    std::filesystem::path scenario_path_;
    std::filesystem::path students_path_;
    std::optional<std::filesystem::path> schedule_path_;
    std::optional<LunchPlan> lunch_plan_;
    SimulationSettings settings_{600};
    int playback_speed_{1};
    std::optional<LunchPolicy> policy_override_;
    bool scenario_dirty_{false};

    CampusEnvironment environment_;
    std::vector<StudentSpec> population_;
    std::optional<RecordedRun> cached_run_;

    std::filesystem::path account_file_;
    AccountStore accounts_;
    std::string current_user_;

    static const char* lunch_plan_name(LunchPlan plan);
    static void apply_lunch_plan(std::vector<StudentSpec>& population, LunchPlan plan);
    void load_flagship(LunchPlan plan);
    bool choose_flagship();
    void reload_population();
    bool authenticate();
    void load_initial_scenario();
    bool run_session();
    std::filesystem::path user_directory() const;
    bool prompt_password(std::string& password);
    RecordedRun load_or_simulate(const CampusEnvironment& env,
                                 const std::vector<StudentSpec>& population, SimTime duration);
    void ensure_run_cached();

    std::string style(const char* ansi_code, const std::string& text) const;
    void print_banner() const;
    void print_status_card() const;
    void print_run_summary(const RecordedRun& run) const;
    void render_tick_dashboard(const RecordedRun& run, SimTime tick, PlaybackView view,
                               const StudentId& selected_student) const;
    void render_tick_overview(const RecordedRun& run, const TickFrame& frame,
                              const std::vector<StudentSnapshot>& students) const;
    void render_tick_details(const RecordedRun& run, const TickFrame& frame,
                             const std::vector<StudentSnapshot>& students, PlaybackView view,
                             const StudentId& selected_student) const;
    std::vector<std::string> render_tick_event_summary(const RecordedRun& run, const TickFrame& frame,
                                                      const std::vector<StudentSnapshot>& students,
                                                      bool include_details) const;

    bool prompt_line(const std::string& label, std::string& line);
    void menu_scenarios();
    void menu_simulation_playback();
    void menu_student_inspector();
    void menu_lunch_timing_experiment();
    void menu_map_and_routes();
    void menu_scenario_editor();
    void show_lunch_timing_comparison();

};

}  // namespace campustwin
