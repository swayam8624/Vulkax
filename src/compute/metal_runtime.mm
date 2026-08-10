#include "vulkax/compute/runtime.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <stdexcept>

namespace vulkax::compute {

ExecutionResult executeMetal(const ComputeProgram& program, std::vector<std::vector<float>> buffers) {
    ExecutionResult result;
    result.backend = backend::BackendKind::Metal;
    const auto start = std::chrono::steady_clock::now();
    @autoreleasepool {
        try {
            const auto validation = validateProgram(program);
            if (!validation.ok()) throw std::invalid_argument(validation.errors.front());
            if (buffers.size() != program.bufferCount) throw std::invalid_argument("buffer count mismatch");
            for (const auto& b : buffers) if (b.size() != program.elementCount) throw std::invalid_argument("buffer length mismatch");

            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            if (!device) throw std::runtime_error("MTLCreateSystemDefaultDevice returned nil");
            result.deviceName = std::string([[device name] UTF8String]);
            NSError* error = nil;
            NSString* source = [NSString stringWithUTF8String:metalInterpreterMsl()];
            id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
            if (!library) throw std::runtime_error(error ? std::string([[error localizedDescription] UTF8String]) : "Metal library compilation failed");
            id<MTLFunction> function = [library newFunctionWithName:@"vulkax_compute_ir"];
            if (!function) throw std::runtime_error("Metal interpreter function missing");
            id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:function error:&error];
            if (!pipeline) throw std::runtime_error(error ? std::string([[error localizedDescription] UTF8String]) : "Metal pipeline creation failed");
            id<MTLCommandQueue> queue = [device newCommandQueue];
            if (!queue) throw std::runtime_error("Metal command queue creation failed");

            const NSUInteger dataBytes = static_cast<NSUInteger>(program.elementCount * sizeof(float));
            id<MTLBuffer> gpuBuffers[4] = {nil, nil, nil, nil};
            for (std::uint32_t i = 0; i < 4; ++i) {
                gpuBuffers[i] = [device newBufferWithLength:dataBytes options:MTLResourceStorageModeShared];
                if (!gpuBuffers[i]) throw std::runtime_error("Metal data buffer allocation failed");
                std::memset([gpuBuffers[i] contents], 0, dataBytes);
                if (i < buffers.size()) std::memcpy([gpuBuffers[i] contents], buffers[i].data(), dataBytes);
            }
            const auto encoded = encodeGpuInstructions(program);
            id<MTLBuffer> ops = [device newBufferWithBytes:encoded.data()
                                                    length:encoded.size() * sizeof(GpuInstruction)
                                                   options:MTLResourceStorageModeShared];
            if (!ops) throw std::runtime_error("Metal op buffer allocation failed");
            id<MTLCommandBuffer> command = [queue commandBuffer];
            if (!command) throw std::runtime_error("Metal command buffer creation failed");
            struct Push { std::uint32_t count; std::uint32_t opIndex; };
            for (std::uint32_t op = 0; op < program.instructions.size(); ++op) {
                id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
                if (!encoder) throw std::runtime_error("Metal compute encoder creation failed");
                [encoder setComputePipelineState:pipeline];
                for (NSUInteger i = 0; i < 4; ++i) [encoder setBuffer:gpuBuffers[i] offset:0 atIndex:i];
                [encoder setBuffer:ops offset:0 atIndex:4];
                const Push push{program.elementCount, op};
                [encoder setBytes:&push length:sizeof(push) atIndex:5];
                const NSUInteger width = std::min<NSUInteger>(64, [pipeline maxTotalThreadsPerThreadgroup]);
                [encoder dispatchThreads:MTLSizeMake(program.elementCount, 1, 1)
                    threadsPerThreadgroup:MTLSizeMake(width, 1, 1)];
                [encoder endEncoding];
            }
            [command commit];
            [command waitUntilCompleted];
            if ([command status] == MTLCommandBufferStatusError) {
                NSError* commandError = [command error];
                throw std::runtime_error(commandError ? std::string([[commandError localizedDescription] UTF8String]) : "Metal command buffer failed");
            }
            result.buffers.resize(program.bufferCount);
            for (std::uint32_t i = 0; i < program.bufferCount; ++i) {
                result.buffers[i].resize(program.elementCount);
                std::memcpy(result.buffers[i].data(), [gpuBuffers[i] contents], dataBytes);
            }
            result.ok = true;
        } catch (const std::exception& e) {
            result.diagnostic = e.what();
        }
    }
    const auto end = std::chrono::steady_clock::now();
    result.wallMilliseconds = std::chrono::duration<double, std::milli>(end - start).count();
    return result;
}

} // namespace vulkax::compute
