cmake_minimum_required(VERSION 3.25)

project(VulkaxNext VERSION 0.5.0 LANGUAGES CXX)
option(VULKAX_BUILD_TESTS "Build Vulkax core tests" ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(vulkax_core STATIC
    src/backend/backend.cpp
    src/backend/probe.cpp
    src/calibration/hyperelastic.cpp
    src/compiler/expression.cpp
    src/compute/compute_ir.cpp
    src/compute/runtime.cpp
    src/field/field.cpp
    src/numerics/solvers.cpp
    src/operators/operator_graph.cpp
    src/optimize/optimizer.cpp
    src/physics/cfd.cpp
    src/physics/dem.cpp
    src/physics/dem_gpu.cpp
    src/physics/fem.cpp
    src/planner/solver_plan.cpp
    src/problem/problem_ir.cpp
    src/problem/validation.cpp
    src/research/operator_influence.cpp
    src/verify/convergence.cpp
    src/verify/result_certificate.cpp
    src/visualization/export.cpp
    src/visualization/scientific.cpp
    src/workflow/fidelity.cpp
)
target_include_directories(vulkax_core PUBLIC include)
target_compile_features(vulkax_core PUBLIC cxx_std_20)

find_package(Vulkan QUIET)
find_program(VULKAX_GLSLANG_VALIDATOR glslangValidator)
if(Vulkan_FOUND)
    target_sources(vulkax_core PRIVATE src/backend/vulkan_probe.cpp)
    target_link_libraries(vulkax_core PRIVATE Vulkan::Vulkan)
    target_compile_definitions(vulkax_core PRIVATE VULKAX_HAS_VULKAN=1)
else()
    target_compile_definitions(vulkax_core PRIVATE VULKAX_HAS_VULKAN=0)
endif()
if(Vulkan_FOUND AND VULKAX_GLSLANG_VALIDATOR)
    target_sources(vulkax_core PRIVATE src/compute/vulkan_runtime.cpp src/physics/dem_vulkan.cpp)
    target_compile_definitions(vulkax_core PRIVATE VULKAX_HAS_VULKAN_RUNTIME=1 VULKAX_HAS_VULKAN_DEM=1 VULKAX_GLSLANG_VALIDATOR_PATH="${VULKAX_GLSLANG_VALIDATOR}")
else()
    target_compile_definitions(vulkax_core PRIVATE VULKAX_HAS_VULKAN_RUNTIME=0 VULKAX_HAS_VULKAN_DEM=0)
endif()

if(APPLE)
    enable_language(OBJCXX)
    find_library(VULKAX_METAL_FRAMEWORK Metal REQUIRED)
    find_library(VULKAX_FOUNDATION_FRAMEWORK Foundation REQUIRED)
    target_sources(vulkax_core PRIVATE src/backend/metal_probe.mm src/compute/metal_runtime.mm)
    set_source_files_properties(src/backend/metal_probe.mm src/compute/metal_runtime.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(vulkax_core PRIVATE ${VULKAX_METAL_FRAMEWORK} ${VULKAX_FOUNDATION_FRAMEWORK})
    target_compile_definitions(vulkax_core PRIVATE VULKAX_HAS_METAL=1 VULKAX_HAS_METAL_RUNTIME=1)
else()
    target_compile_definitions(vulkax_core PRIVATE VULKAX_HAS_METAL=0 VULKAX_HAS_METAL_RUNTIME=0)
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
    foreach(test_name IN ITEMS core foundation compute physics research workflow gpu_physics)
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
