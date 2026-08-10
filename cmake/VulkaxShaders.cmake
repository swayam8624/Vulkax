# Vulkax shader compilation and generated Physics-IR shader targets.
#
# All compiled shader artifacts live in the build tree. Runtime binaries consume
# staged executable-relative resources and never mutate the source checkout.

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

set(VULKAX_STATIC_SHADER_DIR "${CMAKE_BINARY_DIR}/generated/static-shaders")
foreach(GLSL ${GLSL_SOURCE_FILES})
  get_filename_component(FILE_NAME ${GLSL} NAME)
  set(SPIRV "${VULKAX_STATIC_SHADER_DIR}/${FILE_NAME}.spv")
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${VULKAX_STATIC_SHADER_DIR}
    COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV}
    DEPENDS ${GLSL})
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

add_custom_target(
    Shaders
    DEPENDS ${SPIRV_BINARY_FILES}
)

set(VULKAX_GENERATED_SHADER_DIR "${CMAKE_BINARY_DIR}/generated/shaders")
set(VULKAX_GENERATED_WAVE_GLSL "${VULKAX_GENERATED_SHADER_DIR}/vulkax_generated_wave_field.comp")
set(VULKAX_GENERATED_WAVE_SPIRV "${VULKAX_GENERATED_WAVE_GLSL}.spv")
set(VULKAX_GENERATED_WAVE_MSL "${VULKAX_GENERATED_SHADER_DIR}/vulkax_generated_wave_field.metal")
add_custom_command(
  OUTPUT ${VULKAX_GENERATED_WAVE_GLSL}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${VULKAX_GENERATED_SHADER_DIR}
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
target_compile_definitions(
  VulkaxComputeBenchmark PRIVATE
  VULKAX_GENERATED_WAVE_SPIRV="${VULKAX_GENERATED_WAVE_SPIRV}"
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
target_link_libraries(VulkaxStencilComputeBenchmark PRIVATE vulkax_physics_ir Vulkan::Vulkan)
add_dependencies(VulkaxStencilComputeBenchmark VulkaxGeneratedStencilShaders)

add_dependencies(VulkaxComputeBenchmark Shaders)
add_dependencies(VulkaxActiveRayCompaction Shaders)
add_dependencies(VulkaxComputeBenchmark VulkaxGeneratedEquationShaders)
add_dependencies(VulkaxSchwarzschildComputeBenchmark Shaders)
add_dependencies(VulkaxSimulationComputeBenchmark Shaders)
add_dependencies(VulkaxReactionComputeBenchmark Shaders)
add_dependencies(VulkaxMacProjectionCompute Shaders)
add_dependencies(VulkaxSparseBrickCompute Shaders)
add_dependencies(VulkaxParticleComputeBenchmark Shaders)
