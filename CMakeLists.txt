cmake_minimum_required(VERSION 3.25)
project(Vulkax VERSION 0.33.0 LANGUAGES CXX)
option(VULKAX_BUILD_TESTS "Build Vulkax tests" ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_library(vulkax_core STATIC
    src/adapt/goal_oriented.cpp src/autodiff/discrete_adjoint.cpp src/backend/backend.cpp src/backend/probe.cpp src/capture/deformable_dataset.cpp src/cli/captured_deformable.cpp src/cli/deformable_animation.cpp src/cli/deformable_reference.cpp src/cli/energy_cycle.cpp src/cli/research_validation.cpp src/cli/transfer_ablation.cpp src/compiler/expression.cpp src/compute/conformance.cpp src/coupling/mls_embedding.cpp src/coupling/mpm_gaussian.cpp
    src/execution/experiment.cpp src/experiment/design.cpp src/gaussian/gaussian_cloud.cpp src/gaussian/hierarchy.cpp src/geometry/mesh.cpp src/geometry/voxelize.cpp src/inverse/hyperelastic.cpp
    src/numerics/dense.cpp src/numerics/grid.cpp src/operators/operator_graph.cpp src/optimize/optimizer.cpp
    src/planning/solver_plan.cpp src/problem/document.cpp src/problem/problem_ir.cpp src/problem/validation.cpp
    src/render/camera.cpp src/render/capture.cpp src/render/gaussian.cpp src/render/gaussian_tiles.cpp src/render/headless.cpp src/render/image_metrics.cpp
    src/research/affine_flip_sweep.cpp src/research/captured_deformable.cpp src/research/deformable_world.cpp src/research/energy_cycle.cpp src/research/nonlinear_deformable_world.cpp src/research/timestep_convergence.cpp src/research/transfer_ablation.cpp src/research/transfer_diagnostics.cpp src/research/operator_influence.cpp src/research/local_operator_influence.cpp
    src/solvers/dem.cpp src/solvers/dem_broadphase.cpp src/solvers/diffusion.cpp src/solvers/fem.cpp
    src/solvers/hyperelastic_fem.cpp src/solvers/incompressible2d.cpp src/solvers/mac3d.cpp src/solvers/mpm.cpp src/solvers/rotating_drum.cpp
    src/verify/convergence.cpp src/verify/result_certificate.cpp src/visualization/scientific.cpp
    src/world/correspondence_graph.cpp src/world/transaction.cpp src/world/world_ir.cpp)
target_include_directories(vulkax_core PUBLIC include)
target_compile_features(vulkax_core PUBLIC cxx_std_20)
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    target_sources(vulkax_core PRIVATE src/backend/vulkan_probe.cpp)
    target_link_libraries(vulkax_core PRIVATE Vulkan::Vulkan)
    target_compile_definitions(vulkax_core PRIVATE VULKAX_HAS_VULKAN=1)
    find_program(VULKAX_GLSLANG_VALIDATOR glslangValidator)
    if(VULKAX_GLSLANG_VALIDATOR)
        set(VULKAX_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
        set(VULKAX_CONFORMANCE_SPV "${VULKAX_SHADER_DIR}/conformance.spv")
        set(VULKAX_PARTICLE_VERT_SPV "${VULKAX_SHADER_DIR}/particle.vert.spv")
        set(VULKAX_PARTICLE_FRAG_SPV "${VULKAX_SHADER_DIR}/particle.frag.spv")
        set(VULKAX_GAUSSIAN_VERT_SPV "${VULKAX_SHADER_DIR}/gaussian.vert.spv")
        set(VULKAX_GAUSSIAN_FRAG_SPV "${VULKAX_SHADER_DIR}/gaussian.frag.spv")
        add_custom_command(OUTPUT "${VULKAX_CONFORMANCE_SPV}" "${VULKAX_PARTICLE_VERT_SPV}" "${VULKAX_PARTICLE_FRAG_SPV}" "${VULKAX_GAUSSIAN_VERT_SPV}" "${VULKAX_GAUSSIAN_FRAG_SPV}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VULKAX_SHADER_DIR}"
            COMMAND "${VULKAX_GLSLANG_VALIDATOR}" -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/conformance.comp" -o "${VULKAX_CONFORMANCE_SPV}"
            COMMAND "${VULKAX_GLSLANG_VALIDATOR}" -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/particle.vert" -o "${VULKAX_PARTICLE_VERT_SPV}"
            COMMAND "${VULKAX_GLSLANG_VALIDATOR}" -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/particle.frag" -o "${VULKAX_PARTICLE_FRAG_SPV}"
            COMMAND "${VULKAX_GLSLANG_VALIDATOR}" -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/gaussian.vert" -o "${VULKAX_GAUSSIAN_VERT_SPV}"
            COMMAND "${VULKAX_GLSLANG_VALIDATOR}" -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/gaussian.frag" -o "${VULKAX_GAUSSIAN_FRAG_SPV}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/conformance.comp" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/particle.vert" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/particle.frag" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/gaussian.vert" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/gaussian.frag" VERBATIM)
        add_custom_target(vulkax_gpu_shaders DEPENDS "${VULKAX_CONFORMANCE_SPV}" "${VULKAX_PARTICLE_VERT_SPV}" "${VULKAX_PARTICLE_FRAG_SPV}" "${VULKAX_GAUSSIAN_VERT_SPV}" "${VULKAX_GAUSSIAN_FRAG_SPV}")
        add_dependencies(vulkax_core vulkax_gpu_shaders)
        target_sources(vulkax_core PRIVATE src/compute/vulkan_conformance.cpp src/render/vulkan_headless.cpp src/render/vulkan_gaussian.cpp)
        target_compile_definitions(vulkax_core PRIVATE VULKAX_HAS_VULKAN_COMPUTE=1 VULKAX_HAS_VULKAN_RENDER=1 VULKAX_CONFORMANCE_SPV_PATH="${VULKAX_CONFORMANCE_SPV}" VULKAX_PARTICLE_VERT_SPV_PATH="${VULKAX_PARTICLE_VERT_SPV}" VULKAX_PARTICLE_FRAG_SPV_PATH="${VULKAX_PARTICLE_FRAG_SPV}" VULKAX_GAUSSIAN_VERT_SPV_PATH="${VULKAX_GAUSSIAN_VERT_SPV}" VULKAX_GAUSSIAN_FRAG_SPV_PATH="${VULKAX_GAUSSIAN_FRAG_SPV}")
    else()
        target_compile_definitions(vulkax_core PRIVATE VULKAX_HAS_VULKAN_COMPUTE=0 VULKAX_HAS_VULKAN_RENDER=0)
    endif()
else()
    target_compile_definitions(vulkax_core PRIVATE VULKAX_HAS_VULKAN=0 VULKAX_HAS_VULKAN_COMPUTE=0 VULKAX_HAS_VULKAN_RENDER=0)
endif()
if(APPLE)
    enable_language(OBJCXX)
    find_library(VULKAX_METAL_FRAMEWORK Metal REQUIRED)
    find_library(VULKAX_FOUNDATION_FRAMEWORK Foundation REQUIRED)
    target_sources(vulkax_core PRIVATE src/backend/metal_probe.mm src/compute/metal_conformance.mm src/render/metal_headless.mm src/render/metal_gaussian.mm)
    set_source_files_properties(src/backend/metal_probe.mm src/compute/metal_conformance.mm src/render/metal_headless.mm src/render/metal_gaussian.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(vulkax_core PRIVATE ${VULKAX_METAL_FRAMEWORK} ${VULKAX_FOUNDATION_FRAMEWORK})
    target_compile_definitions(vulkax_core PRIVATE VULKAX_HAS_METAL=1 VULKAX_HAS_METAL_COMPUTE=1 VULKAX_HAS_METAL_RENDER=1)
else()
    target_compile_definitions(vulkax_core PRIVATE VULKAX_HAS_METAL=0 VULKAX_HAS_METAL_COMPUTE=0 VULKAX_HAS_METAL_RENDER=0)
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
    foreach(test_name IN ITEMS core numerics research local_influence compiler_inverse compute coupling visualization planning_fluid fem_dem_scale certificate execution drum_hyperelastic adapt document render geometry camera_capture mac3d autodiff world_gaussian gaussian_render gaussian_scaling mpm_world deformable_world nonlinear_deformable_world timestep_convergence transfer_ablation transfer_diagnostics energy_cycle affine_flip_sweep captured_deformable)
        add_executable(vulkax_${test_name}_tests tests/${test_name}_tests.cpp)
        target_link_libraries(vulkax_${test_name}_tests PRIVATE vulkax_core)
        if(MSVC)
            target_compile_options(vulkax_${test_name}_tests PRIVATE /W4 /permissive- /UNDEBUG)
        else()
            target_compile_options(vulkax_${test_name}_tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
        endif()
        add_test(NAME vulkax_${test_name} COMMAND vulkax_${test_name}_tests)
    endforeach()
endif()