from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise SystemExit(f'missing marker: {label}')
    return text.replace(old, new, 1)

path = Path('cmake/VulkaxDependencies.cmake')
text = path.read_text()
marker = '''else()
  find_package(glfw3 3.3 REQUIRED)
  set(GLFW_LIB glfw)
  message(STATUS "Found GLFW")
endif()
if (NOT GLFW_LIB)
  message(FATAL_ERROR "Could not find glfw library!")
else()
  message(STATUS "Using glfw lib at: ${GLFW_LIB}")
endif()
'''
replacement = '''else()
  find_package(glfw3 3.3 REQUIRED)
  message(STATUS "Found GLFW")
endif()

# Prefer package-provided imported targets on every platform. MSYS2, Homebrew,
# Linux distributions and vcpkg disagree on the filename of the GLFW library,
# but their CMake packages expose one of these stable target names.
if (TARGET glfw)
  set(VULKAX_GLFW_TARGET glfw)
elseif (TARGET glfw3)
  set(VULKAX_GLFW_TARGET glfw3)
elseif (GLFW_LIB)
  # Legacy explicit GLFW_PATH installs do not necessarily ship a CMake package.
  set(VULKAX_GLFW_TARGET glfw3)
else()
  message(FATAL_ERROR "Could not resolve a GLFW CMake target")
endif()
message(STATUS "Using GLFW target: ${VULKAX_GLFW_TARGET}")
'''
text = replace_once(text, marker, replacement, 'GLFW target discovery')
path.write_text(text)

root = Path('CMakeLists.txt')
text = root.read_text()
text = text.replace(
    'target_link_libraries(${PROJECT_NAME} glfw3 Vulkan::Vulkan nlohmann_json::nlohmann_json)',
    'target_link_libraries(${PROJECT_NAME} ${VULKAX_GLFW_TARGET} Vulkan::Vulkan nlohmann_json::nlohmann_json)')
text = text.replace(
    'target_link_libraries(${PROJECT_NAME} glfw Vulkan::Vulkan nlohmann_json::nlohmann_json)',
    'target_link_libraries(${PROJECT_NAME} ${VULKAX_GLFW_TARGET} Vulkan::Vulkan nlohmann_json::nlohmann_json)')
root.write_text(text)

apps = Path('cmake/VulkaxApplications.cmake')
text = apps.read_text()
text = text.replace(
    'VulkaxPhysicsVulkanDirect PRIVATE glfw vulkax_relativity vulkax_sim vulkax_runtime_paths Vulkan::Vulkan',
    'VulkaxPhysicsVulkanDirect PRIVATE ${VULKAX_GLFW_TARGET} vulkax_relativity vulkax_sim vulkax_runtime_paths Vulkan::Vulkan')
apps.write_text(text)

Path('scripts/phase12_windows.py').unlink()
