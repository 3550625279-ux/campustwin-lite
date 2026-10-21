#include "campustwin/environment.hpp"
#include "campustwin/not_implemented.hpp"
#include <iostream>

int main() {
    // TODO(A): implement the independent demonstration in the task document.
    try {
        campustwin::CampusEnvironment{}.load_directory("data/scenarios/mvp");
        return 0;
    } catch (const campustwin::NotImplemented& error) {
        std::cerr << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
