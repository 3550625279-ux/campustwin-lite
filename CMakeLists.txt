cmake_minimum_required(VERSION 3.16)
project(CampusTwinLite VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(campustwin_contracts INTERFACE)
target_include_directories(campustwin_contracts INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(campustwin_contracts INTERFACE cxx_std_17)

foreach(module environment students resources reporting simulation)
    add_library(campustwin_${module} STATIC src/${module}/${module}.cpp)
    target_link_libraries(campustwin_${module} PUBLIC campustwin_contracts)
endforeach()

add_executable(campustwin app/main.cpp)
target_link_libraries(campustwin PRIVATE campustwin_environment campustwin_students
    campustwin_resources campustwin_reporting campustwin_simulation)

add_executable(demo_a examples/demo_a.cpp)
target_link_libraries(demo_a PRIVATE campustwin_environment)
add_executable(demo_b examples/demo_b.cpp)
target_link_libraries(demo_b PRIVATE campustwin_students)
add_executable(demo_c examples/demo_c.cpp)
target_link_libraries(demo_c PRIVATE campustwin_resources)

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    add_test(NAME sample_data_contract COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/validate_sample_data.py ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(student_engine_tests tests/student_engine_tests.cpp)
    target_link_libraries(student_engine_tests PRIVATE campustwin_students)
    add_test(NAME student_engine_behavior COMMAND student_engine_tests ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME student_demo COMMAND demo_b --students ${CMAKE_CURRENT_SOURCE_DIR}/data/students/mvp.csv
        --student S001 --wait 3 --duration 20)
    set_tests_properties(student_demo PROPERTIES PASS_REGULAR_EXPRESSION
        "SUMMARY students=1 arrivals=1 finished=1")
endif()
# Add integrated simulation/output tests after A/C and the real runner exist.
