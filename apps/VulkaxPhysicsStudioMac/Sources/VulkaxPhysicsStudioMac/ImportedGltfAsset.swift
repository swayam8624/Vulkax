import Foundation
import simd

private enum GltfImportError: LocalizedError {
    case invalid(String)
    case unsupported(String)

    var errorDescription: String? {
        switch self {
        case let .invalid(message): return "Invalid glTF: \(message)"
        case let .unsupported(message): return "Unsupported glTF feature: \(message)"
        }
    }
}

private struct GltfSource {
    let json: [String: Any]
    let binaryChunk: Data?
    let sourceURL: URL

    static func load(_ url: URL) throws -> GltfSource {
        let data = try Data(contentsOf: url)
        if url.pathExtension.lowercased() != "glb" {
            guard let json = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
                throw GltfImportError.invalid("root JSON object is missing")
            }
            return .init(json: json, binaryChunk: nil, sourceURL: url)
        }
        guard data.count >= 20,
              data.readUInt32LE(at: 0) == 0x46546C67,
              data.readUInt32LE(at: 4) == 2,
              Int(data.readUInt32LE(at: 8)) <= data.count else {
            throw GltfImportError.invalid("GLB header is malformed or not glTF 2.0")
        }
        var offset = 12
        var jsonData: Data?
        var binary: Data?
        while offset + 8 <= data.count {
            let length = Int(data.readUInt32LE(at: offset))
            let type = data.readUInt32LE(at: offset + 4)
            offset += 8
            guard length >= 0, offset + length <= data.count else {
                throw GltfImportError.invalid("GLB chunk exceeds file bounds")
            }
            let chunk = data.subdata(in: offset..<(offset + length))
            if type == 0x4E4F534A { jsonData = chunk }
            if type == 0x004E4942 { binary = chunk }
            offset += length
        }
        guard let jsonData,
              let json = try JSONSerialization.jsonObject(with: jsonData) as? [String: Any] else {
            throw GltfImportError.invalid("GLB JSON chunk is missing")
        }
        return .init(json: json, binaryChunk: binary, sourceURL: url)
    }
}

private final class GltfReader {
    let source: GltfSource
    let buffers: [Data]
    let bufferViews: [[String: Any]]
    let accessors: [[String: Any]]
    let meshes: [[String: Any]]
    let nodes: [[String: Any]]
    let materialsJson: [[String: Any]]
    let textures: [[String: Any]]
    let images: [[String: Any]]

    init(source: GltfSource) throws {
        self.source = source
        bufferViews = source.json["bufferViews"] as? [[String: Any]] ?? []
        accessors = source.json["accessors"] as? [[String: Any]] ?? []
        meshes = source.json["meshes"] as? [[String: Any]] ?? []
        nodes = source.json["nodes"] as? [[String: Any]] ?? []
        materialsJson = source.json["materials"] as? [[String: Any]] ?? []
        textures = source.json["textures"] as? [[String: Any]] ?? []
        images = source.json["images"] as? [[String: Any]] ?? []
        let declarations = source.json["buffers"] as? [[String: Any]] ?? []
        var loaded: [Data] = []
        for (index, declaration) in declarations.enumerated() {
            if let uri = declaration["uri"] as? String {
                loaded.append(try Self.resolveUri(uri, relativeTo: source.sourceURL))
            } else if index == 0, let binary = source.binaryChunk {
                loaded.append(binary)
            } else {
                throw GltfImportError.invalid("buffer \(index) has neither URI nor GLB binary chunk")
            }
            if let required = declaration["byteLength"] as? Int, loaded[index].count < required {
                throw GltfImportError.invalid("buffer \(index) is shorter than byteLength")
            }
        }
        buffers = loaded
    }

    static func resolveUri(_ uri: String, relativeTo sourceURL: URL) throws -> Data {
        if uri.hasPrefix("data:") {
            guard let comma = uri.firstIndex(of: ",") else {
                throw GltfImportError.invalid("data URI has no payload")
            }
            let metadata = String(uri[..<comma])
            let payload = String(uri[uri.index(after: comma)...])
            if metadata.contains(";base64") {
                guard let data = Data(base64Encoded: payload, options: .ignoreUnknownCharacters) else {
                    throw GltfImportError.invalid("base64 data URI is malformed")
                }
                return data
            }
            guard let decoded = payload.removingPercentEncoding?.data(using: .utf8) else {
                throw GltfImportError.invalid("percent-encoded data URI is malformed")
            }
            return decoded
        }
        guard !uri.contains("://") else {
            throw GltfImportError.unsupported("remote buffer/image URIs are not fetched")
        }
        let url = sourceURL.deletingLastPathComponent().appendingPathComponent(uri).standardizedFileURL
        return try Data(contentsOf: url)
    }

    func accessor(_ index: Int) throws -> [String: Any] {
        guard accessors.indices.contains(index) else { throw GltfImportError.invalid("accessor index out of range") }
        let value = accessors[index]
        if value["sparse"] != nil { throw GltfImportError.unsupported("sparse accessors") }
        return value
    }

    func view(_ index: Int) throws -> [String: Any] {
        guard bufferViews.indices.contains(index) else { throw GltfImportError.invalid("bufferView index out of range") }
        return bufferViews[index]
    }

    func bytes(accessorIndex: Int) throws -> (Data, [String: Any], Int, Int) {
        let accessor = try accessor(accessorIndex)
        guard let viewIndex = accessor["bufferView"] as? Int else {
            throw GltfImportError.unsupported("accessors without bufferView")
        }
        let view = try view(viewIndex)
        guard let bufferIndex = view["buffer"] as? Int, buffers.indices.contains(bufferIndex) else {
            throw GltfImportError.invalid("bufferView references invalid buffer")
        }
        let offset = (view["byteOffset"] as? Int ?? 0) + (accessor["byteOffset"] as? Int ?? 0)
        let stride = view["byteStride"] as? Int ?? 0
        return (buffers[bufferIndex], accessor, offset, stride)
    }

    func readFloatVectors(accessorIndex: Int, components: Int, role: String) throws -> [[Float]] {
        let (data, accessor, offset, explicitStride) = try bytes(accessorIndex: accessorIndex)
        let expectedType = components == 2 ? "VEC2" : (components == 3 ? "VEC3" : "VEC4")
        guard accessor["componentType"] as? Int == 5126,
              accessor["type"] as? String == expectedType,
              let count = accessor["count"] as? Int, count >= 0 else {
            throw GltfImportError.unsupported("\(role) must use FLOAT \(expectedType)")
        }
        let elementSize = components * 4
        let stride = explicitStride == 0 ? elementSize : explicitStride
        guard stride >= elementSize else { throw GltfImportError.invalid("\(role) byteStride is too small") }
        var result: [[Float]] = []
        result.reserveCapacity(count)
        for index in 0..<count {
            let begin = offset + index * stride
            guard begin >= 0, begin + elementSize <= data.count else {
                throw GltfImportError.invalid("\(role) accessor exceeds buffer")
            }
            var vector: [Float] = []
            for component in 0..<components {
                let value = data.readFloat32LE(at: begin + component * 4)
                guard value.isFinite else { throw GltfImportError.invalid("\(role) contains non-finite values") }
                vector.append(value)
            }
            result.append(vector)
        }
        return result
    }

    func readIndices(accessorIndex: Int) throws -> [UInt32] {
        let (data, accessor, offset, explicitStride) = try bytes(accessorIndex: accessorIndex)
        guard accessor["type"] as? String == "SCALAR", let count = accessor["count"] as? Int else {
            throw GltfImportError.invalid("index accessor must be SCALAR")
        }
        let componentType = accessor["componentType"] as? Int ?? -1
        let width: Int
        switch componentType {
        case 5121: width = 1
        case 5123: width = 2
        case 5125: width = 4
        default: throw GltfImportError.unsupported("index component type \(componentType)")
        }
        let stride = explicitStride == 0 ? width : explicitStride
        var result: [UInt32] = []
        result.reserveCapacity(count)
        for index in 0..<count {
            let begin = offset + index * stride
            guard begin >= 0, begin + width <= data.count else { throw GltfImportError.invalid("index accessor exceeds buffer") }
            switch width {
            case 1: result.append(UInt32(data[begin]))
            case 2: result.append(UInt32(data.readUInt16LE(at: begin)))
            default: result.append(data.readUInt32LE(at: begin))
            }
        }
        return result
    }

    func imageData(index: Int) throws -> (Data, String?)? {
        guard images.indices.contains(index) else { return nil }
        let image = images[index]
        if let uri = image["uri"] as? String {
            let mime = uri.hasPrefix("data:") ? String(uri.dropFirst(5).prefix { $0 != ";" && $0 != "," }) : nil
            return (try Self.resolveUri(uri, relativeTo: source.sourceURL), mime)
        }
        if let viewIndex = image["bufferView"] as? Int {
            let view = try view(viewIndex)
            guard let bufferIndex = view["buffer"] as? Int, buffers.indices.contains(bufferIndex),
                  let length = view["byteLength"] as? Int else { throw GltfImportError.invalid("image bufferView is malformed") }
            let offset = view["byteOffset"] as? Int ?? 0
            guard offset >= 0, offset + length <= buffers[bufferIndex].count else { throw GltfImportError.invalid("image bufferView exceeds buffer") }
            return (buffers[bufferIndex].subdata(in: offset..<(offset + length)), image["mimeType"] as? String)
        }
        return nil
    }

    func materials() throws -> [ImportedMeshMaterial] {
        if materialsJson.isEmpty { return [.default] }
        return try materialsJson.map { source in
            var result = ImportedMeshMaterial()
            if let name = source["name"] as? String { result.name = name }
            if let pbr = source["pbrMetallicRoughness"] as? [String: Any] {
                if let factor = pbr["baseColorFactor"] as? [NSNumber], factor.count == 4 {
                    result.baseColorFactor = SIMD4(factor[0].floatValue, factor[1].floatValue, factor[2].floatValue, factor[3].floatValue)
                }
                if let value = pbr["metallicFactor"] as? NSNumber { result.metallicFactor = value.floatValue }
                if let value = pbr["roughnessFactor"] as? NSNumber { result.roughnessFactor = value.floatValue }
                if let textureInfo = pbr["baseColorTexture"] as? [String: Any], let textureIndex = textureInfo["index"] as? Int,
                   textures.indices.contains(textureIndex), let imageIndex = textures[textureIndex]["source"] as? Int,
                   let image = try imageData(index: imageIndex) {
                    result.baseColorTextureData = image.0
                    result.baseColorTextureMimeType = image.1
                }
                if let textureInfo = pbr["metallicRoughnessTexture"] as? [String: Any], let textureIndex = textureInfo["index"] as? Int,
                   textures.indices.contains(textureIndex), let imageIndex = textures[textureIndex]["source"] as? Int,
                   let image = try imageData(index: imageIndex) {
                    result.metallicRoughnessTextureData = image.0
                    result.metallicRoughnessTextureMimeType = image.1
                }
            }
            if let normalInfo = source["normalTexture"] as? [String: Any], let textureIndex = normalInfo["index"] as? Int,
               textures.indices.contains(textureIndex), let imageIndex = textures[textureIndex]["source"] as? Int,
               let image = try imageData(index: imageIndex) {
                result.normalTextureData = image.0
                result.normalTextureMimeType = image.1
                if let scale = normalInfo["scale"] as? NSNumber { result.normalScale = scale.floatValue }
            }
            if let emissive = source["emissiveFactor"] as? [NSNumber], emissive.count == 3 {
                result.emissiveFactor = SIMD3(emissive[0].floatValue, emissive[1].floatValue, emissive[2].floatValue)
            }
            result.metallicFactor = min(max(result.metallicFactor, 0), 1)
            result.roughnessFactor = min(max(result.roughnessFactor, 0.04), 1)
            return result
        }
    }
}

extension ImportedObstacleMesh {
    static func loadGLTF(from url: URL, requireWatertight: Bool = true) throws -> ImportedObstacleMesh {
        let source = try GltfSource.load(url)
        guard (source.json["asset"] as? [String: Any])?["version"] as? String == "2.0" else {
            throw GltfImportError.unsupported("only glTF 2.0 is supported")
        }
        let reader = try GltfReader(source: source)
        guard !reader.meshes.isEmpty else { throw GltfImportError.invalid("scene contains no meshes") }
        let materials = try reader.materials()
        var positions: [SIMD3<Float>] = [], normals: [SIMD3<Float>] = []
        var texCoords: [SIMD2<Float>] = [], indices: [UInt32] = []
        var triangleMaterials: [UInt16] = []

        func appendMesh(_ meshIndex: Int, transform: simd_float4x4) throws {
            guard reader.meshes.indices.contains(meshIndex), let primitives = reader.meshes[meshIndex]["primitives"] as? [[String: Any]] else {
                throw GltfImportError.invalid("node references invalid mesh")
            }
            let upper = simd_float3x3(columns: (
                SIMD3(transform.columns.0.x, transform.columns.0.y, transform.columns.0.z),
                SIMD3(transform.columns.1.x, transform.columns.1.y, transform.columns.1.z),
                SIMD3(transform.columns.2.x, transform.columns.2.y, transform.columns.2.z)))
            let normalMatrix = abs(simd_determinant(upper)) > 1e-12 ? simd_transpose(simd_inverse(upper)) : matrix_identity_float3x3
            for primitive in primitives {
                guard primitive["mode"] as? Int ?? 4 == 4 else { throw GltfImportError.unsupported("only TRIANGLES primitives are supported") }
                guard let attributes = primitive["attributes"] as? [String: Any], let positionAccessor = attributes["POSITION"] as? Int else {
                    throw GltfImportError.invalid("primitive has no POSITION accessor")
                }
                let rawPositions = try reader.readFloatVectors(accessorIndex: positionAccessor, components: 3, role: "POSITION")
                let rawNormals = try (attributes["NORMAL"] as? Int).map { try reader.readFloatVectors(accessorIndex: $0, components: 3, role: "NORMAL") }
                let rawUV = try (attributes["TEXCOORD_0"] as? Int).map { try reader.readFloatVectors(accessorIndex: $0, components: 2, role: "TEXCOORD_0") }
                guard rawNormals == nil || rawNormals!.count == rawPositions.count, rawUV == nil || rawUV!.count == rawPositions.count else {
                    throw GltfImportError.invalid("vertex attribute counts do not match POSITION")
                }
                let base = UInt32(positions.count)
                for vertex in rawPositions.indices {
                    let world = transform * SIMD4(rawPositions[vertex][0], rawPositions[vertex][1], rawPositions[vertex][2], 1)
                    positions.append(SIMD3(world.x, world.y, world.z))
                    if let rawNormals {
                        let n = normalMatrix * SIMD3(rawNormals[vertex][0], rawNormals[vertex][1], rawNormals[vertex][2])
                        let l2 = simd_length_squared(n); normals.append(l2 > 1e-16 ? n / sqrt(l2) : .zero)
                    } else { normals.append(.zero) }
                    if let rawUV { texCoords.append(SIMD2(rawUV[vertex][0], rawUV[vertex][1])) } else { texCoords.append(.zero) }
                }
                let local = try (primitive["indices"] as? Int).map { try reader.readIndices(accessorIndex: $0) } ?? rawPositions.indices.map(UInt32.init)
                guard local.count.isMultiple(of: 3), local.allSatisfy({ Int($0) < rawPositions.count }) else { throw GltfImportError.invalid("primitive indices are invalid") }
                let material = primitive["material"] as? Int ?? 0
                guard material >= 0, material < materials.count, material <= Int(UInt16.max) else { throw GltfImportError.invalid("material index is invalid") }
                for triangle in stride(from: 0, to: local.count, by: 3) {
                    indices.append(contentsOf: [base + local[triangle], base + local[triangle + 1], base + local[triangle + 2]])
                    triangleMaterials.append(UInt16(material))
                }
            }
        }

        func localTransform(_ node: [String: Any]) throws -> simd_float4x4 {
            if let matrix = node["matrix"] as? [NSNumber] {
                guard matrix.count == 16 else { throw GltfImportError.invalid("node matrix must contain 16 values") }
                return simd_float4x4(columns: (
                    SIMD4(matrix[0].floatValue, matrix[1].floatValue, matrix[2].floatValue, matrix[3].floatValue),
                    SIMD4(matrix[4].floatValue, matrix[5].floatValue, matrix[6].floatValue, matrix[7].floatValue),
                    SIMD4(matrix[8].floatValue, matrix[9].floatValue, matrix[10].floatValue, matrix[11].floatValue),
                    SIMD4(matrix[12].floatValue, matrix[13].floatValue, matrix[14].floatValue, matrix[15].floatValue)))
            }
            let t = (node["translation"] as? [NSNumber]).flatMap { $0.count == 3 ? SIMD3($0[0].floatValue, $0[1].floatValue, $0[2].floatValue) : nil } ?? .zero
            let s = (node["scale"] as? [NSNumber]).flatMap { $0.count == 3 ? SIMD3($0[0].floatValue, $0[1].floatValue, $0[2].floatValue) : nil } ?? SIMD3(repeating: 1)
            let r = (node["rotation"] as? [NSNumber]).flatMap { $0.count == 4 ? simd_quatf(ix: $0[0].floatValue, iy: $0[1].floatValue, iz: $0[2].floatValue, r: $0[3].floatValue) : nil } ?? simd_quatf(angle: 0, axis: SIMD3(0, 1, 0))
            var translation = matrix_identity_float4x4; translation.columns.3 = SIMD4(t, 1)
            var scale = matrix_identity_float4x4; scale.columns.0.x = s.x; scale.columns.1.y = s.y; scale.columns.2.z = s.z
            return translation * simd_float4x4(simd_normalize(r)) * scale
        }

        var visiting = Set<Int>()
        func visit(_ index: Int, parent: simd_float4x4) throws {
            guard reader.nodes.indices.contains(index), !visiting.contains(index) else { throw GltfImportError.invalid("invalid or cyclic node hierarchy") }
            visiting.insert(index); defer { visiting.remove(index) }
            let node = reader.nodes[index], world = parent * (try localTransform(node))
            if let mesh = node["mesh"] as? Int { try appendMesh(mesh, transform: world) }
            for child in node["children"] as? [Int] ?? [] { try visit(child, parent: world) }
        }

        let scenes = source.json["scenes"] as? [[String: Any]] ?? []
        if !scenes.isEmpty {
            let selected = source.json["scene"] as? Int ?? 0
            guard scenes.indices.contains(selected) else { throw GltfImportError.invalid("default scene index is invalid") }
            for node in scenes[selected]["nodes"] as? [Int] ?? [] { try visit(node, parent: matrix_identity_float4x4) }
        } else if !reader.nodes.isEmpty {
            let children = Set(reader.nodes.flatMap { $0["children"] as? [Int] ?? [] })
            for node in reader.nodes.indices where !children.contains(node) { try visit(node, parent: matrix_identity_float4x4) }
        } else {
            for mesh in reader.meshes.indices { try appendMesh(mesh, transform: matrix_identity_float4x4) }
        }
        guard !positions.isEmpty, !indices.isEmpty else { throw GltfImportError.invalid("selected scene contains no triangle geometry") }

        var accumulated = Array(repeating: SIMD3<Float>.zero, count: positions.count)
        for triangle in stride(from: 0, to: indices.count, by: 3) {
            let a = Int(indices[triangle]), b = Int(indices[triangle + 1]), c = Int(indices[triangle + 2])
            let face = simd_cross(positions[b] - positions[a], positions[c] - positions[a])
            if simd_length_squared(face) > 1e-16 { accumulated[a] += face; accumulated[b] += face; accumulated[c] += face }
        }
        for index in normals.indices where simd_length_squared(normals[index]) <= 1e-16 {
            let l2 = simd_length_squared(accumulated[index]); normals[index] = l2 > 1e-16 ? accumulated[index] / sqrt(l2) : SIMD3(0, 1, 0)
        }
        return try normalizedMesh(positions: positions, indices: indices, normals: normals, texCoords: texCoords,
                                  triangleMaterialIndices: triangleMaterials, materials: materials, requireWatertight: requireWatertight)
    }
}

private extension Data {
    func readUInt16LE(at offset: Int) -> UInt16 { UInt16(self[offset]) | (UInt16(self[offset + 1]) << 8) }
    func readUInt32LE(at offset: Int) -> UInt32 { UInt32(self[offset]) | (UInt32(self[offset + 1]) << 8) | (UInt32(self[offset + 2]) << 16) | (UInt32(self[offset + 3]) << 24) }
    func readFloat32LE(at offset: Int) -> Float { Float(bitPattern: readUInt32LE(at: offset)) }
}
