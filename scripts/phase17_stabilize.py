from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]

def run(script: str) -> None:
    subprocess.run(["python3", str(root / script)], cwd=root, check=True)

# Finish the source portions of the prematurely merged Windows and canonical
# compiler phases. Both scripts are idempotent against the current pre-patch
# main and remove themselves after applying their intended migrations.
run("scripts/phase12_windows.py")

atlas = root / "src/atlas/streaming/tile_source.cpp"
text = atlas.read_text()
old = 'if (relativeToRoot.empty() || relativeToRoot.native().starts_with("..")) {'
new = (
    'const auto firstComponent = relativeToRoot.begin();\n'
    '        if (relativeToRoot.empty() ||\n'
    '            (firstComponent != relativeToRoot.end() &&\n'
    '             *firstComponent == std::filesystem::path{".."})) {'
)
if old in text:
    atlas.write_text(text.replace(old, new, 1))
else:
    if 'firstComponent == relativeToRoot.end()' in text:
        raise SystemExit('unexpected Atlas containment implementation')
    if 'std::filesystem::path{".."}' not in text:
        raise SystemExit('Atlas containment marker missing')

fix_atlas = root / "scripts/phase12_fix_atlas.py"
if fix_atlas.exists():
    fix_atlas.unlink()

run("scripts/phase13_canonical_bridge.py")

package = root / "apps/VulkaxPhysicsStudioMac/Package.swift"
text = package.read_text()
bad = '    cxxLanguageStandard: .cxx20,\n    swiftLanguageModes: [.v5]\n)'
good = '    swiftLanguageModes: [.v5],\n    cxxLanguageStandard: .cxx20\n)'
if bad in text:
    text = text.replace(bad, good, 1)
elif good not in text:
    raise SystemExit('SwiftPM C++ language-standard marker missing')
package.write_text(text)

# Wire the already-merged first vulkax_gpu slice into the build. The wave
# compute executable was migrated to this context before its CMake target was
# added, leaving current main link-incomplete.
cmake = root / "CMakeLists.txt"
text = cmake.read_text()
runtime_block = '''add_library(vulkax_runtime_paths STATIC src/runtime_paths.cpp)\ntarget_compile_features(vulkax_runtime_paths PUBLIC cxx_std_20)\ntarget_include_directories(vulkax_runtime_paths PUBLIC ${PROJECT_SOURCE_DIR}/src)\n'''
gpu_block = runtime_block + '''\nadd_library(vulkax_gpu STATIC src/vulkax/gpu/vulkan_compute_context.cpp)\ntarget_compile_features(vulkax_gpu PUBLIC cxx_std_20)\ntarget_include_directories(vulkax_gpu PUBLIC ${PROJECT_SOURCE_DIR}/src)\ntarget_link_libraries(vulkax_gpu PUBLIC Vulkan::Vulkan)\n'''
if 'add_library(vulkax_gpu STATIC src/vulkax/gpu/vulkan_compute_context.cpp)' not in text:
    if runtime_block not in text:
        raise SystemExit('runtime target marker missing')
    text = text.replace(runtime_block, gpu_block, 1)
text = text.replace(
    'target_link_libraries(VulkaxComputeBenchmark PRIVATE Vulkan::Vulkan vulkax_runtime_paths)',
    'target_link_libraries(VulkaxComputeBenchmark PRIVATE vulkax_gpu vulkax_runtime_paths)')
cmake.write_text(text)

# Temporary patch scripts have no place in the durable repository.
for relative in [
    'scripts/phase13_fix_manifest.py',
    'scripts/phase17_stabilize.py',
]:
    path = root / relative
    if path.exists():
        path.unlink()
