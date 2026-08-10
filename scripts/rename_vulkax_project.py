from pathlib import Path

root = Path('CMakeLists.txt')
text = root.read_text()
if 'set(VULKAX_LEGACY_TARGET LveEngine)' not in text:
    text = text.replace('set(NAME LveEngine)\n', 'set(VULKAX_LEGACY_TARGET LveEngine)\n', 1)
text = text.replace('project(${NAME} VERSION 0.24.0)', 'project(Vulkax VERSION 0.24.0)', 1)
text = text.replace('${PROJECT_NAME}', '${VULKAX_LEGACY_TARGET}')
if 'project(Vulkax VERSION 0.24.0)' not in text or 'add_executable(${VULKAX_LEGACY_TARGET} ${SOURCES})' not in text:
    raise SystemExit('root CMake identity migration incomplete')
root.write_text(text)

apps = Path('cmake/VulkaxApplications.cmake')
text = apps.read_text().replace('${PROJECT_NAME}', '${VULKAX_LEGACY_TARGET}')
if 'add_dependencies(${VULKAX_LEGACY_TARGET} Shaders)' not in text:
    raise SystemExit('application legacy-target migration incomplete')
apps.write_text(text)
