cmake_minimum_required(VERSION 3.25)

project(Vulkax VERSION 0.4.0 LANGUAGES CXX)

option(VULKAX_BUILD_TESTS "Build Vulkax tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(vulkax_core STATIC
    src/backend/backend.cpp
    src/backend/probe.cpp
    src/compiler/expression.cpp
    src/compute/conformance.cpp
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
    find_program(VULKAX_GLSLANG_VALIDATOR glslangValidator)
    if(VULKAX_GLSLANG_VALIDATOR)
        set(VULKAX_CONFORMANCE_SPV "${CMAKE_CURRENT_BINARY_DIR}/shaders/conformance.spv")
        add_custom_command(
            OUTPUT "${VULKAX_CONFORMANCE_SPV}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
            COMMAND "${VULKAX_GLSLANG_VALIDATOR}" -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/conformance.comp" -o "${VULKAX_CONFORMANCE_SPV}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/conformance.comp"
            VERBATIM)
        add_custom_target(vulkax_compute_shaders DEPENDS "${VULKAX_CONFORMANCE_SPV}")
        add_dependencies(vulkax_core vulkax_compute_shaders)
        target_sources(vulkax_core PRIVATE src/compute/vulkan_conformance.cpp)
        target_compile_definitions(vulkax_core PRIVATE VULKAX_HAS_VULKAN_COMPUTE=1
            VULKAX_CONFORMANCE_SPV_PATH="${VULKAX_CONFORMANCE_SPV}")
    else()
        target_compile_definitions(vulkax_core PRIVATE VULKAX_HAS_VULKAN_COMPUTE=0)
    endif()
else()
    target_compile_definitions(vulkax_core PRIVATE VULKAX_HAS_VULKAN=0 VULKAX_HAS_VULKAN_COMPUTE=0)
endif()

if(APPLE)
    enable_language(OBJCXX)
    find_library(VULKAX_METAL_FRAMEWORK Metal REQUIRED)
    find_library(VULKAX_FOUNDATION_FRAMEWORK Foundation REQUIRED)
    target_sources(vulkax_core PRIVATE src/backend/metal_probe.mm src/compute/metal_conformance.mm)
    set_source_files_properties(src/backend/metal_probe.mm src/compute/metal_conformance.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(vulkax_core PRIVATE ${VULKAX_METAL_FRAMEWORK} ${VULKAX_FOUNDATION_FRAMEWORK})
    target_compile_definitions(vulkax_core PRIVATE VULKAX_HAS_METAL=1 VULKAX_HAS_METAL_COMPUTE=1)
else()
    target_compile_definitions(vulkax_core PRIVATE VULKAX_HAS_METAL=0 VULKAX_HAS_METAL_COMPUTE=0)
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

    foreach(test_name IN ITEMS core numerics research compiler_inverse compute)
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
