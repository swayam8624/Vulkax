import Foundation
import ModelIO

extension ImportedObstacleMesh {
    static func canImportWithModelIO(extension fileExtension: String) -> Bool {
        let normalized = fileExtension.lowercased().trimmingCharacters(in: CharacterSet(charactersIn: "."))
        return !normalized.isEmpty && MDLAsset.canImportFileExtension(normalized)
    }

    static func loadModelIOCompatibility(
        from url: URL,
        requireWatertight: Bool = true
    ) throws -> ImportedObstacleMesh {
        let fileExtension = url.pathExtension.lowercased()
        guard canImportWithModelIO(extension: fileExtension) else {
            throw NSError(
                domain: "VulkaxModelIOImport",
                code: 1,
                userInfo: [NSLocalizedDescriptionKey:
                    "Model I/O on this macOS installation cannot import .\(fileExtension). Convert the asset to glTF/GLB for full Vulkax PBR support."])
        }
        guard MDLAsset.canExportFileExtension("obj") else {
            throw NSError(
                domain: "VulkaxModelIOImport",
                code: 2,
                userInfo: [NSLocalizedDescriptionKey: "Model I/O cannot export the compatibility mesh to OBJ on this system."])
        }

        let asset = MDLAsset(url: url)
        guard asset.count > 0 else {
            throw NSError(
                domain: "VulkaxModelIOImport",
                code: 3,
                userInfo: [NSLocalizedDescriptionKey: "The imported asset contains no scene objects."])
        }

        let temporaryRoot = FileManager.default.temporaryDirectory
            .appendingPathComponent("vulkax-modelio-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: temporaryRoot, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: temporaryRoot) }
        let flattened = temporaryRoot.appendingPathComponent("compatibility.obj")
        try asset.export(to: flattened)
        return try loadOBJ(from: flattened, requireWatertight: requireWatertight)
    }
}
