#pragma once
#include "campustwin/contracts.hpp"
#include <fstream>
#include <utility>
namespace campustwin {
class CsvRunWriter final : public IRunWriter {
public:
    explicit CsvRunWriter(std::filesystem::path output_directory)
        : output_directory_(std::move(output_directory)) {}
    void begin(const SimulationSettings& settings, const CampusScenario& scenario) override;
    void write_tick(SimTime now,
                    const std::vector<ServiceCompleted>& completed,
                    const std::vector<StudentArrival>& arrivals,
                    const std::vector<ServiceStarted>& started,
                    const std::vector<StudentSnapshot>& students,
                    const std::vector<FacilitySnapshot>& facilities) override;
    void finish(const std::vector<FacilitySummary>& facilities) override;
private:
    std::filesystem::path output_directory_;
    std::ofstream events_, students_, facilities_, summary_;
    bool begun_{false};
    std::int64_t next_tick_{0};
    SimTime duration_{0};
};
} // namespace campustwin
