#pragma once
#include "campustwin/contracts.hpp"
#include <fstream>
#include <utility>
namespace campustwin {
class CsvRunWriter final : public IRunWriter {
public:
    explicit CsvRunWriter(std::filesystem::path output_directory, bool activity_output = false)
        : output_directory_(std::move(output_directory)), activity_output_(activity_output) {}
    void begin(const SimulationSettings& settings, const CampusScenario& scenario) override;
    void write_tick(SimTime now,
                    const std::vector<ServiceCompleted>& completed,
                    const std::vector<StudentArrival>& arrivals,
                    const std::vector<ServiceStarted>& started,
                    const std::vector<StudentSnapshot>& students,
                    const std::vector<FacilitySnapshot>& facilities) override;
    void finish(const std::vector<FacilitySummary>& facilities) override;
    void write_decisions(SimTime now, const std::vector<LunchDecision>& decisions) override;
    void write_seats(SimTime now, const std::vector<SeatRequest>& requests,
                    const std::vector<SeatGranted>& grants, const std::vector<SeatSnapshot>& seats) override;
    void write_activities(const std::vector<ActivityRecord>& records) override;
private:
    std::filesystem::path output_directory_;
    std::ofstream events_, students_, facilities_, summary_, decisions_;
    std::ofstream activities_, seat_events_, seat_snapshots_;
    bool activity_output_{false}, activities_written_{false};
    std::int64_t last_seat_tick_{-1};
    bool begun_{false};
    std::int64_t next_tick_{0};
    std::int64_t last_decision_tick_{-1};
    SimTime duration_{0};
};
} // namespace campustwin
