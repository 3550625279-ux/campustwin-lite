#include "campustwin/environment.hpp"
#include "campustwin/not_implemented.hpp"

namespace campustwin {
void CampusEnvironment::load_directory(const std::filesystem::path&) {
    not_implemented("A", "load and validate scenario CSV files");
}
void CampusEnvironment::save_directory(const std::filesystem::path&) const {
    not_implemented("A", "save scenario CSV files");
}
void CampusEnvironment::replace_scenario(CampusScenario) {
    not_implemented("A", "validate and replace scenario atomically");
}
const CampusScenario& CampusEnvironment::scenario() const {
    not_implemented("A", "expose a read-only validated scenario");
}
RouteInfo CampusEnvironment::find_route(const LocationId&, const LocationId&) const {
    not_implemented("A", "shortest route lookup");
}
}  // namespace campustwin
