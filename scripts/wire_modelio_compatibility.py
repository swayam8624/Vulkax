from pathlib import Path

extensions = '["obj", "gltf", "glb", "fbx", "abc", "usd", "usda", "usdc", "usdz", "ply", "stl"]'

mesh = Path('apps/VulkaxPhysicsStudioMac/Sources/VulkaxPhysicsStudioMac/ImportedObstacleMesh.swift')
text = mesh.read_text()
old = '''        default:
            throw NSError(
                domain: "VulkaxMeshImport", code: 1,
                userInfo: [NSLocalizedDescriptionKey: "Unsupported model format .\\(url.pathExtension). Use OBJ, glTF or GLB."])
'''
new = '''        default:
            if canImportWithModelIO(extension: url.pathExtension) {
                return try loadModelIOCompatibility(from: url, requireWatertight: requireWatertight)
            }
            throw NSError(
                domain: "VulkaxMeshImport", code: 1,
                userInfo: [NSLocalizedDescriptionKey:
                    "Unsupported model format .\\(url.pathExtension). Use OBJ/glTF/GLB, or a format Model I/O reports as importable on this macOS installation."])
'''
if old in text:
    text = text.replace(old, new, 1)
elif 'loadModelIOCompatibility(from: url' not in text:
    raise SystemExit('generic mesh loader marker missing')
mesh.write_text(text)

main = Path('apps/VulkaxPhysicsStudioMac/Sources/VulkaxPhysicsStudioMac/main.swift')
text = main.read_text()
old_picker = '["obj", "gltf", "glb"].compactMap { UTType(filenameExtension: $0) }'
new_picker = extensions + '.compactMap { UTType(filenameExtension: $0) }'
if old_picker in text:
    text = text.replace(old_picker, new_picker, 1)
elif new_picker not in text:
    raise SystemExit('native model picker marker missing')
main.write_text(text)

workspace = Path('apps/VulkaxPhysicsStudioMac/Sources/VulkaxPhysicsStudioMac/StudioWorkspaceView.swift')
text = workspace.read_text()
old_drop = '["obj", "gltf", "glb"].contains(url.pathExtension.lowercased())'
new_drop = extensions + '.contains(url.pathExtension.lowercased())'
if old_drop in text:
    text = text.replace(old_drop, new_drop, 1)
elif new_drop not in text:
    raise SystemExit('viewport drop marker missing')
text = text.replace('Drop OBJ, glTF or GLB · or add a car/model',
                    'Drop OBJ/glTF/GLB or a supported Model I/O asset · or add a car/model')
text = text.replace('Import OBJ, glTF or GLB visual models. Vulkax creates a safe physics proxy when needed.',
                    'Import OBJ/glTF/GLB or a macOS Model I/O-compatible static asset. Vulkax creates a safe physics proxy when needed.')
workspace.write_text(text)
