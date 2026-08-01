import Foundation
import simd

struct ImportedObstacleMesh {
    var vertices: [SIMD4<Float>]
    var indices: [UInt32]

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

        var lower = positions[0]
        var upper = positions[0]
        for position in positions {
            lower = simd_min(lower, position)
            upper = simd_max(upper, position)
        }
        let extent = upper - lower
        let scale = 0.28 / max(extent.x, max(extent.y, extent.z), 1e-6)
        let centre = 0.5 * (lower + upper)
        let target = SIMD3<Float>(0.66, 0.30, 0.50)
        let normalized = positions.map { SIMD4<Float>((($0 - centre) * scale) + target, 1) }
        return ImportedObstacleMesh(vertices: normalized, indices: triangles)
    }
}
