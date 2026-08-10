import Foundation
import simd

struct StudioCameraKeyframe: Identifiable, Codable, Equatable {
    var id = UUID()
    var timeSeconds: Float
    var camera: StudioCamera
}

struct StudioCameraTrack: Codable, Equatable {
    var keyframes: [StudioCameraKeyframe] = []

    var isEmpty: Bool { keyframes.isEmpty }

    mutating func insert(timeSeconds: Float, camera: StudioCamera) {
        let time = max(0, timeSeconds)
        if let index = keyframes.firstIndex(where: { abs($0.timeSeconds - time) < 1e-4 }) {
            keyframes[index].camera = camera
        } else {
            keyframes.append(.init(timeSeconds: time, camera: camera))
        }
        keyframes.sort { $0.timeSeconds < $1.timeSeconds }
    }

    mutating func remove(id: UUID) {
        keyframes.removeAll { $0.id == id }
    }

    func camera(at timeSeconds: Float, fallback: StudioCamera) -> StudioCamera {
        guard !keyframes.isEmpty else { return fallback }
        if timeSeconds <= keyframes[0].timeSeconds { return keyframes[0].camera }
        if timeSeconds >= keyframes[keyframes.count - 1].timeSeconds { return keyframes[keyframes.count - 1].camera }
        guard let upperIndex = keyframes.firstIndex(where: { $0.timeSeconds >= timeSeconds }), upperIndex > 0 else {
            return fallback
        }
        let lower = keyframes[upperIndex - 1]
        let upper = keyframes[upperIndex]
        let span = max(upper.timeSeconds - lower.timeSeconds, 1e-6)
        let linear = max(0, min(1, (timeSeconds - lower.timeSeconds) / span))
        // Smoothstep avoids visible velocity discontinuities at camera keys while
        // preserving deterministic interpolation for offline capture.
        let t = linear * linear * (3 - 2 * linear)
        var result = StudioCamera(
            position: simd_mix(lower.camera.position, upper.camera.position, SIMD3<Float>(repeating: t)),
            target: simd_mix(lower.camera.target, upper.camera.target, SIMD3<Float>(repeating: t)),
            up: simd_mix(lower.camera.up, upper.camera.up, SIMD3<Float>(repeating: t)),
            verticalFovDegrees: lower.camera.verticalFovDegrees +
                (upper.camera.verticalFovDegrees - lower.camera.verticalFovDegrees) * t,
            exposure: lower.camera.exposure + (upper.camera.exposure - lower.camera.exposure) * t)
        result.sanitize()
        return result
    }
}

func runCameraTrackSmoke() -> Bool {
    var track = StudioCameraTrack()
    var start = StudioCamera.default
    start.position = SIMD3(0, 0, 4)
    var end = StudioCamera.default
    end.position = SIMD3(4, 2, 0)
    end.target = SIMD3(0, 0.4, 0)
    end.verticalFovDegrees = 60
    end.exposure = 2
    track.insert(timeSeconds: 0, camera: start)
    track.insert(timeSeconds: 10, camera: end)
    let midpoint = track.camera(at: 5, fallback: .default)
    guard simd_distance(midpoint.position, SIMD3(2, 1, 2)) < 1e-5,
          abs(midpoint.verticalFovDegrees - 53.47) < 1e-3,
          abs(midpoint.exposure - 1.5) < 1e-5 else {
        return false
    }
    let encoded = try? JSONEncoder().encode(track)
    guard let encoded,
          let decoded = try? JSONDecoder().decode(StudioCameraTrack.self, from: encoded),
          decoded.keyframes.count == 2 else { return false }
    return true
}
