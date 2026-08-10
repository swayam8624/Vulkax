import Foundation
import Metal
import MetalKit
import simd

private struct StudioSceneVertex {
    var positionBody: SIMD4<Float>
    var normal: SIMD4<Float>
    var texCoord: SIMD4<Float>
}

private struct StudioSceneCameraUniforms {
    var positionExposure: SIMD4<Float>
    var target: SIMD4<Float>
    var upFov: SIMD4<Float>
    var aspectNearFar: SIMD4<Float>
}

private struct StudioSceneMaterialUniforms {
    var baseColor: SIMD4<Float>
    var emissiveMetallic: SIMD4<Float>
    var roughnessFlags: SIMD4<Float>
}

private struct StudioSceneBatch {
    let vertices: MTLBuffer
    let vertexCount: Int
    var material: StudioSceneMaterialUniforms
    let baseColorTexture: MTLTexture
    let metallicRoughnessTexture: MTLTexture
    let normalTexture: MTLTexture
}

final class StudioSceneMeshRenderer {
    private let interactivePipeline: MTLRenderPipelineState
    private let capturePipeline: MTLRenderPipelineState
    private let depthState: MTLDepthStencilState
    private let samplerState: MTLSamplerState
    private let whiteTexture: MTLTexture
    private let flatNormalTexture: MTLTexture
    private var simulatedBatches: [StudioSceneBatch] = []
    private var staticBatches: [StudioSceneBatch] = []
    private var staticBodies: MTLBuffer?
    private var activeRevision: UInt64 = .max
    private var captureDepth: MTLTexture?

    init(device: MTLDevice, interactiveColorFormat: MTLPixelFormat, depthFormat: MTLPixelFormat) throws {
        let source = """
        #include <metal_stdlib>
        using namespace metal;

        struct SceneVertex { float4 positionBody; float4 normal; float4 texCoord; };
        struct RigidMeshState {
            float4 positionMass;
            float4 orientation;
            float4 linearVelocity;
            float4 angularVelocity;
            float4 diagonalInertia;
            float4 scale;
        };
        struct Camera {
            float4 positionExposure;
            float4 target;
            float4 upFov;
            float4 aspectNearFar;
        };
        struct Material {
            float4 baseColor;
            float4 emissiveMetallic;
            float4 roughnessFlags;
        };
        struct Out {
            float4 position [[position]];
            float3 worldPosition;
            float3 normal;
            float2 uv;
        };

        float3 rotateByQuaternion(float3 value, float4 q) {
            float3 t = 2.0 * cross(q.xyz, value);
            return value + q.w * t + cross(q.xyz, t);
        }

        vertex Out studioSceneVertex(
            device const SceneVertex* vertices [[buffer(0)]],
            device const RigidMeshState* bodies [[buffer(1)]],
            constant Camera& camera [[buffer(2)]],
            uint vertexId [[vertex_id]]) {
            SceneVertex source = vertices[vertexId];
            uint bodyIndex = uint(max(source.positionBody.w, 0.0));
            RigidMeshState body = bodies[bodyIndex];
            float4 q = normalize(body.orientation);
            float3 local = source.positionBody.xyz * body.scale.xyz;
            float3 world = rotateByQuaternion(local, q) + body.positionMass.xyz;
            float3 normal = normalize(rotateByQuaternion(source.normal.xyz, q));

            float3 forward = normalize(camera.target.xyz - camera.positionExposure.xyz);
            float3 referenceUp = normalize(camera.upFov.xyz);
            float3 right = normalize(cross(forward, referenceUp));
            float3 cameraUp = normalize(cross(right, forward));
            float3 relative = world - camera.positionExposure.xyz;
            float viewX = dot(relative, right);
            float viewY = dot(relative, cameraUp);
            float viewZ = max(dot(relative, forward), camera.aspectNearFar.y);
            float tanHalf = tan(clamp(camera.upFov.w, 10.0, 120.0) * (M_PI_F / 180.0) * 0.5);
            float nearPlane = camera.aspectNearFar.y;
            float farPlane = camera.aspectNearFar.z;
            float clipZ = (farPlane / (farPlane - nearPlane)) * viewZ -
                          (farPlane * nearPlane / (farPlane - nearPlane));

            Out out;
            out.position = float4(
                viewX / (camera.aspectNearFar.x * tanHalf),
                viewY / tanHalf,
                clipZ,
                viewZ);
            out.worldPosition = world;
            out.normal = normal;
            out.uv = source.texCoord.xy;
            return out;
        }

        float distributionGGX(float3 n, float3 h, float roughness) {
            float a = roughness * roughness;
            float a2 = a * a;
            float ndoth = max(dot(n, h), 0.0);
            float ndoth2 = ndoth * ndoth;
            float denominator = ndoth2 * (a2 - 1.0) + 1.0;
            return a2 / max(M_PI_F * denominator * denominator, 1e-5);
        }

        float geometrySchlickGGX(float ndotv, float roughness) {
            float r = roughness + 1.0;
            float k = (r * r) / 8.0;
            return ndotv / max(ndotv * (1.0 - k) + k, 1e-5);
        }

        float geometrySmith(float3 n, float3 v, float3 l, float roughness) {
            return geometrySchlickGGX(max(dot(n, v), 0.0), roughness) *
                   geometrySchlickGGX(max(dot(n, l), 0.0), roughness);
        }

        float3 fresnelSchlick(float cosTheta, float3 f0) {
            return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
        }

        fragment half4 studioSceneFragment(
            Out in [[stage_in]],
            constant Camera& camera [[buffer(2)]],
            constant Material& material [[buffer(3)]],
            texture2d<float> baseColorTexture [[texture(0)]],
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
            float3 v = normalize(camera.positionExposure.xyz - in.worldPosition);
            float3 l = normalize(float3(-0.42, 0.82, 0.52));
            float3 h = normalize(v + l);
            float ndotl = max(dot(n, l), 0.0);
            float ndotv = max(dot(n, v), 0.0);
            float3 f0 = mix(float3(0.04), albedo, metallic);
            float3 f = fresnelSchlick(max(dot(h, v), 0.0), f0);
            float d = distributionGGX(n, h, roughness);
            float g = geometrySmith(n, v, l, roughness);
            float3 specular = (d * g * f) / max(4.0 * ndotv * ndotl, 1e-4);
            float3 kd = (1.0 - f) * (1.0 - metallic);
            float3 keyRadiance = float3(4.2, 4.0, 3.7);
            float3 color = (kd * albedo / M_PI_F + specular) * keyRadiance * ndotl;

            float horizon = clamp(0.5 + 0.5 * n.y, 0.0, 1.0);
            float3 ambient = mix(float3(0.025, 0.032, 0.045), float3(0.16, 0.19, 0.22), horizon);
            color += ambient * albedo * (0.55 + 0.45 * (1.0 - metallic));
            float rim = pow(max(1.0 - ndotv, 0.0), 3.0) * (0.10 + 0.16 * metallic);
            color += rim * float3(0.34, 0.58, 0.72);
            color += emissive;
            color *= exp2(clamp(camera.positionExposure.w, -6.0, 6.0));
            color = color / (1.0 + color);
            color = pow(max(color, 0.0), float3(1.0 / 2.2));
            return half4(half3(color), half(alpha));
        }
        """
        let library = try device.makeLibrary(source: source, options: nil)
        guard let vertex = library.makeFunction(name: "studioSceneVertex"),
              let fragment = library.makeFunction(name: "studioSceneFragment") else {
            throw NSError(domain: "VulkaxSceneRenderer", code: 1,
                          userInfo: [NSLocalizedDescriptionKey: "Scene mesh shaders are unavailable"])
        }

        func makePipeline(_ colorFormat: MTLPixelFormat) throws -> MTLRenderPipelineState {
            let descriptor = MTLRenderPipelineDescriptor()
            descriptor.label = "Vulkax PBR scene mesh \(colorFormat.rawValue)"
            descriptor.vertexFunction = vertex
            descriptor.fragmentFunction = fragment
            descriptor.colorAttachments[0].pixelFormat = colorFormat
            descriptor.depthAttachmentPixelFormat = depthFormat
            return try device.makeRenderPipelineState(descriptor: descriptor)
        }
        interactivePipeline = try makePipeline(interactiveColorFormat)
        capturePipeline = try makePipeline(.bgra8Unorm)

        let depth = MTLDepthStencilDescriptor()
        depth.depthCompareFunction = .less
        depth.isDepthWriteEnabled = true
        guard let depthState = device.makeDepthStencilState(descriptor: depth) else {
            throw NSError(domain: "VulkaxSceneRenderer", code: 2,
                          userInfo: [NSLocalizedDescriptionKey: "Could not create scene depth state"])
        }
        self.depthState = depthState

        let sampler = MTLSamplerDescriptor()
        sampler.minFilter = .linear
        sampler.magFilter = .linear
        sampler.mipFilter = .linear
        sampler.sAddressMode = .repeat
        sampler.tAddressMode = .repeat
        guard let samplerState = device.makeSamplerState(descriptor: sampler) else {
            throw NSError(domain: "VulkaxSceneRenderer", code: 3,
                          userInfo: [NSLocalizedDescriptionKey: "Could not create PBR material sampler"])
        }
        self.samplerState = samplerState

        let white = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .rgba8Unorm_srgb, width: 1, height: 1, mipmapped: false)
        white.usage = [.shaderRead]
        guard let whiteTexture = device.makeTexture(descriptor: white) else {
            throw NSError(domain: "VulkaxSceneRenderer", code: 4,
                          userInfo: [NSLocalizedDescriptionKey: "Could not create fallback material texture"])
        }
        var pixel: [UInt8] = [255, 255, 255, 255]
        whiteTexture.replace(
            region: MTLRegionMake2D(0, 0, 1, 1), mipmapLevel: 0,
            withBytes: &pixel, bytesPerRow: 4)
        self.whiteTexture = whiteTexture

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

    func rebuildIfNeeded(device: MTLDevice, model: PhysicsModel) {
        guard activeRevision != model.obstacleMeshRevision else { return }
        activeRevision = model.obstacleMeshRevision
        let simulated = model.obstacleItems.filter {
            $0.role.participatesInSimulation && $0.collisionProxy != .none
        }
        let visualOnly = model.obstacleItems.filter {
            !$0.role.participatesInSimulation || $0.collisionProxy == .none
        }
        simulatedBatches = makeBatches(device: device, items: simulated)
        staticBatches = makeBatches(device: device, items: visualOnly)
        let staticBodyData = visualOnly.flatMap { bodyState($0.body) }
        staticBodies = buffer(device: device, values: staticBodyData)
    }

    func captureDepthTexture(device: MTLDevice, width: Int, height: Int) -> MTLTexture? {
        if let captureDepth, captureDepth.width == width, captureDepth.height == height { return captureDepth }
        let descriptor = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .depth32Float, width: width, height: height, mipmapped: false)
        descriptor.usage = [.renderTarget]
        descriptor.storageMode = .private
        captureDepth = device.makeTexture(descriptor: descriptor)
        return captureDepth
    }

    func encode(
        encoder: MTLRenderCommandEncoder,
        model: PhysicsModel,
        camera: StudioCamera,
        simulatedBodyBuffer: MTLBuffer?,
        capture: Bool,
        aspect: Float
    ) {
        var cameraUniforms = StudioSceneCameraUniforms(
            positionExposure: SIMD4(camera.position, camera.exposure),
            target: SIMD4(camera.target, 0),
            upFov: SIMD4(camera.up, camera.verticalFovDegrees),
            aspectNearFar: SIMD4(max(aspect, 0.01), 0.01, 100.0, 0))
        encoder.setRenderPipelineState(capture ? capturePipeline : interactivePipeline)
        encoder.setDepthStencilState(depthState)
        encoder.setVertexBytes(&cameraUniforms, length: MemoryLayout<StudioSceneCameraUniforms>.stride, index: 2)
        encoder.setFragmentBytes(&cameraUniforms, length: MemoryLayout<StudioSceneCameraUniforms>.stride, index: 2)
        encoder.setFragmentSamplerState(samplerState, index: 0)

        if let simulatedBodyBuffer {
            encode(batches: simulatedBatches, bodyBuffer: simulatedBodyBuffer, encoder: encoder)
        }
        if let staticBodies {
            encode(batches: staticBatches, bodyBuffer: staticBodies, encoder: encoder)
        }
    }

    private func encode(
        batches: [StudioSceneBatch],
        bodyBuffer: MTLBuffer,
        encoder: MTLRenderCommandEncoder
    ) {
        encoder.setVertexBuffer(bodyBuffer, offset: 0, index: 1)
        for var batch in batches {
            encoder.setVertexBuffer(batch.vertices, offset: 0, index: 0)
            encoder.setFragmentBytes(
                &batch.material,
                length: MemoryLayout<StudioSceneMaterialUniforms>.stride,
                index: 3)
            encoder.setFragmentTexture(batch.baseColorTexture, index: 0)
            encoder.setFragmentTexture(batch.metallicRoughnessTexture, index: 1)
            encoder.setFragmentTexture(batch.normalTexture, index: 2)
            encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: batch.vertexCount)
        }
    }

    private func makeBatches(device: MTLDevice, items: [ObstacleSceneItem]) -> [StudioSceneBatch] {
        var batches: [StudioSceneBatch] = []
        let textureLoader = MTKTextureLoader(device: device)
        for (bodyIndex, item) in items.enumerated() {
            let mesh = item.visualMesh
            var verticesByMaterial: [Int: [StudioSceneVertex]] = [:]
            for triangle in stride(from: 0, to: mesh.indices.count, by: 3) {
                guard triangle + 2 < mesh.indices.count else { continue }
                let ia = Int(mesh.indices[triangle])
                let ib = Int(mesh.indices[triangle + 1])
                let ic = Int(mesh.indices[triangle + 2])
                guard ia < mesh.vertices.count, ib < mesh.vertices.count, ic < mesh.vertices.count else { continue }
                let a4 = mesh.vertices[ia], b4 = mesh.vertices[ib], c4 = mesh.vertices[ic]
                let a = SIMD3(a4.x, a4.y, a4.z)
                let b = SIMD3(b4.x, b4.y, b4.z)
                let c = SIMD3(c4.x, c4.y, c4.z)
                let cross = simd_cross(b - a, c - a)
                let lengthSquared = simd_length_squared(cross)
                guard lengthSquared > 1e-14 else { continue }
                let faceNormal = cross / sqrt(lengthSquared)
                let triangleIndex = triangle / 3
                let materialIndex = triangleIndex < mesh.triangleMaterialIndices.count
                    ? Int(mesh.triangleMaterialIndices[triangleIndex]) : 0
                var destination = verticesByMaterial[materialIndex, default: []]
                for vertexIndex in [ia, ib, ic] {
                    let position4 = mesh.vertices[vertexIndex]
                    let position = SIMD3(position4.x, position4.y, position4.z)
                    let suppliedNormal = vertexIndex < mesh.vertexNormals.count
                        ? mesh.vertexNormals[vertexIndex] : faceNormal
                    let normalLengthSquared = simd_length_squared(suppliedNormal)
                    let normal = normalLengthSquared > 1e-14
                        ? suppliedNormal / sqrt(normalLengthSquared) : faceNormal
                    let uv = vertexIndex < mesh.vertexTexCoords.count
                        ? mesh.vertexTexCoords[vertexIndex] : .zero
                    destination.append(.init(
                        positionBody: SIMD4(position, Float(bodyIndex)),
                        normal: SIMD4(normal, 0),
                        texCoord: SIMD4(uv.x, uv.y, 0, 0)))
                }
                verticesByMaterial[materialIndex] = destination
            }

            for materialIndex in verticesByMaterial.keys.sorted() {
                guard let vertices = verticesByMaterial[materialIndex],
                      let vertexBuffer = buffer(device: device, values: vertices) else { continue }
                let sourceMaterial = materialIndex < mesh.materials.count
                    ? mesh.materials[materialIndex] : .default
                func loadedTexture(_ data: Data?, srgb: Bool, fallback: MTLTexture) -> MTLTexture {
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
                let material = StudioSceneMaterialUniforms(
                    baseColor: sourceMaterial.baseColorFactor,
                    emissiveMetallic: SIMD4(
                        sourceMaterial.emissiveFactor.x,
                        sourceMaterial.emissiveFactor.y,
                        sourceMaterial.emissiveFactor.z,
                        sourceMaterial.metallicFactor),
                    roughnessFlags: SIMD4(sourceMaterial.roughnessFactor, sourceMaterial.normalScale, 0, 0))
                batches.append(.init(
                    vertices: vertexBuffer,
                    vertexCount: vertices.count,
                    material: material,
                    baseColorTexture: texture,
                    metallicRoughnessTexture: metallicRoughnessTexture,
                    normalTexture: normalTexture))
            }
        }
        return batches
    }

    private func bodyState(_ body: RigidObstacleConfiguration) -> [SIMD4<Float>] {
        let radians = SIMD3<Float>(body.rotationDegrees.x, body.rotationDegrees.y, body.rotationDegrees.z) * (.pi / 180)
        let qx = simd_quatf(angle: radians.x, axis: SIMD3(1, 0, 0))
        let qy = simd_quatf(angle: radians.y, axis: SIMD3(0, 1, 0))
        let qz = simd_quatf(angle: radians.z, axis: SIMD3(0, 0, 1))
        let q = simd_normalize(qz * qy * qx).vector
        return [
            SIMD4(body.position.x, body.position.y, body.position.z, max(body.mass, 0.001)),
            SIMD4(q.x, q.y, q.z, q.w),
            SIMD4(body.linearVelocity.x, body.linearVelocity.y, body.linearVelocity.z, 0),
            SIMD4(body.angularVelocity.x, body.angularVelocity.y, body.angularVelocity.z, 0),
            SIMD4(body.diagonalInertia.x, body.diagonalInertia.y, body.diagonalInertia.z, 0),
            SIMD4(body.scale.x, body.scale.y, body.scale.z, 0)
        ]
    }

    private func buffer<T>(device: MTLDevice, values: [T]) -> MTLBuffer? {
        guard !values.isEmpty else { return nil }
        return values.withUnsafeBytes { bytes in
            guard let base = bytes.baseAddress else { return nil }
            return device.makeBuffer(bytes: base, length: bytes.count, options: .storageModeShared)
        }
    }
}
