import Foundation
import Metal

final class MetalPipelineArtifactCache {
    static let shared = MetalPipelineArtifactCache()

    private var pipelines: [String: MTLComputePipelineState] = [:]
    private let lock = NSLock()

    func pipeline(device: MTLDevice, source: String, sourceHash: UInt64) throws -> MTLComputePipelineState {
        let key = "\(device.registryID)-\(String(sourceHash, radix: 16))"
        lock.lock()
        if let cached = pipelines[key] {
            lock.unlock()
            return cached
        }
        lock.unlock()

        let library = try device.makeLibrary(source: source, options: nil)
        guard let function = library.makeFunction(name: "renderCompiledEquation") else {
            throw NSError(domain: "VulkaxEquation", code: 1,
                          userInfo: [NSLocalizedDescriptionKey: "generated Metal entry point is missing"])
        }
        let descriptor = MTLComputePipelineDescriptor()
        descriptor.computeFunction = function
        descriptor.label = "Vulkax equation \(String(sourceHash, radix: 16))"

        if #available(macOS 11.0, *) {
            let directory = try cacheDirectory()
            let archiveURL = directory.appendingPathComponent(key).appendingPathExtension("metallibarchive")
            let archiveDescriptor = MTLBinaryArchiveDescriptor()
            archiveDescriptor.url = FileManager.default.fileExists(atPath: archiveURL.path) ? archiveURL : nil
            if let archive = try? device.makeBinaryArchive(descriptor: archiveDescriptor) {
                descriptor.binaryArchives = [archive]
                try? archive.addComputePipelineFunctions(descriptor: descriptor)
                let pipeline = try device.makeComputePipelineState(
                    descriptor: descriptor, options: [], reflection: nil)
                try? archive.serialize(to: archiveURL)
                lock.lock()
                pipelines[key] = pipeline
                lock.unlock()
                return pipeline
            }
        }

        let pipeline = try device.makeComputePipelineState(function: function)
        lock.lock()
        pipelines[key] = pipeline
        lock.unlock()
        return pipeline
    }

    private func cacheDirectory() throws -> URL {
        let root = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0]
        let directory = root.appendingPathComponent("Vulkax/Pipelines", isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        return directory
    }
}
