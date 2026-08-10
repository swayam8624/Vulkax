from pathlib import Path

main = Path('apps/VulkaxPhysicsStudioMac/Sources/VulkaxPhysicsStudioMac/main.swift')
text = main.read_text()
old_picker = 'panel.allowedContentTypes = [UTType(filenameExtension: "obj") ?? .data]'
new_picker = 'panel.allowedContentTypes = ["obj", "gltf", "glb"].compactMap { UTType(filenameExtension: $0) }'
if old_picker in text:
    text = text.replace(old_picker, new_picker, 1)
elif new_picker not in text:
    raise SystemExit('model picker marker missing')
old_loader = 'let visualMesh = try ImportedObstacleMesh.loadOBJ(from: url, requireWatertight: false)'
new_loader = 'let visualMesh = try ImportedObstacleMesh.load(from: url, requireWatertight: false)'
if old_loader in text:
    text = text.replace(old_loader, new_loader, 1)
elif new_loader not in text:
    raise SystemExit('generic model loader marker missing')
old_save = '''                let path = try PhysicsProjectIO.packageObstacle(
                    from: item.url, for: url, assetName: "obstacle-\\(index).obj")
'''
new_save = '''                let sourceExtension = item.url.pathExtension.lowercased()
                let assetExtension = sourceExtension.isEmpty ? "obj" : sourceExtension
                let path = try PhysicsProjectIO.packageObstacle(
                    from: item.url, for: url, assetName: "obstacle-\\(index).\\(assetExtension)")
'''
if old_save in text:
    text = text.replace(old_save, new_save, 1)
elif 'assetExtension = sourceExtension.isEmpty ? "obj" : sourceExtension' not in text:
    raise SystemExit('project asset extension marker missing')
main.write_text(text)

project = Path('apps/VulkaxPhysicsStudioMac/Sources/VulkaxPhysicsStudioMac/PhysicsProject.swift')
text = project.read_text()
old = '''    static func packageObstacle(
        from source: URL,
        for projectURL: URL,
        assetName: String = "obstacle.obj"
    ) throws -> String {
        let assetDirectory = projectURL.deletingPathExtension().appendingPathExtension("assets")
        try FileManager.default.createDirectory(
            at: assetDirectory, withIntermediateDirectories: true)
        let destination = assetDirectory.appendingPathComponent(assetName)
        if source.standardizedFileURL != destination.standardizedFileURL {
            try Data(contentsOf: source).write(to: destination, options: .atomic)
        }
        return assetDirectory.lastPathComponent + "/" + destination.lastPathComponent
    }
'''
new = '''    static func packageObstacle(
        from source: URL,
        for projectURL: URL,
        assetName: String = "obstacle.obj"
    ) throws -> String {
        let assetDirectory = projectURL.deletingPathExtension().appendingPathExtension("assets")
        try FileManager.default.createDirectory(
            at: assetDirectory, withIntermediateDirectories: true)

        func safeRelativePath(_ raw: String) throws -> String? {
            if raw.hasPrefix("data:") { return nil }
            guard URL(string: raw)?.scheme == nil else {
                throw CocoaError(.fileReadUnsupportedScheme)
            }
            let decoded = raw.removingPercentEncoding ?? raw
            let components = NSString(string: decoded).pathComponents
            guard !decoded.hasPrefix("/"), !components.contains(".."), !components.contains("~") else {
                throw CocoaError(.fileReadInvalidFileName)
            }
            return decoded
        }

        func copyAsset(_ input: URL, _ output: URL) throws {
            try FileManager.default.createDirectory(
                at: output.deletingLastPathComponent(), withIntermediateDirectories: true)
            if input.standardizedFileURL == output.standardizedFileURL { return }
            if FileManager.default.fileExists(atPath: output.path) {
                try FileManager.default.removeItem(at: output)
            }
            try FileManager.default.copyItem(at: input, to: output)
        }

        let destination = assetDirectory.appendingPathComponent(assetName)
        try copyAsset(source, destination)
        if source.pathExtension.lowercased() == "gltf" {
            guard let root = try JSONSerialization.jsonObject(with: Data(contentsOf: source)) as? [String: Any] else {
                throw CocoaError(.fileReadCorruptFile)
            }
            var dependencies: [String] = []
            for buffer in root["buffers"] as? [[String: Any]] ?? [] {
                if let uri = buffer["uri"] as? String { dependencies.append(uri) }
            }
            for image in root["images"] as? [[String: Any]] ?? [] {
                if let uri = image["uri"] as? String { dependencies.append(uri) }
            }
            let sourceRoot = source.deletingLastPathComponent()
            let packagedRoot = assetDirectory.standardizedFileURL.path + "/"
            for raw in Set(dependencies) {
                guard let relative = try safeRelativePath(raw) else { continue }
                let input = sourceRoot.appendingPathComponent(relative).standardizedFileURL
                let output = assetDirectory.appendingPathComponent(relative).standardizedFileURL
                guard output.path.hasPrefix(packagedRoot) else { throw CocoaError(.fileReadInvalidFileName) }
                try copyAsset(input, output)
            }
        }
        return assetDirectory.lastPathComponent + "/" + destination.lastPathComponent
    }
'''
if old in text:
    text = text.replace(old, new, 1)
elif 'func safeRelativePath(_ raw: String)' not in text:
    raise SystemExit('PhysicsProject packageObstacle marker missing')
project.write_text(text)
