include(./.env.cmake OPTIONAL RESULT_VARIABLE LOCAL_ENV)
message(STATUS "Local .env.cmake: ${LOCAL_ENV}")

cmake_minimum_required(VERSION 3.24)

set(VULKAX_LEGACY_TARGET LveEngine)

message(STATUS "using ${CMAKE_GENERATOR}")
if (CMAKE_GENERATOR STREQUAL "MinGW Makefiles")
    if (NOT MINGW_PATH) 
      message(FATAL_ERROR "MINGW_PATH not set in .env.cmake")
    endif()
    set(USE_MINGW "True")
    set(CMAKE_C_COMPILER ${MINGW_PATH}/bin/gcc.exe)
    set(CMAKE_CXX_COMPILER  ${MINGW_PATH}/bin/g++.exe)
endif()

project(Vulkax VERSION 0.24.0)

include(cmake/VulkaxDependencies.cmake)

add_library(vulkax_runtime_paths STATIC src/runtime_paths.cpp)
target_compile_features(vulkax_runtime_paths PUBLIC cxx_std_20)
target_include_directories(vulkax_runtime_paths PUBLIC ${PROJECT_SOURCE_DIR}/src)

add_library(vulkax_gpu STATIC src/vulkax/gpu/vulkan_compute_context.cpp)
target_compile_features(vulkax_gpu PUBLIC cxx_std_20)
target_include_directories(vulkax_gpu PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(vulkax_gpu PUBLIC Vulkan::Vulkan)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/*.cpp)
list(FILTER SOURCES EXCLUDE REGEX "/src/atlas/")
list(FILTER SOURCES EXCLUDE REGEX "/src/vulkax/")
list(REMOVE_ITEM SOURCES "${PROJECT_SOURCE_DIR}/src/runtime_paths.cpp")
if (APPLE)
  enable_language(OBJCXX)
  list(REMOVE_ITEM SOURCES
       "${PROJECT_SOURCE_DIR}/src/platform/desktop_map_controls_stub.cpp")
  list(APPEND SOURCES
       "${PROJECT_SOURCE_DIR}/src/platform/desktop_map_controls_macos.mm")
endif()

add_library(
  atlas_core STATIC
  src/atlas/core/geodesy.cpp
  src/atlas/core/sha256.cpp
  src/atlas/core/tile_key.cpp
  src/atlas/core/dataset.cpp
)
target_compile_features(atlas_core PUBLIC cxx_std_20)
target_include_directories(atlas_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(
  atlas_core PUBLIC glm::glm nlohmann_json::nlohmann_json
)

add_library(
  atlas_network STATIC
  src/atlas/streaming/tile_source.cpp
)
target_compile_features(atlas_network PUBLIC cxx_std_20)
target_include_directories(atlas_network PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(atlas_network PUBLIC atlas_core CURL::libcurl)

add_library(
  atlas_navigation STATIC
  src/atlas/navigation/replay_providers.cpp
  src/atlas/navigation/gateway_provider.cpp
  src/atlas/navigation/local_navigation.cpp
)
target_compile_features(atlas_navigation PUBLIC cxx_std_20)
target_include_directories(atlas_navigation PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(
  atlas_navigation
  PUBLIC atlas_core atlas_network nlohmann_json::nlohmann_json
)

add_library(
  atlas_research STATIC
  src/atlas/research/route_predictive_scheduler.cpp
)
target_compile_features(atlas_research PUBLIC cxx_std_20)
target_include_directories(atlas_research PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(atlas_research PUBLIC atlas_core atlas_navigation)

add_library(
  atlas_renderer STATIC
  src/atlas/renderer/globe_mesh.cpp
  src/atlas/renderer/route_mesh.cpp
)
target_compile_features(atlas_renderer PUBLIC cxx_std_20)
target_include_directories(atlas_renderer PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(atlas_renderer PUBLIC atlas_core atlas_navigation)

add_library(
  atlas_streaming STATIC
  src/atlas/atlas_runtime.cpp
  src/atlas/streaming/tile_cache.cpp
  src/atlas/streaming/regional_pack.cpp
  src/atlas/streaming/globe_selector.cpp
)
target_compile_features(atlas_streaming PUBLIC cxx_std_20)
target_include_directories(atlas_streaming PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(
  atlas_streaming
  PUBLIC atlas_core atlas_network atlas_navigation atlas_research
         ${ATLAS_SQLITE_TARGET}
)

add_library(VulkaxAtlasCore INTERFACE)
target_link_libraries(
  VulkaxAtlasCore
  INTERFACE atlas_core atlas_network atlas_navigation atlas_research
            atlas_renderer atlas_streaming
)

# Physics Studio is intentionally independent from the legacy Atlas stack so it
# can be built, tested, and eventually hosted by a Qt/Vulkan editor on its own.
add_library(
  vulkax_equation STATIC
  src/vulkax/equation/equation.cpp
  src/vulkax/equation/glsl_compiler.cpp
)
target_compile_features(vulkax_equation PUBLIC cxx_std_20)
target_include_directories(vulkax_equation PUBLIC ${PROJECT_SOURCE_DIR}/src)

add_library(
  vulkax_sim STATIC
  src/vulkax/sim/simulation_graph.cpp
  src/vulkax/sim/particle_system.cpp
  src/vulkax/sim/buoyant_smoke.cpp
  src/vulkax/sim/fluid_rigid_coupling.cpp
  src/vulkax/sim/sparse_brick_storage.cpp
)
target_compile_features(vulkax_sim PUBLIC cxx_std_20)
target_include_directories(vulkax_sim PUBLIC ${PROJECT_SOURCE_DIR}/src)

add_library(
  vulkax_physics_ir STATIC
  src/vulkax/physics/physics_ir.cpp
  src/vulkax/physics/medium_inference.cpp
  src/vulkax/physics/pipeline_cache.cpp
  src/vulkax/physics/vulkan_resource_layout.cpp
  src/vulkax/physics/vulkan_resource_arena.cpp
  src/vulkax/physics/compute_ir.cpp
  src/vulkax/physics/vector_compute_ir.cpp
  src/vulkax/physics/tensor_compute_ir.cpp
  src/vulkax/physics/stencil_ir.cpp
)
target_compile_features(vulkax_physics_ir PUBLIC cxx_std_20)
target_include_directories(vulkax_physics_ir PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_include_directories(vulkax_physics_ir PUBLIC ${Vulkan_INCLUDE_DIRS})
target_link_libraries(vulkax_physics_ir PUBLIC Vulkan::Vulkan)
target_link_libraries(vulkax_physics_ir PUBLIC vulkax_equation)

add_library(
  vulkax_relativity STATIC
  src/vulkax/relativity/kerr_geodesic.cpp
  src/vulkax/relativity/kerr_jacobi.cpp
  src/vulkax/relativity/schwarzschild_lensing.cpp
)
target_compile_features(vulkax_relativity PUBLIC cxx_std_20)
target_include_directories(vulkax_relativity PUBLIC ${PROJECT_SOURCE_DIR}/src)

add_library(
  vulkax_research STATIC
  src/vulkax/research/quality_controller.cpp
)
target_compile_features(vulkax_research PUBLIC cxx_std_20)
target_include_directories(vulkax_research PUBLIC ${PROJECT_SOURCE_DIR}/src)

add_executable(VulkaxRelativityBenchmark tools/relativity_benchmark.cpp)
set_target_properties(VulkaxRelativityBenchmark PROPERTIES OUTPUT_NAME vulkax-relativity)
target_compile_features(VulkaxRelativityBenchmark PRIVATE cxx_std_20)
target_link_libraries(VulkaxRelativityBenchmark PRIVATE vulkax_relativity)

add_executable(VulkaxComputeBenchmark tools/vulkax_compute.cpp)
set_target_properties(VulkaxComputeBenchmark PROPERTIES OUTPUT_NAME vulkax-compute)
target_compile_features(VulkaxComputeBenchmark PRIVATE cxx_std_20)
target_link_libraries(VulkaxComputeBenchmark PRIVATE vulkax_gpu vulkax_runtime_paths)

add_executable(VulkaxActiveRayCompaction tools/vulkax_active_ray_compaction.cpp)
set_target_properties(VulkaxActiveRayCompaction PROPERTIES OUTPUT_NAME vulkax-active-rays)
target_compile_features(VulkaxActiveRayCompaction PRIVATE cxx_std_20)
target_include_directories(VulkaxActiveRayCompaction PRIVATE ${Vulkan_INCLUDE_DIRS})
target_link_libraries(VulkaxActiveRayCompaction PRIVATE vulkax_relativity Vulkan::Vulkan vulkax_runtime_paths)

add_executable(VulkaxSchwarzschildComputeBenchmark tools/vulkax_schwarzschild_compute.cpp)
set_target_properties(VulkaxSchwarzschildComputeBenchmark PROPERTIES OUTPUT_NAME vulkax-schwarzschild-compute)
target_compile_features(VulkaxSchwarzschildComputeBenchmark PRIVATE cxx_std_20)
target_link_libraries(VulkaxSchwarzschildComputeBenchmark PRIVATE vulkax_relativity Vulkan::Vulkan vulkax_runtime_paths)

add_executable(VulkaxEquationShaderEmit tools/equation_shader_emit.cpp)
set_target_properties(VulkaxEquationShaderEmit PROPERTIES OUTPUT_NAME vulkax-equation-shader)
target_compile_features(VulkaxEquationShaderEmit PRIVATE cxx_std_20)
target_link_libraries(VulkaxEquationShaderEmit PRIVATE vulkax_physics_ir)

add_executable(VulkaxStencilShaderEmit tools/stencil_shader_emit.cpp)
set_target_properties(VulkaxStencilShaderEmit PROPERTIES OUTPUT_NAME vulkax-stencil-shader)
target_compile_features(VulkaxStencilShaderEmit PRIVATE cxx_std_20)
target_link_libraries(VulkaxStencilShaderEmit PRIVATE vulkax_physics_ir)

add_executable(VulkaxSimulationComputeBenchmark tools/vulkax_sim_compute.cpp)
set_target_properties(VulkaxSimulationComputeBenchmark PROPERTIES OUTPUT_NAME vulkax-sim-compute)
target_compile_features(VulkaxSimulationComputeBenchmark PRIVATE cxx_std_20)
target_link_libraries(VulkaxSimulationComputeBenchmark PRIVATE vulkax_sim Vulkan::Vulkan vulkax_runtime_paths)

add_executable(VulkaxReactionComputeBenchmark tools/vulkax_reaction_compute.cpp)
set_target_properties(VulkaxReactionComputeBenchmark PROPERTIES OUTPUT_NAME vulkax-reaction-compute)
target_compile_features(VulkaxReactionComputeBenchmark PRIVATE cxx_std_20)
target_link_libraries(VulkaxReactionComputeBenchmark PRIVATE vulkax_sim Vulkan::Vulkan vulkax_runtime_paths)

add_executable(VulkaxMacProjectionCompute tools/vulkax_mac_projection_compute.cpp)
set_target_properties(VulkaxMacProjectionCompute PROPERTIES OUTPUT_NAME vulkax-mac-projection)
target_compile_features(VulkaxMacProjectionCompute PRIVATE cxx_std_20)
target_link_libraries(VulkaxMacProjectionCompute PRIVATE vulkax_sim Vulkan::Vulkan vulkax_runtime_paths)

add_executable(VulkaxSparseBrickCompute tools/vulkax_sparse_brick_compute.cpp)
set_target_properties(VulkaxSparseBrickCompute PROPERTIES OUTPUT_NAME vulkax-sparse-bricks)
target_compile_features(VulkaxSparseBrickCompute PRIVATE cxx_std_20)
target_link_libraries(VulkaxSparseBrickCompute PRIVATE vulkax_sim Vulkan::Vulkan vulkax_runtime_paths)

add_executable(VulkaxParticleComputeBenchmark tools/vulkax_particle_compute.cpp)
set_target_properties(VulkaxParticleComputeBenchmark PROPERTIES OUTPUT_NAME vulkax-particle-compute)
target_compile_features(VulkaxParticleComputeBenchmark PRIVATE cxx_std_20)
target_link_libraries(VulkaxParticleComputeBenchmark PRIVATE vulkax_sim Vulkan::Vulkan vulkax_runtime_paths)

add_executable(VulkaxEquationBenchmark tools/equation_benchmark.cpp)
set_target_properties(VulkaxEquationBenchmark PROPERTIES OUTPUT_NAME vulkax-equations)
target_compile_features(VulkaxEquationBenchmark PRIVATE cxx_std_20)
target_link_libraries(VulkaxEquationBenchmark PRIVATE vulkax_equation)
add_custom_target(vulkax_equations DEPENDS VulkaxEquationBenchmark)

add_executable(VulkaxQualityBenchmark tools/quality_benchmark.cpp)
set_target_properties(VulkaxQualityBenchmark PROPERTIES OUTPUT_NAME vulkax-quality-benchmark)
target_compile_features(VulkaxQualityBenchmark PRIVATE cxx_std_20)
target_link_libraries(VulkaxQualityBenchmark PRIVATE vulkax_equation vulkax_research)

# Homebrew exposes Qt 6 as split qtbase/qtdeclarative formulae on macOS. Keep
# the hints local and optional so non-editor research builds stay portable.
option(VULKAX_BUILD_PHYSICS_STUDIO "Build the Qt Physics Studio compatibility editor" ON)
option(
  VULKAX_REQUIRE_PHYSICS_STUDIO
  "Fail configuration when the Qt Physics Studio dependencies are unavailable"
  OFF
)
if (VULKAX_BUILD_PHYSICS_STUDIO)
  if (APPLE)
    list(APPEND CMAKE_PREFIX_PATH
      /opt/homebrew/opt/qtbase
      /opt/homebrew/opt/qtdeclarative
    )
    # Qt 6's umbrella package uses NO_DEFAULT_PATH when resolving modules.
    # Homebrew splits base and declarative modules, so tell Qt itself where
    # the second package root is instead of relying on CMAKE_PREFIX_PATH.
    set(
      QT_ADDITIONAL_PACKAGES_PREFIX_PATH
      "/opt/homebrew/opt/qtdeclarative"
      CACHE STRING "Additional Homebrew Qt module prefix" FORCE
    )
  endif()
  # Ubuntu 24.04 ships Qt 6.4.2. Physics Studio uses no API newer than 6.4,
  # so keep the portable editor compatible with the oldest supported CI Qt.
  find_package(Qt6 6.4 QUIET COMPONENTS Core Gui Qml Quick QuickControls2 Widgets)
  if (Qt6_FOUND)
    if (APPLE)
      add_executable(
        VulkaxPhysicsStudio MACOSX_BUNDLE
        src/vulkax/editor/main.cpp
        src/vulkax/editor/studio_controller.cpp
        src/vulkax/editor/vulkan_field_executor.cpp
        src/vulkax/editor/resources.qrc
      )
    else()
      add_executable(
        VulkaxPhysicsStudio
        src/vulkax/editor/main.cpp
        src/vulkax/editor/studio_controller.cpp
        src/vulkax/editor/vulkan_field_executor.cpp
        src/vulkax/editor/resources.qrc
      )
    endif()
    target_compile_features(VulkaxPhysicsStudio PRIVATE cxx_std_20)
    target_include_directories(
      VulkaxPhysicsStudio PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/include)
    target_link_libraries(
      VulkaxPhysicsStudio PRIVATE
      vulkax_equation Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Widgets
      vulkax_research vulkax_sim vulkax_relativity vulkax_physics_ir vulkax_runtime_paths Vulkan::Vulkan
    )
    if (TARGET PkgConfig::OPENEXR)
      target_link_libraries(VulkaxPhysicsStudio PRIVATE PkgConfig::OPENEXR)
      target_compile_definitions(VulkaxPhysicsStudio PRIVATE VULKAX_HAS_OPENEXR=1)
    endif()
    set_target_properties(
      VulkaxPhysicsStudio PROPERTIES
      OUTPUT_NAME "Vulkax Physics Studio"
      AUTOMOC ON
      AUTORCC ON
      MACOSX_BUNDLE_GUI_IDENTIFIER "local.vulkax.physicsstudio"
      MACOSX_BUNDLE_BUNDLE_NAME "Vulkax Physics Studio"
    )
    add_custom_target(physics_studio_app DEPENDS VulkaxPhysicsStudio)
    add_dependencies(VulkaxPhysicsStudio Shaders)
    # Keep CTest independent from a locally deployed .app. The release target
    # is a macOS bundle, while the test runner uses the development Qt runtime
    # directly. This prevents a deployed bundle's copied frameworks from being
    # loaded alongside Homebrew Qt during headless and QML smoke tests.
    if (BUILD_TESTING AND APPLE)
      add_executable(
        VulkaxPhysicsStudioTestRunner
        src/vulkax/editor/main.cpp
        src/vulkax/editor/studio_controller.cpp
        src/vulkax/editor/vulkan_field_executor.cpp
        src/vulkax/editor/resources.qrc
      )
      target_compile_features(VulkaxPhysicsStudioTestRunner PRIVATE cxx_std_20)
      target_include_directories(
        VulkaxPhysicsStudioTestRunner PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/include)
      target_link_libraries(
        VulkaxPhysicsStudioTestRunner PRIVATE
        vulkax_equation Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Widgets
        vulkax_research vulkax_sim vulkax_relativity vulkax_physics_ir vulkax_runtime_paths Vulkan::Vulkan
      )
      if (TARGET PkgConfig::OPENEXR)
        target_link_libraries(VulkaxPhysicsStudioTestRunner PRIVATE PkgConfig::OPENEXR)
        target_compile_definitions(VulkaxPhysicsStudioTestRunner PRIVATE VULKAX_HAS_OPENEXR=1)
      endif()
      set_target_properties(
        VulkaxPhysicsStudioTestRunner PROPERTIES
        OUTPUT_NAME "vulkax-physics-studio-test"
        AUTOMOC ON
        AUTORCC ON
      )
      add_dependencies(VulkaxPhysicsStudioTestRunner Shaders)
    endif()
  else()
    if (VULKAX_REQUIRE_PHYSICS_STUDIO)
      message(FATAL_ERROR
        "Qt6 6.4 Core/Gui/Qml/Quick/QuickControls2/Widgets are required, but unavailable"
      )
    endif()
    message(WARNING
      "Qt6 6.4 Core/Gui/Qml/Quick/QuickControls2/Widgets not found; "
      "Physics Studio compatibility target is unavailable"
    )
  endif()
endif()

add_executable(VulkaxAtlasBuilder tools/atlas_build.cpp)
set_target_properties(VulkaxAtlasBuilder PROPERTIES OUTPUT_NAME atlas-build)
target_compile_features(VulkaxAtlasBuilder PRIVATE cxx_std_20)
target_link_libraries(VulkaxAtlasBuilder PRIVATE VulkaxAtlasCore)

add_executable(VulkaxAtlasBenchmark tools/atlas_benchmark.cpp)
set_target_properties(VulkaxAtlasBenchmark PROPERTIES OUTPUT_NAME atlas-benchmark)
target_compile_features(VulkaxAtlasBenchmark PRIVATE cxx_std_20)
target_link_libraries(VulkaxAtlasBenchmark PRIVATE VulkaxAtlasCore)

add_executable(${VULKAX_LEGACY_TARGET} ${SOURCES})

target_compile_features(${VULKAX_LEGACY_TARGET} PUBLIC cxx_std_20)
execute_process(
  COMMAND git rev-parse HEAD
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  OUTPUT_VARIABLE VULKAX_BUILD_GIT_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if (NOT VULKAX_BUILD_GIT_COMMIT)
  set(VULKAX_BUILD_GIT_COMMIT "unknown")
endif()
target_compile_definitions(${VULKAX_LEGACY_TARGET} PRIVATE VULKAX_BUILD_GIT_COMMIT="${VULKAX_BUILD_GIT_COMMIT}")
target_link_libraries(${VULKAX_LEGACY_TARGET} glm::glm VulkaxAtlasCore vulkax_runtime_paths)

set_property(TARGET ${VULKAX_LEGACY_TARGET} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/build")

if (WIN32)
  message(STATUS "CREATING BUILD FOR WINDOWS")

  if (USE_MINGW)
    target_include_directories(${VULKAX_LEGACY_TARGET} PUBLIC
      ${MINGW_PATH}/include
    )
    target_link_directories(${VULKAX_LEGACY_TARGET} PUBLIC
      ${MINGW_PATH}/lib
    )
  endif()

  target_include_directories(${VULKAX_LEGACY_TARGET} PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${Vulkan_INCLUDE_DIRS}
    ${TINYOBJ_PATH}
    ${GLFW_INCLUDE_DIRS}
    ${GLM_PATH}
    )

  if (GLFW_LIB AND NOT GLFW_LIB STREQUAL "glfw")
    target_link_directories(${VULKAX_LEGACY_TARGET} PUBLIC ${GLFW_LIB})
  endif()

  target_link_libraries(${VULKAX_LEGACY_TARGET} glfw3 Vulkan::Vulkan nlohmann_json::nlohmann_json)
elseif (UNIX)
    message(STATUS "CREATING BUILD FOR UNIX")
    target_include_directories(${VULKAX_LEGACY_TARGET} PUBLIC
      ${PROJECT_SOURCE_DIR}/src
      ${TINYOBJ_PATH}
    )
    target_link_libraries(${VULKAX_LEGACY_TARGET} glfw Vulkan::Vulkan nlohmann_json::nlohmann_json)
    if (APPLE)
      target_link_libraries(${VULKAX_LEGACY_TARGET} "-framework Cocoa" "-framework QuartzCore")
    endif()
endif()


include(cmake/VulkaxShaders.cmake)
include(cmake/VulkaxRuntimeResources.cmake)

include(cmake/VulkaxApplications.cmake)
include(cmake/VulkaxTests.cmake)
