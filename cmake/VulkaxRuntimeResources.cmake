# Portable Vulkax runtime resource staging and install layout.
#
# Runtime binaries resolve resources relative to their executable (or an
# explicit VULKAX_RESOURCE_ROOT). Keep the build tree self-contained so tools
# can be copied and run without the source checkout.

set(VULKAX_RUNTIME_RESOURCE_DIR "${CMAKE_BINARY_DIR}/resources")

add_custom_target(
  VulkaxRuntimeResources ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${VULKAX_RUNTIME_RESOURCE_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${PROJECT_SOURCE_DIR}/shaders"
          "${VULKAX_RUNTIME_RESOURCE_DIR}/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${PROJECT_SOURCE_DIR}/data"
          "${VULKAX_RUNTIME_RESOURCE_DIR}/data"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${PROJECT_SOURCE_DIR}/config"
          "${VULKAX_RUNTIME_RESOURCE_DIR}/config"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${PROJECT_SOURCE_DIR}/models"
          "${VULKAX_RUNTIME_RESOURCE_DIR}/models"
  DEPENDS Shaders
  COMMENT "Staging relocatable Vulkax runtime resources"
)

foreach(target_name IN ITEMS
    ${PROJECT_NAME}
    VulkaxComputeBenchmark
    VulkaxActiveRayCompaction
    VulkaxSchwarzschildComputeBenchmark
    VulkaxSimulationComputeBenchmark
    VulkaxReactionComputeBenchmark
    VulkaxMacProjectionCompute
    VulkaxSparseBrickCompute
    VulkaxParticleComputeBenchmark
    VulkaxPhysicsStudio
    VulkaxPhysicsStudioTestRunner)
  if (TARGET ${target_name})
    add_dependencies(${target_name} VulkaxRuntimeResources)
  endif()
endforeach()

include(GNUInstallDirs)
install(
  DIRECTORY "${PROJECT_SOURCE_DIR}/shaders/"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/vulkax/shaders"
)
install(
  DIRECTORY "${PROJECT_SOURCE_DIR}/data/"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/vulkax/data"
)
install(
  DIRECTORY "${PROJECT_SOURCE_DIR}/config/"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/vulkax/config"
)
install(
  DIRECTORY "${PROJECT_SOURCE_DIR}/models/"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/vulkax/models"
)
