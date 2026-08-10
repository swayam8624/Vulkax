import AVFoundation
import CoreVideo
import Foundation
import Metal

struct CinematicCaptureTarget {
    let pixelBuffer: CVPixelBuffer
    let texture: MTLTexture
    let frameIndex: Int
}

struct CinematicCaptureRequest {
    let revision: UInt64
    let outputURL: URL
    let settings: CinematicCaptureSettings
    let camera: StudioCamera
    let cameraTrack: StudioCameraTrack
    let timelineSeconds: Float
}

enum CinematicCaptureError: LocalizedError {
    case writerCreation(String)
    case writerInputRejected
    case pixelBufferPoolUnavailable
    case pixelBufferAllocation(CVReturn)
    case metalTextureCache(CVReturn)
    case metalTextureUnavailable
    case encoderBackpressure
    case appendFailed(String)

    var errorDescription: String? {
        switch self {
        case let .writerCreation(message): return "Could not create movie writer: \(message)"
        case .writerInputRejected: return "Movie writer rejected the video input"
        case .pixelBufferPoolUnavailable: return "Movie pixel-buffer pool is unavailable"
        case let .pixelBufferAllocation(status): return "Could not allocate capture pixel buffer (\(status))"
        case let .metalTextureCache(status): return "Could not create Metal capture texture (\(status))"
        case .metalTextureUnavailable: return "Capture pixel buffer did not expose a Metal texture"
        case .encoderBackpressure: return "The movie encoder could not keep up with deterministic capture"
        case let .appendFailed(message): return "Could not append movie frame: \(message)"
        }
    }
}

final class CinematicCaptureSession {
    let outputURL: URL
    let settings: CinematicCaptureSettings
    let width: Int
    let height: Int
    let frameRate: Int
    let targetFrameCount: Int

    private let writer: AVAssetWriter
    private let input: AVAssetWriterInput
    private let adaptor: AVAssetWriterInputPixelBufferAdaptor
    private var textureCache: CVMetalTextureCache?
    private let lock = NSLock()
    private var appendedFrames = 0
    private var finishing = false
    private var completed = false

    init(outputURL: URL, settings: CinematicCaptureSettings, device: MTLDevice) throws {
        self.outputURL = outputURL
        self.settings = settings
        let dimensions = settings.resolution.dimensions
        width = dimensions.width
        height = dimensions.height
        frameRate = settings.frameRate.rawValue
        targetFrameCount = max(1, Int((settings.durationSeconds * Double(frameRate)).rounded()))

        try? FileManager.default.removeItem(at: outputURL)
        do {
            writer = try AVAssetWriter(outputURL: outputURL, fileType: .mov)
        } catch {
            throw CinematicCaptureError.writerCreation(error.localizedDescription)
        }

        let bitsPerSecond = settings.resolution == .fourK ? 55_000_000 : 18_000_000
        let videoSettings: [String: Any] = [
            AVVideoCodecKey: AVVideoCodecType.hevc,
            AVVideoWidthKey: width,
            AVVideoHeightKey: height,
            AVVideoCompressionPropertiesKey: [
                AVVideoAverageBitRateKey: bitsPerSecond,
                AVVideoExpectedSourceFrameRateKey: frameRate,
                AVVideoMaxKeyFrameIntervalKey: frameRate * 2
            ]
        ]
        input = AVAssetWriterInput(mediaType: .video, outputSettings: videoSettings)
        input.expectsMediaDataInRealTime = false
        guard writer.canAdd(input) else { throw CinematicCaptureError.writerInputRejected }
        writer.add(input)

        adaptor = AVAssetWriterInputPixelBufferAdaptor(
            assetWriterInput: input,
            sourcePixelBufferAttributes: [
                kCVPixelBufferPixelFormatTypeKey as String: Int(kCVPixelFormatType_32BGRA),
                kCVPixelBufferWidthKey as String: width,
                kCVPixelBufferHeightKey as String: height,
                kCVPixelBufferMetalCompatibilityKey as String: true,
                kCVPixelBufferIOSurfacePropertiesKey as String: [:]
            ])

        var cache: CVMetalTextureCache?
        let cacheStatus = CVMetalTextureCacheCreate(kCFAllocatorDefault, nil, device, nil, &cache)
        guard cacheStatus == kCVReturnSuccess else {
            throw CinematicCaptureError.metalTextureCache(cacheStatus)
        }
        textureCache = cache

        guard writer.startWriting() else {
            throw CinematicCaptureError.writerCreation(writer.error?.localizedDescription ?? "startWriting failed")
        }
        writer.startSession(atSourceTime: .zero)
    }

    var shouldEncodeMoreFrames: Bool {
        lock.lock(); defer { lock.unlock() }
        return !finishing && !completed && appendedFrames < targetFrameCount
    }

    var frameCount: Int {
        lock.lock(); defer { lock.unlock() }
        return appendedFrames
    }

    func makeTarget(device: MTLDevice) throws -> CinematicCaptureTarget {
        lock.lock()
        let index = appendedFrames
        let canAllocate = !finishing && !completed && index < targetFrameCount
        lock.unlock()
        guard canAllocate else { throw CinematicCaptureError.appendFailed("capture already complete") }
        guard let pool = adaptor.pixelBufferPool else { throw CinematicCaptureError.pixelBufferPoolUnavailable }

        var buffer: CVPixelBuffer?
        let allocation = CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, pool, &buffer)
        guard allocation == kCVReturnSuccess, let buffer else {
            throw CinematicCaptureError.pixelBufferAllocation(allocation)
        }
        guard let textureCache else { throw CinematicCaptureError.metalTextureUnavailable }
        var wrappedTexture: CVMetalTexture?
        let textureStatus = CVMetalTextureCacheCreateTextureFromImage(
            kCFAllocatorDefault, textureCache, buffer, nil, .bgra8Unorm,
            width, height, 0, &wrappedTexture)
        guard textureStatus == kCVReturnSuccess else {
            throw CinematicCaptureError.metalTextureCache(textureStatus)
        }
        guard let wrappedTexture, let texture = CVMetalTextureGetTexture(wrappedTexture) else {
            throw CinematicCaptureError.metalTextureUnavailable
        }
        texture.label = "Vulkax cinematic frame \(index)"
        return .init(pixelBuffer: buffer, texture: texture, frameIndex: index)
    }

    func append(_ target: CinematicCaptureTarget) throws -> Bool {
        lock.lock()
        defer { lock.unlock() }
        guard !finishing, !completed else { return true }
        guard target.frameIndex == appendedFrames else {
            throw CinematicCaptureError.appendFailed(
                "out-of-order frame \(target.frameIndex), expected \(appendedFrames)")
        }
        guard input.isReadyForMoreMediaData else { throw CinematicCaptureError.encoderBackpressure }
        let presentationTime = CMTime(
            value: CMTimeValue(target.frameIndex), timescale: CMTimeScale(frameRate))
        guard adaptor.append(target.pixelBuffer, withPresentationTime: presentationTime) else {
            throw CinematicCaptureError.appendFailed(
                writer.error?.localizedDescription ?? "AVAssetWriter append returned false")
        }
        appendedFrames += 1
        return appendedFrames >= targetFrameCount
    }

    func finish(completion: @escaping (Result<URL, Error>) -> Void) {
        lock.lock()
        guard !finishing, !completed else { lock.unlock(); return }
        finishing = true
        input.markAsFinished()
        lock.unlock()
        writer.finishWriting { [weak self] in
            guard let self else { return }
            self.lock.lock()
            self.completed = true
            self.finishing = false
            let error = self.writer.error
            self.lock.unlock()
            if let error { completion(.failure(error)) }
            else { completion(.success(self.outputURL)) }
        }
    }
}

func runCinematicCaptureWriterSmoke() -> Bool {
    guard let device = MTLCreateSystemDefaultDevice() else {
        FileHandle.standardError.write(Data("No Metal device for cinematic capture smoke\n".utf8))
        return false
    }
    let url = FileManager.default.temporaryDirectory
        .appendingPathComponent("vulkax-4k-capture-smoke-\(UUID().uuidString).mov")
    var settings = CinematicCaptureSettings()
    settings.resolution = .fourK
    settings.frameRate = .fps24
    settings.durationSeconds = 2.0 / 24.0
    do {
        let capture = try CinematicCaptureSession(outputURL: url, settings: settings, device: device)
        for _ in 0..<2 {
            let target = try capture.makeTarget(device: device)
            CVPixelBufferLockBaseAddress(target.pixelBuffer, [])
            if let base = CVPixelBufferGetBaseAddress(target.pixelBuffer) {
                memset(base, 0, CVPixelBufferGetBytesPerRow(target.pixelBuffer) * 2160)
            }
            CVPixelBufferUnlockBaseAddress(target.pixelBuffer, [])
            _ = try capture.append(target)
        }
        let semaphore = DispatchSemaphore(value: 0)
        var finishError: Error?
        capture.finish { result in
            if case let .failure(error) = result { finishError = error }
            semaphore.signal()
        }
        guard semaphore.wait(timeout: .now() + 30) == .success, finishError == nil else {
            throw finishError ?? CinematicCaptureError.appendFailed("writer finish timeout")
        }
        let asset = AVAsset(url: url)
        guard let track = asset.tracks(withMediaType: .video).first else {
            throw CinematicCaptureError.appendFailed("movie contains no video track")
        }
        let size = track.naturalSize
        guard Int(abs(size.width)) == 3840, Int(abs(size.height)) == 2160 else {
            throw CinematicCaptureError.appendFailed(
                "expected 3840x2160, got \(size.width)x\(size.height)")
        }
        try? FileManager.default.removeItem(at: url)
        print("Vulkax cinematic capture smoke passed: 3840x2160 HEVC .mov")
        return true
    } catch {
        try? FileManager.default.removeItem(at: url)
        FileHandle.standardError.write(Data("Vulkax cinematic capture smoke failed: \(error)\n".utf8))
        return false
    }
}
