from pathlib import Path

mesh = Path('apps/VulkaxPhysicsStudioMac/Sources/VulkaxPhysicsStudioMac/ImportedObstacleMesh.swift')
text = mesh.read_text()
marker = '''    var baseColorTextureData: Data?
    var baseColorTextureMimeType: String?
'''
addition = marker + '''    var metallicRoughnessTextureData: Data?
    var metallicRoughnessTextureMimeType: String?
    var normalTextureData: Data?
    var normalTextureMimeType: String?
    var normalScale: Float = 1
'''
if 'metallicRoughnessTextureData' not in text:
    if marker not in text: raise SystemExit('material texture marker missing')
    text = text.replace(marker, addition, 1)
mesh.write_text(text)

importer = Path('apps/VulkaxPhysicsStudioMac/Sources/VulkaxPhysicsStudioMac/ImportedGltfAsset.swift')
text = importer.read_text()
base_block = '''                if let textureInfo = pbr["baseColorTexture"] as? [String: Any], let textureIndex = textureInfo["index"] as? Int,
                   textures.indices.contains(textureIndex), let imageIndex = textures[textureIndex]["source"] as? Int,
                   let image = try imageData(index: imageIndex) {
                    result.baseColorTextureData = image.0
                    result.baseColorTextureMimeType = image.1
                }
'''
mr_block = base_block + '''                if let textureInfo = pbr["metallicRoughnessTexture"] as? [String: Any], let textureIndex = textureInfo["index"] as? Int,
                   textures.indices.contains(textureIndex), let imageIndex = textures[textureIndex]["source"] as? Int,
                   let image = try imageData(index: imageIndex) {
                    result.metallicRoughnessTextureData = image.0
                    result.metallicRoughnessTextureMimeType = image.1
                }
'''
if 'metallicRoughnessTextureData' not in text:
    if base_block not in text: raise SystemExit('base color importer marker missing')
    text = text.replace(base_block, mr_block, 1)
normal_insert = '''            if let normalInfo = source["normalTexture"] as? [String: Any], let textureIndex = normalInfo["index"] as? Int,
               textures.indices.contains(textureIndex), let imageIndex = textures[textureIndex]["source"] as? Int,
               let image = try imageData(index: imageIndex) {
                result.normalTextureData = image.0
                result.normalTextureMimeType = image.1
                if let scale = normalInfo["scale"] as? NSNumber { result.normalScale = scale.floatValue }
            }
'''
if 'result.normalTextureData' not in text:
    marker = '            if let emissive = source["emissiveFactor"] as? [NSNumber], emissive.count == 3 {\n'
    if marker not in text: raise SystemExit('normal texture insertion marker missing')
    text = text.replace(marker, normal_insert + marker, 1)
importer.write_text(text)

renderer = Path('apps/VulkaxPhysicsStudioMac/Sources/VulkaxPhysicsStudioMac/StudioSceneMeshRenderer.swift')
text = renderer.read_text()
text = text.replace(
'''    let baseColorTexture: MTLTexture
}''',
'''    let baseColorTexture: MTLTexture
    let metallicRoughnessTexture: MTLTexture
    let normalTexture: MTLTexture
}''', 1)
text = text.replace(
'''    private let whiteTexture: MTLTexture
''',
'''    private let whiteTexture: MTLTexture
    private let flatNormalTexture: MTLTexture
''', 1)
old_sig = '''            texture2d<float> baseColorTexture [[texture(0)]],
            sampler surfaceSampler [[sampler(0)]]) {
            float4 sampled = baseColorTexture.sample(surfaceSampler, in.uv);
            float4 baseSample = sampled * material.baseColor;
            float3 albedo = max(baseSample.rgb, float3(0.0));
            float alpha = clamp(baseSample.a, 0.0, 1.0);
            float metallic = clamp(material.emissiveMetallic.w, 0.0, 1.0);
            float roughness = clamp(material.roughnessFlags.x, 0.04, 1.0);
            float3 emissive = max(material.emissiveMetallic.xyz, float3(0.0));

            float3 n = normalize(in.normal);
'''
new_sig = '''            texture2d<float> baseColorTexture [[texture(0)]],
            texture2d<float> metallicRoughnessTexture [[texture(1)]],
            texture2d<float> normalTexture [[texture(2)]],
            sampler surfaceSampler [[sampler(0)]]) {
            float4 sampled = baseColorTexture.sample(surfaceSampler, in.uv);
            float4 mrSample = metallicRoughnessTexture.sample(surfaceSampler, in.uv);
            float4 baseSample = sampled * material.baseColor;
            float3 albedo = max(baseSample.rgb, float3(0.0));
            float alpha = clamp(baseSample.a, 0.0, 1.0);
            float metallic = clamp(material.emissiveMetallic.w * mrSample.b, 0.0, 1.0);
            float roughness = clamp(material.roughnessFlags.x * mrSample.g, 0.04, 1.0);
            float3 emissive = max(material.emissiveMetallic.xyz, float3(0.0));

            float3 n = normalize(in.normal);
            float3 dpdx = dfdx(in.worldPosition);
            float3 dpdy = dfdy(in.worldPosition);
            float2 duvdx = dfdx(in.uv);
            float2 duvdy = dfdy(in.uv);
            float determinant = duvdx.x * duvdy.y - duvdx.y * duvdy.x;
            if (abs(determinant) > 1e-7) {
                float3 tangent = normalize((dpdx * duvdy.y - dpdy * duvdx.y) / determinant);
                tangent = normalize(tangent - n * dot(n, tangent));
                float3 bitangent = normalize(cross(n, tangent));
                float3 mapped = normalTexture.sample(surfaceSampler, in.uv).xyz * 2.0 - 1.0;
                mapped.xy *= material.roughnessFlags.y;
                mapped = normalize(mapped);
                n = normalize(tangent * mapped.x + bitangent * mapped.y + n * mapped.z);
            }
'''
if old_sig in text:
    text = text.replace(old_sig, new_sig, 1)
elif 'metallicRoughnessTexture [[texture(1)]]' not in text:
    raise SystemExit('PBR fragment marker missing')
white_tail = '''        self.whiteTexture = whiteTexture
    }
'''
flat_tail = '''        self.whiteTexture = whiteTexture

        let flat = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .rgba8Unorm, width: 1, height: 1, mipmapped: false)
        flat.usage = [.shaderRead]
        guard let flatNormalTexture = device.makeTexture(descriptor: flat) else {
            throw NSError(domain: "VulkaxSceneRenderer", code: 5,
                          userInfo: [NSLocalizedDescriptionKey: "Could not create fallback normal texture"])
        }
        var normalPixel: [UInt8] = [128, 128, 255, 255]
        flatNormalTexture.replace(
            region: MTLRegionMake2D(0, 0, 1, 1), mipmapLevel: 0,
            withBytes: &normalPixel, bytesPerRow: 4)
        self.flatNormalTexture = flatNormalTexture
    }
'''
if 'self.flatNormalTexture' not in text:
    if white_tail not in text: raise SystemExit('fallback texture marker missing')
    text = text.replace(white_tail, flat_tail, 1)
text = text.replace(
'''            encoder.setFragmentTexture(batch.baseColorTexture, index: 0)
            encoder.drawPrimitives''',
'''            encoder.setFragmentTexture(batch.baseColorTexture, index: 0)
            encoder.setFragmentTexture(batch.metallicRoughnessTexture, index: 1)
            encoder.setFragmentTexture(batch.normalTexture, index: 2)
            encoder.drawPrimitives''', 1)
old_texture = '''                let texture: MTLTexture
                if let textureData = sourceMaterial.baseColorTextureData,
                   let loaded = try? textureLoader.newTexture(
                    data: textureData,
                    options: [
                        .SRGB: true,
                        .textureUsage: NSNumber(value: MTLTextureUsage.shaderRead.rawValue)
                    ]) {
                    texture = loaded
                } else {
                    texture = whiteTexture
                }
'''
new_texture = '''                func loadedTexture(_ data: Data?, srgb: Bool, fallback: MTLTexture) -> MTLTexture {
                    guard let data, let loaded = try? textureLoader.newTexture(
                        data: data,
                        options: [
                            .SRGB: NSNumber(value: srgb),
                            .textureUsage: NSNumber(value: MTLTextureUsage.shaderRead.rawValue)
                        ]) else { return fallback }
                    return loaded
                }
                let texture = loadedTexture(sourceMaterial.baseColorTextureData, srgb: true, fallback: whiteTexture)
                let metallicRoughnessTexture = loadedTexture(
                    sourceMaterial.metallicRoughnessTextureData, srgb: false, fallback: whiteTexture)
                let normalTexture = loadedTexture(
                    sourceMaterial.normalTextureData, srgb: false, fallback: flatNormalTexture)
'''
if old_texture in text:
    text = text.replace(old_texture, new_texture, 1)
elif 'let metallicRoughnessTexture = loadedTexture' not in text:
    raise SystemExit('material texture loader marker missing')
text = text.replace(
'''                    roughnessFlags: SIMD4(sourceMaterial.roughnessFactor, 0, 0, 0))''',
'''                    roughnessFlags: SIMD4(sourceMaterial.roughnessFactor, sourceMaterial.normalScale, 0, 0))''', 1)
text = text.replace(
'''                    material: material,
                    baseColorTexture: texture))''',
'''                    material: material,
                    baseColorTexture: texture,
                    metallicRoughnessTexture: metallicRoughnessTexture,
                    normalTexture: normalTexture))''', 1)
renderer.write_text(text)
