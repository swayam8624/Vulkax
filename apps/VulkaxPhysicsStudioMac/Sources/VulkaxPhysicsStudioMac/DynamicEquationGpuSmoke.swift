import Foundation
import Metal
import VulkaxRuntimeContract

private func renderScalarPresetGpu(
    _ preset: ScalarPreset, device: MTLDevice, queue: MTLCommandQueue
) throws -> (minimum: Float, maximum: Float) {
    let compiled = try ScalarEquationCompiler.compile(preset.equation)
    let valuesByName = Dictionary(uniqueKeysWithValues: preset.parameters.map { ($0.name, $0.value) })
    let parameters = try compiled.parameterNames.map { name -> Float in
        guard let value = valuesByName[name] else {
            throw NSError(
                domain: "VulkaxFormulaPresets", code: 1,
                userInfo: [NSLocalizedDescriptionKey: "preset \(preset.id) does not define \(name)"])
        }
        return value
    }
    let library = try device.makeLibrary(source: compiled.metalSource, options: nil)
    guard let function = library.makeFunction(name: "renderCompiledEquation") else {
        throw NSError(
            domain: "VulkaxFormulaPresets", code: 2,
            userInfo: [NSLocalizedDescriptionKey: "preset \(preset.id) has no GPU entry point"])
    }
    let pipeline = try device.makeComputePipelineState(function: function)
    let width = 96
    let height = 64
    let descriptor = MTLTextureDescriptor.texture2DDescriptor(
        pixelFormat: .rgba16Float, width: width, height: height, mipmapped: false)
    descriptor.usage = [.shaderRead, .shaderWrite]
    descriptor.storageMode = .shared
    guard let output = device.makeTexture(descriptor: descriptor),
          let command = queue.makeCommandBuffer(),
          let encoder = command.makeComputeCommandEncoder() else {
        throw NSError(
            domain: "VulkaxFormulaPresets", code: 3,
            userInfo: [NSLocalizedDescriptionKey: "could not allocate GPU resources for \(preset.id)"])
    }
    var uniforms = WaveUniforms(
        time: 0.75, amplitude: 0, wavenumber: 0, angularFrequency: 0,
        width: Float(width), height: Float(height))
    encoder.setComputePipelineState(pipeline)
    encoder.setTexture(output, index: 0)
    encoder.setBytes(&uniforms, length: MemoryLayout<WaveUniforms>.stride, index: 0)
    parameters.withUnsafeBytes { bytes in
        encoder.setBytes(bytes.baseAddress!, length: bytes.count, index: 1)
    }
    encoder.dispatchThreadgroups(
        MTLSize(width: (width + 15) / 16, height: (height + 15) / 16, depth: 1),
        threadsPerThreadgroup: MTLSize(width: 16, height: 16, depth: 1))
    encoder.endEncoding()
    command.commit()
    command.waitUntilCompleted()
    guard command.status == .completed else {
        throw command.error ?? NSError(
            domain: "VulkaxFormulaPresets", code: 4,
            userInfo: [NSLocalizedDescriptionKey: "GPU dispatch failed for \(preset.id)"])
    }
    var halfPixels = [UInt16](repeating: 0, count: width * height * 4)
    halfPixels.withUnsafeMutableBytes {
        output.getBytes(
            $0.baseAddress!, bytesPerRow: width * 4 * MemoryLayout<UInt16>.stride,
            from: MTLRegionMake2D(0, 0, width, height), mipmapLevel: 0)
    }
    let red = stride(from: 0, to: halfPixels.count, by: 4).map {
        Float(Float16(bitPattern: halfPixels[$0]))
    }
    guard red.allSatisfy(\.isFinite), let minimum = red.min(), let maximum = red.max(),
          maximum > minimum + 0.01 else {
        throw NSError(
            domain: "VulkaxFormulaPresets", code: 5,
            userInfo: [NSLocalizedDescriptionKey: "preset \(preset.id) produced invalid or flat output"])
    }
    return (minimum, maximum)
}

func runAllFormulaPresetsGpuSmoke() -> Bool {
    guard let device = MTLCreateSystemDefaultDevice(), let queue = device.makeCommandQueue() else {
        FileHandle.standardError.write(Data("Vulkax formula preset smoke failed: no Metal device\n".utf8))
        return false
    }
    do {
        for preset in ScalarPreset.builtins {
            let range = try renderScalarPresetGpu(preset, device: device, queue: queue)
            print("\(preset.id): passed, radiance=[\(range.minimum), \(range.maximum)]")
        }
        print("Vulkax all formula presets GPU smoke passed: \(device.name)")
        return true
    } catch {
        FileHandle.standardError.write(Data("Vulkax formula preset smoke failed: \(error)\n".utf8))
        return false
    }
}

func runDynamicEquationProjectGpuSmoke() -> Bool {
    guard let device = MTLCreateSystemDefaultDevice(), let queue = device.makeCommandQueue() else {
        FileHandle.standardError.write(Data("Vulkax dynamic equation smoke failed: no Metal device\n".utf8))
        return false
    }
    do {
        let expression = "amplitude * sin(wavenumber * x - angular_frequency * t) + 0.25 * cos(y)"
        let compiled = try ScalarEquationCompiler.compile(expression)
        guard compiled.parameterNames == ["amplitude", "angular_frequency", "wavenumber"] else {
            throw NSError(
                domain: "VulkaxDynamicEquation", code: 1,
                userInfo: [NSLocalizedDescriptionKey: "parameter extraction order changed"])
        }
        let library = try device.makeLibrary(source: compiled.metalSource, options: nil)
        guard let function = library.makeFunction(name: "renderCompiledEquation") else {
            throw NSError(
                domain: "VulkaxDynamicEquation", code: 2,
                userInfo: [NSLocalizedDescriptionKey: "compiled equation entry point is missing"])
        }
        let pipeline = try device.makeComputePipelineState(function: function)
        let width = 96
        let height = 64
        let descriptor = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .rgba16Float, width: width, height: height, mipmapped: false)
        descriptor.usage = [.shaderRead, .shaderWrite]
        descriptor.storageMode = .shared
        guard let output = device.makeTexture(descriptor: descriptor),
              let command = queue.makeCommandBuffer(),
              let encoder = command.makeComputeCommandEncoder() else {
            throw NSError(
                domain: "VulkaxDynamicEquation", code: 3,
                userInfo: [NSLocalizedDescriptionKey: "could not allocate equation GPU resources"])
        }

        var uniforms = WaveUniforms(
            time: 0.75, amplitude: 0, wavenumber: 0, angularFrequency: 0,
            width: Float(width), height: Float(height))
        let parameters: [Float] = [1.4, 2.25, 1.75]
        var request = VulkaxFrameRequest()
        request.abiVersion = VULKAX_RUNTIME_ABI_VERSION
        request.visualization = UInt32(VULKAX_VISUALIZATION_SCALAR_FIELD.rawValue)
        request.drawableWidth = UInt32(width)
        request.drawableHeight = UInt32(height)
        request.frameIndex = 1
        request.timelineSeconds = uniforms.time
        request.deltaSeconds = 1.0 / 60.0
        request.renderScale = 1
        request.resetHistory = 1
        request.parameterCount = UInt32(parameters.count)
        request.parameterHash = compiled.sourceHash
        guard request.abiVersion == VULKAX_RUNTIME_ABI_VERSION,
              request.parameterCount == compiled.parameterNames.count else {
            throw NSError(
                domain: "VulkaxDynamicEquation", code: 4,
                userInfo: [NSLocalizedDescriptionKey: "runtime ABI request is inconsistent"])
        }

        encoder.setComputePipelineState(pipeline)
        encoder.setTexture(output, index: 0)
        encoder.setBytes(&uniforms, length: MemoryLayout<WaveUniforms>.stride, index: 0)
        parameters.withUnsafeBytes { bytes in
            encoder.setBytes(bytes.baseAddress!, length: bytes.count, index: 1)
        }
        encoder.dispatchThreadgroups(
            MTLSize(width: (width + 15) / 16, height: (height + 15) / 16, depth: 1),
            threadsPerThreadgroup: MTLSize(width: 16, height: 16, depth: 1))
        encoder.endEncoding()
        command.commit()
        command.waitUntilCompleted()
        guard command.status == .completed else {
            throw command.error ?? NSError(
                domain: "VulkaxDynamicEquation", code: 5,
                userInfo: [NSLocalizedDescriptionKey: "compiled equation dispatch failed"])
        }

        var halfPixels = [UInt16](repeating: 0, count: width * height * 4)
        halfPixels.withUnsafeMutableBytes {
            output.getBytes(
                $0.baseAddress!, bytesPerRow: width * 4 * MemoryLayout<UInt16>.stride,
                from: MTLRegionMake2D(0, 0, width, height), mipmapLevel: 0)
        }
        let values = halfPixels.map { Float(Float16(bitPattern: $0)) }
        let red = stride(from: 0, to: values.count, by: 4).map { values[$0] }
        guard red.allSatisfy(\.isFinite),
              let minimum = red.min(), let maximum = red.max(), maximum > minimum + 0.05 else {
            throw NSError(
                domain: "VulkaxDynamicEquation", code: 6,
                userInfo: [NSLocalizedDescriptionKey: "compiled equation produced invalid or flat radiance"])
        }

        let temporary = FileManager.default.temporaryDirectory
            .appendingPathComponent("vulkax-dynamic-equation-\(UUID().uuidString).vxp")
        defer { try? FileManager.default.removeItem(at: temporary) }
        var body = RigidObstacleConfiguration.default
        body.rotationDegrees = .init(x: 12, y: 34, z: 56)
        body.angularVelocity = .init(x: 0.5, y: -0.25, z: 1.0)
        let project = PhysicsProjectFile(
            name: "Dynamic GPU Equation",
            preset: "custom",
            visualization: "scalar-field",
            expression: expression,
            timelineSeconds: uniforms.time,
            parameters: Dictionary(uniqueKeysWithValues: zip(compiled.parameterNames, parameters)),
            graph: .builtIn(for: .wave, scalarEquation: expression),
            obstacleBody: body)
        try PhysicsProjectIO.save(project, to: temporary)
        let restored = try PhysicsProjectIO.load(from: temporary)
        guard restored.expression == expression,
              restored.parameters == project.parameters,
              restored.timelineSeconds == project.timelineSeconds,
              restored.obstacleBody == body else {
            throw NSError(
                domain: "VulkaxDynamicEquation", code: 7,
                userInfo: [NSLocalizedDescriptionKey: "project round trip changed live GPU inputs"])
        }
        let obstacleSource = temporary.deletingLastPathComponent()
            .appendingPathComponent("vulkax-obstacle-\(UUID().uuidString).obj")
        let assetDirectory = temporary.deletingPathExtension().appendingPathExtension("assets")
        defer {
            try? FileManager.default.removeItem(at: obstacleSource)
            try? FileManager.default.removeItem(at: assetDirectory)
        }
        try "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n".write(
            to: obstacleSource, atomically: true, encoding: .utf8)
        let packaged = try PhysicsProjectIO.packageObstacle(from: obstacleSource, for: temporary)
        let packagedURL = URL(
            fileURLWithPath: packaged, relativeTo: temporary.deletingLastPathComponent())
            .standardizedFileURL
        guard !packaged.hasPrefix("/"),
              FileManager.default.fileExists(atPath: packagedURL.path),
              try String(contentsOf: packagedURL, encoding: .utf8) ==
                  String(contentsOf: obstacleSource, encoding: .utf8) else {
            throw NSError(
                domain: "VulkaxDynamicEquation", code: 8,
                userInfo: [NSLocalizedDescriptionKey: "project obstacle was not packaged portably"])
        }
        print("Vulkax dynamic equation project GPU smoke passed: \(device.name) parameters=\(compiled.parameterNames.joined(separator: ",")) radiance=[\(minimum), \(maximum)]")
        return true
    } catch {
        FileHandle.standardError.write(Data("Vulkax dynamic equation smoke failed: \(error)\n".utf8))
        return false
    }
}
