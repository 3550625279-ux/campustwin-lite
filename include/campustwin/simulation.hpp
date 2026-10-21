#pragma once
#include "campustwin/contracts.hpp"

namespace campustwin {
class SimulationRunner {
public:
    void run(const SimulationSettings& settings,
             const ICampusEnvironment& environment,
             const std::vector<StudentSpec>& population,
             IStudentEngine& students,
             IFacilityEngine& facilities,
             IRunWriter& writer) const;
};
}  // namespace campustwin
