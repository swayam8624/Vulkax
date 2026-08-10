# Vulkax CTest registry and validation targets.
# Included after all runtime/application targets are available.

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
    TensorComputeIrTests
    tests/tensor_compute_ir_tests.cpp
  )
  target_compile_features(TensorComputeIrTests PRIVATE cxx_std_20)
  target_link_libraries(TensorComputeIrTests PRIVATE vulkax_physics_ir)
  vulkax_enable_test_assertions(TensorComputeIrTests)
  add_test(NAME tensor_compute_ir COMMAND TensorComputeIrTests)
  set_tests_properties(tensor_compute_ir PROPERTIES LABELS "physics_ir;tensor_field")

  add_executable(
    GeneratedTransportTests
    tests/generated_transport_tests.cpp
  )
  target_compile_features(GeneratedTransportTests PRIVATE cxx_std_20)
  target_link_libraries(GeneratedTransportTests PRIVATE vulkax_physics_ir)
  vulkax_enable_test_assertions(GeneratedTransportTests)
  add_test(NAME generated_transport COMMAND GeneratedTransportTests)
  set_tests_properties(generated_transport PROPERTIES LABELS "physics_ir;generated_solver;transport")

  add_executable(
    MediumInferenceTests
    tests/medium_inference_tests.cpp
  )
  target_compile_features(MediumInferenceTests PRIVATE cxx_std_20)
  target_link_libraries(MediumInferenceTests PRIVATE vulkax_physics_ir)
  add_test(NAME medium_inference COMMAND MediumInferenceTests)

  add_executable(
    VulkanResourceArenaTests
    tests/vulkan_resource_arena_tests.cpp
  )
  target_compile_features(VulkanResourceArenaTests PRIVATE cxx_std_20)
  target_link_libraries(VulkanResourceArenaTests PRIVATE vulkax_physics_ir Vulkan::Vulkan)
  add_test(NAME vulkan_reflected_resource_runtime COMMAND VulkanResourceArenaTests)
  set_tests_properties(
    vulkan_reflected_resource_runtime PROPERTIES
    SKIP_RETURN_CODE 77
    LABELS "native_gpu;physics_ir"
  )

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

  add_executable(SparseBrickStorageTests tests/sparse_brick_storage_tests.cpp)
  target_compile_features(SparseBrickStorageTests PRIVATE cxx_std_20)
  target_link_libraries(SparseBrickStorageTests PRIVATE vulkax_sim)
  vulkax_enable_test_assertions(SparseBrickStorageTests)
  add_test(NAME sparse_brick_storage COMMAND SparseBrickStorageTests)
  set_tests_properties(sparse_brick_storage PROPERTIES LABELS "volume_example;sparse_storage")

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
  add_test(NAME vulkan_sparse_brick_compute COMMAND VulkaxSparseBrickCompute)
  set_tests_properties(
    vulkan_sparse_brick_compute PROPERTIES
    LABELS "native_gpu;volume_example;sparse_storage")
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
      NAME physics_studio_gpu_resize_smoke
      COMMAND ${VULKAX_PHYSICS_STUDIO_TEST_EXECUTABLE} --gpu-resize-smoke
    )
    set_tests_properties(
      physics_studio_gpu_resize_smoke PROPERTIES
      ENVIRONMENT "${VULKAX_QT_TEST_ENV}"
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      LABELS "native_gpu;runtime_contract"
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
              tests/fixtures/airflow_cube.obj
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
