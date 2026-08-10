from pathlib import Path

source = Path('src/vulkax/physics/generated_transport.cpp')
text = source.read_text()
if '#include <bit>' not in text:
    text = text.replace('#include <algorithm>\n', '#include <algorithm>\n#include <bit>\n', 1)
source.write_text(text)

cmake = Path('CMakeLists.txt')
text = cmake.read_text()
marker = '''  src/vulkax/physics/tensor_compute_ir.cpp
  src/vulkax/physics/stencil_ir.cpp
'''
replacement = '''  src/vulkax/physics/tensor_compute_ir.cpp
  src/vulkax/physics/stencil_ir.cpp
  src/vulkax/physics/generated_transport.cpp
'''
if 'src/vulkax/physics/generated_transport.cpp' not in text:
    if marker not in text: raise SystemExit('physics IR source marker missing')
    text = text.replace(marker, replacement, 1)
cmake.write_text(text)

tests = Path('cmake/VulkaxTests.cmake')
text = tests.read_text()
marker = '''  add_executable(
    MediumInferenceTests
    tests/medium_inference_tests.cpp
  )
'''
block = '''  add_executable(
    GeneratedTransportTests
    tests/generated_transport_tests.cpp
  )
  target_compile_features(GeneratedTransportTests PRIVATE cxx_std_20)
  target_link_libraries(GeneratedTransportTests PRIVATE vulkax_physics_ir)
  vulkax_enable_test_assertions(GeneratedTransportTests)
  add_test(NAME generated_transport COMMAND GeneratedTransportTests)
  set_tests_properties(generated_transport PROPERTIES LABELS "physics_ir;generated_solver;transport")

'''
if 'GeneratedTransportTests' not in text:
    if marker not in text: raise SystemExit('generated transport test insertion marker missing')
    text = text.replace(marker, block + marker, 1)
tests.write_text(text)
