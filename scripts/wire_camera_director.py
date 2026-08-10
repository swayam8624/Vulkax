from pathlib import Path

main = Path('apps/VulkaxPhysicsStudioMac/Sources/VulkaxPhysicsStudioMac/main.swift')
text = main.read_text()
marker = '    @Published var camera = StudioCamera.default\n'
if 'cameraTrack = StudioCameraTrack()' not in text:
    if marker not in text: raise SystemExit('camera state marker missing')
    text = text.replace(marker, marker + '    @Published var cameraTrack = StudioCameraTrack()\n', 1)
text = text.replace(
    '        camera = .default\n        mediumOverride = nil\n',
    '        camera = .default\n        cameraTrack = .init()\n        mediumOverride = nil\n', 1)
text = text.replace(
    '            camera = project.camera\n            mediumOverride = project.mediumOverride\n',
    '            camera = project.camera\n            cameraTrack = project.cameraTrack\n            mediumOverride = project.mediumOverride\n', 1)
text = text.replace(
    '                camera: camera,\n                captureSettings: captureSettings)',
    '                camera: camera,\n                cameraTrack: cameraTrack,\n                captureSettings: captureSettings)', 1)
old_request = '''        pendingCaptureRequest = CinematicCaptureRequest(
            revision: captureRequestRevision,
            outputURL: url,
            settings: captureSettings,
            camera: camera,
            timelineSeconds: time)
'''
new_request = '''        pendingCaptureRequest = CinematicCaptureRequest(
            revision: captureRequestRevision,
            outputURL: url,
            settings: captureSettings,
            camera: camera,
            cameraTrack: cameraTrack,
            timelineSeconds: time)
'''
if old_request in text:
    text = text.replace(old_request, new_request, 1)
elif 'cameraTrack: cameraTrack' not in text:
    raise SystemExit('capture request marker missing')
method_marker = '    func applyCameraPreset(_ preset: StudioCameraPreset) {\n'
if 'func addOrUpdateCameraKeyframe()' not in text:
    methods = '''    var effectiveTimelineCamera: StudioCamera {
        cameraTrack.camera(at: time, fallback: camera)
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

'''
    if method_marker not in text: raise SystemExit('camera method marker missing')
    text = text.replace(method_marker, methods + method_marker, 1)
text = text.replace(
    '    private var activeCaptureCamera: StudioCamera?\n    private var captureStartTimeline: Float = 0\n',
    '    private var activeCaptureCamera: StudioCamera?\n    private var activeCaptureTrack = StudioCameraTrack()\n    private var captureStartTimeline: Float = 0\n', 1)
text = text.replace(
    '            activeCaptureCamera = request.camera\n            captureStartTimeline = request.timelineSeconds\n',
    '            activeCaptureCamera = request.camera\n            activeCaptureTrack = request.cameraTrack\n            captureStartTimeline = request.timelineSeconds\n', 1)
text = text.replace(
    '        let effectiveCamera = activeCaptureCamera.flatMap { activeCapture == nil ? nil : $0 } ?? model.camera\n',
    '''        let effectiveCamera: StudioCamera
        if activeCapture != nil {
            effectiveCamera = activeCaptureTrack.camera(
                at: simulationTime,
                fallback: activeCaptureCamera ?? model.camera)
        } else {
            effectiveCamera = model.cameraTrack.camera(at: simulationTime, fallback: model.camera)
        }
''', 1)
text = text.replace('                activeCaptureCamera = nil\n                failedSession?.finish', '                activeCaptureCamera = nil\n                activeCaptureTrack = .init()\n                failedSession?.finish')
text = text.replace('                            self?.activeCaptureCamera = nil\n                            DispatchQueue.main.async', '                            self?.activeCaptureCamera = nil\n                            self?.activeCaptureTrack = .init()\n                            DispatchQueue.main.async')
text = text.replace('                    self?.activeCaptureCamera = nil\n                    failedSession?.finish', '                    self?.activeCaptureCamera = nil\n                    self?.activeCaptureTrack = .init()\n                    failedSession?.finish')
cli_marker = '''        if CommandLine.arguments.contains("--native-cinematic-capture-smoke") {
            exit(runCinematicCaptureWriterSmoke() ? EXIT_SUCCESS : EXIT_FAILURE)
        }
'''
if '--native-camera-track-smoke' not in text:
    if cli_marker not in text: raise SystemExit('CLI smoke marker missing')
    text = text.replace(cli_marker, cli_marker + '''        if CommandLine.arguments.contains("--native-camera-track-smoke") {
            exit(runCameraTrackSmoke() ? EXIT_SUCCESS : EXIT_FAILURE)
        }
''', 1)
main.write_text(text)

capture = Path('apps/VulkaxPhysicsStudioMac/Sources/VulkaxPhysicsStudioMac/CinematicCaptureSession.swift')
text = capture.read_text()
if 'let cameraTrack: StudioCameraTrack' not in text:
    text = text.replace('    let camera: StudioCamera\n    let timelineSeconds: Float\n',
                        '    let camera: StudioCamera\n    let cameraTrack: StudioCameraTrack\n    let timelineSeconds: Float\n', 1)
capture.write_text(text)

project = Path('apps/VulkaxPhysicsStudioMac/Sources/VulkaxPhysicsStudioMac/PhysicsProject.swift')
text = project.read_text()
text = text.replace('    var version = 8\n', '    var version = 9\n', 1)
if 'var cameraTrack: StudioCameraTrack' not in text:
    text = text.replace('    var camera: StudioCamera\n    var captureSettings: CinematicCaptureSettings\n',
                        '    var camera: StudioCamera\n    var cameraTrack: StudioCameraTrack\n    var captureSettings: CinematicCaptureSettings\n', 1)
    text = text.replace('        case camera\n        case captureSettings = "capture_settings"\n',
                        '        case camera\n        case cameraTrack = "camera_track"\n        case captureSettings = "capture_settings"\n', 1)
    text = text.replace('        camera: StudioCamera = .default,\n        captureSettings: CinematicCaptureSettings = .init()\n',
                        '        camera: StudioCamera = .default,\n        cameraTrack: StudioCameraTrack = .init(),\n        captureSettings: CinematicCaptureSettings = .init()\n', 1)
    text = text.replace('        self.camera = camera\n        self.captureSettings = captureSettings\n',
                        '        self.camera = camera\n        self.cameraTrack = cameraTrack\n        self.captureSettings = captureSettings\n', 1)
    text = text.replace('        camera = try container.decodeIfPresent(StudioCamera.self, forKey: .camera) ?? .default\n        captureSettings = try container.decodeIfPresent(',
                        '        camera = try container.decodeIfPresent(StudioCamera.self, forKey: .camera) ?? .default\n        cameraTrack = try container.decodeIfPresent(StudioCameraTrack.self, forKey: .cameraTrack) ?? .init()\n        captureSettings = try container.decodeIfPresent(', 1)
project.write_text(text)

workspace = Path('apps/VulkaxPhysicsStudioMac/Sources/VulkaxPhysicsStudioMac/StudioWorkspaceView.swift')
text = workspace.read_text()
text = text.replace('            Text("Drop an OBJ or add a car/model")', '            Text("Drop OBJ, glTF or GLB · or add a car/model")')
text = text.replace('        .help("Import an OBJ visual model. Vulkax creates a safe physics proxy when needed.")',
                    '        .help("Import OBJ, glTF or GLB visual models. Vulkax creates a safe physics proxy when needed.")')
text = text.replace(
    '                        Label(String(format: "%.1f°", model.camera.verticalFovDegrees), systemImage: "camera.aperture")\n                        Text(String(format: "EV %.2f", model.camera.exposure))\n',
    '                        Label(String(format: "%.1f°", model.effectiveTimelineCamera.verticalFovDegrees), systemImage: "camera.aperture")\n                        Text(String(format: "EV %.2f", model.effectiveTimelineCamera.exposure))\n', 1)
text = text.replace(
    '            Text(model.playing ? "LIVE GPU" : "PAUSED")\n                .font(.caption2.bold())\n',
    '            if !model.cameraTrack.isEmpty {\n                Text("\\(model.cameraTrack.keyframes.count) CAM KEYS")\n                    .font(.caption2.bold()).foregroundStyle(.cyan)\n            }\n            Text(model.playing ? "LIVE GPU" : "PAUSED")\n                .font(.caption2.bold())\n', 1)
track_marker = '''            scalar("Exposure", value: cameraBinding(\\.exposure), range: 0.05...8)
            Divider()
            Text("CAPTURE").sectionLabel()
'''
track_replacement = '''            scalar("Exposure", value: cameraBinding(\\.exposure), range: 0.05...8)
            Divider()
            HStack {
                Text("DIRECTOR TRACK").sectionLabel()
                Spacer()
                Text("\\(model.cameraTrack.keyframes.count) keys")
                    .font(.caption2.monospacedDigit()).foregroundStyle(.secondary)
            }
            HStack {
                Button { model.addOrUpdateCameraKeyframe() } label: {
                    Label("Add / Update Key", systemImage: "diamond.fill")
                }
                .buttonStyle(.borderedProminent)
                Button("Use Key at Playhead") { model.useKeyedCameraAtPlayhead() }
                    .buttonStyle(.bordered)
                    .disabled(model.cameraTrack.isEmpty)
            }
            if !model.cameraTrack.keyframes.isEmpty {
                VStack(spacing: 4) {
                    ForEach(model.cameraTrack.keyframes) { key in
                        HStack {
                            Image(systemName: "diamond.fill").font(.caption2).foregroundStyle(.cyan)
                            Button(String(format: "%.2f s", key.timeSeconds)) {
                                model.time = key.timeSeconds
                                model.camera = key.camera
                                model.accumulationResetToken &+= 1
                            }
                            .buttonStyle(.plain)
                            Spacer()
                            Text(String(format: "%.0f°", key.camera.verticalFovDegrees))
                                .font(.caption2.monospacedDigit()).foregroundStyle(.secondary)
                            Button(role: .destructive) { model.removeCameraKeyframe(key.id) } label: {
                                Image(systemName: "xmark.circle.fill")
                            }
                            .buttonStyle(.borderless)
                        }
                        .padding(.vertical, 3)
                    }
                }
                .padding(8)
                .background(Color.black.opacity(0.20), in: RoundedRectangle(cornerRadius: 8))
            }
            Divider()
            Text("CAPTURE").sectionLabel()
'''
if track_marker in text:
    text = text.replace(track_marker, track_replacement, 1)
elif 'DIRECTOR TRACK' not in text:
    raise SystemExit('camera inspector marker missing')
text = text.replace('            Text("Capture uses the current Studio camera. Orbit/pan/dolly to frame the shot before recording.")',
                    '            Text(model.cameraTrack.isEmpty ? "Capture uses the current Studio camera." : "Capture follows the director camera track from the current playhead.")')
text = text.replace('            guard let url, url.pathExtension.lowercased() == "obj" else { return }',
                    '            guard let url, ["obj", "gltf", "glb"].contains(url.pathExtension.lowercased()) else { return }', 1)
workspace.write_text(text)
