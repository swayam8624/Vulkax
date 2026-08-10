#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CMAKE = ROOT / "CMakeLists.txt"

text = CMAKE.read_text()

# Extract dependency discovery/configuration from the root build file.
dep_start_marker = "# 1. Set VULKAN_SDK_PATH in .env.cmake to target specific vulkan version\n"
dep_end_marker = "file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/*.cpp)\n"
dep_start = text.index(dep_start_marker)
dep_end = text.index(dep_end_marker)
dependencies = text[dep_start:dep_end]

old_vulkan = '''# 1. Set VULKAN_SDK_PATH in .env.cmake to target specific vulkan version
if (DEFINED VULKAN_SDK_PATH)
  set(Vulkan_INCLUDE_DIRS "${VULKAN_SDK_PATH}/Include") # 1.1 Make sure this include path is correct
  set(Vulkan_LIBRARIES "${VULKAN_SDK_PATH}/Lib") # 1.2 Make sure lib path is correct
  set(Vulkan_FOUND "True")
else()
  find_package(Vulkan REQUIRED) # throws error if could not find Vulkan
  message(STATUS "Found Vulkan: $ENV{VULKAN_SDK}")
endif()
'''
new_vulkan = '''# VULKAN_SDK_PATH is an optional package-root hint, not a replacement for
# FindVulkan's imported targets/library discovery. Point FindVulkan at the SDK
# and let it populate Vulkan_INCLUDE_DIRS/Vulkan_LIBRARIES consistently.
if (DEFINED VULKAN_SDK_PATH AND NOT DEFINED Vulkan_ROOT)
  set(Vulkan_ROOT "${VULKAN_SDK_PATH}")
endif()
find_package(Vulkan REQUIRED)
message(STATUS "Found Vulkan: ${Vulkan_LIBRARY}")
'''
if old_vulkan not in dependencies:
    raise RuntimeError("expected Vulkan dependency block not found")
dependencies = dependencies.replace(old_vulkan, new_vulkan, 1)

# The old block checked a manually-forced Vulkan_FOUND value. FindVulkan is
# REQUIRED now, so the redundant fatal branch can be replaced with a concise
# status line.
old_found = '''if (NOT Vulkan_FOUND)
\tmessage(FATAL_ERROR "Could not find Vulkan library!")
else()
\tmessage(STATUS "Using vulkan lib at: ${Vulkan_LIBRARIES}")
endif()
'''
new_found = 'message(STATUS "Using Vulkan libraries: ${Vulkan_LIBRARIES}")\n'
if old_found not in dependencies:
    raise RuntimeError("expected Vulkan_FOUND block not found")
dependencies = dependencies.replace(old_found, new_found, 1)

module_header = '''# Vulkax dependency discovery.
#
# This file is included from the root CMakeLists after project(). Keep machine-
# local overrides as package hints; do not synthesize *_FOUND or library values.

'''
(ROOT / "cmake" / "VulkaxDependencies.cmake").write_text(module_header + dependencies)
text = text[:dep_start] + "include(cmake/VulkaxDependencies.cmake)\n\n" + text[dep_end:]

# Extract shader generation/build logic into its own module at the same include
# scope so all existing variables and targets remain visible to later tests.
shader_start_marker = "############## Build SHADERS #######################\n"
shader_end_marker = "# A deliberately small direct Vulkan presentation target. It shares the\n"
shader_start = text.index(shader_start_marker)
shader_end = text.index(shader_end_marker)
shaders = text[shader_start:shader_end]

old_wave_paths = '''set(VULKAX_GENERATED_WAVE_GLSL "${PROJECT_SOURCE_DIR}/shaders/vulkax_generated_wave_field.comp")
set(VULKAX_GENERATED_WAVE_SPIRV "${VULKAX_GENERATED_WAVE_GLSL}.spv")
set(VULKAX_GENERATED_WAVE_MSL "${CMAKE_BINARY_DIR}/generated/shaders/vulkax_generated_wave_field.metal")
'''
new_wave_paths = '''set(VULKAX_GENERATED_SHADER_DIR "${CMAKE_BINARY_DIR}/generated/shaders")
set(VULKAX_GENERATED_WAVE_GLSL "${VULKAX_GENERATED_SHADER_DIR}/vulkax_generated_wave_field.comp")
set(VULKAX_GENERATED_WAVE_SPIRV "${VULKAX_GENERATED_WAVE_GLSL}.spv")
set(VULKAX_GENERATED_WAVE_MSL "${VULKAX_GENERATED_SHADER_DIR}/vulkax_generated_wave_field.metal")
'''
if old_wave_paths not in shaders:
    raise RuntimeError("expected generated wave path block not found")
shaders = shaders.replace(old_wave_paths, new_wave_paths, 1)

old_wave_emit = '''add_custom_command(
  OUTPUT ${VULKAX_GENERATED_WAVE_GLSL}
  COMMAND $<TARGET_FILE:VulkaxEquationShaderEmit>
'''
new_wave_emit = '''add_custom_command(
  OUTPUT ${VULKAX_GENERATED_WAVE_GLSL}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${VULKAX_GENERATED_SHADER_DIR}
  COMMAND $<TARGET_FILE:VulkaxEquationShaderEmit>
'''
if old_wave_emit not in shaders:
    raise RuntimeError("expected generated wave command not found")
shaders = shaders.replace(old_wave_emit, new_wave_emit, 1)

# The compute benchmark used to infer the generated path through ENGINE_DIR.
# Give it the actual build artifact path instead.
needle = '''add_custom_target(
  VulkaxGeneratedEquationShaders
  DEPENDS ${VULKAX_GENERATED_WAVE_SPIRV} ${VULKAX_GENERATED_WAVE_MSL}
)
'''
replacement = needle + '''target_compile_definitions(
  VulkaxComputeBenchmark PRIVATE
  VULKAX_GENERATED_WAVE_SPIRV="${VULKAX_GENERATED_WAVE_SPIRV}"
)
'''
if needle not in shaders:
    raise RuntimeError("expected generated equation target not found")
shaders = shaders.replace(needle, replacement, 1)

shader_header = '''# Vulkax shader compilation and generated Physics-IR shader targets.
#
# Static legacy shaders still emit beside their checked source files because
# older executables resolve them through ENGINE_DIR. New/generated artifacts
# must live under CMAKE_BINARY_DIR; migrate legacy paths only alongside the
# runtime-resource contract that consumes them.

'''
(ROOT / "cmake" / "VulkaxShaders.cmake").write_text(shader_header + shaders)
text = text[:shader_start] + "include(cmake/VulkaxShaders.cmake)\n\n" + text[shader_end:]
CMAKE.write_text(text)

# Teach the wave compute tool to consume the build-tree path supplied by CMake.
compute_path = ROOT / "tools" / "vulkax_compute.cpp"
compute = compute_path.read_text()
old_read = '''    const auto code = readFile(std::filesystem::path{ENGINE_DIR} /
        (generated ? "shaders/vulkax_generated_wave_field.comp.spv" : "shaders/vulkax_wave_field.comp.spv"));
'''
new_read = '''    const auto shaderPath = generated
#ifdef VULKAX_GENERATED_WAVE_SPIRV
        ? std::filesystem::path{VULKAX_GENERATED_WAVE_SPIRV}
#else
        ? std::filesystem::path{ENGINE_DIR} / "shaders/vulkax_generated_wave_field.comp.spv"
#endif
        : std::filesystem::path{ENGINE_DIR} / "shaders/vulkax_wave_field.comp.spv";
    const auto code = readFile(shaderPath);
'''
if old_read not in compute:
    raise RuntimeError("expected vulkax-compute shader path expression not found")
compute_path.write_text(compute.replace(old_read, new_read, 1))

presets = {
    "version": 6,
    "cmakeMinimumRequired": {"major": 3, "minor": 24, "patch": 0},
    "configurePresets": [
        {
            "name": "dev-release",
            "displayName": "Vulkax development release",
            "generator": "Ninja",
            "binaryDir": "${sourceDir}/build/dev-release",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Release",
                "BUILD_TESTING": "ON",
                "VULKAX_BUILD_PHYSICS_STUDIO": "ON",
            },
        },
        {
            "name": "headless-release",
            "displayName": "Vulkax headless/research release",
            "inherits": "dev-release",
            "binaryDir": "${sourceDir}/build/headless-release",
            "cacheVariables": {"VULKAX_BUILD_PHYSICS_STUDIO": "OFF"},
        },
    ],
    "buildPresets": [
        {"name": "dev-release", "configurePreset": "dev-release"},
        {"name": "headless-release", "configurePreset": "headless-release"},
    ],
    "testPresets": [
        {
            "name": "dev-release",
            "configurePreset": "dev-release",
            "output": {"outputOnFailure": True},
        },
        {
            "name": "headless-release",
            "configurePreset": "headless-release",
            "output": {"outputOnFailure": True},
        },
    ],
}
(ROOT / "CMakePresets.json").write_text(json.dumps(presets, indent=2) + "\n")

# These files exist only to perform this one connector-mediated refactor. They
# delete themselves so the final PR contains only the durable build changes.
for temporary in (
    ROOT / "tools" / "maintenance" / "phase2_refactor.py",
    ROOT / ".github" / "workflows" / "phase2-refactor.yml",
):
    if temporary.exists():
        temporary.unlink()
