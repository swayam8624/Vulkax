import Foundation
import Metal
import simd

private struct StudioSceneVertex {
    var positionBody: SIMD4<Float>
    var normal: SIMD4<Float>
}

private struct StudioSceneCameraUniforms {
    var positionExposure: SIMD4<Float>
    var target: SIMD4<Float>
    var upFov: SIMD4<Float>
    var aspectNearFar: SIMD4<Float>
}

final class StudioSceneMeshRenderer {
    private let interactivePipeline: MTLRenderPipelineState
    private let capturePipeline: MTLRenderPipelineState
    private let depthState: MTLDepthStencilState
    private var simulatedVertices: MTLBuffer?
    private var simulatedVertexCount = 0
    private var staticVertices: MTLBuffer?
    private var staticVertexCount = 0
    private var staticBodies: MTLBuffer?
    private var activeRevision: UInt64 = .max
    private var captureDepth: MTLTexture?

    init(device: MTLDevice, interactiveColorFormat: MTLPixelFormat, depthFormat: MTLPixelFormat) throws {
        let source = """
        #include <metal_stdlib>
        using namespace metal;

        struct SceneVertex { float4 positionBody; float4 normal; };
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
        struct Out {
            float4 position [[position]];
            float3 worldPosition;
            float3 normal;
            float bodyIndex;
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
            out.bodyIndex = float(bodyIndex);
            return out;
        }

        fragment half4 studioSceneFragment(Out in [[stage_in]], constant Camera& camera [[buffer(2)]]) {
            float3 n = normalize(in.normal);
            float3 view = normalize(camera.positionExposure.xyz - in.worldPosition);
            float3 key = normalize(float3(-0.45, 0.85, 0.55));
            float3 rimDirection = normalize(float3(0.65, 0.25, -0.70));
            float diffuse = max(dot(n, key), 0.0);
            float rim = pow(max(1.0 - dot(n, view), 0.0), 2.4);
            float fill = 0.18 + 0.16 * max(dot(n, rimDirection), 0.0);
            float3 base = mix(float3(0.055, 0.075, 0.095), float3(0.10, 0.36, 0.38),
                              0.45 + 0.25 * sin(in.bodyIndex * 1.73));
            float3 color = base * (fill + 0.92 * diffuse) + float3(0.20, 0.75, 0.80) * rim * 0.42;
            float specular = pow(max(dot(reflect(-key, n), view), 0.0), 48.0);
            color += specular * 0.45;
            color = color / (1.0 + color);
            color = pow(max(color, 0.0), float3(1.0 / 2.2));
            return half4(half3(color), 1.0h);
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
            descriptor.label = "Vulkax scene mesh \(colorFormat.rawValue)"
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
        let simulatedData = makeVertices(simulated)
        simulatedVertexCount = simulatedData.count
        simulatedVertices = buffer(device: device, values: simulatedData)
        let staticData = makeVertices(visualOnly)
        staticVertexCount = staticData.count
        staticVertices = buffer(device: device, values: staticData)
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

        if simulatedVertexCount > 0, let simulatedVertices, let simulatedBodyBuffer {
            encoder.setVertexBuffer(simulatedVertices, offset: 0, index: 0)
            encoder.setVertexBuffer(simulatedBodyBuffer, offset: 0, index: 1)
            encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: simulatedVertexCount)
        }
        if staticVertexCount > 0, let staticVertices, let staticBodies {
            encoder.setVertexBuffer(staticVertices, offset: 0, index: 0)
            encoder.setVertexBuffer(staticBodies, offset: 0, index: 1)
            encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: staticVertexCount)
        }
    }

    private func makeVertices(_ items: [ObstacleSceneItem]) -> [StudioSceneVertex] {
        var result: [StudioSceneVertex] = []
        for (bodyIndex, item) in items.enumerated() {
            for triangle in stride(from: 0, to: item.visualMesh.indices.count, by: 3) {
                guard triangle + 2 < item.visualMesh.indices.count else { continue }
                let ia = Int(item.visualMesh.indices[triangle])
                let ib = Int(item.visualMesh.indices[triangle + 1])
                let ic = Int(item.visualMesh.indices[triangle + 2])
                guard ia < item.visualMesh.vertices.count,
                      ib < item.visualMesh.vertices.count,
                      ic < item.visualMesh.vertices.count else { continue }
                let a4 = item.visualMesh.vertices[ia]
                let b4 = item.visualMesh.vertices[ib]
                let c4 = item.visualMesh.vertices[ic]
                let a = SIMD3(a4.x, a4.y, a4.z)
                let b = SIMD3(b4.x, b4.y, b4.z)
                let c = SIMD3(c4.x, c4.y, c4.z)
                let cross = simd_cross(b - a, c - a)
                let lengthSquared = simd_length_squared(cross)
                guard lengthSquared > 1e-14 else { continue }
                let normal = cross / sqrt(lengthSquared)
                let n = SIMD4(normal, 0)
                result.append(.init(positionBody: SIMD4(a, Float(bodyIndex)), normal: n))
                result.append(.init(positionBody: SIMD4(b, Float(bodyIndex)), normal: n))
                result.append(.init(positionBody: SIMD4(c, Float(bodyIndex)), normal: n))
            }
        }
        return result
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
