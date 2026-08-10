# Vulkax application entrypoints, direct presentation targets and platform packaging.
# Included after core/research targets and shader targets are defined.

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
  VulkaxPhysicsVulkanDirect PRIVATE glfw vulkax_relativity vulkax_sim Vulkan::Vulkan)
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
