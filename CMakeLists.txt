cmake_minimum_required(VERSION 3.20)
project(ee538_scheduler VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(EE538_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer (Clang/GCC)" OFF)
add_library(ee538_core
    src/input.cpp
    src/graph.cpp
    src/resources.cpp
    src/scheduling.cpp
    src/simulator.cpp
    src/cost_model.cpp
    src/metrics.cpp
)
target_include_directories(ee538_core PUBLIC include third_party)
if(MSVC)
  target_compile_options(ee538_core PUBLIC /W4 /permissive-)
else()
  target_compile_options(ee538_core PUBLIC -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  if(EE538_SANITIZERS)
    target_compile_options(ee538_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(ee538_core PUBLIC -fsanitize=address,undefined)
  endif()
endif()
add_executable(ee538_scheduler src/main.cpp)
target_link_libraries(ee538_scheduler PRIVATE ee538_core)
enable_testing()
add_executable(ee538_tests tests/test_core.cpp)
target_link_libraries(ee538_tests PRIVATE ee538_core)
add_test(NAME core COMMAND ee538_tests ${CMAKE_CURRENT_SOURCE_DIR}/examples/hand_calculated.json)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.py $<TARGET_FILE:ee538_scheduler> ${CMAKE_CURRENT_SOURCE_DIR})
endif()
add_executable(ee538_cost_model_tests tests/test_cost_model.cpp)
target_link_libraries(ee538_cost_model_tests PRIVATE ee538_core)
add_test(NAME cost_model COMMAND ee538_cost_model_tests)
add_executable(ee538_metrics_tests tests/test_metrics.cpp)
target_link_libraries(ee538_metrics_tests PRIVATE ee538_core)
add_test(NAME metrics COMMAND ee538_metrics_tests)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME checker
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_checker.py
                $<TARGET_FILE:ee538_scheduler>
    )
endif()
if(Python3_Interpreter_FOUND)
    add_test(
        NAME dependency_profile
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dependency.py
                $<TARGET_FILE:ee538_scheduler>
    )
endif()