import SwiftUI
import MetalKit
import ObjectiveC
import Darwin
import VulkaxRuntimeContract
import UniformTypeIdentifiers

enum VisualizerMode: Float, CaseIterable, Identifiable, Codable {
    case wave = 0
    case schwarzschild = 1
    case volumeSmoke = 2

    var id: Float { rawValue }
    var title: String {
        switch self {
        case .wave: return "Wave Field"
        case .schwarzschild: return "Schwarzschild"
        case .volumeSmoke: return "Volume Smoke"
        }
    }
}

struct ObstacleSceneItem: Identifiable {
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

final class PhysicsModel: ObservableObject {
    @Published var projectName = "Untitled Physics"
    @Published var scalarPresetId = "wave-field"
    @Published var equationSource = ScalarPreset.builtins[0].equation
    @Published var liveParameters = ScalarPreset.builtins[0].parameters
    @Published private(set) var equationStatus = "Ready"
    @Published private(set) var compiledMetalSource = ""
    @Published private(set) var compiledParameterNames: [String] = []
    @Published private(set) var compiledSourceHash: UInt64 = 0
    @Published private(set) var compileRevision: UInt64 = 0
    @Published private(set) var projectURL: URL?
    @Published private(set) var runtimeStatus = "Metal runtime starting"
    @Published var blackHoleMass: Float = 1.0
    @Published var diskGain: Float = 1.0
    @Published var cameraScale: Float = 1.0
    @Published var smokeBuoyancy: Float = 1.0
    @Published var smokeTurbulence: Float = 1.0
    @Published var volumeExtinction: Float = 2.2
    @Published var volumeEmission: Float = 1.0
    @Published var camera = StudioCamera.default
    @Published var cameraTrack = StudioCameraTrack()
    @Published var mediumOverride: SimulationMedium?
    @Published var captureSettings = CinematicCaptureSettings()
    @Published var capturePanelPresented = false
    @Published private(set) var isCapturing = false
    @Published private(set) var captureRequestRevision: UInt64 = 0
    @Published private(set) var pendingCaptureRequest: CinematicCaptureRequest?
    @Published private(set) var obstacleItems: [ObstacleSceneItem] = []
    @Published var selectedObstacleID: UUID?
    @Published private(set) var obstacleMeshRevision: UInt64 = 0
    private var defaultObstacleBody = RigidObstacleConfiguration.default
    var obstacleMesh: ImportedObstacleMesh? {
        selectedObstacleIndex.map { obstacleItems[$0].mesh }
    }
    var obstacleMeshURL: URL? {
        selectedObstacleIndex.map { obstacleItems[$0].url }
    }
    var obstacleBody: RigidObstacleConfiguration {
        get { selectedObstacleIndex.map { obstacleItems[$0].body } ?? defaultObstacleBody }
        set {
            if let index = selectedObstacleIndex {
                obstacleItems[index].body = newValue
            } else {
                defaultObstacleBody = newValue
            }
            obstacleMeshRevision &+= 1
            accumulationResetToken &+= 1
        }
    }
    @Published var accumulationResetToken: UInt32 = 0
    @Published var playing = true
    @Published var time: Float = 0.0
    @Published var executionGraph = EquationRuntimeGraph.builtIn(
        for: .wave, scalarEquation: ScalarPreset.builtins[0].equation)
    @Published var mode: VisualizerMode = .wave {
        didSet {
            executionGraph = .builtIn(for: mode, scalarEquation: equationSource)
            accumulationResetToken &+= 1
        }
    }

    init() {
        if CommandLine.arguments.contains("--black-hole-smoke") {
            mode = .schwarzschild
        } else if CommandLine.arguments.contains("--volume-smoke") {
            mode = .volumeSmoke
        }
        compileEquation()
    }

    private var selectedObstacleIndex: Int? {
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

    var effectiveTimelineCamera: StudioCamera {
        cameraTrack.camera(at: time, fallback: camera)
    }

    func setRuntimeStatus(_ status: String) {
        runtimeStatus = status
    }

    func selectObstacle(_ id: UUID?) {
        selectedObstacleID = id
    }

    func updateSelectedObstacleRole(_ role: SceneEntityRole) {
        guard let index = selectedObstacleIndex else { return }
        obstacleItems[index].role = role
        if !role.participatesInSimulation {
            obstacleItems[index].collisionProxy = .none
        } else if obstacleItems[index].collisionProxy == .none {
            obstacleItems[index].collisionProxy = obstacleItems[index].visualMesh.diagnostics.isWatertight
                ? .renderMesh : .boundsBox
            obstacleItems[index].mesh = proxyMesh(
                visualMesh: obstacleItems[index].visualMesh,
                kind: obstacleItems[index].collisionProxy)
        }
        obstacleMeshRevision &+= 1
        accumulationResetToken &+= 1
    }

    func updateSelectedCollisionProxy(_ kind: CollisionProxyKind) {
        guard let index = selectedObstacleIndex else { return }
        let effective = obstacleItems[index].role.participatesInSimulation ? kind : .none
        obstacleItems[index].collisionProxy = effective
        obstacleItems[index].mesh = proxyMesh(
            visualMesh: obstacleItems[index].visualMesh,
            kind: effective)
        obstacleMeshRevision &+= 1
        accumulationResetToken &+= 1
    }

    func removeSelectedObstacle() {
        guard let index = selectedObstacleIndex else { return }
        obstacleItems.remove(at: index)
        selectedObstacleID = obstacleItems.last?.id
        obstacleMeshRevision &+= 1
        accumulationResetToken &+= 1
    }

    func updateParameter(id: String, value: Float) {
        guard let index = liveParameters.firstIndex(where: { $0.id == id }) else { return }
        liveParameters[index].value = value
        accumulationResetToken &+= 1
    }

    func selectScalarPreset(_ preset: ScalarPreset) {
        scalarPresetId = preset.id
        equationSource = preset.equation
        liveParameters = preset.parameters
        mode = .wave
        compileEquation()
    }

    func compileEquation() {
        do {
            let old = Dictionary(uniqueKeysWithValues: liveParameters.map { ($0.name, $0) })
            let preset = ScalarPreset.builtins.first(where: { $0.id == scalarPresetId })
            let defaults = Dictionary(uniqueKeysWithValues: (preset?.parameters ?? []).map { ($0.name, $0) })
            let compiled = try ScalarEquationCompiler.compile(equationSource)
            liveParameters = compiled.parameterNames.map { name in
                old[name] ?? defaults[name] ?? ScalarPresetParameter(
                    name: name, value: 1, minimum: -10, maximum: 10, units: "scalar")
            }
            compiledMetalSource = compiled.metalSource
            compiledParameterNames = compiled.parameterNames
            compiledSourceHash = compiled.sourceHash
            if mode == .wave {
                executionGraph = .builtIn(for: .wave, scalarEquation: equationSource)
            }
            compileRevision &+= 1
            accumulationResetToken &+= 1
            equationStatus = "Compiled on CPU · Metal pipeline queued"
        } catch {
            equationStatus = error.localizedDescription
        }
    }

    func resetProject() {
        projectName = "Untitled Physics"
        projectURL = nil
        scalarPresetId = "wave-field"
        equationSource = ScalarPreset.builtins[0].equation
        liveParameters = ScalarPreset.builtins[0].parameters
        time = 0
        mode = .wave
        obstacleItems = []
        selectedObstacleID = nil
        obstacleMeshRevision &+= 1
        defaultObstacleBody = .default
        camera = .default
        cameraTrack = .init()
        mediumOverride = nil
        captureSettings = .init()
        compileEquation()
    }

    func openProject() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [UTType(filenameExtension: "vxp") ?? .json]
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            let project = try PhysicsProjectIO.load(from: url)
            projectName = project.name
            projectURL = url
            scalarPresetId = project.preset
            equationSource = project.expression
            liveParameters = project.parameters.map {
                ScalarPresetParameter(name: $0.key, value: $0.value, minimum: -10, maximum: 10, units: "scalar")
            }.sorted { $0.name < $1.name }
            time = project.timelineSeconds
            executionGraph = project.graph
            mode = project.graph.visualization
            defaultObstacleBody = project.obstacleBody
            camera = project.camera
            cameraTrack = project.cameraTrack
            mediumOverride = project.mediumOverride
            captureSettings = project.captureSettings
            var restored: [ObstacleSceneItem] = []
            for record in project.obstacles {
                let resolved = try PhysicsProjectIO.resolveAsset(record.meshPath, relativeTo: url)
                let visual = try ImportedObstacleMesh.load(from: resolved, requireWatertight: false)
                let kind = record.role.participatesInSimulation ? record.collisionProxy : .none
                restored.append(.init(
                    id: UUID(), mesh: proxyMesh(visualMesh: visual, kind: kind), visualMesh: visual,
                    url: resolved, body: record.body, role: record.role, collisionProxy: kind))
            }
            obstacleItems = restored
            selectedObstacleID = restored.first?.id
            obstacleMeshRevision &+= 1
            compileEquation()
            equationStatus = "Project opened"
        } catch {
            equationStatus = "Open failed: \(error.localizedDescription)"
        }
    }

    func saveProject() {
        let target: URL
        if let projectURL {
            target = projectURL
        } else {
            let panel = NSSavePanel()
            panel.allowedContentTypes = [UTType(filenameExtension: "vxp") ?? .json]
            panel.nameFieldStringValue = projectName.replacingOccurrences(of: " ", with: "-") + ".vxp"
            guard panel.runModal() == .OK, let url = panel.url else { return }
            target = url
        }
        do {
            var records: [ProjectObstacleRecord] = []
            for (index, item) in obstacleItems.enumerated() {
                let sourceExtension = item.url.pathExtension.lowercased()
                let assetExtension = sourceExtension.isEmpty ? "obj" : sourceExtension
                let path = try PhysicsProjectIO.packageObstacle(
                    from: item.url, for: target, assetName: "obstacle-\(index).\(assetExtension)")
                records.append(.init(
                    meshPath: path, body: item.body, role: item.role, collisionProxy: item.collisionProxy))
            }
            let project = PhysicsProjectFile(
                name: projectName,
                preset: scalarPresetId,
                visualization: Self.key(for: mode),
                expression: equationSource,
                timelineSeconds: time,
                parameters: Dictionary(uniqueKeysWithValues: liveParameters.map { ($0.name, $0.value) }),
                graph: executionGraph,
                obstacleMeshPath: records.first?.meshPath,
                obstacleBody: records.first?.body ?? defaultObstacleBody,
                obstacles: records,
                mediumOverride: mediumOverride,
                camera: camera,
                cameraTrack: cameraTrack,
                captureSettings: captureSettings)
            try PhysicsProjectIO.save(project, to: target)
            projectURL = target
            equationStatus = "Project saved"
        } catch {
            equationStatus = "Save failed: \(error.localizedDescription)"
        }
    }

    func addObstacleMesh() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = ["obj", "gltf", "glb"].compactMap { UTType(filenameExtension: $0) }
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            try loadObstacleMesh(from: url)
            mode = .volumeSmoke
            equationStatus = "GPU obstacle active: \(url.lastPathComponent)"
        } catch {
            equationStatus = "Obstacle rejected: \(error.localizedDescription)"
        }
    }

    func loadObstacleMesh(from url: URL) throws {
        let visualMesh = try ImportedObstacleMesh.load(from: url, requireWatertight: false)
        let defaultProxy: CollisionProxyKind = visualMesh.diagnostics.isWatertight ? .renderMesh : .boundsBox
        let item = ObstacleSceneItem(
            id: UUID(), mesh: proxyMesh(visualMesh: visualMesh, kind: defaultProxy), visualMesh: visualMesh,
            url: url, body: .default, role: .fluidObstacle, collisionProxy: defaultProxy)
        obstacleItems.append(item)
        selectedObstacleID = item.id
        obstacleMeshRevision &+= 1
        accumulationResetToken &+= 1
    }

    func addOrUpdateCameraKeyframe() {
        cameraTrack.insert(timeSeconds: time, camera: camera)
        accumulationResetToken &+= 1
        equationStatus = String(format: "Camera key saved at %.2f s", time)
    }

    func removeCameraKeyframe(_ id: UUID) {
        cameraTrack.remove(id: id)
        accumulationResetToken &+= 1
        equationStatus = "Camera key removed"
    }

    func useKeyedCameraAtPlayhead() {
        guard !cameraTrack.isEmpty else { return }
        camera = cameraTrack.camera(at: time, fallback: camera)
        accumulationResetToken &+= 1
    }

    func applyCameraPreset(_ preset: StudioCameraPreset) {
        camera.apply(preset: preset)
        accumulationResetToken &+= 1
    }

    func requestCinematicCapture() {
        capturePanelPresented = false
        if isCapturing {
            equationStatus = "A cinematic capture is already in progress"
            return
        }
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.quickTimeMovie]
        panel.nameFieldStringValue = projectName.replacingOccurrences(of: " ", with: "-") + "-simulation.mov"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        captureRequestRevision &+= 1
        pendingCaptureRequest = CinematicCaptureRequest(
            revision: captureRequestRevision,
            outputURL: url,
            settings: captureSettings,
            camera: camera,
            cameraTrack: cameraTrack,
            timelineSeconds: time)
        equationStatus = "Preparing \(captureSettings.resolution.title) capture · \(captureSettings.frameRate.title)"
    }

    func markCaptureStarted() {
        DispatchQueue.main.async {
            self.isCapturing = true
            self.equationStatus = "Recording \(self.captureSettings.resolution.title) · deterministic GPU frames"
        }
    }

    func markCaptureFinished(message: String) {
        DispatchQueue.main.async {
            self.isCapturing = false
            self.pendingCaptureRequest = nil
            self.equationStatus = message
        }
    }

    private static func key(for mode: VisualizerMode) -> String {
        switch mode {
        case .wave: return "scalar-field"
        case .schwarzschild: return "relativity"
        case .volumeSmoke: return "volume"
        }
    }

    private static func mode(for key: String) -> VisualizerMode {
        switch key {
        case "relativity": return .schwarzschild
        case "volume": return .volumeSmoke
        default: return .wave
        }
    }

    private func proxyMesh(visualMesh: ImportedObstacleMesh, kind: CollisionProxyKind) -> ImportedObstacleMesh {
        switch kind {
        case .none, .renderMesh: return visualMesh
        case .boundsBox: return ImportedObstacleMesh.boxProxy(for: visualMesh)
        case .sphere:
            // The numerical path has a box proxy today; sphere remains a
            // semantic request and is conservatively represented by the closed bounds.
            return ImportedObstacleMesh.boxProxy(for: visualMesh)
        }
    }
}

// The remainder of this file contains the Metal runtime, simulation kernels,
// native smoke entrypoints and application entrypoint.
// It is intentionally unchanged by this state-model repair.
