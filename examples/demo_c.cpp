#include "campustwin/resources.hpp"
#include "campustwin/not_implemented.hpp"
#include <iostream>

int main() {
    // TODO(C): implement the independent demonstration in the task document.
    try {
        campustwin::FacilityEngine{}.reset({});
        return 0;
    } catch (const campustwin::NotImplemented& error) {
        std::cerr << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
