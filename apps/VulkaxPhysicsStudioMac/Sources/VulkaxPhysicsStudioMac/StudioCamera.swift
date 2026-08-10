import Foundation
import simd

enum StudioCameraPreset: String, CaseIterable, Identifiable {
    case perspective
    case front
    case top
    case isometric
    case closeUp

    var id: String { rawValue }
    var title: String {
        switch self {
        case .perspective: return "Perspective"
        case .front: return "Front"
        case .top: return "Top"
        case .isometric: return "Isometric"
        case .closeUp: return "Close-up"
        }
    }
}

struct StudioCamera: Equatable, Codable {
    var position = SIMD3<Float>(0.0, 0.10, 3.10)
    var target = SIMD3<Float>(0.0, 0.10, 0.0)
    var up = SIMD3<Float>(0.0, 1.0, 0.0)
    var verticalFovDegrees: Float = 46.94
    var exposure: Float = 1.0

    static let `default` = StudioCamera()

    mutating func apply(_ preset: StudioCameraPreset) {
        switch preset {
        case .perspective: self = .default
        case .front:
            position = SIMD3(0.0, 0.20, 3.5); target = SIMD3(0.0, 0.20, 0.0); up = SIMD3(0, 1, 0); verticalFovDegrees = 42
        case .top:
            position = SIMD3(0.0, 3.5, 0.001); target = SIMD3(0.0, 0.20, 0.0); up = SIMD3(0, 0, -1); verticalFovDegrees = 44
        case .isometric:
            position = SIMD3(2.65, 2.10, 2.85); target = SIMD3(0.0, 0.25, 0.0); up = SIMD3(0, 1, 0); verticalFovDegrees = 48
        case .closeUp:
            position = SIMD3(1.10, 0.70, 1.40); target = SIMD3(0.0, 0.28, 0.0); up = SIMD3(0, 1, 0); verticalFovDegrees = 52
        }
        sanitize()
    }

    mutating func orbit(deltaX: Float, deltaY: Float) {
        let offset = position - target
        let radius = max(simd_length(offset), 0.05)
        var yaw = atan2(offset.x, offset.z)
        var pitch = asin(max(-0.98, min(0.98, offset.y / radius)))
        yaw -= deltaX * 0.006
        pitch = max(-1.45, min(1.45, pitch + deltaY * 0.006))
        let horizontal = cos(pitch) * radius
        position = target + SIMD3(sin(yaw) * horizontal, sin(pitch) * radius, cos(yaw) * horizontal)
        up = SIMD3(0, 1, 0)
        sanitize()
    }

    mutating func pan(deltaX: Float, deltaY: Float) {
        let forward = normalized(target - position, fallback: SIMD3(0, 0, -1))
        let right = normalized(simd_cross(forward, up), fallback: SIMD3(1, 0, 0))
        let cameraUp = normalized(simd_cross(right, forward), fallback: SIMD3(0, 1, 0))
        let distance = max(simd_length(target - position), 0.1)
        let scale = distance * 0.0018
        let movement = right * (-deltaX * scale) + cameraUp * (deltaY * scale)
        position += movement; target += movement; sanitize()
    }

    mutating func dolly(_ delta: Float) {
        let forward = normalized(target - position, fallback: SIMD3(0, 0, -1))
        let distance = max(simd_length(target - position), 0.05)
        let requested = distance * delta * 0.0018
        let bounded = max(-distance + 0.035, min(distance * 0.80, requested))
        position += forward * bounded; sanitize()
    }

    mutating func sanitize() {
        verticalFovDegrees = max(10, min(120, verticalFovDegrees))
        exposure = max(0.01, min(32, exposure))
        if !position.x.isFinite || !position.y.isFinite || !position.z.isFinite { position = Self.default.position }
        if !target.x.isFinite || !target.y.isFinite || !target.z.isFinite { target = Self.default.target }
        if simd_length_squared(target - position) < 1e-8 { target = position + SIMD3(0, 0, -1) }
        if !up.x.isFinite || !up.y.isFinite || !up.z.isFinite || simd_length_squared(up) < 1e-8 { up = SIMD3(0, 1, 0) }
        up = simd_normalize(up)
    }

    private func normalized(_ value: SIMD3<Float>, fallback: SIMD3<Float>) -> SIMD3<Float> {
        let lengthSquared = simd_length_squared(value)
        return lengthSquared > 1e-10 ? value / sqrt(lengthSquared) : fallback
    }
}

struct CinematicCaptureSettings: Equatable, Codable {
    enum Resolution: String, CaseIterable, Identifiable, Codable {
        case fullHD = "1920x1080"
        case fourK = "3840x2160"
        var id: String { rawValue }
        var title: String { self == .fourK ? "4K UHD" : "1080p" }
        var dimensions: (width: Int, height: Int) { self == .fourK ? (3840, 2160) : (1920, 1080) }
    }
    enum FrameRate: Int, CaseIterable, Identifiable, Codable {
        case fps24 = 24
        case fps30 = 30
        case fps60 = 60
        var id: Int { rawValue }
        var title: String { "\(rawValue) fps" }
    }
    var resolution: Resolution = .fourK
    var frameRate: FrameRate = .fps30
    var durationSeconds: Double = 10
    var includeHud = false
}
