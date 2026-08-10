import AppKit
import MetalKit

final class StudioMetalView: MTKView {
    weak var physicsModel: PhysicsModel?
    private var lastDragLocation: NSPoint?

    override var acceptsFirstResponder: Bool { true }

    override func mouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        lastDragLocation = convert(event.locationInWindow, from: nil)
    }

    override func mouseDragged(with event: NSEvent) {
        let location = convert(event.locationInWindow, from: nil)
        guard let previous = lastDragLocation else {
            lastDragLocation = location
            return
        }
        let deltaX = Float(location.x - previous.x)
        let deltaY = Float(location.y - previous.y)
        lastDragLocation = location
        DispatchQueue.main.async { [weak self] in
            guard let model = self?.physicsModel else { return }
            if event.modifierFlags.contains(.shift) {
                model.camera.pan(deltaX: deltaX, deltaY: deltaY)
            } else {
                model.camera.orbit(deltaX: deltaX, deltaY: deltaY)
            }
            model.accumulationResetToken &+= 1
        }
    }

    override func mouseUp(with event: NSEvent) {
        lastDragLocation = nil
    }

    override func rightMouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        lastDragLocation = convert(event.locationInWindow, from: nil)
    }

    override func rightMouseDragged(with event: NSEvent) {
        let location = convert(event.locationInWindow, from: nil)
        guard let previous = lastDragLocation else {
            lastDragLocation = location
            return
        }
        let deltaX = Float(location.x - previous.x)
        let deltaY = Float(location.y - previous.y)
        lastDragLocation = location
        DispatchQueue.main.async { [weak self] in
            guard let model = self?.physicsModel else { return }
            model.camera.pan(deltaX: deltaX, deltaY: deltaY)
            model.accumulationResetToken &+= 1
        }
    }

    override func rightMouseUp(with event: NSEvent) {
        lastDragLocation = nil
    }

    override func scrollWheel(with event: NSEvent) {
        let amount = Float(event.scrollingDeltaY) * (event.hasPreciseScrollingDeltas ? 1.0 : 8.0)
        DispatchQueue.main.async { [weak self] in
            guard let model = self?.physicsModel else { return }
            model.camera.dolly(amount)
            model.accumulationResetToken &+= 1
        }
    }

    override func magnify(with event: NSEvent) {
        DispatchQueue.main.async { [weak self] in
            guard let model = self?.physicsModel else { return }
            model.camera.dolly(Float(-event.magnification * 260.0))
            model.accumulationResetToken &+= 1
        }
    }
}
