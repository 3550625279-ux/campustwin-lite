#pragma once
#include "campustwin/contracts.hpp"
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
    // TODO(C): own output streams, validate headers and write failures.
};
}  // namespace campustwin
