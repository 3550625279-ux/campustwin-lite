#include "campustwin/reporting.hpp"
#include "campustwin/not_implemented.hpp"

namespace campustwin {
void CsvRunWriter::begin(const SimulationSettings&, const CampusScenario&) {
    not_implemented("C", "open fresh output files and write CSV headers");
}
void CsvRunWriter::write_tick(SimTime,
                            const std::vector<ServiceCompleted>&,
                            const std::vector<StudentArrival>&,
                            const std::vector<ServiceStarted>&,
                            const std::vector<StudentSnapshot>&,
                            const std::vector<FacilitySnapshot>&) {
    not_implemented("C", "write phase-ordered events and snapshots");
}
void CsvRunWriter::finish(const std::vector<FacilitySummary>&) {
    not_implemented("C", "write summaries and verify output streams");
}
}  // namespace campustwin
