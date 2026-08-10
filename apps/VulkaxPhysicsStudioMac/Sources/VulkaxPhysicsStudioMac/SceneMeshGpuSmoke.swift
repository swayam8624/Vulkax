import Foundation
import Metal

func runStudioSceneMeshRendererSmoke(path: String) -> Bool {
    guard let device = MTLCreateSystemDefaultDevice(),
          let queue = device.makeCommandQueue() else {
        FileHandle.standardError.write(Data("No Metal device for scene mesh smoke\n".utf8))
        return false
    }
    do {
        let model = PhysicsModel()
        model.importObstacleMesh(from: URL(fileURLWithPath: path))
        model.selectedObstacleRole = .visual
        model.selectedCollisionProxy = .none
        let renderer = try StudioSceneMeshRenderer(
            device: device, interactiveColorFormat: .bgra8Unorm, depthFormat: .depth32Float)
        renderer.rebuildIfNeeded(device: device, model: model)

        let colorDescriptor = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .bgra8Unorm, width: 160, height: 90, mipmapped: false)
        colorDescriptor.usage = [.renderTarget, .shaderRead]
        colorDescriptor.storageMode = .private
        let depthDescriptor = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .depth32Float, width: 160, height: 90, mipmapped: false)
        depthDescriptor.usage = [.renderTarget]
        depthDescriptor.storageMode = .private
        guard let color = device.makeTexture(descriptor: colorDescriptor),
              let depth = device.makeTexture(descriptor: depthDescriptor),
              let command = queue.makeCommandBuffer() else { return false }
        let pass = MTLRenderPassDescriptor()
        pass.colorAttachments[0].texture = color
        pass.colorAttachments[0].loadAction = .clear
        pass.colorAttachments[0].storeAction = .store
        pass.colorAttachments[0].clearColor = MTLClearColor(red: 0.01, green: 0.02, blue: 0.04, alpha: 1)
        pass.depthAttachment.texture = depth
        pass.depthAttachment.loadAction = .clear
        pass.depthAttachment.storeAction = .dontCare
        pass.depthAttachment.clearDepth = 1
        guard let encoder = command.makeRenderCommandEncoder(descriptor: pass) else { return false }
        renderer.encode(
            encoder: encoder,
            model: model,
            camera: .default,
            simulatedBodyBuffer: nil,
            capture: true,
            aspect: 160.0 / 90.0)
        encoder.endEncoding()
        command.commit()
        command.waitUntilCompleted()
        guard command.status == .completed else {
            throw command.error ?? NSError(domain: "VulkaxSceneMeshSmoke", code: 1)
        }
        print("Vulkax scene mesh Metal smoke passed: \(model.selectedObstacleName)")
        return true
    } catch {
        FileHandle.standardError.write(Data("Vulkax scene mesh Metal smoke failed: \(error)\n".utf8))
        return false
    }
}
