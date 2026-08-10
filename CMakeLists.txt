cmake_minimum_required(VERSION 3.25)

project(VulkaxNext VERSION 0.1.0 LANGUAGES CXX)

option(VULKAX_BUILD_TESTS "Build Vulkax core tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(vulkax_core STATIC
    src/backend/backend.cpp
    src/operators/operator_graph.cpp
    src/problem/problem_ir.cpp
    src/problem/validation.cpp
    src/verify/result_certificate.cpp
)

target_include_directories(vulkax_core PUBLIC include)
target_compile_features(vulkax_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(vulkax_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(vulkax_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(vulkax src/main.cpp)
target_link_libraries(vulkax PRIVATE vulkax_core)

if(VULKAX_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_executable(vulkax_core_tests tests/core_tests.cpp)
    target_link_libraries(vulkax_core_tests PRIVATE vulkax_core)
    if(MSVC)
        target_compile_options(vulkax_core_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(vulkax_core_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME vulkax_core COMMAND vulkax_core_tests)
endif()
