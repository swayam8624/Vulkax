import AppKit
import Foundation

struct LiveParameter: Identifiable, Codable, Equatable {
    var id: String { name }
    let name: String
    var value: Float
    var minimum: Float
    var maximum: Float
    var units: String
}

struct ScalarPreset: Identifiable {
    let id: String
    let title: String
    let equation: String
    let parameters: [LiveParameter]

    static let builtins: [ScalarPreset] = [
        .init(id: "wave-field", title: "Wave Field",
              equation: "amplitude * sin(wavenumber * x - angular_frequency * t)",
              parameters: [
                .init(name: "amplitude", value: 1, minimum: 0, maximum: 10, units: "field"),
                .init(name: "angular_frequency", value: 3, minimum: 0.01, maximum: 50, units: "rad/s"),
                .init(name: "wavenumber", value: 2, minimum: 0.01, maximum: 50, units: "rad/m")]),
        .init(id: "gravity-potential", title: "Gravity Potential",
              equation: "-gravitational_parameter / sqrt(x*x + y*y + z*z + softening*softening)",
              parameters: [
                .init(name: "gravitational_parameter", value: 8, minimum: 0.001, maximum: 100, units: "m3/s2"),
                .init(name: "softening", value: 0.25, minimum: 0.001, maximum: 10, units: "m")]),
        .init(id: "quantum-wavepacket", title: "Quantum Wavepacket",
              equation: "exp(-((x-velocity*t)*(x-velocity*t))/(2*width*width))*cos(wavenumber*x-angular_frequency*t)",
              parameters: [
                .init(name: "angular_frequency", value: 3, minimum: 0.01, maximum: 100, units: "rad/s"),
                .init(name: "velocity", value: 0.6, minimum: -10, maximum: 10, units: "m/s"),
                .init(name: "wavenumber", value: 5, minimum: 0.01, maximum: 100, units: "rad/m"),
                .init(name: "width", value: 1.2, minimum: 0.01, maximum: 20, units: "m")]),
        .init(id: "electromagnetic-pulse", title: "Electromagnetic Pulse",
              equation: "amplitude * exp(-decay*t) * sin(wavenumber*x-angular_frequency*t)",
              parameters: [
                .init(name: "amplitude", value: 1, minimum: 0, maximum: 10, units: "field"),
                .init(name: "angular_frequency", value: 5, minimum: 0.01, maximum: 100, units: "rad/s"),
                .init(name: "decay", value: 0.3, minimum: 0, maximum: 10, units: "1/s"),
                .init(name: "wavenumber", value: 4, minimum: 0.01, maximum: 100, units: "rad/m")])
    ]
}

enum RuntimeGraphKind: String, Codable {
    case scalarField = "scalar-field"
    case kerrRelativity = "kerr-relativity"
    case incompressibleVolume = "incompressible-volume"
}

struct EquationRuntimeGraph: Codable, Equatable {
    var kind: RuntimeGraphKind
    var equations: [String]
    var passes: [RuntimeGraphPass]

    func contains(_ kernel: String) -> Bool {
        passes.contains { $0.kernel == kernel }
    }

    static func builtIn(for mode: VisualizerMode, scalarEquation: String) -> EquationRuntimeGraph {
        switch mode {
        case .wave:
            return .init(kind: .scalarField, equations: ["field = \(scalarEquation)"],
                         passes: [.init(kernel: "evaluate_scalar_field"), .init(kernel: "tone_map_present")])
        case .schwarzschild:
            return .init(kind: .kerrRelativity,
                         equations: ["g_mu_nu dx^mu dx^nu = 0", "D^2 xi^mu / dlambda^2 = -R^mu_ab_nu k^a k^b xi^nu"],
                         passes: [.init(kernel: "integrate_active_rays"), .init(kernel: "compact_active_rays"),
                                  .init(kernel: "resolve_disk_events"), .init(kernel: "radiative_transfer"),
                                  .init(kernel: "tone_map_present")])
        case .volumeSmoke:
            return .init(kind: .incompressibleVolume,
                         equations: ["du/dt + u dot grad(u) = -grad(p) + f", "div(u) = 0", "drho/dt + u dot grad(rho) = 0"],
                         passes: [.init(kernel: "gpu_cfl"), .init(kernel: "advect_velocity"),
                                  .init(kernel: "apply_forces"), .init(kernel: "curl_vorticity"),
                                  .init(kernel: "divergence"), .init(kernel: "multigrid_pressure", iterations: 1),
                                  .init(kernel: "project_velocity"), .init(kernel: "advect_scalars"),
                                  .init(kernel: "volume_transport"), .init(kernel: "tone_map_present")])
        }
    }
}

struct RuntimeGraphPass: Codable, Equatable {
    var kernel: String
    var reads: [String]
    var writes: [String]
    var iterations: Int

    init(kernel: String, reads: [String] = [], writes: [String] = [], iterations: Int = 1) {
        self.kernel = kernel
        self.reads = reads
        self.writes = writes
        self.iterations = max(1, iterations)
    }

    init(from decoder: Decoder) throws {
        if let legacy = try? decoder.singleValueContainer().decode(String.self) {
            self.init(kernel: legacy)
            return
        }
        let container = try decoder.container(keyedBy: CodingKeys.self)
        self.init(
            kernel: try container.decode(String.self, forKey: .kernel),
            reads: try container.decodeIfPresent([String].self, forKey: .reads) ?? [],
            writes: try container.decodeIfPresent([String].self, forKey: .writes) ?? [],
            iterations: try container.decodeIfPresent(Int.self, forKey: .iterations) ?? 1)
    }
}

struct CodableVector3: Codable, Equatable {
    var x: Float
    var y: Float
    var z: Float
}

struct RigidObstacleConfiguration: Codable, Equatable {
    var position: CodableVector3
    var rotationDegrees: CodableVector3
    var scale: CodableVector3
    var linearVelocity: CodableVector3
    var angularVelocity: CodableVector3
    var diagonalInertia: CodableVector3
    var mass: Float

    static let `default` = RigidObstacleConfiguration(
        position: .init(x: 0.66, y: 0.30, z: 0.50),
        rotationDegrees: .init(x: 0, y: 0, z: 0),
        scale: .init(x: 1, y: 1, z: 1),
        linearVelocity: .init(x: 0.035, y: 0, z: 0),
        angularVelocity: .init(x: 0, y: 0, z: 0),
        diagonalInertia: .init(x: 0.012, y: 0.012, z: 0.012),
        mass: 2)
}

struct ProjectObstacleRecord: Codable, Equatable {
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

struct PhysicsProjectFile: Codable {
    var format = "vulkax.physics-project"
    var version = 8
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
    var captureSettings: CinematicCaptureSettings

    enum CodingKeys: String, CodingKey {
        case format, version, name, preset, visualization, expression, parameters, graph
        case timelineSeconds = "timeline_seconds"
        case obstacleMeshPath = "obstacle_mesh_path"
        case obstacleBody = "obstacle_body"
        case obstacles
        case mediumOverride = "medium_override"
        case camera
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
        guard project.format == "vulkax.physics-project", (1...8).contains(project.version) else {
            throw CocoaError(.fileReadCorruptFile)
        }
        return project
    }
}
