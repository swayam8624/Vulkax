from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise SystemExit(f'missing marker: {label}')
    return text.replace(old, new, 1)

capture_path = Path('apps/VulkaxPhysicsStudioMac/Sources/VulkaxPhysicsStudioMac/CinematicCaptureSession.swift')
capture = capture_path.read_text()
request_marker = '''struct CinematicCaptureTarget {
    let pixelBuffer: CVPixelBuffer
    let texture: MTLTexture
    let frameIndex: Int
}
'''
request_block = request_marker + '''
struct CinematicCaptureRequest {
    let revision: UInt64
    let outputURL: URL
    let settings: CinematicCaptureSettings
    let camera: StudioCamera
    let timelineSeconds: Float
}
'''
if 'struct CinematicCaptureRequest' not in capture:
    capture = replace_once(capture, request_marker, request_block, 'capture request')
capture_path.write_text(capture)

main_path = Path('apps/VulkaxPhysicsStudioMac/Sources/VulkaxPhysicsStudioMac/main.swift')
text = main_path.read_text()
text = replace_once(text, '''    @Published private(set) var isCapturing = false
    @Published private(set) var captureRequestRevision: UInt64 = 0
''', '''    @Published private(set) var isCapturing = false
    @Published private(set) var captureRequestRevision: UInt64 = 0
    @Published private(set) var pendingCaptureRequest: CinematicCaptureRequest?
''', 'capture model property')
text = replace_once(text, '''    func requestCinematicCapture() {
        capturePanelPresented = false
        captureRequestRevision &+= 1
        equationStatus = "Cinematic capture requested · \(captureSettings.resolution.title) · \(captureSettings.frameRate.title)"
    }
''', '''    func requestCinematicCapture() {
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
            timelineSeconds: time)
        equationStatus = "Preparing \(captureSettings.resolution.title) capture · \(captureSettings.frameRate.title)"
    }
''', 'capture request method')

text = replace_once(text, '''    private let commandQueue: MTLCommandQueue
    private let displayPipeline: MTLRenderPipelineState
''', '''    private let commandQueue: MTLCommandQueue
    private let displayPipeline: MTLRenderPipelineState
    private let captureDisplayPipeline: MTLRenderPipelineState
    private let sceneRenderer: StudioSceneMeshRenderer
''', 'renderer pipeline properties')
text = replace_once(text, '''    private var frameIndex: UInt64 = 0
    private var lastResetToken: UInt32 = 0
''', '''    private var frameIndex: UInt64 = 0
    private var lastResetToken: UInt32 = 0
    private var captureSession: CinematicCaptureSession?
    private var activeCaptureCamera: StudioCamera?
    private var captureStartTimeline: Float = 0
    private var lastCaptureRequestRevision: UInt64 = 0
''', 'renderer capture state')
text = replace_once(text, '''            descriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat
            descriptor.colorAttachments[0].isBlendingEnabled = false
            self.displayPipeline = try device.makeRenderPipelineState(descriptor: descriptor)
''', '''            descriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat
            descriptor.colorAttachments[0].isBlendingEnabled = false
            descriptor.depthAttachmentPixelFormat = .depth32Float
            self.displayPipeline = try device.makeRenderPipelineState(descriptor: descriptor)
            let captureDescriptor = MTLRenderPipelineDescriptor()
            captureDescriptor.vertexFunction = library.makeFunction(name: "fullscreen")
            captureDescriptor.fragmentFunction = library.makeFunction(name: "display")
            captureDescriptor.colorAttachments[0].pixelFormat = .bgra8Unorm
            captureDescriptor.colorAttachments[0].isBlendingEnabled = false
            captureDescriptor.depthAttachmentPixelFormat = .depth32Float
            self.captureDisplayPipeline = try device.makeRenderPipelineState(descriptor: captureDescriptor)
            self.sceneRenderer = try StudioSceneMeshRenderer(
                device: device,
                interactiveColorFormat: view.colorPixelFormat,
                depthFormat: .depth32Float)
''', 'capture and scene pipelines')
text = replace_once(text, '''        view.device = device
        view.delegate = self
        view.framebufferOnly = true
''', '''        view.device = device
        view.delegate = self
        view.depthStencilPixelFormat = .depth32Float
        view.framebufferOnly = true
''', 'depth pixel format')

schedule_marker = '''    private func scheduleEquationPipeline(device: MTLDevice, model: PhysicsModel) {
'''
capture_sync = '''    private func synchronizeCaptureRequest(device: MTLDevice, model: PhysicsModel) {
        guard model.captureRequestRevision != lastCaptureRequestRevision else { return }
        lastCaptureRequestRevision = model.captureRequestRevision
        guard let request = model.pendingCaptureRequest, request.revision == model.captureRequestRevision else { return }
        do {
            captureSession = try CinematicCaptureSession(
                outputURL: request.outputURL, settings: request.settings, device: device)
            activeCaptureCamera = request.camera
            captureStartTimeline = request.timelineSeconds
            model.accumulationResetToken &+= 1
            DispatchQueue.main.async { [weak model] in
                model?.reportCaptureState(
                    active: true,
                    message: "Recording \(request.settings.resolution.title) · \(request.settings.frameRate.title) · \(request.outputURL.lastPathComponent)")
            }
        } catch {
            captureSession = nil
            activeCaptureCamera = nil
            DispatchQueue.main.async { [weak model] in
                model?.reportCaptureState(active: false, message: "Capture failed: \(error.localizedDescription)")
            }
        }
    }

'''
if 'private func synchronizeCaptureRequest' not in text:
    text = replace_once(text, schedule_marker, capture_sync + schedule_marker, 'capture synchronizer')

old_draw_start = '''    func draw(in view: MTKView) {
        guard let model, let device = view.device, let drawable = view.currentDrawable, let pass = view.currentRenderPassDescriptor,
              let command = commandQueue.makeCommandBuffer() else { return }
        guard inFlightFrames.wait(timeout: .now()) == .success else { return }
        var submitted = false
        defer { if !submitted { inFlightFrames.signal() } }
        let isRelativity = model.executionGraph.contains("integrate_active_rays")
        let isVolume = model.executionGraph.contains("volume_transport")
        let isScalar = model.executionGraph.contains("evaluate_scalar_field")
        let renderScale: CGFloat = isRelativity ? 0.55 : 1.0
        guard let radiance = ensureHdrRadiance(view.drawableSize, scale: renderScale, device: device) else { return }
        let now = CACurrentMediaTime()
        let delta = min(Float(now - lastTime), 1.0 / 20.0)
        lastTime = now
        scheduleEquationPipeline(device: device, model: model)
        frameIndex &+= 1
'''
new_draw_start = '''    func draw(in view: MTKView) {
        guard let model, let device = view.device, let drawable = view.currentDrawable, let pass = view.currentRenderPassDescriptor,
              let command = commandQueue.makeCommandBuffer() else { return }
        guard inFlightFrames.wait(timeout: .now()) == .success else { return }
        var submitted = false
        defer { if !submitted { inFlightFrames.signal() } }
        synchronizeCaptureRequest(device: device, model: model)
        let activeCapture = captureSession?.shouldEncodeMoreFrames == true ? captureSession : nil
        let isRelativity = model.executionGraph.contains("integrate_active_rays")
        let isVolume = model.executionGraph.contains("volume_transport")
        let isScalar = model.executionGraph.contains("evaluate_scalar_field")
        let renderScale: CGFloat = activeCapture == nil && isRelativity ? 0.55 : 1.0
        let renderSize = activeCapture.map { CGSize(width: $0.width, height: $0.height) } ?? view.drawableSize
        guard let radiance = ensureHdrRadiance(renderSize, scale: renderScale, device: device) else { return }
        let now = CACurrentMediaTime()
        let interactiveDelta = min(Float(now - lastTime), 1.0 / 20.0)
        lastTime = now
        let delta = activeCapture.map { 1.0 / Float($0.frameRate) } ?? interactiveDelta
        let simulationTime = activeCapture.map {
            captureStartTimeline + Float($0.frameCount) / Float($0.frameRate)
        } ?? model.time
        let effectiveCamera = activeCaptureCamera.flatMap { activeCapture == nil ? nil : $0 } ?? model.camera
        scheduleEquationPipeline(device: device, model: model)
        sceneRenderer.rebuildIfNeeded(device: device, model: model)
        frameIndex &+= 1
'''
text = replace_once(text, old_draw_start, new_draw_start, 'draw start')
text = text.replace('        frameRequest.timelineSeconds = model.time\n', '        frameRequest.timelineSeconds = simulationTime\n', 1)
text = text.replace('        frameRequest.renderScale = isRelativity ? 0.55 : 1.0\n', '        frameRequest.renderScale = Float(renderScale)\n', 1)
text = text.replace('        let uniforms = WaveUniforms(time: model.time,\n', '        let uniforms = WaveUniforms(time: simulationTime,\n', 1)
text = replace_once(text, '''        copy.cameraPositionExposure = SIMD4(model.camera.position, model.camera.exposure)
        copy.cameraTarget = SIMD4(model.camera.target, 0)
        copy.cameraUpFov = SIMD4(model.camera.up, model.camera.verticalFovDegrees)
''', '''        copy.cameraPositionExposure = SIMD4(effectiveCamera.position, effectiveCamera.exposure)
        copy.cameraTarget = SIMD4(effectiveCamera.target, 0)
        copy.cameraUpFov = SIMD4(effectiveCamera.up, effectiveCamera.verticalFovDegrees)
''', 'effective camera uniforms')

old_present = '''        guard let encoder = command.makeRenderCommandEncoder(descriptor: pass) else { return }
        encoder.setRenderPipelineState(displayPipeline)
        encoder.setFragmentTexture(displayRadiance, index: 0)
        encoder.setFragmentBytes(&copy, length: MemoryLayout<WaveUniforms>.stride, index: 0)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        encoder.endEncoding()
        let submittedFrame = frameRequest
'''
new_present = '''        pass.depthAttachment.clearDepth = 1.0
        guard let encoder = command.makeRenderCommandEncoder(descriptor: pass) else { return }
        encoder.setRenderPipelineState(displayPipeline)
        encoder.setFragmentTexture(displayRadiance, index: 0)
        encoder.setFragmentBytes(&copy, length: MemoryLayout<WaveUniforms>.stride, index: 0)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        sceneRenderer.encode(
            encoder: encoder,
            model: model,
            camera: effectiveCamera,
            simulatedBodyBuffer: obstacleBody,
            capture: false,
            aspect: Float(max(view.drawableSize.width, 1) / max(view.drawableSize.height, 1)))
        encoder.endEncoding()

        var captureTarget: CinematicCaptureTarget?
        if let activeCapture {
            do {
                let target = try activeCapture.makeTarget(device: device)
                guard let captureDepth = sceneRenderer.captureDepthTexture(
                    device: device, width: activeCapture.width, height: activeCapture.height) else {
                    throw CinematicCaptureError.metalTextureUnavailable
                }
                let capturePass = MTLRenderPassDescriptor()
                capturePass.colorAttachments[0].texture = target.texture
                capturePass.colorAttachments[0].loadAction = .clear
                capturePass.colorAttachments[0].storeAction = .store
                capturePass.colorAttachments[0].clearColor = MTLClearColor(red: 0.01, green: 0.02, blue: 0.06, alpha: 1)
                capturePass.depthAttachment.texture = captureDepth
                capturePass.depthAttachment.loadAction = .clear
                capturePass.depthAttachment.storeAction = .dontCare
                capturePass.depthAttachment.clearDepth = 1.0
                guard let captureEncoder = command.makeRenderCommandEncoder(descriptor: capturePass) else { return }
                captureEncoder.setRenderPipelineState(captureDisplayPipeline)
                captureEncoder.setFragmentTexture(displayRadiance, index: 0)
                captureEncoder.setFragmentBytes(&copy, length: MemoryLayout<WaveUniforms>.stride, index: 0)
                captureEncoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
                sceneRenderer.encode(
                    encoder: captureEncoder,
                    model: model,
                    camera: effectiveCamera,
                    simulatedBodyBuffer: obstacleBody,
                    capture: true,
                    aspect: Float(activeCapture.width) / Float(activeCapture.height))
                captureEncoder.endEncoding()
                captureTarget = target
            } catch {
                let failedSession = captureSession
                captureSession = nil
                activeCaptureCamera = nil
                failedSession?.finish { _ in }
                DispatchQueue.main.async { [weak model] in
                    model?.reportCaptureState(active: false, message: "Capture failed: \(error.localizedDescription)")
                }
            }
        }
        let submittedFrame = frameRequest
'''
text = replace_once(text, old_present, new_present, 'presentation and capture render')

old_handler_tail = '''            self?.latestTelemetry = telemetry
            DispatchQueue.main.async { model?.reportFrame(telemetry) }
            inFlightFrames.signal()
        }
        command.present(drawable)
        command.commit()
        submitted = true
        if model.playing { DispatchQueue.main.async { model.time += delta } }
'''
new_handler_tail = '''            self?.latestTelemetry = telemetry
            if completed.status == .completed,
               let target = captureTarget,
               let capture = activeCapture {
                do {
                    let finished = try capture.append(target)
                    if finished {
                        capture.finish { [weak self, weak model] result in
                            self?.captureSession = nil
                            self?.activeCaptureCamera = nil
                            DispatchQueue.main.async {
                                switch result {
                                case let .success(url):
                                    model?.reportCaptureState(
                                        active: false,
                                        message: "Saved cinematic capture · \(url.lastPathComponent)")
                                case let .failure(error):
                                    model?.reportCaptureState(
                                        active: false,
                                        message: "Capture finalize failed: \(error.localizedDescription)")
                                }
                            }
                        }
                    }
                } catch {
                    let failedSession = self?.captureSession
                    self?.captureSession = nil
                    self?.activeCaptureCamera = nil
                    failedSession?.finish { _ in }
                    DispatchQueue.main.async {
                        model?.reportCaptureState(active: false, message: "Capture encode failed: \(error.localizedDescription)")
                    }
                }
            }
            DispatchQueue.main.async { model?.reportFrame(telemetry) }
            inFlightFrames.signal()
        }
        command.present(drawable)
        command.commit()
        submitted = true
        if captureTarget != nil {
            // Offline capture is deliberately serialized so AVAssetWriter sees
            // exact frame ordering and each simulation step is exactly 1/fps.
            command.waitUntilCompleted()
            DispatchQueue.main.async { model.time = simulationTime + delta }
        } else if model.playing {
            DispatchQueue.main.async { model.time += delta }
        }
'''
text = replace_once(text, old_handler_tail, new_handler_tail, 'capture completion')

text = replace_once(text, '''        view.physicsModel = model
        view.colorPixelFormat = .bgra8Unorm_srgb
''', '''        view.physicsModel = model
        view.colorPixelFormat = .bgra8Unorm_srgb
        view.depthStencilPixelFormat = .depth32Float
''', 'metal view depth')
text = replace_once(text, '''        if CommandLine.arguments.contains("--native-all-formulas-gpu-smoke") {
            exit(runAllFormulaPresetsGpuSmoke() ? EXIT_SUCCESS : EXIT_FAILURE)
        }
''', '''        if CommandLine.arguments.contains("--native-all-formulas-gpu-smoke") {
            exit(runAllFormulaPresetsGpuSmoke() ? EXIT_SUCCESS : EXIT_FAILURE)
        }
        if CommandLine.arguments.contains("--native-cinematic-capture-smoke") {
            exit(runCinematicCaptureWriterSmoke() ? EXIT_SUCCESS : EXIT_FAILURE)
        }
''', 'capture smoke cli')
main_path.write_text(text)

Path('scripts/phase11_wire_capture.py').unlink()
