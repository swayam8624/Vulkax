import AppKit
import MetalKit

final class StudioMetalView: MTKView {
    weak var physicsModel: PhysicsModel?
    private var lastDragLocation: NSPoint?

    override var acceptsFirstResponder: Bool { true }

    override func mouseDown(with event: NSEvent) { window?.makeFirstResponder(self); lastDragLocation = convert(event.locationInWindow, from: nil) }
    override func mouseUp(with event: NSEvent) { lastDragLocation = nil }
    override func rightMouseDown(with event: NSEvent) { window?.makeFirstResponder(self); lastDragLocation = convert(event.locationInWindow, from: nil) }
    override func rightMouseUp(with event: NSEvent) { lastDragLocation = nil }

    override func mouseDragged(with event: NSEvent) {
        drag(event, alwaysPan: false)
    }

    override func rightMouseDragged(with event: NSEvent) {
        drag(event, alwaysPan: true)
    }

    private func drag(_ event: NSEvent, alwaysPan: Bool) {
        let location = convert(event.locationInWindow, from: nil)
        guard let previous = lastDragLocation else { lastDragLocation = location; return }
        let dx = Float(location.x - previous.x)
        let dy = Float(location.y - previous.y)
        lastDragLocation = location
        DispatchQueue.main.async { [weak self] in
            guard let model = self?.physicsModel else { return }
            if alwaysPan || event.modifierFlags.contains(.shift) { model.camera.pan(deltaX: dx, deltaY: dy) }
            else { model.camera.orbit(deltaX: dx, deltaY: dy) }
            model.accumulationResetToken &+= 1
        }
    }

    override func scrollWheel(with event: NSEvent) {
        let multiplier: Float = event.hasPreciseScrollingDeltas ? 1 : 8
        let amount = Float(event.scrollingDeltaY) * multiplier
        DispatchQueue.main.async { [weak self] in
            guard let model = self?.physicsModel else { return }
            model.camera.dolly(amount)
            model.accumulationResetToken &+= 1
        }
    }

    override func magnify(with event: NSEvent) {
        DispatchQueue.main.async { [weak self] in
            guard let model = self?.physicsModel else { return }
            model.camera.dolly(Float(-event.magnification * 260))
            model.accumulationResetToken &+= 1
        }
    }
}
