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
        "OBJ is not a closed manifold: \(diagnostics.degenerateTriangles) degenerate triangles, " +
            "\(diagnostics.boundaryEdges) boundary edges, \(diagnostics.nonManifoldEdges) " +
            "non-manifold edges, \(diagnostics.inconsistentWindingEdges) winding conflicts"
    }
}

struct ImportedObstacleMesh {
    var vertices: [SIMD4<Float>]
    var indices: [UInt32]
    var diagnostics: ObstacleMeshDiagnostics

    static func loadOBJ(from url: URL) throws -> ImportedObstacleMesh {
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

        var edgeUses: [MeshEdge: EdgeUse] = [:]
        var degenerateTriangles = 0
        for triangle in stride(from: 0, to: triangles.count, by: 3) {
            let triangleIndices = [triangles[triangle], triangles[triangle + 1], triangles[triangle + 2]]
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
            triangleCount: triangles.count / 3,
            degenerateTriangles: degenerateTriangles,
            boundaryEdges: edgeUses.values.filter { $0.count == 1 }.count,
            nonManifoldEdges: edgeUses.values.filter { $0.count != 1 && $0.count != 2 }.count,
            inconsistentWindingEdges: edgeUses.values.filter { $0.count == 2 && $0.orientation != 0 }.count)
        guard diagnostics.isWatertight else {
            throw ObstacleMeshTopologyError(diagnostics: diagnostics)
        }

        var lower = positions[0]
        var upper = positions[0]
        for position in positions {
            lower = simd_min(lower, position)
            upper = simd_max(upper, position)
        }
        let extent = upper - lower
        let scale = 0.28 / max(extent.x, max(extent.y, extent.z), 1e-6)
        let centre = 0.5 * (lower + upper)
        let normalized = positions.map { SIMD4<Float>(($0 - centre) * scale, 1) }
        return ImportedObstacleMesh(vertices: normalized, indices: triangles, diagnostics: diagnostics)
    }
}
