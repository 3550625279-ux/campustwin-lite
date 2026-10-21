#include "campustwin/students.hpp"
#include "campustwin/not_implemented.hpp"
#include <iostream>

int main() {
    // TODO(B): implement the independent demonstration in the task document.
    try {
        (void)campustwin::read_student_specs("data/students/mvp.csv");
        return 0;
    } catch (const campustwin::NotImplemented& error) {
        std::cerr << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
