from pathlib import Path
import re


def replace_exact(text: str, old: str, new: str, label: str, count: int = 1) -> str:
    if old not in text:
        raise SystemExit(f"missing marker: {label}")
    return text.replace(old, new, count)

project_path = Path('apps/VulkaxPhysicsStudioMac/Sources/VulkaxPhysicsStudioMac/PhysicsProject.swift')
project = project_path.read_text()
project = replace_exact(project, '''struct ProjectObstacleRecord: Codable, Equatable {
    var meshPath: String
    var body: RigidObstacleConfiguration

    enum CodingKeys: String, CodingKey {
        case meshPath = "mesh_path"
        case body
    }
}
''', '''struct ProjectObstacleRecord: Codable, Equatable {
    var meshPath: String
    var body: RigidObstacleConfiguration
    var role: SceneEntityRole?
    var collisionProxy: CollisionProxyKind?

    init(
        meshPath: String,
        body: RigidObstacleConfiguration,
        role: SceneEntityRole? = nil,
        collisionProxy: CollisionProxyKind? = nil
    ) {
        self.meshPath = meshPath
        self.body = body
        self.role = role
        self.collisionProxy = collisionProxy
    }

    enum CodingKeys: String, CodingKey {
        case meshPath = "mesh_path"
        case body, role
        case collisionProxy = "collision_proxy"
    }
}
''', 'project obstacle record')
project = project.replace('    var version = 7\n', '    var version = 8\n', 1)
project = replace_exact(project, '''    var obstacleBody: RigidObstacleConfiguration
    var obstacles: [ProjectObstacleRecord]
''', '''    var obstacleBody: RigidObstacleConfiguration
    var obstacles: [ProjectObstacleRecord]
    var mediumOverride: SimulationMedium?
    var camera: StudioCamera
    var captureSettings: CinematicCaptureSettings
''', 'project new fields')
project = replace_exact(project, '''        case obstacleBody = "obstacle_body"
        case obstacles
''', '''        case obstacleBody = "obstacle_body"
        case obstacles
        case mediumOverride = "medium_override"
        case camera
        case captureSettings = "capture_settings"
''', 'project coding keys')
project = replace_exact(project, '''        obstacleMeshPath: String? = nil,
        obstacleBody: RigidObstacleConfiguration = .default,
        obstacles: [ProjectObstacleRecord] = []
    ) {
''', '''        obstacleMeshPath: String? = nil,
        obstacleBody: RigidObstacleConfiguration = .default,
        obstacles: [ProjectObstacleRecord] = [],
        mediumOverride: SimulationMedium? = nil,
        camera: StudioCamera = .default,
        captureSettings: CinematicCaptureSettings = .init()
    ) {
''', 'project initializer params')
project = replace_exact(project, '''        self.obstacleMeshPath = obstacleMeshPath
        self.obstacleBody = obstacleBody
        self.obstacles = obstacles
    }
''', '''        self.obstacleMeshPath = obstacleMeshPath
        self.obstacleBody = obstacleBody
        self.obstacles = obstacles
        self.mediumOverride = mediumOverride
        self.camera = camera
        self.captureSettings = captureSettings
    }
''', 'project initializer assignments')
project = replace_exact(project, '''        } else {
            obstacles = []
        }
    }
}
''', '''        } else {
            obstacles = []
        }
        mediumOverride = try container.decodeIfPresent(SimulationMedium.self, forKey: .mediumOverride)
        camera = try container.decodeIfPresent(StudioCamera.self, forKey: .camera) ?? .default
        captureSettings = try container.decodeIfPresent(
            CinematicCaptureSettings.self, forKey: .captureSettings) ?? .init()
    }
}
''', 'project decoder fields')
project = project.replace('(1...7).contains(project.version)', '(1...8).contains(project.version)', 1)
project_path.write_text(project)

main_path = Path('apps/VulkaxPhysicsStudioMac/Sources/VulkaxPhysicsStudioMac/main.swift')
text = main_path.read_text()
text = replace_exact(text, '''struct ObstacleSceneItem: Identifiable {
    let id: UUID
    var mesh: ImportedObstacleMesh
    var url: URL
    var body: RigidObstacleConfiguration
}
''', '''struct ObstacleSceneItem: Identifiable {
    let id: UUID
    // mesh is the closed simulation proxy consumed by voxelization/contact.
    var mesh: ImportedObstacleMesh
    // visualMesh preserves the imported car/prop topology even when it is not
    // suitable for numerical coupling.
    var visualMesh: ImportedObstacleMesh
    var url: URL
    var body: RigidObstacleConfiguration
    var role: SceneEntityRole
    var collisionProxy: CollisionProxyKind
}
''', 'scene item')
text = replace_exact(text, '''    @Published var volumeExtinction: Float = 2.2
    @Published var volumeEmission: Float = 1.0
''', '''    @Published var volumeExtinction: Float = 2.2
    @Published var volumeEmission: Float = 1.0
    @Published var camera = StudioCamera.default
    @Published var mediumOverride: SimulationMedium?
    @Published var captureSettings = CinematicCaptureSettings()
    @Published var capturePanelPresented = false
    @Published private(set) var isCapturing = false
    @Published private(set) var captureRequestRevision: UInt64 = 0
''', 'model scene properties')
text = replace_exact(text, '''    private var selectedObstacleIndex: Int? {
        guard let selectedObstacleID else { return nil }
        return obstacleItems.firstIndex { $0.id == selectedObstacleID }
    }
''', '''    private var selectedObstacleIndex: Int? {
        guard let selectedObstacleID else { return nil }
        return obstacleItems.firstIndex { $0.id == selectedObstacleID }
    }

    var inferredMedium: MediumInferenceResult {
        SimulationMediumInference.infer(equation: equationSource, visualizerMode: mode)
    }

    var activeMedium: SimulationMedium { mediumOverride ?? inferredMedium.medium }

    var selectedObstacleName: String {
        selectedObstacleIndex.map { obstacleItems[$0].url.deletingPathExtension().lastPathComponent } ?? "No selection"
    }

    var selectedVisualDiagnostics: String {
        selectedObstacleIndex.map { obstacleItems[$0].visualMesh.diagnostics.summary } ?? ""
    }

    var selectedProxyDescription: String {
        guard let index = selectedObstacleIndex else { return "No simulation proxy" }
        let item = obstacleItems[index]
        switch item.collisionProxy {
        case .none: return "Visual-only entity; excluded from the numerical domain."
        case .renderMesh: return "The imported closed mesh is used directly for voxelization/contact."
        case .box: return "A closed bounds proxy is used for stable GPU simulation; the imported model remains the visual asset."
        case .convexHull: return "Convex-hull mode currently uses the conservative bounds proxy until hull generation lands."
        case .sphere: return "Sphere mode currently uses the conservative bounds proxy until analytic proxy voxelization lands."
        }
    }

    var selectedObstacleRole: SceneEntityRole {
        get { selectedObstacleIndex.map { obstacleItems[$0].role } ?? .visual }
        set {
            guard let index = selectedObstacleIndex else { return }
            obstacleItems[index].role = newValue
            obstacleMeshRevision &+= 1
            accumulationResetToken &+= 1
        }
    }

    var selectedCollisionProxy: CollisionProxyKind {
        get { selectedObstacleIndex.map { obstacleItems[$0].collisionProxy } ?? .none }
        set {
            guard let index = selectedObstacleIndex else { return }
            var item = obstacleItems[index]
            var effective = newValue
            if effective == .renderMesh && !item.visualMesh.diagnostics.isWatertight {
                effective = .box
                equationStatus = "Visual mesh is open/non-manifold; using a closed box simulation proxy"
            }
            item.collisionProxy = effective
            item.mesh = effective == .renderMesh ? item.visualMesh : ImportedObstacleMesh.boxProxy(for: item.visualMesh)
            obstacleItems[index] = item
            obstacleMeshRevision &+= 1
            accumulationResetToken &+= 1
        }
    }
''', 'model computed scene state')
text = replace_exact(text, '''    func newProject() {
        projectName = "Untitled Physics"
        projectURL = nil
        time = 0
        playing = true
''', '''    func newProject() {
        projectName = "Untitled Physics"
        projectURL = nil
        time = 0
        playing = true
        camera = .default
        mediumOverride = nil
        captureSettings = .init()
''', 'new project camera')
text = replace_exact(text, '''            time = project.timelineSeconds
            compileEquation()
''', '''            time = project.timelineSeconds
            camera = project.camera
            mediumOverride = project.mediumOverride
            captureSettings = project.captureSettings
            compileEquation()
''', 'project open camera')
text = replace_exact(text, '''                try loadObstacleMesh(from: meshURL, body: record.body)
''', '''                try loadObstacleMesh(
                    from: meshURL,
                    body: record.body,
                    role: record.role,
                    collisionProxy: record.collisionProxy)
''', 'project obstacle load')
text = replace_exact(text, '''        let item = ObstacleSceneItem(
            id: UUID(), mesh: try ImportedObstacleMesh.loadOBJ(from: url), url: url, body: body)
        obstacleItems.append(item)
''', '''        let visualMesh = try ImportedObstacleMesh.loadOBJ(from: url, requireWatertight: false)
        let defaultProxy: CollisionProxyKind = visualMesh.diagnostics.isWatertight ? .renderMesh : .box
        var effectiveProxy = collisionProxy ?? defaultProxy
        if effectiveProxy == .renderMesh && !visualMesh.diagnostics.isWatertight { effectiveProxy = .box }
        let simulationMesh = effectiveProxy == .renderMesh
            ? visualMesh : ImportedObstacleMesh.boxProxy(for: visualMesh)
        let item = ObstacleSceneItem(
            id: UUID(), mesh: simulationMesh, visualMesh: visualMesh, url: url, body: body,
            role: role ?? .fluidObstacle, collisionProxy: effectiveProxy)
        obstacleItems.append(item)
''', 'obstacle load implementation')
text = replace_exact(text, '''    private func loadObstacleMesh(
        from url: URL,
        body suppliedBody: RigidObstacleConfiguration? = nil
    ) throws {
''', '''    private func loadObstacleMesh(
        from url: URL,
        body suppliedBody: RigidObstacleConfiguration? = nil,
        role: SceneEntityRole? = nil,
        collisionProxy: CollisionProxyKind? = nil
    ) throws {
''', 'obstacle load signature')
text = replace_exact(text, '''                return ProjectObstacleRecord(meshPath: path, body: item.body)
''', '''                return ProjectObstacleRecord(
                    meshPath: path,
                    body: item.body,
                    role: item.role,
                    collisionProxy: item.collisionProxy)
''', 'project obstacle save')
text = replace_exact(text, '''                obstacleMeshPath: obstacleRecords.first?.meshPath,
                obstacleBody: obstacleRecords.first?.body ?? .default,
                obstacles: obstacleRecords)
''', '''                obstacleMeshPath: obstacleRecords.first?.meshPath,
                obstacleBody: obstacleRecords.first?.body ?? .default,
                obstacles: obstacleRecords,
                mediumOverride: mediumOverride,
                camera: camera,
                captureSettings: captureSettings)
''', 'project save camera')
text = replace_exact(text, '''    private static func key(for mode: VisualizerMode) -> String {
''', '''    func applyCameraPreset(_ preset: StudioCameraPreset) {
        camera.apply(preset)
        accumulationResetToken &+= 1
    }

    func requestCinematicCapture() {
        capturePanelPresented = false
        captureRequestRevision &+= 1
        equationStatus = "Cinematic capture requested · \(captureSettings.resolution.title) · \(captureSettings.frameRate.title)"
    }

    func reportCaptureState(active: Bool, message: String) {
        isCapturing = active
        equationStatus = message
    }

    private static func key(for mode: VisualizerMode) -> String {
''', 'camera/capture methods')
text = replace_exact(text, '''struct WaveUniforms {
    var time: Float
    var amplitude: Float
    var wavenumber: Float
    var angularFrequency: Float
    var width: Float
    var height: Float
    var padding: SIMD4<Float> = .zero
    var control: SIMD4<Float> = .zero
    var renderParameters: SIMD4<Float> = .zero
}
''', '''struct WaveUniforms {
    var time: Float
    var amplitude: Float
    var wavenumber: Float
    var angularFrequency: Float
    var width: Float
    var height: Float
    var padding: SIMD4<Float> = .zero
    var control: SIMD4<Float> = .zero
    var renderParameters: SIMD4<Float> = .zero
    // Trailing fields keep the original shader ABI prefix intact for runtime-
    // compiled scalar equations while giving 3D renderers a real camera.
    var cameraPositionExposure: SIMD4<Float> = SIMD4(0, 0.1, 3.1, 1)
    var cameraTarget: SIMD4<Float> = SIMD4(0, 0.1, 0, 0)
    var cameraUpFov: SIMD4<Float> = SIMD4(0, 1, 0, 46.94)
}
''', 'wave uniforms')
text = replace_exact(text, '''    float4 padding;
    float4 control;
    float4 renderParameters;
};
''', '''    float4 padding;
    float4 control;
    float4 renderParameters;
    float4 cameraPositionExposure;
    float4 cameraTarget;
    float4 cameraUpFov;
};
''', 'metal uniforms')
text = replace_exact(text, '''    float2 uv = (float2(pixel) + 0.5) / float2(u.width, u.height);
    float aspect = u.width / max(1.0, u.height);
    float3 origin = float3(0.0, 0.1, 3.1);
    float3 direction = normalize(float3((uv.x - 0.5) * 1.65 * aspect, (0.5 - uv.y) * 1.65, -1.9));
''', '''    float2 uv = (float2(pixel) + 0.5) / float2(u.width, u.height);
    float aspect = u.width / max(1.0, u.height);
    float3 origin = u.cameraPositionExposure.xyz;
    float3 forward = normalize(u.cameraTarget.xyz - origin);
    float3 referenceUp = normalize(u.cameraUpFov.xyz);
    float3 right = normalize(cross(forward, referenceUp));
    float3 cameraUp = normalize(cross(right, forward));
    float tanHalfFov = tan(radians(clamp(u.cameraUpFov.w, 10.0, 120.0)) * 0.5);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float3 direction = normalize(
        forward + ndc.x * aspect * tanHalfFov * right + ndc.y * tanHalfFov * cameraUp);
''', 'volume camera')
text = replace_exact(text, '''    float3 linear = float3(radiance.sample(linearSampler, in.uv).rgb);
''', '''    float3 linear = float3(radiance.sample(linearSampler, in.uv).rgb);
    linear *= max(u.cameraPositionExposure.w, 0.001);
''', 'display exposure')
text = replace_exact(text, '''        var copy = uniforms
        if isVolume {
''', '''        var copy = uniforms
        copy.cameraPositionExposure = SIMD4(model.camera.position, model.camera.exposure)
        copy.cameraTarget = SIMD4(model.camera.target, 0)
        copy.cameraUpFov = SIMD4(model.camera.up, model.camera.verticalFovDegrees)
        if isVolume {
''', 'per-frame camera uniforms')
text = replace_exact(text, '''        for (bodyIndex, item) in model.obstacleItems.enumerated() {
''', '''        let simulatedItems = model.obstacleItems.filter {
            $0.role.participatesInSimulation && $0.collisionProxy != .none
        }
        for (bodyIndex, item) in simulatedItems.enumerated() {
''', 'simulation entity filter')
text = replace_exact(text, '''            obstacleBodyCount = UInt32(model.obstacleItems.count)
''', '''            obstacleBodyCount = UInt32(simulatedItems.count)
''', 'simulation body count')
text = replace_exact(text, '''        let view = MTKView(frame: .zero, device: MTLCreateSystemDefaultDevice())
        view.colorPixelFormat = .bgra8Unorm_srgb
''', '''        let view = StudioMetalView(frame: .zero, device: MTLCreateSystemDefaultDevice())
        view.physicsModel = model
        view.colorPixelFormat = .bgra8Unorm_srgb
''', 'interactive metal view')

pattern = re.compile(r'struct ContentView: View \{.*?\n\}\n\n@main struct VulkaxPhysicsStudioMacApp', re.S)
replacement = '''struct ContentView: View {
    @StateObject private var model = PhysicsModel()

    var body: some View {
        StudioWorkspaceView(model: model)
    }
}

@main struct VulkaxPhysicsStudioMacApp'''
text, count = pattern.subn(replacement, text, count=1)
if count != 1:
    raise SystemExit('could not replace legacy ContentView shell')
main_path.write_text(text)

Path('scripts/phase10_wire_studio.py').unlink()
