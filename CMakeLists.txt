include(./.env.cmake OPTIONAL RESULT_VARIABLE LOCAL_ENV)
message(STATUS "Local .env.cmake: ${LOCAL_ENV}")

cmake_minimum_required(VERSION 3.11.0)

set(NAME LveEngine)

message(STATUS "using ${CMAKE_GENERATOR}")
if (CMAKE_GENERATOR STREQUAL "MinGW Makefiles")
    if (NOT MINGW_PATH) 
      message(FATAL_ERROR "MINGW_PATH not set in .env.cmake")
    endif()
    set(USE_MINGW "True")
    set(CMAKE_C_COMPILER ${MINGW_PATH}/bin/gcc.exe)
    set(CMAKE_CXX_COMPILER  ${MINGW_PATH}/bin/g++.exe)
endif()

project(${NAME} VERSION 0.24.0)

# 1. Set VULKAN_SDK_PATH in .env.cmake to target specific vulkan version
if (DEFINED VULKAN_SDK_PATH)
  set(Vulkan_INCLUDE_DIRS "${VULKAN_SDK_PATH}/Include") # 1.1 Make sure this include path is correct
  set(Vulkan_LIBRARIES "${VULKAN_SDK_PATH}/Lib") # 1.2 Make sure lib path is correct
  set(Vulkan_FOUND "True")
else()
  find_package(Vulkan REQUIRED) # throws error if could not find Vulkan
  message(STATUS "Found Vulkan: $ENV{VULKAN_SDK}")
endif()
find_package(nlohmann_json 3.2.0 REQUIRED)
find_package(glm REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(CURL REQUIRED)
find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
  pkg_check_modules(OPENEXR QUIET IMPORTED_TARGET OpenEXR)
endif()
if (TARGET SQLite3::SQLite3)
  set(ATLAS_SQLITE_TARGET SQLite3::SQLite3)
elseif (TARGET SQLite::SQLite3)
  set(ATLAS_SQLITE_TARGET SQLite::SQLite3)
else()
  add_library(atlas_sqlite_imported INTERFACE)
  target_include_directories(
    atlas_sqlite_imported INTERFACE ${SQLite3_INCLUDE_DIRS}
  )
  target_link_libraries(
    atlas_sqlite_imported INTERFACE ${SQLite3_LIBRARIES}
  )
  set(ATLAS_SQLITE_TARGET atlas_sqlite_imported)
endif()
if (NOT Vulkan_FOUND)
	message(FATAL_ERROR "Could not find Vulkan library!")
else()
	message(STATUS "Using vulkan lib at: ${Vulkan_LIBRARIES}")
endif()


# 2. Set GLFW_PATH in .env.cmake to target specific glfw
if (DEFINED GLFW_PATH)
  message(STATUS "Using GLFW path specified in .env")
  set(GLFW_INCLUDE_DIRS "${GLFW_PATH}/include")
  if (MSVC)
    set(GLFW_LIB "${GLFW_PATH}/lib-vc2019") # 2.1 Update lib-vc2019 to use same version as your visual studio
  elseif (CMAKE_GENERATOR STREQUAL "MinGW Makefiles")
    message(STATUS "USING MINGW")
    set(GLFW_LIB "${GLFW_PATH}/lib-mingw-w64") # 2.1 make sure matches glfw mingw subdirectory
  endif()
else()
  find_package(glfw3 3.3 REQUIRED)
  set(GLFW_LIB glfw)
  message(STATUS "Found GLFW")
endif()
if (NOT GLFW_LIB)
	message(FATAL_ERROR "Could not find glfw library!")
else()
	message(STATUS "Using glfw lib at: ${GLFW_LIB}")
endif()

include_directories(external)

# If TINYOBJ_PATH not specified in .env.cmake, try fetching from git repo
if (NOT TINYOBJ_PATH)
  message(STATUS "TINYOBJ_PATH not specified in .env.cmake, using external/tinyobjloader")
  set(TINYOBJ_PATH external/tinyobjloader)
endif()

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/*.cpp)
list(FILTER SOURCES EXCLUDE REGEX "/src/atlas/")
list(FILTER SOURCES EXCLUDE REGEX "/src/vulkax/")
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
)
target_compile_features(vulkax_sim PUBLIC cxx_std_20)
target_include_directories(vulkax_sim PUBLIC ${PROJECT_SOURCE_DIR}/src)

add_library(
  vulkax_physics_ir STATIC
  src/vulkax/physics/physics_ir.cpp
  src/vulkax/physics/pipeline_cache.cpp
  src/vulkax/physics/vulkan_resource_layout.cpp
  src/vulkax/physics/compute_ir.cpp
  src/vulkax/physics/stencil_ir.cpp
)
target_compile_features(vulkax_physics_ir PUBLIC cxx_std_20)
target_include_directories(vulkax_physics_ir PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_include_directories(vulkax_physics_ir PUBLIC ${Vulkan_INCLUDE_DIRS})
target_link_libraries(vulkax_physics_ir PUBLIC ${Vulkan_LIBRARIES})
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
target_compile_definitions(VulkaxComputeBenchmark PRIVATE ENGINE_DIR="${PROJECT_SOURCE_DIR}/")
target_link_libraries(VulkaxComputeBenchmark PRIVATE ${Vulkan_LIBRARIES})

add_executable(VulkaxActiveRayCompaction tools/vulkax_active_ray_compaction.cpp)
set_target_properties(VulkaxActiveRayCompaction PROPERTIES OUTPUT_NAME vulkax-active-rays)
target_compile_features(VulkaxActiveRayCompaction PRIVATE cxx_std_20)
target_compile_definitions(VulkaxActiveRayCompaction PRIVATE ENGINE_DIR="${PROJECT_SOURCE_DIR}/")
target_include_directories(VulkaxActiveRayCompaction PRIVATE ${Vulkan_INCLUDE_DIRS})
target_link_libraries(VulkaxActiveRayCompaction PRIVATE vulkax_relativity ${Vulkan_LIBRARIES})

add_executable(VulkaxSchwarzschildComputeBenchmark tools/vulkax_schwarzschild_compute.cpp)
set_target_properties(VulkaxSchwarzschildComputeBenchmark PROPERTIES OUTPUT_NAME vulkax-schwarzschild-compute)
target_compile_features(VulkaxSchwarzschildComputeBenchmark PRIVATE cxx_std_20)
target_compile_definitions(VulkaxSchwarzschildComputeBenchmark PRIVATE ENGINE_DIR="${PROJECT_SOURCE_DIR}/")
target_link_libraries(VulkaxSchwarzschildComputeBenchmark PRIVATE vulkax_relativity ${Vulkan_LIBRARIES})

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
target_compile_definitions(VulkaxSimulationComputeBenchmark PRIVATE ENGINE_DIR="${PROJECT_SOURCE_DIR}/")
target_link_libraries(VulkaxSimulationComputeBenchmark PRIVATE vulkax_sim ${Vulkan_LIBRARIES})

add_executable(VulkaxReactionComputeBenchmark tools/vulkax_reaction_compute.cpp)
set_target_properties(VulkaxReactionComputeBenchmark PROPERTIES OUTPUT_NAME vulkax-reaction-compute)
target_compile_features(VulkaxReactionComputeBenchmark PRIVATE cxx_std_20)
target_compile_definitions(VulkaxReactionComputeBenchmark PRIVATE ENGINE_DIR="${PROJECT_SOURCE_DIR}/")
target_link_libraries(VulkaxReactionComputeBenchmark PRIVATE vulkax_sim ${Vulkan_LIBRARIES})

add_executable(VulkaxMacProjectionCompute tools/vulkax_mac_projection_compute.cpp)
set_target_properties(VulkaxMacProjectionCompute PROPERTIES OUTPUT_NAME vulkax-mac-projection)
target_compile_features(VulkaxMacProjectionCompute PRIVATE cxx_std_20)
target_compile_definitions(VulkaxMacProjectionCompute PRIVATE ENGINE_DIR="${PROJECT_SOURCE_DIR}/")
target_link_libraries(VulkaxMacProjectionCompute PRIVATE ${Vulkan_LIBRARIES})

add_executable(VulkaxParticleComputeBenchmark tools/vulkax_particle_compute.cpp)
set_target_properties(VulkaxParticleComputeBenchmark PROPERTIES OUTPUT_NAME vulkax-particle-compute)
target_compile_features(VulkaxParticleComputeBenchmark PRIVATE cxx_std_20)
target_compile_definitions(VulkaxParticleComputeBenchmark PRIVATE ENGINE_DIR="${PROJECT_SOURCE_DIR}/")
target_link_libraries(VulkaxParticleComputeBenchmark PRIVATE vulkax_sim ${Vulkan_LIBRARIES})

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
    target_compile_definitions(VulkaxPhysicsStudio PRIVATE ENGINE_DIR="${PROJECT_SOURCE_DIR}/")
    target_include_directories(
      VulkaxPhysicsStudio PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/include)
    target_link_libraries(
      VulkaxPhysicsStudio PRIVATE
      vulkax_equation Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Widgets
      vulkax_research vulkax_sim vulkax_relativity ${Vulkan_LIBRARIES}
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
      target_compile_definitions(VulkaxPhysicsStudioTestRunner PRIVATE ENGINE_DIR="${PROJECT_SOURCE_DIR}/")
      target_include_directories(
        VulkaxPhysicsStudioTestRunner PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/include)
      target_link_libraries(
        VulkaxPhysicsStudioTestRunner PRIVATE
        vulkax_equation Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Widgets
        vulkax_research vulkax_sim vulkax_relativity ${Vulkan_LIBRARIES}
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

add_executable(${PROJECT_NAME} ${SOURCES})

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
target_compile_definitions(${PROJECT_NAME} PRIVATE ENGINE_DIR="${PROJECT_SOURCE_DIR}/")
target_link_libraries(${PROJECT_NAME} glm::glm VulkaxAtlasCore)

set_property(TARGET ${PROJECT_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/build")

if (WIN32)
  message(STATUS "CREATING BUILD FOR WINDOWS")

  if (USE_MINGW)
    target_include_directories(${PROJECT_NAME} PUBLIC
      ${MINGW_PATH}/include
    )
    target_link_directories(${PROJECT_NAME} PUBLIC
      ${MINGW_PATH}/lib
    )
  endif()

  target_include_directories(${PROJECT_NAME} PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${Vulkan_INCLUDE_DIRS}
    ${TINYOBJ_PATH}
    ${GLFW_INCLUDE_DIRS}
    ${GLM_PATH}
    )

  target_link_directories(${PROJECT_NAME} PUBLIC
    ${Vulkan_LIBRARIES}
    ${GLFW_LIB}
  )

  target_link_libraries(${PROJECT_NAME} glfw3 vulkan-1 nlohmann_json::nlohmann_json)
elseif (UNIX)
    message(STATUS "CREATING BUILD FOR UNIX")
    target_include_directories(${PROJECT_NAME} PUBLIC
      ${PROJECT_SOURCE_DIR}/src
      ${TINYOBJ_PATH}
    )
    target_link_libraries(${PROJECT_NAME} glfw ${Vulkan_LIBRARIES} nlohmann_json::nlohmann_json)
    if (APPLE)
      target_link_libraries(${PROJECT_NAME} "-framework Cocoa" "-framework QuartzCore")
    endif()
endif()


############## Build SHADERS #######################

# Find all vertex and fragment sources within shaders directory
# taken from VBlancos vulkan tutorial
# https://github.com/vblanco20-1/vulkan-guide/blob/all-chapters/CMakeLists.txt
find_program(GLSL_VALIDATOR glslangValidator HINTS 
  ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} 
  /usr/bin 
  /usr/local/bin 
  ${VULKAN_SDK_PATH}/Bin
  ${VULKAN_SDK_PATH}/Bin32
  $ENV{VULKAN_SDK}/Bin/ 
  $ENV{VULKAN_SDK}/Bin32/
)

# get all .vert and .frag files in shaders directory
file(GLOB_RECURSE GLSL_SOURCE_FILES
  "${PROJECT_SOURCE_DIR}/shaders/*.comp"
  "${PROJECT_SOURCE_DIR}/shaders/*.frag"
  "${PROJECT_SOURCE_DIR}/shaders/*.vert"
)

foreach(GLSL ${GLSL_SOURCE_FILES})
  get_filename_component(FILE_NAME ${GLSL} NAME)
  set(SPIRV "${PROJECT_SOURCE_DIR}/shaders/${FILE_NAME}.spv")
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV}
    DEPENDS ${GLSL})
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

add_custom_target(
    Shaders
    DEPENDS ${SPIRV_BINARY_FILES}
)

set(VULKAX_GENERATED_WAVE_GLSL "${PROJECT_SOURCE_DIR}/shaders/vulkax_generated_wave_field.comp")
set(VULKAX_GENERATED_WAVE_SPIRV "${VULKAX_GENERATED_WAVE_GLSL}.spv")
set(VULKAX_GENERATED_WAVE_MSL "${CMAKE_BINARY_DIR}/generated/shaders/vulkax_generated_wave_field.metal")
add_custom_command(
  OUTPUT ${VULKAX_GENERATED_WAVE_GLSL}
  COMMAND $<TARGET_FILE:VulkaxEquationShaderEmit>
          --preset wave-field --output ${VULKAX_GENERATED_WAVE_GLSL}
  DEPENDS VulkaxEquationShaderEmit
  COMMENT "Emitting AST-generated Vulkax wave compute shader"
)
add_custom_command(
  OUTPUT ${VULKAX_GENERATED_WAVE_SPIRV}
  COMMAND ${GLSL_VALIDATOR} -V ${VULKAX_GENERATED_WAVE_GLSL} -o ${VULKAX_GENERATED_WAVE_SPIRV}
  DEPENDS ${VULKAX_GENERATED_WAVE_GLSL}
  COMMENT "Compiling AST-generated Vulkax wave compute shader"
)
add_custom_command(
  OUTPUT ${VULKAX_GENERATED_WAVE_MSL}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated/shaders
  COMMAND $<TARGET_FILE:VulkaxEquationShaderEmit>
          --preset wave-field --backend msl --output ${VULKAX_GENERATED_WAVE_MSL}
  DEPENDS VulkaxEquationShaderEmit
  COMMENT "Emitting Physics-IR-generated Vulkax wave Metal shader"
)
add_custom_target(
  VulkaxGeneratedEquationShaders
  DEPENDS ${VULKAX_GENERATED_WAVE_SPIRV} ${VULKAX_GENERATED_WAVE_MSL}
)

set(VULKAX_GENERATED_STENCIL_DIR "${CMAKE_BINARY_DIR}/generated/shaders")
set(VULKAX_GENERATED_DIFFUSION_GLSL "${VULKAX_GENERATED_STENCIL_DIR}/vulkax_diffusion.comp")
set(VULKAX_GENERATED_DIFFUSION_SPIRV "${VULKAX_GENERATED_DIFFUSION_GLSL}.spv")
set(VULKAX_GENERATED_DIFFUSION_MSL "${VULKAX_GENERATED_STENCIL_DIR}/vulkax_diffusion.metal")
set(VULKAX_GENERATED_GRAY_SCOTT_GLSL "${VULKAX_GENERATED_STENCIL_DIR}/vulkax_gray_scott.comp")
set(VULKAX_GENERATED_GRAY_SCOTT_SPIRV "${VULKAX_GENERATED_GRAY_SCOTT_GLSL}.spv")
set(VULKAX_GENERATED_GRAY_SCOTT_MSL "${VULKAX_GENERATED_STENCIL_DIR}/vulkax_gray_scott.metal")
add_custom_command(
  OUTPUT ${VULKAX_GENERATED_DIFFUSION_GLSL}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${VULKAX_GENERATED_STENCIL_DIR}
  COMMAND $<TARGET_FILE:VulkaxStencilShaderEmit>
          --backend glsl --output ${VULKAX_GENERATED_DIFFUSION_GLSL}
  DEPENDS VulkaxStencilShaderEmit
  COMMENT "Emitting Physics-IR-generated diffusion stencil GLSL"
)
add_custom_command(
  OUTPUT ${VULKAX_GENERATED_DIFFUSION_SPIRV}
  COMMAND ${GLSL_VALIDATOR} -V ${VULKAX_GENERATED_DIFFUSION_GLSL}
          -o ${VULKAX_GENERATED_DIFFUSION_SPIRV}
  DEPENDS ${VULKAX_GENERATED_DIFFUSION_GLSL}
  COMMENT "Validating generated diffusion stencil SPIR-V"
)
add_custom_command(
  OUTPUT ${VULKAX_GENERATED_DIFFUSION_MSL}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${VULKAX_GENERATED_STENCIL_DIR}
  COMMAND $<TARGET_FILE:VulkaxStencilShaderEmit>
          --backend msl --output ${VULKAX_GENERATED_DIFFUSION_MSL}
  DEPENDS VulkaxStencilShaderEmit
  COMMENT "Emitting Physics-IR-generated diffusion stencil MSL"
)
add_custom_command(
  OUTPUT ${VULKAX_GENERATED_GRAY_SCOTT_GLSL}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${VULKAX_GENERATED_STENCIL_DIR}
  COMMAND $<TARGET_FILE:VulkaxStencilShaderEmit>
          --system gray-scott --backend glsl --output ${VULKAX_GENERATED_GRAY_SCOTT_GLSL}
  DEPENDS VulkaxStencilShaderEmit
  COMMENT "Emitting Physics-IR-generated coupled Gray-Scott GLSL"
)
add_custom_command(
  OUTPUT ${VULKAX_GENERATED_GRAY_SCOTT_SPIRV}
  COMMAND ${GLSL_VALIDATOR} -V ${VULKAX_GENERATED_GRAY_SCOTT_GLSL}
          -o ${VULKAX_GENERATED_GRAY_SCOTT_SPIRV}
  DEPENDS ${VULKAX_GENERATED_GRAY_SCOTT_GLSL}
  COMMENT "Validating generated coupled Gray-Scott SPIR-V"
)
add_custom_command(
  OUTPUT ${VULKAX_GENERATED_GRAY_SCOTT_MSL}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${VULKAX_GENERATED_STENCIL_DIR}
  COMMAND $<TARGET_FILE:VulkaxStencilShaderEmit>
          --system gray-scott --backend msl --output ${VULKAX_GENERATED_GRAY_SCOTT_MSL}
  DEPENDS VulkaxStencilShaderEmit
  COMMENT "Emitting Physics-IR-generated coupled Gray-Scott MSL"
)
set(VULKAX_GENERATED_STENCIL_OUTPUTS
    ${VULKAX_GENERATED_DIFFUSION_SPIRV} ${VULKAX_GENERATED_DIFFUSION_MSL}
    ${VULKAX_GENERATED_GRAY_SCOTT_SPIRV} ${VULKAX_GENERATED_GRAY_SCOTT_MSL})
if (APPLE)
  find_program(VULKAX_XCRUN xcrun)
  if (VULKAX_XCRUN)
    set(VULKAX_GENERATED_DIFFUSION_AIR "${VULKAX_GENERATED_STENCIL_DIR}/vulkax_diffusion.air")
    add_custom_command(
      OUTPUT ${VULKAX_GENERATED_DIFFUSION_AIR}
      COMMAND ${VULKAX_XCRUN} -sdk macosx metal -c ${VULKAX_GENERATED_DIFFUSION_MSL}
              -o ${VULKAX_GENERATED_DIFFUSION_AIR}
      DEPENDS ${VULKAX_GENERATED_DIFFUSION_MSL}
      COMMENT "Validating generated diffusion stencil MSL"
    )
    list(APPEND VULKAX_GENERATED_STENCIL_OUTPUTS ${VULKAX_GENERATED_DIFFUSION_AIR})
    set(VULKAX_GENERATED_GRAY_SCOTT_AIR "${VULKAX_GENERATED_STENCIL_DIR}/vulkax_gray_scott.air")
    add_custom_command(
      OUTPUT ${VULKAX_GENERATED_GRAY_SCOTT_AIR}
      COMMAND ${VULKAX_XCRUN} -sdk macosx metal -c ${VULKAX_GENERATED_GRAY_SCOTT_MSL}
              -o ${VULKAX_GENERATED_GRAY_SCOTT_AIR}
      DEPENDS ${VULKAX_GENERATED_GRAY_SCOTT_MSL}
      COMMENT "Validating generated coupled Gray-Scott MSL"
    )
    list(APPEND VULKAX_GENERATED_STENCIL_OUTPUTS ${VULKAX_GENERATED_GRAY_SCOTT_AIR})
  endif()
endif()
add_custom_target(VulkaxGeneratedStencilShaders DEPENDS ${VULKAX_GENERATED_STENCIL_OUTPUTS})

add_executable(VulkaxStencilComputeBenchmark tools/vulkax_stencil_compute.cpp)
set_target_properties(VulkaxStencilComputeBenchmark PROPERTIES OUTPUT_NAME vulkax-stencil-compute)
target_compile_features(VulkaxStencilComputeBenchmark PRIVATE cxx_std_20)
target_compile_definitions(
  VulkaxStencilComputeBenchmark PRIVATE
  VULKAX_GENERATED_DIFFUSION_SPIRV="${VULKAX_GENERATED_DIFFUSION_SPIRV}"
  VULKAX_GENERATED_GRAY_SCOTT_SPIRV="${VULKAX_GENERATED_GRAY_SCOTT_SPIRV}"
)
target_link_libraries(VulkaxStencilComputeBenchmark PRIVATE vulkax_physics_ir ${Vulkan_LIBRARIES})
add_dependencies(VulkaxStencilComputeBenchmark VulkaxGeneratedStencilShaders)

add_dependencies(VulkaxComputeBenchmark Shaders)
add_dependencies(VulkaxActiveRayCompaction Shaders)
add_dependencies(VulkaxComputeBenchmark VulkaxGeneratedEquationShaders)
add_dependencies(VulkaxSchwarzschildComputeBenchmark Shaders)
add_dependencies(VulkaxSimulationComputeBenchmark Shaders)
add_dependencies(VulkaxReactionComputeBenchmark Shaders)
add_dependencies(VulkaxMacProjectionCompute Shaders)
add_dependencies(VulkaxParticleComputeBenchmark Shaders)

# A deliberately small direct Vulkan presentation target. It shares the
# established swapchain implementation with the legacy renderer, but owns no
# Qt surface or CPU image bridge. This is the portable reference path for the
# Physics Studio GPU-frame architecture.
add_executable(
  VulkaxPhysicsVulkanDirect
  tools/vulkax_physics_vulkan_direct.cpp
  src/vulkax/relativity/kerr_live_queue.cpp
  src/vulkax/sim/mac_live_volume.cpp
  src/lve_window.cpp
  src/lve_device.cpp
  src/lve_swap_chain.cpp
  src/lve_renderer.cpp
  src/lve_pipeline.cpp
  src/lve_buffer.cpp
  src/lve_model.cpp
  src/runtime_paths.cpp
)
set_target_properties(VulkaxPhysicsVulkanDirect PROPERTIES OUTPUT_NAME vulkax-physics-vulkan-direct)
target_compile_features(VulkaxPhysicsVulkanDirect PRIVATE cxx_std_20)
target_compile_definitions(VulkaxPhysicsVulkanDirect PRIVATE ENGINE_DIR="${PROJECT_SOURCE_DIR}/")
target_include_directories(
  VulkaxPhysicsVulkanDirect PRIVATE
  ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/include ${TINYOBJ_PATH})
target_link_libraries(
  VulkaxPhysicsVulkanDirect PRIVATE glfw vulkax_relativity ${Vulkan_LIBRARIES})
if (TARGET PkgConfig::OPENEXR)
  target_link_libraries(VulkaxPhysicsVulkanDirect PRIVATE PkgConfig::OPENEXR)
  target_compile_definitions(VulkaxPhysicsVulkanDirect PRIVATE VULKAX_HAS_OPENEXR=1)
  add_executable(VulkaxExrQuality tools/exr_quality.cpp)
  set_target_properties(VulkaxExrQuality PROPERTIES OUTPUT_NAME vulkax-exr-quality)
  target_compile_features(VulkaxExrQuality PRIVATE cxx_std_20)
  target_link_libraries(VulkaxExrQuality PRIVATE PkgConfig::OPENEXR)
endif()
if (APPLE)
  target_link_libraries(VulkaxPhysicsVulkanDirect PRIVATE "-framework Cocoa" "-framework QuartzCore")
endif()
add_dependencies(VulkaxPhysicsVulkanDirect Shaders)

add_dependencies(${PROJECT_NAME} Shaders)
add_custom_command(
  TARGET ${PROJECT_NAME}
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy
          $<TARGET_FILE:${PROJECT_NAME}>
          $<TARGET_FILE_DIR:${PROJECT_NAME}>/VulkaxAtlas
  COMMENT "Creating the VulkaxAtlas application entrypoint"
)
if (APPLE)
  set(VULKAX_ATLAS_APP_DIR
      "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Vulkax.app")
  configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/VulkaxAtlasInfo.plist.in"
    "${PROJECT_BINARY_DIR}/VulkaxAtlasInfo.plist"
    @ONLY
  )
  add_custom_command(
    TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${VULKAX_ATLAS_APP_DIR}/Contents/MacOS"
    COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:${PROJECT_NAME}>
            "${VULKAX_ATLAS_APP_DIR}/Contents/MacOS/VulkaxGeoBEACON"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${PROJECT_BINARY_DIR}/VulkaxAtlasInfo.plist"
            "${VULKAX_ATLAS_APP_DIR}/Contents/Info.plist"
    COMMENT "Packaging the native Vulkax macOS application"
  )
  add_custom_target(
    VulkaxAppResources ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${VULKAX_ATLAS_APP_DIR}/Contents/Resources/shaders"
            "${VULKAX_ATLAS_APP_DIR}/Contents/Resources/data/connaught_place"
            "${VULKAX_ATLAS_APP_DIR}/Contents/Resources/data/central_london"
            "${VULKAX_ATLAS_APP_DIR}/Contents/Resources/data/central_tokyo"
            "${VULKAX_ATLAS_APP_DIR}/Contents/Resources/data/midtown_manhattan"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/shaders"
            "${VULKAX_ATLAS_APP_DIR}/Contents/Resources/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/data/connaught_place/generated"
            "${VULKAX_ATLAS_APP_DIR}/Contents/Resources/data/connaught_place/generated"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/data/central_london/generated"
            "${VULKAX_ATLAS_APP_DIR}/Contents/Resources/data/central_london/generated"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/data/central_tokyo/generated"
            "${VULKAX_ATLAS_APP_DIR}/Contents/Resources/data/central_tokyo/generated"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/data/midtown_manhattan/generated"
            "${VULKAX_ATLAS_APP_DIR}/Contents/Resources/data/midtown_manhattan/generated"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/data/cities.json"
            "${VULKAX_ATLAS_APP_DIR}/Contents/Resources/data/cities.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/data/connaught_place/navigation.json"
            "${PROJECT_SOURCE_DIR}/data/connaught_place/LICENSE-ODbL.md"
            "${PROJECT_SOURCE_DIR}/data/connaught_place/README.md"
            "${VULKAX_ATLAS_APP_DIR}/Contents/Resources/data/connaught_place"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/data/central_london/navigation.json"
            "${PROJECT_SOURCE_DIR}/data/central_london/LICENSE-ODbL.md"
            "${PROJECT_SOURCE_DIR}/data/central_london/README.md"
            "${VULKAX_ATLAS_APP_DIR}/Contents/Resources/data/central_london"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/data/central_tokyo/navigation.json"
            "${PROJECT_SOURCE_DIR}/data/central_tokyo/LICENSE-ODbL.md"
            "${PROJECT_SOURCE_DIR}/data/central_tokyo/README.md"
            "${VULKAX_ATLAS_APP_DIR}/Contents/Resources/data/central_tokyo"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/data/midtown_manhattan/navigation.json"
            "${PROJECT_SOURCE_DIR}/data/midtown_manhattan/LICENSE-ODbL.md"
            "${PROJECT_SOURCE_DIR}/data/midtown_manhattan/README.md"
            "${VULKAX_ATLAS_APP_DIR}/Contents/Resources/data/midtown_manhattan"
    DEPENDS ${PROJECT_NAME} Shaders
    COMMENT "Bundling checked Vulkax city and shader resources"
  )
endif()
if (APPLE)
  add_custom_target(
    atlas_app_desktop DEPENDS ${PROJECT_NAME} VulkaxAppResources)
else()
  add_custom_target(atlas_app_desktop DEPENDS ${PROJECT_NAME})
endif()

include(CTest)
if (BUILD_TESTING)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)

  # Tests use assert() for compact numerical and contract checks. Release
  # builds define NDEBUG, so explicitly retain assertions for every C++ test
  # executable instead of allowing a green suite with its checks compiled out.
  function(vulkax_enable_test_assertions target_name)
    if (TARGET ${target_name})
      if (MSVC)
        target_compile_options(${target_name} PRIVATE /UNDEBUG)
      else()
        target_compile_options(${target_name} PRIVATE -UNDEBUG)
      endif()
    endif()
  endfunction()

  if (APPLE)
    add_test(
      NAME physics_vulkan_direct_presentation
      COMMAND VulkaxPhysicsVulkanDirect --smoke
    )
    set_tests_properties(
      physics_vulkan_direct_presentation PROPERTIES
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      LABELS "native_gpu;vulkan_presentation"
    )
    add_test(
      NAME physics_vulkan_direct_schwarzschild_presentation
      COMMAND VulkaxPhysicsVulkanDirect --black-hole --smoke
    )
    set_tests_properties(
      physics_vulkan_direct_schwarzschild_presentation PROPERTIES
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      LABELS "native_gpu;vulkan_presentation;black_hole_example"
    )
    add_test(
      NAME physics_vulkan_direct_kerr_presentation
      COMMAND VulkaxPhysicsVulkanDirect --kerr --spin 0.8 --smoke
    )
    set_tests_properties(
      physics_vulkan_direct_kerr_presentation PROPERTIES
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      LABELS "native_gpu;vulkan_presentation;black_hole_example;kerr_reference"
    )
    add_test(
      NAME physics_vulkan_direct_volume_presentation
      COMMAND VulkaxPhysicsVulkanDirect --volume --smoke --frames 1
    )
    set_tests_properties(
      physics_vulkan_direct_volume_presentation PROPERTIES
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      LABELS "native_gpu;volume_example;vulkan_presentation"
    )
    if (TARGET PkgConfig::OPENEXR)
      set(VULKAX_DIRECT_SCHWARZSCHILD_EXR
          ${CMAKE_BINARY_DIR}/direct-vulkan-captures/schwarzschild.exr)
      add_test(
        NAME physics_vulkan_direct_schwarzschild_exr
        COMMAND VulkaxPhysicsVulkanDirect --black-hole --smoke
                --output ${VULKAX_DIRECT_SCHWARZSCHILD_EXR}
      )
      set_tests_properties(
        physics_vulkan_direct_schwarzschild_exr PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        RUN_SERIAL TRUE
        LABELS "native_gpu;vulkan_presentation;black_hole_example;hdr_export"
      )
      add_test(
        NAME physics_vulkan_direct_schwarzschild_exr_validate
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_openexr_export.py
                ${VULKAX_DIRECT_SCHWARZSCHILD_EXR}
      )
      set_tests_properties(
        physics_vulkan_direct_schwarzschild_exr_validate PROPERTIES
        DEPENDS physics_vulkan_direct_schwarzschild_exr
        LABELS "black_hole_example;hdr_export"
      )
      set(VULKAX_DIRECT_KERR_EXR
          ${CMAKE_BINARY_DIR}/direct-vulkan-captures/kerr.exr)
      add_test(
        NAME physics_vulkan_direct_kerr_exr
        COMMAND VulkaxPhysicsVulkanDirect --kerr --spin 0.8 --smoke
                --output ${VULKAX_DIRECT_KERR_EXR}
      )
      set_tests_properties(
        physics_vulkan_direct_kerr_exr PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        RUN_SERIAL TRUE
        LABELS "native_gpu;vulkan_presentation;black_hole_example;kerr_reference;hdr_export"
      )
      add_test(
        NAME physics_vulkan_direct_kerr_exr_validate
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_openexr_export.py
                ${VULKAX_DIRECT_KERR_EXR}
      )
      set_tests_properties(
        physics_vulkan_direct_kerr_exr_validate PROPERTIES
        DEPENDS physics_vulkan_direct_kerr_exr
        LABELS "black_hole_example;kerr_reference;hdr_export"
      )
      add_test(
        NAME physics_vulkan_direct_exr_quality_metrics
        COMMAND VulkaxExrQuality ${VULKAX_DIRECT_SCHWARZSCHILD_EXR}
                ${VULKAX_DIRECT_KERR_EXR} ${VULKAX_DIRECT_SCHWARZSCHILD_EXR}
      )
      set_tests_properties(
        physics_vulkan_direct_exr_quality_metrics PROPERTIES
        DEPENDS "physics_vulkan_direct_schwarzschild_exr;physics_vulkan_direct_kerr_exr"
        LABELS "black_hole_example;kerr_reference;hdr_export;image_quality"
      )
      set(VULKAX_DIRECT_KERR_1SPP_EXR
          ${CMAKE_BINARY_DIR}/direct-vulkan-captures/kerr-1spp.exr)
      set(VULKAX_DIRECT_KERR_4SPP_EXR
          ${CMAKE_BINARY_DIR}/direct-vulkan-captures/kerr-4spp.exr)
      set(VULKAX_DIRECT_KERR_16SPP_EXR
          ${CMAKE_BINARY_DIR}/direct-vulkan-captures/kerr-16spp.exr)
      foreach(VULKAX_KERR_SAMPLES IN ITEMS 1 4 16)
        add_test(
          NAME physics_vulkan_direct_kerr_${VULKAX_KERR_SAMPLES}spp_exr
          COMMAND VulkaxPhysicsVulkanDirect --kerr --spin 0.8 --smoke
                  --frames ${VULKAX_KERR_SAMPLES}
                  --output
                  ${CMAKE_BINARY_DIR}/direct-vulkan-captures/kerr-${VULKAX_KERR_SAMPLES}spp.exr
        )
        set_tests_properties(
          physics_vulkan_direct_kerr_${VULKAX_KERR_SAMPLES}spp_exr PROPERTIES
          WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
          RUN_SERIAL TRUE
          LABELS "native_gpu;black_hole_example;kerr_reference;hdr_export;convergence"
        )
      endforeach()
      add_test(
        NAME physics_vulkan_direct_kerr_sample_convergence
        COMMAND VulkaxExrQuality --convergence
                ${VULKAX_DIRECT_KERR_1SPP_EXR}
                ${VULKAX_DIRECT_KERR_4SPP_EXR}
                ${VULKAX_DIRECT_KERR_16SPP_EXR}
      )
      set_tests_properties(
        physics_vulkan_direct_kerr_sample_convergence PROPERTIES
        DEPENDS "physics_vulkan_direct_kerr_1spp_exr;physics_vulkan_direct_kerr_4spp_exr;physics_vulkan_direct_kerr_16spp_exr"
        LABELS "black_hole_example;kerr_reference;hdr_export;convergence"
      )
      add_test(
        NAME physics_vulkan_direct_kerr_late_flicker
        COMMAND VulkaxExrQuality --flicker
                ${VULKAX_DIRECT_KERR_4SPP_EXR}
                ${VULKAX_DIRECT_KERR_16SPP_EXR}
                0.00005 0.985
      )
      set_tests_properties(
        physics_vulkan_direct_kerr_late_flicker PROPERTIES
        DEPENDS "physics_vulkan_direct_kerr_4spp_exr;physics_vulkan_direct_kerr_16spp_exr"
        LABELS "black_hole_example;kerr_reference;hdr_export;temporal_stability"
      )
      if (EXISTS ${PROJECT_SOURCE_DIR}/tests/golden/kerr-16spp-reference.exr)
        add_test(
          NAME physics_vulkan_direct_kerr_golden
          COMMAND VulkaxExrQuality
                  ${PROJECT_SOURCE_DIR}/tests/golden/kerr-16spp-reference.exr
                  ${VULKAX_DIRECT_KERR_16SPP_EXR}
                  --assert 0.0001 0.97
        )
        set_tests_properties(
          physics_vulkan_direct_kerr_golden PROPERTIES
          DEPENDS physics_vulkan_direct_kerr_16spp_exr
          LABELS "black_hole_example;kerr_reference;hdr_export;golden_image"
        )
      endif()
    endif()
  endif()

  add_executable(
    BeaconCoreTests
    tests/beacon_core_tests.cpp
    src/beacon/beacon_research.cpp
    src/beacon/benchmark_config.cpp
    src/beacon/cluster_scan_reference.cpp
  )
  target_compile_features(BeaconCoreTests PRIVATE cxx_std_17)
  target_include_directories(BeaconCoreTests PRIVATE ${PROJECT_SOURCE_DIR}/src ${TINYOBJ_PATH})
  target_link_libraries(BeaconCoreTests PRIVATE glm::glm)
  add_test(NAME beacon_core COMMAND BeaconCoreTests)

  add_executable(
    EquationCoreTests
    tests/equation_core_tests.cpp
  )
  target_compile_features(EquationCoreTests PRIVATE cxx_std_20)
  target_link_libraries(EquationCoreTests PRIVATE vulkax_equation)
  add_test(NAME equation_core COMMAND EquationCoreTests)

  add_executable(RuntimeContractTests tests/runtime_contract_tests.cpp)
  target_compile_features(RuntimeContractTests PRIVATE cxx_std_20)
  target_include_directories(
    RuntimeContractTests PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/include)
  vulkax_enable_test_assertions(RuntimeContractTests)
  add_test(NAME runtime_contract COMMAND RuntimeContractTests)

  add_executable(
    EquationGlslTests
    tests/equation_glsl_tests.cpp
  )
  target_compile_features(EquationGlslTests PRIVATE cxx_std_20)
  target_link_libraries(EquationGlslTests PRIVATE vulkax_equation)
  add_test(NAME equation_glsl COMMAND EquationGlslTests)

  add_executable(
    PhysicsIrTests
    tests/physics_ir_tests.cpp
  )
  target_compile_features(PhysicsIrTests PRIVATE cxx_std_20)
  target_link_libraries(PhysicsIrTests PRIVATE vulkax_physics_ir)
  add_dependencies(PhysicsIrTests VulkaxGeneratedStencilShaders)
  add_test(NAME physics_ir COMMAND PhysicsIrTests)

  add_executable(
    SimulationGraphTests
    tests/simulation_graph_tests.cpp
  )
  target_compile_features(SimulationGraphTests PRIVATE cxx_std_20)
  target_link_libraries(SimulationGraphTests PRIVATE vulkax_sim)
  add_test(NAME simulation_graph COMMAND SimulationGraphTests)

  add_executable(
    ParticleGravityTests
    tests/particle_gravity_tests.cpp
  )
  target_compile_features(ParticleGravityTests PRIVATE cxx_std_20)
  target_link_libraries(ParticleGravityTests PRIVATE vulkax_sim)
  add_test(NAME particle_gravity COMMAND ParticleGravityTests)

  add_executable(
    BuoyantSmokeTests
    tests/buoyant_smoke_tests.cpp
  )
  target_compile_features(BuoyantSmokeTests PRIVATE cxx_std_20)
  target_link_libraries(BuoyantSmokeTests PRIVATE vulkax_sim)
  add_test(NAME buoyant_smoke COMMAND BuoyantSmokeTests)
  set_tests_properties(buoyant_smoke PROPERTIES LABELS "buoyant_smoke_example")

  add_executable(FluidRigidCouplingTests tests/fluid_rigid_coupling_tests.cpp)
  target_compile_features(FluidRigidCouplingTests PRIVATE cxx_std_20)
  target_link_libraries(FluidRigidCouplingTests PRIVATE vulkax_sim)
  add_test(NAME fluid_rigid_coupling COMMAND FluidRigidCouplingTests)
  set_tests_properties(fluid_rigid_coupling PROPERTIES LABELS "airflow_object_example")

  add_executable(
    SchwarzschildLensingTests
    tests/schwarzschild_lensing_tests.cpp
  )
  target_compile_features(SchwarzschildLensingTests PRIVATE cxx_std_20)
  target_link_libraries(SchwarzschildLensingTests PRIVATE vulkax_relativity)
  add_test(NAME schwarzschild_lensing COMMAND SchwarzschildLensingTests)
  set_tests_properties(schwarzschild_lensing PROPERTIES LABELS "black_hole_example")

  add_executable(
    KerrGeodesicTests
    tests/kerr_geodesic_tests.cpp
  )
  target_compile_features(KerrGeodesicTests PRIVATE cxx_std_20)
  target_link_libraries(KerrGeodesicTests PRIVATE vulkax_relativity)
  vulkax_enable_test_assertions(KerrGeodesicTests)
  add_test(NAME kerr_geodesic COMMAND KerrGeodesicTests)
  set_tests_properties(kerr_geodesic PROPERTIES LABELS "black_hole_example;kerr_reference")

  add_executable(
    SchwarzschildThinDiskTests
    tests/schwarzschild_thin_disk_tests.cpp
  )
  target_compile_features(SchwarzschildThinDiskTests PRIVATE cxx_std_20)
  target_link_libraries(SchwarzschildThinDiskTests PRIVATE vulkax_relativity)
  add_test(NAME schwarzschild_thin_disk COMMAND SchwarzschildThinDiskTests)
  set_tests_properties(schwarzschild_thin_disk PROPERTIES LABELS "black_hole_example")

  add_executable(
    QualityControllerTests
    tests/quality_controller_tests.cpp
  )
  target_compile_features(QualityControllerTests PRIVATE cxx_std_20)
  target_link_libraries(QualityControllerTests PRIVATE vulkax_research)
  add_test(NAME quality_controller COMMAND QualityControllerTests)

  add_test(
    NAME vulkan_wave_compute
    COMMAND VulkaxComputeBenchmark --width 32 --height 18
            --output ${CMAKE_BINARY_DIR}/vulkan-wave-compute
  )
  add_test(NAME vulkan_active_ray_compaction COMMAND VulkaxActiveRayCompaction)
  set_tests_properties(
    vulkan_active_ray_compaction PROPERTIES
    LABELS "black_hole_example;kerr_reference;native_gpu"
  )
  add_test(
    NAME vulkan_generated_wave_compute
    COMMAND VulkaxComputeBenchmark --generated --width 32 --height 18
            --output ${CMAKE_BINARY_DIR}/vulkan-generated-wave-compute
  )
  add_test(NAME vulkan_generated_stencil_compute COMMAND VulkaxStencilComputeBenchmark)
  set_tests_properties(vulkan_generated_stencil_compute PROPERTIES LABELS "native_gpu;physics_ir")
  add_test(
    NAME vulkan_generated_coupled_stencil_compute
    COMMAND VulkaxStencilComputeBenchmark --coupled)
  set_tests_properties(
    vulkan_generated_coupled_stencil_compute PROPERTIES LABELS "native_gpu;physics_ir")
  add_test(NAME vulkan_mac_projection_compute COMMAND VulkaxMacProjectionCompute)
  set_tests_properties(
    vulkan_mac_projection_compute PROPERTIES LABELS "native_gpu;volume_example")
  add_test(
    NAME vulkan_imported_mesh_airflow
    COMMAND VulkaxMacProjectionCompute --steps 4
            --mesh ${PROJECT_SOURCE_DIR}/tests/fixtures/airflow_cube.obj
  )
  set_tests_properties(
    vulkan_imported_mesh_airflow PROPERTIES
    LABELS "native_gpu;volume_example;airflow_object_example"
  )
  add_test(
    NAME vulkan_multiple_obstacle_contacts
    COMMAND VulkaxMacProjectionCompute --steps 4 --bodies 2
            --mesh ${PROJECT_SOURCE_DIR}/tests/fixtures/airflow_cube.obj
  )
  set_tests_properties(
    vulkan_multiple_obstacle_contacts PROPERTIES
    LABELS "native_gpu;volume_example;airflow_object_example"
  )
  add_test(
    NAME vulkan_schwarzschild_compute
    COMMAND VulkaxSchwarzschildComputeBenchmark --rays 16 --steps 30000
            --output ${CMAKE_BINARY_DIR}/vulkan-schwarzschild-compute
  )
  add_test(
    NAME vulkan_wave_simulation_compute
    COMMAND VulkaxSimulationComputeBenchmark --steps 16
            --output ${CMAKE_BINARY_DIR}/vulkan-wave-simulation
  )
  add_test(
    NAME vulkan_reaction_diffusion_compute
    COMMAND VulkaxReactionComputeBenchmark --steps 16
            --output ${CMAKE_BINARY_DIR}/vulkan-reaction-diffusion
  )
  add_test(
    NAME vulkan_particle_gravity_compute
    COMMAND VulkaxParticleComputeBenchmark --steps 64
            --output ${CMAKE_BINARY_DIR}/vulkan-particle-gravity
  )
  add_test(
    NAME physics_quality_benchmark
    COMMAND VulkaxQualityBenchmark --frames 24 --width 160 --height 90 --target-ms 0.5
            --output ${CMAKE_BINARY_DIR}/physics-quality-benchmark
  )
  add_test(
    NAME physics_quality_benchmark_validate
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_quality_benchmark.py
            ${CMAKE_BINARY_DIR}/physics-quality-benchmark
  )
  set_tests_properties(
    physics_quality_benchmark_validate PROPERTIES
    DEPENDS physics_quality_benchmark
  )

  if (TARGET VulkaxPhysicsStudio)
    if (TARGET VulkaxPhysicsStudioTestRunner)
      set(VULKAX_PHYSICS_STUDIO_TEST_EXECUTABLE $<TARGET_FILE:VulkaxPhysicsStudioTestRunner>)
    else()
      set(VULKAX_PHYSICS_STUDIO_TEST_EXECUTABLE $<TARGET_FILE:VulkaxPhysicsStudio>)
    endif()
    if (APPLE)
      # The development executable links Homebrew Qt. A previously deployed
      # app bundle may also contain Qt frameworks/plugins; letting Qt discover
      # those bundled plugins loads two Qt copies in one test process. Force
      # CTest to resolve plugins and QML modules from the same Homebrew prefix
      # as the linked frameworks.
      get_filename_component(VULKAX_QTBASE_PREFIX "${Qt6_DIR}/../../.." REALPATH)
      get_filename_component(VULKAX_QML_PREFIX "${Qt6Qml_DIR}/../../.." REALPATH)
      set(VULKAX_QT_TEST_ENV
        "QT_QPA_PLATFORM=cocoa"
        "QT_QUICK_BACKEND=software"
        "QT_PLUGIN_PATH=${VULKAX_QTBASE_PREFIX}/share/qt/plugins"
        "QT_QPA_PLATFORM_PLUGIN_PATH=${VULKAX_QTBASE_PREFIX}/share/qt/plugins/platforms"
        "QML2_IMPORT_PATH=${VULKAX_QML_PREFIX}/share/qt/qml")
    else()
      set(VULKAX_QT_TEST_ENV "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    endif()
    add_test(NAME physics_studio_smoke COMMAND ${VULKAX_PHYSICS_STUDIO_TEST_EXECUTABLE} --smoke)
    set_tests_properties(
      physics_studio_smoke PROPERTIES
      ENVIRONMENT "${VULKAX_QT_TEST_ENV}"
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    if (TARGET PkgConfig::OPENEXR)
      add_test(
        NAME physics_studio_exr
        COMMAND ${VULKAX_PHYSICS_STUDIO_TEST_EXECUTABLE}
                --export-exr ${CMAKE_BINARY_DIR}/physics-studio-preview.exr
      )
      set_tests_properties(
        physics_studio_exr PROPERTIES
        ENVIRONMENT "${VULKAX_QT_TEST_ENV}"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      )
      add_test(
        NAME physics_studio_exr_validate
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_openexr_export.py
                ${CMAKE_BINARY_DIR}/physics-studio-preview.exr
      )
      set_tests_properties(
        physics_studio_exr_validate PROPERTIES
        DEPENDS physics_studio_exr
      )
    endif()
    add_test(NAME physics_studio_ui_smoke COMMAND ${VULKAX_PHYSICS_STUDIO_TEST_EXECUTABLE} --ui-smoke)
    set_tests_properties(
      physics_studio_ui_smoke PROPERTIES
      ENVIRONMENT "${VULKAX_QT_TEST_ENV}"
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    add_test(NAME physics_studio_project_smoke COMMAND ${VULKAX_PHYSICS_STUDIO_TEST_EXECUTABLE} --project-smoke)
    set_tests_properties(
      physics_studio_project_smoke PROPERTIES
      ENVIRONMENT "${VULKAX_QT_TEST_ENV}"
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    add_test(NAME physics_studio_dynamic_project_smoke COMMAND ${VULKAX_PHYSICS_STUDIO_TEST_EXECUTABLE} --dynamic-project-smoke)
    set_tests_properties(
      physics_studio_dynamic_project_smoke PROPERTIES
      ENVIRONMENT "${VULKAX_QT_TEST_ENV}"
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    add_test(NAME physics_studio_all_presets_smoke COMMAND ${VULKAX_PHYSICS_STUDIO_TEST_EXECUTABLE} --all-presets-smoke)
    set_tests_properties(
      physics_studio_all_presets_smoke PROPERTIES
      ENVIRONMENT "${VULKAX_QT_TEST_ENV}"
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    add_test(NAME physics_studio_unavailable_error_smoke COMMAND ${VULKAX_PHYSICS_STUDIO_TEST_EXECUTABLE} --unavailable-error-smoke)
    set_tests_properties(
      physics_studio_unavailable_error_smoke PROPERTIES
      ENVIRONMENT "${VULKAX_QT_TEST_ENV}"
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    add_test(NAME physics_studio_gpu_preview_smoke COMMAND ${VULKAX_PHYSICS_STUDIO_TEST_EXECUTABLE} --gpu-preview-smoke)
    set_tests_properties(
      physics_studio_gpu_preview_smoke PROPERTIES
      ENVIRONMENT "${VULKAX_QT_TEST_ENV}"
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    add_test(
      NAME physics_studio_gpu_reaction_preview_smoke
      COMMAND ${VULKAX_PHYSICS_STUDIO_TEST_EXECUTABLE} --gpu-reaction-preview-smoke
    )
    set_tests_properties(
      physics_studio_gpu_reaction_preview_smoke PROPERTIES
      ENVIRONMENT "${VULKAX_QT_TEST_ENV}"
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    add_test(
      NAME physics_studio_sequence
      COMMAND ${VULKAX_PHYSICS_STUDIO_TEST_EXECUTABLE}
              --export-sequence ${CMAKE_BINARY_DIR}/physics-studio-sequence --frames 3
    )
    set_tests_properties(
      physics_studio_sequence PROPERTIES
      ENVIRONMENT "${VULKAX_QT_TEST_ENV}"
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    add_test(
      NAME physics_studio_reaction_sequence
      COMMAND ${VULKAX_PHYSICS_STUDIO_TEST_EXECUTABLE}
              --preset reaction-diffusion-seed
              --export-sequence ${CMAKE_BINARY_DIR}/physics-studio-reaction-sequence --frames 3
    )
    set_tests_properties(
      physics_studio_reaction_sequence PROPERTIES
      ENVIRONMENT "${VULKAX_QT_TEST_ENV}"
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    add_test(
      NAME physics_studio_buoyant_smoke_sequence
      COMMAND ${VULKAX_PHYSICS_STUDIO_TEST_EXECUTABLE}
              --preset buoyant-smoke
              --export-sequence ${CMAKE_BINARY_DIR}/physics-studio-buoyant-smoke-sequence --frames 3
    )
    set_tests_properties(
      physics_studio_buoyant_smoke_sequence PROPERTIES
      ENVIRONMENT "${VULKAX_QT_TEST_ENV}"
      LABELS "buoyant_smoke_example"
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    add_test(
      NAME physics_studio_buoyant_smoke_sequence_validate
      COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_physics_sequence.py
              ${CMAKE_BINARY_DIR}/physics-studio-buoyant-smoke-sequence --requires-variation
    )
    set_tests_properties(
      physics_studio_buoyant_smoke_sequence_validate PROPERTIES
      DEPENDS physics_studio_buoyant_smoke_sequence
      LABELS "buoyant_smoke_example"
    )
    add_test(
      NAME physics_studio_reaction_sequence_validate
      COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_physics_sequence.py
              ${CMAKE_BINARY_DIR}/physics-studio-reaction-sequence --requires-variation
    )
    set_tests_properties(
      physics_studio_reaction_sequence_validate PROPERTIES
      DEPENDS physics_studio_reaction_sequence
    )
    add_test(
      NAME physics_studio_sequence_validate
      COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_physics_sequence.py
              ${CMAKE_BINARY_DIR}/physics-studio-sequence
    )
    set_tests_properties(
      physics_studio_sequence_validate PROPERTIES
      DEPENDS physics_studio_sequence
    )
    add_test(
      NAME physics_studio_nbody_sequence
      COMMAND ${VULKAX_PHYSICS_STUDIO_TEST_EXECUTABLE}
              --preset nbody-orbits
              --export-sequence ${CMAKE_BINARY_DIR}/physics-studio-nbody-sequence --frames 3
    )
    set_tests_properties(
      physics_studio_nbody_sequence PROPERTIES
      ENVIRONMENT "${VULKAX_QT_TEST_ENV}"
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    add_test(
      NAME physics_studio_nbody_sequence_validate
      COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_physics_sequence.py
              ${CMAKE_BINARY_DIR}/physics-studio-nbody-sequence --requires-variation
    )
    set_tests_properties(
      physics_studio_nbody_sequence_validate PROPERTIES
      DEPENDS physics_studio_nbody_sequence
    )
    add_test(
      NAME physics_studio_lensing_sequence
      COMMAND ${VULKAX_PHYSICS_STUDIO_TEST_EXECUTABLE}
              --preset schwarzschild-lensing
              --export-sequence ${CMAKE_BINARY_DIR}/physics-studio-lensing-sequence --frames 3
    )
    set_tests_properties(
      physics_studio_lensing_sequence PROPERTIES
      ENVIRONMENT "${VULKAX_QT_TEST_ENV}"
      LABELS "black_hole_example"
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    add_test(
      NAME physics_studio_lensing_sequence_validate
      COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_physics_sequence.py
              ${CMAKE_BINARY_DIR}/physics-studio-lensing-sequence
    )
    set_tests_properties(
      physics_studio_lensing_sequence_validate PROPERTIES
      DEPENDS physics_studio_lensing_sequence
      LABELS "black_hole_example"
    )
  endif()

  if (APPLE AND EXISTS "${PROJECT_SOURCE_DIR}/scripts/vulkax_physics_metal.sh")
    add_test(
      NAME physics_studio_native_metal_gpu
      COMMAND ${PROJECT_SOURCE_DIR}/scripts/vulkax_physics_metal.sh --native-gpu-smoke
    )
    set_tests_properties(
      physics_studio_native_metal_gpu PROPERTIES
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      LABELS "native_gpu"
    )
    add_test(
      NAME physics_studio_native_metal_schwarzschild_gpu
      COMMAND ${PROJECT_SOURCE_DIR}/scripts/vulkax_physics_metal.sh --native-black-hole-gpu-smoke
    )
    set_tests_properties(
      physics_studio_native_metal_schwarzschild_gpu PROPERTIES
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      LABELS "native_gpu;black_hole_example"
    )
    add_test(
      NAME physics_studio_native_metal_volume_gpu
      COMMAND ${PROJECT_SOURCE_DIR}/scripts/vulkax_physics_metal.sh --native-volume-gpu-smoke
    )
    set_tests_properties(
      physics_studio_native_metal_volume_gpu PROPERTIES
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      LABELS "native_gpu;volume_example"
    )
    add_test(
      NAME physics_studio_native_metal_imported_mesh_gpu
      COMMAND ${PROJECT_SOURCE_DIR}/scripts/vulkax_physics_metal.sh
              --native-imported-mesh-gpu-smoke
              ${PROJECT_SOURCE_DIR}/tests/fixtures/airflow_cube.obj
    )
    set_tests_properties(
      physics_studio_native_metal_imported_mesh_gpu PROPERTIES
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      LABELS "native_gpu;volume_example;airflow_object_example"
    )
    add_test(
      NAME physics_studio_native_metal_physics_ir_gpu
      COMMAND ${PROJECT_SOURCE_DIR}/scripts/vulkax_physics_metal.sh
              --native-physics-ir-gpu-smoke ${VULKAX_GENERATED_WAVE_MSL}
    )
    set_tests_properties(
      physics_studio_native_metal_physics_ir_gpu PROPERTIES
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      LABELS "native_gpu;physics_ir"
    )
    add_test(
      NAME physics_studio_native_dynamic_equation_project_gpu
      COMMAND ${PROJECT_SOURCE_DIR}/scripts/vulkax_physics_metal.sh
              --native-dynamic-equation-project-gpu-smoke
    )
    set_tests_properties(
      physics_studio_native_dynamic_equation_project_gpu PROPERTIES
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      LABELS "native_gpu;runtime_contract;project_io"
    )
    add_test(
      NAME physics_studio_native_all_formulas_gpu
      COMMAND ${PROJECT_SOURCE_DIR}/scripts/vulkax_physics_metal.sh
              --native-all-formulas-gpu-smoke
    )
    set_tests_properties(
      physics_studio_native_all_formulas_gpu PROPERTIES
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      LABELS "native_gpu;equation_presets"
    )
  endif()

  add_executable(
    AtlasCoreTests
    tests/atlas_core_tests.cpp
    src/geobeacon/geo_city_registry.cpp
  )
  target_compile_features(AtlasCoreTests PRIVATE cxx_std_20)
  target_compile_definitions(
    AtlasCoreTests PRIVATE ENGINE_DIR="${PROJECT_SOURCE_DIR}/"
  )
  target_link_libraries(AtlasCoreTests PRIVATE VulkaxAtlasCore)
  add_test(NAME atlas_core COMMAND AtlasCoreTests)

  set(ATLAS_TEST_OUTPUT ${CMAKE_BINARY_DIR}/atlas-test-output)
  add_test(
    NAME atlas_manifest_generate
    COMMAND
      VulkaxAtlasBuilder generate-manifest
      ${PROJECT_SOURCE_DIR}/config/atlas/regions/delhi-ncr.json
      ${ATLAS_TEST_OUTPUT}/atlas-dataset.json
  )
  add_test(
    NAME atlas_manifest_validate
    COMMAND
      VulkaxAtlasBuilder validate
      ${ATLAS_TEST_OUTPUT}/atlas-dataset.json
  )
  set_tests_properties(
    atlas_manifest_validate PROPERTIES DEPENDS atlas_manifest_generate
  )
  add_test(
    NAME atlas_pack_geobeacon
    COMMAND
      VulkaxAtlasBuilder pack-geobeacon
      ${PROJECT_SOURCE_DIR}/data/connaught_place/generated/geobeacon.json
      ${ATLAS_TEST_OUTPUT}/connaught-place.vxa
  )

  add_test(
    NAME geobeacon_tools
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_geobeacon_tools.py
  )
  add_test(
    NAME atlas_gateway
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/test_atlas_gateway.py
  )
  add_test(
    NAME connaught_navigation
    COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/test_connaught_navigation.py
  )
  add_test(
    NAME city_dataset_reproducibility
    COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/test_city_dataset_reproducibility.py
  )
  add_test(
    NAME atlas_reference_manifests
    COMMAND
      ${Python3_EXECUTABLE}
      ${PROJECT_SOURCE_DIR}/tests/test_atlas_manifests.py
      $<TARGET_FILE:VulkaxAtlasBuilder>
  )
  add_test(
    NAME atlas_research_benchmark
    COMMAND
      ${Python3_EXECUTABLE}
      ${PROJECT_SOURCE_DIR}/tests/test_atlas_benchmark.py
      $<TARGET_FILE:VulkaxAtlasBenchmark>
  )
  if (APPLE)
    add_test(
      NAME macos_app_bundle
      COMMAND
        ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/test_macos_app_bundle.py
        ${VULKAX_ATLAS_APP_DIR}
    )
  endif()

  foreach(vulkax_test_target
      BeaconCoreTests
      EquationCoreTests
      EquationGlslTests
      PhysicsIrTests
      SimulationGraphTests
      ParticleGravityTests
      BuoyantSmokeTests
      SchwarzschildLensingTests
      SchwarzschildThinDiskTests
      QualityControllerTests
      AtlasCoreTests
      VulkaxPhysicsStudioTestRunner)
    vulkax_enable_test_assertions(${vulkax_test_target})
  endforeach()
endif()
