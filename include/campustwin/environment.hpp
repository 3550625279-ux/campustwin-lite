#pragma once
#include "campustwin/contracts.hpp"

namespace campustwin {
class CampusEnvironment final : public ICampusEnvironment {
public:
    void load_directory(const std::filesystem::path& directory) override;
    void save_directory(const std::filesystem::path& directory) const override;
    void replace_scenario(CampusScenario scenario) override;
    const CampusScenario& scenario() const override;
    RouteInfo find_route(const LocationId& from, const LocationId& to) const override;
private:
    // TODO(A): own the validated scenario and graph here.
};
}  // namespace campustwin
