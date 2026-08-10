cmake_minimum_required(VERSION 3.25)

project(Vulkax VERSION 0.3.0 LANGUAGES CXX)

option(VULKAX_BUILD_TESTS "Build Vulkax tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(vulkax_core STATIC
    src/backend/backend.cpp
    src/backend/probe.cpp
    src/compiler/expression.cpp
    src/experiment/design.cpp
    src/inverse/hyperelastic.cpp
    src/numerics/dense.cpp
    src/numerics/grid.cpp
    src/operators/operator_graph.cpp
    src/optimize/optimizer.cpp
    src/problem/problem_ir.cpp
    src/problem/validation.cpp
    src/research/operator_influence.cpp
    src/solvers/dem.cpp
    src/solvers/diffusion.cpp
    src/verify/convergence.cpp
    src/verify/result_certificate.cpp
)

target_include_directories(vulkax_core PUBLIC include)
target_compile_features(vulkax_core PUBLIC cxx_std_20)

find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    target_sources(vulkax_core PRIVATE src/backend/vulkan_probe.cpp)
    target_link_libraries(vulkax_core PRIVATE Vulkan::Vulkan)
    target_compile_definitions(vulkax_core PRIVATE VULKAX_HAS_VULKAN=1)
else()
    target_compile_definitions(vulkax_core PRIVATE VULKAX_HAS_VULKAN=0)
endif()

if(APPLE)
    enable_language(OBJCXX)
    find_library(VULKAX_METAL_FRAMEWORK Metal REQUIRED)
    find_library(VULKAX_FOUNDATION_FRAMEWORK Foundation REQUIRED)
    target_sources(vulkax_core PRIVATE src/backend/metal_probe.mm)
    set_source_files_properties(src/backend/metal_probe.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(vulkax_core PRIVATE ${VULKAX_METAL_FRAMEWORK} ${VULKAX_FOUNDATION_FRAMEWORK})
    target_compile_definitions(vulkax_core PRIVATE VULKAX_HAS_METAL=1)
else()
    target_compile_definitions(vulkax_core PRIVATE VULKAX_HAS_METAL=0)
endif()

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

    foreach(test_name IN ITEMS core numerics research compiler_inverse)
        add_executable(vulkax_${test_name}_tests tests/${test_name}_tests.cpp)
        target_link_libraries(vulkax_${test_name}_tests PRIVATE vulkax_core)
        if(MSVC)
            target_compile_options(vulkax_${test_name}_tests PRIVATE /W4 /permissive-)
        else()
            target_compile_options(vulkax_${test_name}_tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        add_test(NAME vulkax_${test_name} COMMAND vulkax_${test_name}_tests)
    endforeach()
endif()
