import SwiftUI
import UniformTypeIdentifiers

private enum StudioInspectorTab: String, CaseIterable, Identifiable {
    case simulation = "Simulation"
    case object = "Object"
    case camera = "Camera"
    var id: String { rawValue }
}

struct StudioWorkspaceView: View {
    @ObservedObject var model: PhysicsModel
    @State private var inspectorTab: StudioInspectorTab = .simulation

    var body: some View {
        VStack(spacing: 0) {
            topBar
            HSplitView {
                sceneSidebar
                    .frame(minWidth: 220, idealWidth: 248, maxWidth: 290)
                viewport
                    .frame(minWidth: 680, minHeight: 560)
                inspector
                    .frame(minWidth: 310, idealWidth: 350, maxWidth: 410)
            }
        }
        .frame(minWidth: 1240, minHeight: 760)
        .preferredColorScheme(.dark)
        .background(Color(red: 0.018, green: 0.025, blue: 0.040))
    }

    private var topBar: some View {
        HStack(spacing: 12) {
            HStack(spacing: 8) {
                Image(systemName: "waveform.path.ecg.rectangle.fill")
                    .font(.title3)
                    .foregroundStyle(.mint)
                Text("VULKAX")
                    .font(.system(size: 18, weight: .black, design: .rounded))
                    .tracking(1.2)
            }
            TextField("Project", text: $model.projectName)
                .textFieldStyle(.plain)
                .font(.headline)
                .frame(maxWidth: 260)
            toolbarDivider
            toolbarButton("New", "doc.badge.plus", action: model.newProject)
            toolbarButton("Open", "folder", action: model.openProject)
            toolbarButton("Save", "square.and.arrow.down", action: model.saveProject)
            toolbarDivider
            Button { model.importObstacleMesh() } label: {
                Label("Add Model", systemImage: "car.side")
            }
            .help("Import an OBJ visual model. Vulkax creates a safe physics proxy when needed.")
            Button { model.capturePanelPresented.toggle() } label: {
                Label("Record", systemImage: model.isCapturing ? "record.circle.fill" : "video.badge.plus")
            }
            .tint(model.isCapturing ? .red : .accentColor)
            Spacer()
            mediumBadge(compact: true)
            Text(model.runtimeStatus)
                .font(.caption.monospacedDigit())
                .foregroundStyle(.secondary)
                .lineLimit(1)
        }
        .buttonStyle(.borderless)
        .padding(.horizontal, 14)
        .frame(height: 50)
        .background(.ultraThinMaterial)
        .overlay(alignment: .bottom) { Divider() }
    }

    private var toolbarDivider: some View {
        Divider().frame(height: 22)
    }

    private func toolbarButton(_ title: String, _ icon: String, action: @escaping () -> Void) -> some View {
        Button(action: action) { Label(title, systemImage: icon) }
    }

    private var sceneSidebar: some View {
        VStack(alignment: .leading, spacing: 0) {
            panelHeader("SCENE", icon: "square.3.layers.3d")
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    mediumCard
                    VStack(alignment: .leading, spacing: 8) {
                        Text("VISUALIZER").sectionLabel()
                        Picker("Visualizer", selection: $model.mode) {
                            ForEach(VisualizerMode.allCases) { value in
                                Label(value.title, systemImage: visualizerIcon(value)).tag(value)
                            }
                        }
                        .labelsHidden()
                        .pickerStyle(.radioGroup)
                    }
                    Divider()
                    HStack {
                        Text("ENTITIES").sectionLabel()
                        Spacer()
                        Button { model.importObstacleMesh() } label: { Image(systemName: "plus") }
                            .buttonStyle(.borderless)
                    }
                    if model.obstacleItems.isEmpty {
                        VStack(spacing: 8) {
                            Image(systemName: "cube.transparent")
                                .font(.system(size: 28, weight: .light))
                                .foregroundStyle(.secondary)
                            Text("Drop an OBJ or add a car/model")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                                .multilineTextAlignment(.center)
                        }
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 18)
                        .background(Color.white.opacity(0.025), in: RoundedRectangle(cornerRadius: 10))
                    } else {
                        VStack(spacing: 4) {
                            ForEach(model.obstacleItems) { item in
                                Button { model.selectObstacle(item.id); inspectorTab = .object } label: {
                                    HStack(spacing: 9) {
                                        Image(systemName: item.role == .fluidObstacle ? "wind" : "cube")
                                            .frame(width: 18)
                                            .foregroundStyle(model.selectedObstacleID == item.id ? .mint : .secondary)
                                        VStack(alignment: .leading, spacing: 2) {
                                            Text(item.url.deletingPathExtension().lastPathComponent)
                                                .font(.subheadline.weight(.medium))
                                                .lineLimit(1)
                                            Text("\(item.role.title) · \(item.collisionProxy.title)")
                                                .font(.caption2)
                                                .foregroundStyle(.secondary)
                                                .lineLimit(1)
                                        }
                                        Spacer()
                                        if model.selectedObstacleID == item.id {
                                            Image(systemName: "chevron.right").font(.caption2).foregroundStyle(.secondary)
                                        }
                                    }
                                    .contentShape(Rectangle())
                                    .padding(.horizontal, 8)
                                    .padding(.vertical, 7)
                                    .background(model.selectedObstacleID == item.id ? Color.mint.opacity(0.10) : .clear,
                                                in: RoundedRectangle(cornerRadius: 7))
                                }
                                .buttonStyle(.plain)
                            }
                        }
                    }
                    Divider()
                    Text("CAMERA").sectionLabel()
                    LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 6) {
                        ForEach(StudioCameraPreset.allCases) { preset in
                            Button(preset.title) { model.applyCameraPreset(preset) }
                                .buttonStyle(.bordered)
                                .controlSize(.small)
                        }
                    }
                    Text("Drag: orbit · Shift/right-drag: pan · Scroll: dolly")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                }
                .padding(14)
            }
        }
        .background(Color(red: 0.026, green: 0.038, blue: 0.060))
    }

    private var viewport: some View {
        ZStack {
            MetalWaveView(model: model)
                .onDrop(of: [UTType.fileURL.identifier], isTargeted: nil, perform: handleDrop)
            VStack {
                HStack {
                    mediumBadge(compact: false)
                    Spacer()
                    HStack(spacing: 6) {
                        Label(String(format: "%.1f°", model.camera.verticalFovDegrees), systemImage: "camera.aperture")
                        Text(String(format: "EV %.2f", model.camera.exposure))
                    }
                    .font(.caption.monospacedDigit())
                    .padding(.horizontal, 9).padding(.vertical, 6)
                    .background(.black.opacity(0.48), in: Capsule())
                }
                .padding(12)
                Spacer()
                timelineBar
                    .padding(12)
            }
        }
        .background(Color.black)
        .sheet(isPresented: $model.capturePanelPresented) {
            captureSheet
        }
    }

    private var timelineBar: some View {
        HStack(spacing: 10) {
            Button { model.playing.toggle() } label: {
                Image(systemName: model.playing ? "pause.fill" : "play.fill")
            }
            .buttonStyle(.plain)
            Button {
                model.time = 0
                model.accumulationResetToken &+= 1
            } label: { Image(systemName: "backward.end.fill") }
            .buttonStyle(.plain)
            Slider(value: $model.time, in: 0...60) { editing in
                if editing { model.playing = false }
                model.accumulationResetToken &+= 1
            }
            Text(String(format: "%06.2f s", model.time))
                .font(.caption.monospacedDigit())
                .frame(width: 64, alignment: .trailing)
            Text(model.playing ? "LIVE GPU" : "PAUSED")
                .font(.caption2.bold())
                .foregroundStyle(model.playing ? .mint : .orange)
        }
        .padding(.horizontal, 12).padding(.vertical, 9)
        .background(.black.opacity(0.60), in: RoundedRectangle(cornerRadius: 10))
    }

    private var inspector: some View {
        VStack(spacing: 0) {
            panelHeader("INSPECTOR", icon: "slider.horizontal.3")
            Picker("Inspector", selection: $inspectorTab) {
                ForEach(StudioInspectorTab.allCases) { Text($0.rawValue).tag($0) }
            }
            .pickerStyle(.segmented)
            .padding(12)
            Divider()
            ScrollView {
                Group {
                    switch inspectorTab {
                    case .simulation: simulationInspector
                    case .object: objectInspector
                    case .camera: cameraInspector
                    }
                }
                .padding(14)
            }
        }
        .background(Color(red: 0.026, green: 0.038, blue: 0.060))
    }

    private var simulationInspector: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("DOMAIN / MEDIUM").sectionLabel()
            Picker("Medium", selection: $model.mediumOverride) {
                Text("Auto · \(model.inferredMedium.medium.title)").tag(nil as SimulationMedium?)
                Divider()
                ForEach(SimulationMedium.allCases) { medium in
                    Label(medium.title, systemImage: medium.systemImage).tag(Optional(medium))
                }
            }
            Text("Auto confidence: \(model.inferredMedium.confidencePercent)%")
                .font(.caption.monospacedDigit())
                .foregroundStyle(.secondary)
            ForEach(model.inferredMedium.reasons, id: \.self) { reason in
                Label(reason, systemImage: "lightbulb.min")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Divider()
            if model.mode == .wave {
                Text("EQUATION").sectionLabel()
                Picker("Preset", selection: Binding(
                    get: { model.scalarPresetId }, set: { model.selectScalarPreset($0) })) {
                    ForEach(ScalarPreset.builtins) { Text($0.title).tag($0.id) }
                }
                TextEditor(text: $model.equationSource)
                    .font(.system(.body, design: .monospaced))
                    .frame(minHeight: 124)
                    .padding(7)
                    .background(Color.black.opacity(0.30), in: RoundedRectangle(cornerRadius: 7))
                HStack {
                    Button { model.compileEquation() } label: { Label("Compile", systemImage: "hammer.fill") }
                    Spacer()
                    Text(String(model.compiledSourceHash, radix: 16).prefix(8))
                        .font(.caption.monospaced()).foregroundStyle(.secondary)
                }
                Text(model.equationStatus)
                    .font(.caption)
                    .foregroundStyle(model.equationStatus.contains("failed") || model.equationStatus.contains("Column") ? .red : .secondary)
                ForEach(model.liveParameters) { parameter in liveParameter(parameter) }
            } else if model.mode == .schwarzschild {
                scalar("Mass", value: $model.blackHoleMass, range: 0.25...1.6)
                scalar("Disk gain", value: $model.diskGain, range: 0...4)
                scalar("Camera scale", value: $model.cameraScale, range: 0.5...2)
            } else {
                scalar("Buoyancy", value: $model.smokeBuoyancy, range: 0...3)
                scalar("Turbulence", value: $model.smokeTurbulence, range: 0...3)
                scalar("Extinction", value: $model.volumeExtinction, range: 0.1...5)
                scalar("Emission", value: $model.volumeEmission, range: 0...3)
            }
        }
    }

    @ViewBuilder private var objectInspector: some View {
        if model.selectedObstacleID == nil {
            VStack(spacing: 12) {
                Image(systemName: "cube.transparent").font(.largeTitle).foregroundStyle(.secondary)
                Text("Select a scene entity")
                Button { model.importObstacleMesh() } label: { Label("Add model", systemImage: "plus") }
            }
            .frame(maxWidth: .infinity)
            .padding(.top, 40)
        } else {
            VStack(alignment: .leading, spacing: 14) {
                Text(model.selectedObstacleName).font(.headline)
                Text(model.selectedVisualDiagnostics)
                    .font(.caption).foregroundStyle(.secondary)
                Text("SIMULATION ROLE").sectionLabel()
                Picker("Role", selection: selectedRoleBinding) {
                    ForEach(SceneEntityRole.allCases) { Text($0.title).tag($0) }
                }
                Picker("Proxy", selection: selectedProxyBinding) {
                    ForEach(CollisionProxyKind.allCases) { Text($0.title).tag($0) }
                }
                Text(model.selectedProxyDescription)
                    .font(.caption).foregroundStyle(.secondary)
                Divider()
                HStack {
                    Text("TRANSFORM").sectionLabel()
                    Spacer()
                    Button { model.obstacleBody = .default } label: { Image(systemName: "arrow.counterclockwise") }
                        .buttonStyle(.borderless)
                }
                vector("Position", x: obstacleBinding(\.position.x), y: obstacleBinding(\.position.y), z: obstacleBinding(\.position.z))
                vector("Rotation°", x: obstacleBinding(\.rotationDegrees.x), y: obstacleBinding(\.rotationDegrees.y), z: obstacleBinding(\.rotationDegrees.z))
                vector("Scale", x: obstacleBinding(\.scale.x), y: obstacleBinding(\.scale.y), z: obstacleBinding(\.scale.z))
                scalar("Mass kg", value: obstacleBinding(\.mass), range: 0.01...5000)
                vector("Linear velocity", x: obstacleBinding(\.linearVelocity.x), y: obstacleBinding(\.linearVelocity.y), z: obstacleBinding(\.linearVelocity.z))
                vector("Angular velocity", x: obstacleBinding(\.angularVelocity.x), y: obstacleBinding(\.angularVelocity.y), z: obstacleBinding(\.angularVelocity.z))
                Divider()
                Button(role: .destructive) { model.removeObstacleMesh() } label: { Label("Remove entity", systemImage: "trash") }
            }
        }
    }

    private var cameraInspector: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("VIEW CAMERA").sectionLabel()
            Picker("Preset", selection: Binding(
                get: { StudioCameraPreset.perspective },
                set: { model.applyCameraPreset($0) })) {
                ForEach(StudioCameraPreset.allCases) { Text($0.title).tag($0) }
            }
            vector("Position", x: cameraBinding(\.position.x), y: cameraBinding(\.position.y), z: cameraBinding(\.position.z))
            vector("Target", x: cameraBinding(\.target.x), y: cameraBinding(\.target.y), z: cameraBinding(\.target.z))
            scalar("Vertical FOV", value: cameraBinding(\.verticalFovDegrees), range: 10...120)
            scalar("Exposure", value: cameraBinding(\.exposure), range: 0.05...8)
            Divider()
            Text("CAPTURE").sectionLabel()
            Picker("Resolution", selection: $model.captureSettings.resolution) {
                ForEach(CinematicCaptureSettings.Resolution.allCases) { Text($0.title).tag($0) }
            }
            Picker("Frame rate", selection: $model.captureSettings.frameRate) {
                ForEach(CinematicCaptureSettings.FrameRate.allCases) { Text($0.title).tag($0) }
            }
            HStack {
                Text("Duration")
                Slider(value: $model.captureSettings.durationSeconds, in: 1...120)
                Text("\(Int(model.captureSettings.durationSeconds))s").monospacedDigit()
            }
            Button { model.capturePanelPresented = true } label: { Label("Record from this camera", systemImage: "video.fill") }
                .buttonStyle(.borderedProminent)
        }
    }

    private var mediumCard: some View {
        VStack(alignment: .leading, spacing: 7) {
            HStack {
                Image(systemName: model.activeMedium.systemImage).foregroundStyle(.mint)
                Text(model.activeMedium.title).font(.subheadline.weight(.semibold))
                Spacer()
                Text(model.mediumOverride == nil ? "AUTO" : "MANUAL")
                    .font(.caption2.bold()).foregroundStyle(model.mediumOverride == nil ? .mint : .orange)
            }
            if model.mediumOverride == nil {
                ProgressView(value: model.inferredMedium.confidence)
                    .progressViewStyle(.linear)
                Text("Inference \(model.inferredMedium.confidencePercent)% · click Simulation to override")
                    .font(.caption2).foregroundStyle(.secondary)
            }
        }
        .padding(10)
        .background(Color.mint.opacity(0.07), in: RoundedRectangle(cornerRadius: 10))
    }

    private func mediumBadge(compact: Bool) -> some View {
        HStack(spacing: 6) {
            Image(systemName: model.activeMedium.systemImage)
            Text(model.activeMedium.title)
            if !compact && model.mediumOverride == nil { Text("\(model.inferredMedium.confidencePercent)%") }
        }
        .font(.caption.weight(.semibold))
        .foregroundStyle(.mint)
        .padding(.horizontal, 9).padding(.vertical, 6)
        .background(.black.opacity(0.48), in: Capsule())
    }

    private var captureSheet: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack {
                Image(systemName: "video.fill").foregroundStyle(.red)
                Text("Cinematic Capture").font(.title2.bold())
                Spacer()
            }
            Text("Capture uses the current Studio camera. Orbit/pan/dolly to frame the shot before recording.")
                .foregroundStyle(.secondary)
            Picker("Resolution", selection: $model.captureSettings.resolution) {
                ForEach(CinematicCaptureSettings.Resolution.allCases) { Text("\($0.title) · \($0.rawValue)").tag($0) }
            }
            Picker("Frame rate", selection: $model.captureSettings.frameRate) {
                ForEach(CinematicCaptureSettings.FrameRate.allCases) { Text($0.title).tag($0) }
            }
            HStack {
                Text("Duration")
                Slider(value: $model.captureSettings.durationSeconds, in: 1...120)
                Text("\(Int(model.captureSettings.durationSeconds)) sec").monospacedDigit().frame(width: 58)
            }
            Divider()
            HStack {
                Button("Cancel") { model.capturePanelPresented = false }
                Spacer()
                Button { model.requestCinematicCapture() } label: {
                    Label(model.isCapturing ? "Stop Recording" : "Record .mov", systemImage: "record.circle")
                }
                .buttonStyle(.borderedProminent)
                .tint(.red)
            }
        }
        .padding(24)
        .frame(width: 500)
    }

    private func handleDrop(_ providers: [NSItemProvider]) -> Bool {
        guard let provider = providers.first else { return false }
        provider.loadItem(forTypeIdentifier: UTType.fileURL.identifier, options: nil) { item, _ in
            let url: URL?
            if let data = item as? Data { url = URL(dataRepresentation: data, relativeTo: nil) }
            else { url = item as? URL }
            guard let url, url.pathExtension.lowercased() == "obj" else { return }
            DispatchQueue.main.async { model.importObstacleMesh(from: url); inspectorTab = .object }
        }
        return true
    }

    private func panelHeader(_ title: String, icon: String) -> some View {
        HStack(spacing: 7) {
            Image(systemName: icon).foregroundStyle(.secondary)
            Text(title).font(.caption.bold()).foregroundStyle(.secondary)
            Spacer()
        }
        .padding(.horizontal, 14).frame(height: 36)
        .background(Color.black.opacity(0.16))
        .overlay(alignment: .bottom) { Divider() }
    }

    private func visualizerIcon(_ mode: VisualizerMode) -> String {
        switch mode {
        case .wave: return "waveform.path"
        case .schwarzschild: return "circle.circle"
        case .volumeSmoke: return "cloud.fog.fill"
        }
    }

    private var selectedRoleBinding: Binding<SceneEntityRole> {
        Binding(get: { model.selectedObstacleRole }, set: { model.selectedObstacleRole = $0 })
    }
    private var selectedProxyBinding: Binding<CollisionProxyKind> {
        Binding(get: { model.selectedCollisionProxy }, set: { model.selectedCollisionProxy = $0 })
    }

    private func obstacleBinding(_ keyPath: WritableKeyPath<RigidObstacleConfiguration, Float>) -> Binding<Float> {
        Binding(get: { model.obstacleBody[keyPath: keyPath] }, set: { model.obstacleBody[keyPath: keyPath] = $0 })
    }
    private func cameraBinding(_ keyPath: WritableKeyPath<StudioCamera, Float>) -> Binding<Float> {
        Binding(get: { model.camera[keyPath: keyPath] }, set: {
            model.camera[keyPath: keyPath] = $0
            model.camera.sanitize()
            model.accumulationResetToken &+= 1
        })
    }

    private func vector(_ title: String, x: Binding<Float>, y: Binding<Float>, z: Binding<Float>) -> some View {
        VStack(alignment: .leading, spacing: 5) {
            Text(title).font(.subheadline.weight(.medium))
            HStack(spacing: 6) {
                vectorField("X", x); vectorField("Y", y); vectorField("Z", z)
            }
        }
    }

    private func vectorField(_ axis: String, _ value: Binding<Float>) -> some View {
        HStack(spacing: 3) {
            Text(axis).font(.caption2.bold()).foregroundStyle(.secondary)
            TextField(axis, value: value, format: .number.precision(.fractionLength(3)))
                .textFieldStyle(.roundedBorder)
        }
    }

    private func scalar(_ title: String, value: Binding<Float>, range: ClosedRange<Float>) -> some View {
        VStack(alignment: .leading, spacing: 5) {
            HStack { Text(title); Spacer(); Text(String(format: "%.3f", value.wrappedValue)).monospacedDigit().foregroundStyle(.secondary) }
            Slider(value: value, in: range)
        }
    }

    private func liveParameter(_ parameter: LiveParameter) -> some View {
        scalar(
            parameter.name.replacingOccurrences(of: "_", with: " ").capitalized,
            value: Binding(
                get: { model.liveParameters.first(where: { $0.id == parameter.id })?.value ?? parameter.value },
                set: { model.updateParameter(id: parameter.id, value: $0) }),
            range: parameter.minimum...parameter.maximum)
    }
}

private extension View {
    func sectionLabel() -> some View {
        self.font(.caption2.bold()).foregroundStyle(.secondary).tracking(0.8)
    }
}
