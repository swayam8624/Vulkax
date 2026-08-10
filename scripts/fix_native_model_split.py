from pathlib import Path

main = Path('apps/VulkaxPhysicsStudioMac/Sources/VulkaxPhysicsStudioMac/main.swift')
text = main.read_text()
old = 'enum VisualizerMode: Float, CaseIterable, Identifiable {'
new = 'enum VisualizerMode: Float, CaseIterable, Identifiable, Codable {'
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit('VisualizerMode marker missing')
text = text.replace('old[name] ?? defaults[name] ?? LiveParameter(',
                    'old[name] ?? defaults[name] ?? ScalarPresetParameter(')
main.write_text(text)

project = Path('apps/VulkaxPhysicsStudioMac/Sources/VulkaxPhysicsStudioMac/PhysicsProject.swift')
text = project.read_text()
start = text.find('enum VisualizerMode: String, CaseIterable, Identifiable, Codable {')
if start != -1:
    end_marker = '\n}\n\nstruct ScalarPresetParameter'
    end = text.find(end_marker, start)
    if end == -1: raise SystemExit('duplicate VisualizerMode end marker missing')
    text = text[:start] + 'struct ScalarPresetParameter' + text[end + len(end_marker):]
if 'var units: String = ""' not in text:
    marker = '    var maximum: Float\n'
    if marker not in text: raise SystemExit('ScalarPresetParameter marker missing')
    text = text.replace(marker, marker + '    var units: String = ""\n', 1)
if 'typealias LiveParameter = ScalarPresetParameter' not in text:
    marker = '\n}\n\nstruct ScalarPreset: Identifiable, Hashable, Codable {'
    if marker not in text: raise SystemExit('ScalarPreset marker missing')
    text = text.replace(marker, '\n}\n\ntypealias LiveParameter = ScalarPresetParameter\n\nstruct ScalarPreset: Identifiable, Hashable, Codable {', 1)
project.write_text(text)
