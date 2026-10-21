#include "campustwin/simulation.hpp"
#include "campustwin/not_implemented.hpp"

namespace campustwin {
void SimulationRunner::run(const SimulationSettings&,
                           const ICampusEnvironment&,
                           const std::vector<StudentSpec>&,
                           IStudentEngine&, IFacilityEngine&, IRunWriter&) const {
    not_implemented("A + team", "implement the tick phases in docs/06_INTEGRATION.md");
}
}  // namespace campustwin
