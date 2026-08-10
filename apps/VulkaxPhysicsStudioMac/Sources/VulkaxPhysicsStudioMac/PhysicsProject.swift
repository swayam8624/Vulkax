import Foundation
import simd

struct ScalarPresetParameter: Identifiable, Hashable, Codable {
    var id: String { name }
    var name: String
    var value: Float
    var minimum: Float
    var maximum: Float
    var units: String = ""
}

typealias LiveParameter = ScalarPresetParameter

struct ScalarPreset: Identifiable, Hashable, Codable {
    var id: String
    var title: String
    var equation: String
    var parameters: [ScalarPresetParameter]

    static let builtins: [ScalarPreset] = [
        .init(
            id: "plane-wave",
            title: "Plane Wave",
            equation: "amplitude * sin(wavenumber * x - angular_frequency * t)",
            parameters: [
                .init(name: "amplitude", value: 1.0, minimum: 0.05, maximum: 4.0),
                .init(name: "angular_frequency", value: 2.0, minimum: 0.05, maximum: 12.0),
                .init(name: "wavenumber", value: 1.6, minimum: 0.05, maximum: 8.0)
            ]),
        .init(
            id: "radial-pulse",
            title: "Radial Pulse",
            equation: "amplitude * sin(wavenumber * sqrt(x*x + y*y) - angular_frequency*t)",
            parameters: [
                .init(name: "amplitude", value: 1.2, minimum: 0.05, maximum: 4.0),
                .init(name: "angular_frequency", value: 3.0, minimum: 0.05, maximum: 12.0),
                .init(name: "wavenumber", value: 2.4, minimum: 0.05, maximum: 8.0)
            ])
    ]
}

struct EquationRuntimeGraph: Codable, Equatable {
    var graphVersion: Int
    var visualization: VisualizerMode
    var scalarEquation: String

    static func builtIn(for mode: VisualizerMode, scalarEquation: String) -> EquationRuntimeGraph {
        .init(graphVersion: 1, visualization: mode, scalarEquation: scalarEquation)
    }
}

struct RigidObstacleConfiguration: Codable, Equatable {
    var position: SIMD3<Float>
    var rotationDegrees: SIMD3<Float>
    var scale: SIMD3<Float>
    var linearVelocity: SIMD3<Float>
    var angularVelocity: SIMD3<Float>
    var mass: Float
    var diagonalInertia: SIMD3<Float>

    static let `default` = RigidObstacleConfiguration(
        position: SIMD3(0.5, 0.42, 0.5),
        rotationDegrees: .zero,
        scale: SIMD3(repeating: 1),
        linearVelocity: .zero,
        angularVelocity: .zero,
        mass: 1,
        diagonalInertia: SIMD3(repeating: 0.02))
}

struct ProjectObstacleRecord: Codable, Equatable {
    var meshPath: String
    var body: RigidObstacleConfiguration
    var role: SceneEntityRole
    var collisionProxy: CollisionProxyKind

    init(
        meshPath: String,
        body: RigidObstacleConfiguration,
        role: SceneEntityRole = .fluidObstacle,
        collisionProxy: CollisionProxyKind = .renderMesh
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

struct PhysicsProjectFile: Codable {
    var format = "vulkax.physics-project"
    var version = 9
    var name: String
    var preset: String
    var visualization: String
    var expression: String
    var timelineSeconds: Float
    var parameters: [String: Float]
    var graph: EquationRuntimeGraph
    var obstacleMeshPath: String?
    var obstacleBody: RigidObstacleConfiguration
    var obstacles: [ProjectObstacleRecord]
    var mediumOverride: SimulationMedium?
    var camera: StudioCamera
    var cameraTrack: StudioCameraTrack
    var captureSettings: CinematicCaptureSettings

    enum CodingKeys: String, CodingKey {
        case format, version, name, preset, visualization, expression, parameters, graph
        case timelineSeconds = "timeline_seconds"
        case obstacleMeshPath = "obstacle_mesh_path"
        case obstacleBody = "obstacle_body"
        case obstacles
        case mediumOverride = "medium_override"
        case camera
        case cameraTrack = "camera_track"
        case captureSettings = "capture_settings"
    }

    init(
        name: String,
        preset: String,
        visualization: String,
        expression: String,
        timelineSeconds: Float,
        parameters: [String: Float],
        graph: EquationRuntimeGraph,
        obstacleMeshPath: String? = nil,
        obstacleBody: RigidObstacleConfiguration = .default,
        obstacles: [ProjectObstacleRecord] = [],
        mediumOverride: SimulationMedium? = nil,
        camera: StudioCamera = .default,
        cameraTrack: StudioCameraTrack = .init(),
        captureSettings: CinematicCaptureSettings = .init()
    ) {
        self.name = name
        self.preset = preset
        self.visualization = visualization
        self.expression = expression
        self.timelineSeconds = timelineSeconds
        self.parameters = parameters
        self.graph = graph
        self.obstacleMeshPath = obstacleMeshPath
        self.obstacleBody = obstacleBody
        self.obstacles = obstacles
        self.mediumOverride = mediumOverride
        self.camera = camera
        self.cameraTrack = cameraTrack
        self.captureSettings = captureSettings
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        format = try container.decodeIfPresent(String.self, forKey: .format) ?? ""
        version = try container.decodeIfPresent(Int.self, forKey: .version) ?? 1
        preset = try container.decodeIfPresent(String.self, forKey: .preset) ?? "wave-field"
        name = try container.decodeIfPresent(String.self, forKey: .name) ?? preset
        visualization = try container.decodeIfPresent(String.self, forKey: .visualization) ?? "scalar-field"
        expression = try container.decodeIfPresent(String.self, forKey: .expression) ?? ScalarPreset.builtins[0].equation
        timelineSeconds = try container.decodeIfPresent(Float.self, forKey: .timelineSeconds) ?? 0
        parameters = try container.decodeIfPresent([String: Float].self, forKey: .parameters) ?? [:]
        let fallbackMode: VisualizerMode = visualization == "relativity" ? .schwarzschild :
            (visualization == "volume" ? .volumeSmoke : .wave)
        graph = try container.decodeIfPresent(EquationRuntimeGraph.self, forKey: .graph) ??
            .builtIn(for: fallbackMode, scalarEquation: expression)
        obstacleMeshPath = try container.decodeIfPresent(String.self, forKey: .obstacleMeshPath)
        obstacleBody = try container.decodeIfPresent(
            RigidObstacleConfiguration.self, forKey: .obstacleBody) ?? .default
        if let records = try container.decodeIfPresent([ProjectObstacleRecord].self, forKey: .obstacles) {
            obstacles = records
        } else if let obstacleMeshPath {
            obstacles = [.init(meshPath: obstacleMeshPath, body: obstacleBody)]
        } else {
            obstacles = []
        }
        mediumOverride = try container.decodeIfPresent(SimulationMedium.self, forKey: .mediumOverride)
        camera = try container.decodeIfPresent(StudioCamera.self, forKey: .camera) ?? .default
        cameraTrack = try container.decodeIfPresent(StudioCameraTrack.self, forKey: .cameraTrack) ?? .init()
        captureSettings = try container.decodeIfPresent(
            CinematicCaptureSettings.self, forKey: .captureSettings) ?? .init()
    }
}

enum PhysicsProjectIO {
    static func packageObstacle(
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

    static func save(_ project: PhysicsProjectFile, to url: URL) throws {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        try encoder.encode(project).write(to: url, options: .atomic)
    }

    static func load(from url: URL) throws -> PhysicsProjectFile {
        let project = try JSONDecoder().decode(PhysicsProjectFile.self, from: Data(contentsOf: url))
        guard project.format == "vulkax.physics-project", (1...9).contains(project.version) else {
            throw CocoaError(.fileReadCorruptFile)
        }
        return project
    }
}