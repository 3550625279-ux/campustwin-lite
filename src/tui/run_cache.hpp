#pragma once

#include "campustwin/tui.hpp"

namespace campustwin {

struct RunCacheKey {
    std::filesystem::path file;
    std::string inputs;
};

RunCacheKey make_run_cache_key(const std::filesystem::path& directory,
                              const CampusScenario& scenario,
                              const std::vector<StudentSpec>& population,
                              SimTime duration);

std::optional<RecordedRun> load_run_cache(const RunCacheKey& key, const CampusScenario& scenario,
                                        const std::vector<StudentSpec>& population, SimTime duration);
void save_run_cache(const RunCacheKey& key, const RecordedRun& run);

}  // namespace campustwin
