#pragma once
#include "campustwin/contracts.hpp"
#include <map>
#include <utility>

namespace campustwin {
class CampusEnvironment final : public ICampusEnvironment {
public:
    void load_directory(const std::filesystem::path& directory) override;
    void save_directory(const std::filesystem::path& directory) const override;
    void replace_scenario(CampusScenario scenario) override;
    const CampusScenario& scenario() const override;
    RouteInfo find_route(const LocationId& from, const LocationId& to) const override;

    // A-only editing helpers. B/C continue to use the shared read-only interfaces.
    void add_location(Location location);
    void update_location(Location location);
    void remove_location(const LocationId& id);
    void set_road(Road road); // insert/update one undirected edge
    void remove_road(const LocationId& from, const LocationId& to);
    void set_facility(FacilityConfig facility); // insert/update by facility ID
    void remove_facility(const FacilityId& id);
private:
    CampusScenario scenario_;
    std::map<LocationId, std::vector<std::pair<LocationId, double>>> graph_;
};
}  // namespace campustwin
