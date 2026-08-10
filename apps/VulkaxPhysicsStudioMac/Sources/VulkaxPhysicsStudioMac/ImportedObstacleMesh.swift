import Foundation
import simd

struct ObstacleMeshDiagnostics {
    var triangleCount: Int
    var degenerateTriangles: Int
    var boundaryEdges: Int
    var nonManifoldEdges: Int
    var inconsistentWindingEdges: Int

    var isWatertight: Bool {
        degenerateTriangles == 0 && boundaryEdges == 0 && nonManifoldEdges == 0 &&
            inconsistentWindingEdges == 0
    }

    var summary: String {
        if isWatertight { return "\(triangleCount) triangles · closed manifold" }
        return "\(triangleCount) triangles · \(boundaryEdges) boundary · " +
            "\(nonManifoldEdges) non-manifold · \(inconsistentWindingEdges) winding"
    }
}

struct ImportedMeshMaterial {
    var name: String = "Default"
    var baseColorFactor = SIMD4<Float>(0.18, 0.42, 0.45, 1)
    var metallicFactor: Float = 0
    var roughnessFactor: Float = 0.45
    var emissiveFactor = SIMD3<Float>(repeating: 0)
    var baseColorTextureData: Data?
    var baseColorTextureMimeType: String?
    var metallicRoughnessTextureData: Data?
    var metallicRoughnessTextureMimeType: String?
    var normalTextureData: Data?
    var normalTextureMimeType: String?
    var normalScale: Float = 1

    static let `default` = ImportedMeshMaterial()
}

private struct MeshEdge: Hashable {
    var lower: UInt32
    var upper: UInt32
}

private struct EdgeUse {
    var count = 0
    var orientation = 0
}

private struct ObstacleMeshTopologyError: LocalizedError {
    var diagnostics: ObstacleMeshDiagnostics

    var errorDescription: String? {
        "Mesh is not a closed manifold: \(diagnostics.degenerateTriangles) degenerate triangles, " +
            "\(diagnostics.boundaryEdges) boundary edges, \(diagnostics.nonManifoldEdges) " +
            "non-manifold edges, \(diagnostics.inconsistentWindingEdges) winding conflicts"
    }
}

struct ImportedObstacleMesh {
    var vertices: [SIMD4<Float>]
    var indices: [UInt32]
    var diagnostics: ObstacleMeshDiagnostics
    // Optional render-only attributes. The numerical voxelizer intentionally
    // consumes only vertices/indices so material complexity cannot destabilize physics.
    var vertexNormals: [SIMD3<Float>]
    var vertexTexCoords: [SIMD2<Float>]
    // One material index per triangle. Missing entries fall back to material 0.
    var triangleMaterialIndices: [UInt16]
    var materials: [ImportedMeshMaterial]

    init(
        vertices: [SIMD4<Float>],
        indices: [UInt32],
        diagnostics: ObstacleMeshDiagnostics,
        vertexNormals: [SIMD3<Float>] = [],
        vertexTexCoords: [SIMD2<Float>] = [],
        triangleMaterialIndices: [UInt16] = [],
        materials: [ImportedMeshMaterial] = [.default]
    ) {
        self.vertices = vertices
        self.indices = indices
        self.diagnostics = diagnostics
        self.vertexNormals = vertexNormals
        self.vertexTexCoords = vertexTexCoords
        self.triangleMaterialIndices = triangleMaterialIndices
        self.materials = materials.isEmpty ? [.default] : materials
    }

    static func load(from url: URL, requireWatertight: Bool = true) throws -> ImportedObstacleMesh {
        switch url.pathExtension.lowercased() {
        case "obj": return try loadOBJ(from: url, requireWatertight: requireWatertight)
        case "gltf", "glb": return try loadGLTF(from: url, requireWatertight: requireWatertight)
        default:
            if canImportWithModelIO(extension: url.pathExtension) {
                return try loadModelIOCompatibility(from: url, requireWatertight: requireWatertight)
            }
            throw NSError(
                domain: "VulkaxMeshImport", code: 1,
                userInfo: [NSLocalizedDescriptionKey:
                    "Unsupported model format .\(url.pathExtension). Use OBJ/glTF/GLB, or a format Model I/O reports as importable on this macOS installation."])
        }
    }

    static func loadOBJ(from url: URL, requireWatertight: Bool = true) throws -> ImportedObstacleMesh {
        let source = try String(contentsOf: url, encoding: .utf8)
        var positions: [SIMD3<Float>] = []
        var triangles: [UInt32] = []

        for rawLine in source.split(whereSeparator: \.isNewline) {
            let fields = rawLine.split(whereSeparator: \.isWhitespace)
            guard let record = fields.first else { continue }
            if record == "v", fields.count >= 4 {
                guard let x = Float(fields[1]), let y = Float(fields[2]), let z = Float(fields[3]) else {
                    throw CocoaError(.fileReadCorruptFile)
                }
                positions.append(SIMD3(x, y, z))
            } else if record == "f", fields.count >= 4 {
                let polygon = try fields.dropFirst().map { token -> UInt32 in
                    guard let rawIndex = Int(token.split(separator: "/", omittingEmptySubsequences: false)[0]),
                          rawIndex != 0 else { throw CocoaError(.fileReadCorruptFile) }
                    let resolved = rawIndex > 0 ? rawIndex - 1 : positions.count + rawIndex
                    guard resolved >= 0, resolved < positions.count else { throw CocoaError(.fileReadCorruptFile) }
                    return UInt32(resolved)
                }
                for index in 1..<(polygon.count - 1) {
                    triangles.append(contentsOf: [polygon[0], polygon[index], polygon[index + 1]])
                }
            }
        }
        guard !positions.isEmpty, !triangles.isEmpty else { throw CocoaError(.fileReadCorruptFile) }
        return try normalizedMesh(
            positions: positions, indices: triangles, requireWatertight: requireWatertight)
    }

    static func normalizedMesh(
        positions: [SIMD3<Float>],
        indices: [UInt32],
        normals: [SIMD3<Float>] = [],
        texCoords: [SIMD2<Float>] = [],
        triangleMaterialIndices: [UInt16] = [],
        materials: [ImportedMeshMaterial] = [.default],
        requireWatertight: Bool
    ) throws -> ImportedObstacleMesh {
        guard !positions.isEmpty, indices.count >= 3, indices.count.isMultiple(of: 3) else {
            throw CocoaError(.fileReadCorruptFile)
        }
        guard indices.allSatisfy({ Int($0) < positions.count }) else {
            throw CocoaError(.fileReadCorruptFile)
        }
        if !normals.isEmpty && normals.count != positions.count { throw CocoaError(.fileReadCorruptFile) }
        if !texCoords.isEmpty && texCoords.count != positions.count { throw CocoaError(.fileReadCorruptFile) }

        var edgeUses: [MeshEdge: EdgeUse] = [:]
        var degenerateTriangles = 0
        for triangle in stride(from: 0, to: indices.count, by: 3) {
            let triangleIndices = [indices[triangle], indices[triangle + 1], indices[triangle + 2]]
            let a = positions[Int(triangleIndices[0])]
            let b = positions[Int(triangleIndices[1])]
            let c = positions[Int(triangleIndices[2])]
            if Set(triangleIndices).count != 3 || simd_length_squared(simd_cross(b - a, c - a)) <= 1e-16 {
                degenerateTriangles += 1
                continue
            }
            for edge in 0..<3 {
                let from = triangleIndices[edge]
                let to = triangleIndices[(edge + 1) % 3]
                let key = MeshEdge(lower: min(from, to), upper: max(from, to))
                var use = edgeUses[key, default: EdgeUse()]
                use.count += 1
                use.orientation += from < to ? 1 : -1
                edgeUses[key] = use
            }
        }
        let diagnostics = ObstacleMeshDiagnostics(
            triangleCount: indices.count / 3,
            degenerateTriangles: degenerateTriangles,
            boundaryEdges: edgeUses.values.filter { $0.count == 1 }.count,
            nonManifoldEdges: edgeUses.values.filter { $0.count != 1 && $0.count != 2 }.count,
            inconsistentWindingEdges: edgeUses.values.filter { $0.count == 2 && $0.orientation != 0 }.count)
        if requireWatertight && !diagnostics.isWatertight {
            throw ObstacleMeshTopologyError(diagnostics: diagnostics)
        }

        var lower = positions[0]
        var upper = positions[0]
        for position in positions {
            lower = simd_min(lower, position)
            upper = simd_max(upper, position)
        }
        let extent = upper - lower
        let scale = Float(0.28) / max(extent.x, max(extent.y, extent.z), Float(1e-6))
        let centre = Float(0.5) * (lower + upper)
        let normalized = positions.map { SIMD4<Float>(($0 - centre) * scale, 0) }
        let normalizedNormals = normals.map { normal -> SIMD3<Float> in
            let lengthSquared = simd_length_squared(normal)
            return lengthSquared > 1e-16 ? normal / sqrt(lengthSquared) : SIMD3<Float>(0, 1, 0)
        }
        return ImportedObstacleMesh(
            vertices: normalized,
            indices: indices,
            diagnostics: diagnostics,
            vertexNormals: normalizedNormals,
            vertexTexCoords: texCoords,
            triangleMaterialIndices: triangleMaterialIndices,
            materials: materials)
    }

    // A car/prop render mesh is often open, non-manifold or far too detailed
    // for robust voxelization. Use its bounds as a closed physics proxy while
    // retaining the original mesh and material data as the visual asset.
    static func boxProxy(for visualMesh: ImportedObstacleMesh) -> ImportedObstacleMesh {
        guard let first = visualMesh.vertices.first else { return fixedBoxProxy() }
        var lower = SIMD3<Float>(first.x, first.y, first.z)
        var upper = lower
        for vertex in visualMesh.vertices {
            let position = SIMD3<Float>(vertex.x, vertex.y, vertex.z)
            lower = simd_min(lower, position)
            upper = simd_max(upper, position)
        }
        let centre = Float(0.5) * (lower + upper)
        let halfExtent = simd_max(Float(0.5) * (upper - lower), SIMD3<Float>(repeating: 0.006))
        lower = centre - halfExtent
        upper = centre + halfExtent
        let vertices: [SIMD4<Float>] = [
            SIMD4(lower.x, lower.y, lower.z, 0), SIMD4(upper.x, lower.y, lower.z, 0),
            SIMD4(upper.x, upper.y, lower.z, 0), SIMD4(lower.x, upper.y, lower.z, 0),
            SIMD4(lower.x, lower.y, upper.z, 0), SIMD4(upper.x, lower.y, upper.z, 0),
            SIMD4(upper.x, upper.y, upper.z, 0), SIMD4(lower.x, upper.y, upper.z, 0)]
        let indices: [UInt32] = [
            0, 2, 1, 0, 3, 2,
            4, 5, 6, 4, 6, 7,
            0, 4, 7, 0, 7, 3,
            1, 2, 6, 1, 6, 5,
            3, 7, 6, 3, 6, 2,
            0, 1, 5, 0, 5, 4]
        return ImportedObstacleMesh(
            vertices: vertices,
            indices: indices,
            diagnostics: .init(
                triangleCount: 12, degenerateTriangles: 0, boundaryEdges: 0,
                nonManifoldEdges: 0, inconsistentWindingEdges: 0))
    }

    private static func fixedBoxProxy() -> ImportedObstacleMesh {
        let visual = ImportedObstacleMesh(
            vertices: [SIMD4<Float>(-0.1, -0.1, -0.1, 0), SIMD4<Float>(0.1, 0.1, 0.1, 0)],
            indices: [],
            diagnostics: .init(
                triangleCount: 0, degenerateTriangles: 0, boundaryEdges: 0,
                nonManifoldEdges: 0, inconsistentWindingEdges: 0))
        return boxProxy(for: visual)
    }
}
