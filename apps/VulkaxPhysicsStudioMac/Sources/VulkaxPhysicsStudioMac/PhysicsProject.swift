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

struct PhysicsProjectFile: Codable {
    var format = "vulkax.physics-project"
    var version = 5
    var name: String
    var preset: String
    var visualization: String
    var expression: String
    var timelineSeconds: Float
    var parameters: [String: Float]
    var graph: EquationRuntimeGraph
    var obstacleMeshPath: String?

    enum CodingKeys: String, CodingKey {
        case format, version, name, preset, visualization, expression, parameters, graph
        case timelineSeconds = "timeline_seconds"
        case obstacleMeshPath = "obstacle_mesh_path"
    }

    init(
        name: String,
        preset: String,
        visualization: String,
        expression: String,
        timelineSeconds: Float,
        parameters: [String: Float],
        graph: EquationRuntimeGraph,
        obstacleMeshPath: String? = nil
    ) {
        self.name = name
        self.preset = preset
        self.visualization = visualization
        self.expression = expression
        self.timelineSeconds = timelineSeconds
        self.parameters = parameters
        self.graph = graph
        self.obstacleMeshPath = obstacleMeshPath
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
    }
}

enum PhysicsProjectIO {
    static func packageObstacle(from source: URL, for projectURL: URL) throws -> String {
        let assetDirectory = projectURL.deletingPathExtension().appendingPathExtension("assets")
        try FileManager.default.createDirectory(
            at: assetDirectory, withIntermediateDirectories: true)
        let destination = assetDirectory.appendingPathComponent("obstacle.obj")
        if source.standardizedFileURL != destination.standardizedFileURL {
            try Data(contentsOf: source).write(to: destination, options: .atomic)
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
        guard project.format == "vulkax.physics-project", (1...5).contains(project.version) else {
            throw CocoaError(.fileReadCorruptFile)
        }
        return project
    }
}
