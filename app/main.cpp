#include "campustwin/environment.hpp"
#include "campustwin/students.hpp"
#include "campustwin/resources.hpp"
#include "campustwin/reporting.hpp"
#include "campustwin/simulation.hpp"
#include "campustwin/not_implemented.hpp"
#include <iostream>

int main() {
    // TODO(A): parse the CLI, load scenario/population, then run.
    // This entry only verifies linking. It does NOT implement the MVP.
    try {
        campustwin::CampusEnvironment environment;
        campustwin::StudentEngine students;
        campustwin::FacilityEngine facilities;
        campustwin::CsvRunWriter writer("outputs/mvp");
        campustwin::SimulationRunner{}.run({}, environment, {}, students, facilities, writer);
        return 0;
    } catch (const campustwin::NotImplemented& error) {
        std::cerr << error.what() << "\nScaffold only; CLI and MVP are not implemented.\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
