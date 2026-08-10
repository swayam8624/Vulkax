from pathlib import Path

cmake = Path('CMakeLists.txt')
text = cmake.read_text()
old = '''  src/vulkax/physics/compute_ir.cpp
  src/vulkax/physics/stencil_ir.cpp
'''
new = '''  src/vulkax/physics/compute_ir.cpp
  src/vulkax/physics/vector_compute_ir.cpp
  src/vulkax/physics/tensor_compute_ir.cpp
  src/vulkax/physics/stencil_ir.cpp
'''
if old in text:
    text = text.replace(old, new, 1)
elif 'src/vulkax/physics/tensor_compute_ir.cpp' not in text:
    raise SystemExit('vulkax_physics_ir source marker missing')
cmake.write_text(text)

tests = Path('cmake/VulkaxTests.cmake')
text = tests.read_text()
marker = '''  add_executable(
    MediumInferenceTests
    tests/medium_inference_tests.cpp
  )
'''
block = '''  add_executable(
    TensorComputeIrTests
    tests/tensor_compute_ir_tests.cpp
  )
  target_compile_features(TensorComputeIrTests PRIVATE cxx_std_20)
  target_link_libraries(TensorComputeIrTests PRIVATE vulkax_physics_ir)
  vulkax_enable_test_assertions(TensorComputeIrTests)
  add_test(NAME tensor_compute_ir COMMAND TensorComputeIrTests)
  set_tests_properties(tensor_compute_ir PROPERTIES LABELS "physics_ir;tensor_field")

'''
if 'TensorComputeIrTests' not in text:
    if marker not in text: raise SystemExit('VulkaxTests insertion marker missing')
    text = text.replace(marker, block + marker, 1)
tests.write_text(text)
