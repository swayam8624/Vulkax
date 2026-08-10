# Vulkax dependency discovery.
#
# This file is included from the root CMakeLists after project(). Keep machine-
# local overrides as package hints; do not synthesize *_FOUND or library values.

# VULKAN_SDK_PATH is an optional package-root hint, not a replacement for
# FindVulkan's imported targets/library discovery. Point FindVulkan at the SDK
# and let it populate Vulkan_INCLUDE_DIRS/Vulkan_LIBRARIES consistently.
if (DEFINED VULKAN_SDK_PATH AND NOT DEFINED Vulkan_ROOT)
  set(Vulkan_ROOT "${VULKAN_SDK_PATH}")
endif()
find_package(Vulkan REQUIRED)
message(STATUS "Found Vulkan: ${Vulkan_LIBRARY}")
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
message(STATUS "Using Vulkan libraries: ${Vulkan_LIBRARIES}")


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
