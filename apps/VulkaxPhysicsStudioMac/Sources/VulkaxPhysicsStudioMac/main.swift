import SwiftUI
import MetalKit
import ObjectiveC

final class PhysicsModel: ObservableObject {
    @Published var amplitude: Float = 1.0
    @Published var wavenumber: Float = 2.0
    @Published var angularFrequency: Float = 3.0
    @Published var playing = true
    @Published var time: Float = 0.0
}

struct WaveUniforms {
    var time: Float
    var amplitude: Float
    var wavenumber: Float
    var angularFrequency: Float
    var width: Float
    var height: Float
    var padding: SIMD2<Float> = .zero
}

private var rendererAssociationKey: UInt8 = 0

private let waveShader = """
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float time;
    float amplitude;
    float wavenumber;
    float angularFrequency;
    float width;
    float height;
    float2 padding;
};

struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

vertex VertexOut fullscreen(uint id [[vertex_id]]) {
    constexpr float2 positions[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    VertexOut out;
    out.position = float4(positions[id], 0.0, 1.0);
    out.uv = positions[id] * 0.5 + 0.5;
    return out;
}

float3 palette(float value) {
    float shadow = pow(clamp(value, 0.0, 1.0), 0.72);
    return float3(
        0.025 + 0.91 * pow(shadow, 1.55),
        0.055 + 0.54 * sin(shadow * 1.47),
        0.13 + 0.70 * (1.0 - shadow) * (1.0 - shadow));
}

fragment float4 wave(VertexOut in [[stage_in]], constant Uniforms& u [[buffer(0)]]) {
    float aspect = u.width / max(1.0, u.height);
    float2 p = (in.uv - 0.5) * float2(8.0 * aspect, 8.0);
    float phase = u.wavenumber * p.x - u.angularFrequency * u.time;
    float radial = 0.18 * sin(1.35 * length(p) - 0.7 * u.time);
    float field = u.amplitude * (sin(phase) + radial);
    float3 radiance = palette(0.5 + 0.5 * tanh(field));
    // A small high-frequency dither removes flat 8-bit display bands after the
    // CAMetalLayer's final conversion without affecting the simulation field.
    float dither = fract(sin(dot(in.uv * u.width, float2(12.9898, 78.233))) * 43758.5453) - 0.5;
    return float4(max(radiance + dither / 255.0, 0.0), 1.0);
}
"""

final class MetalWaveRenderer: NSObject, MTKViewDelegate {
    private weak var model: PhysicsModel?
    private let commandQueue: MTLCommandQueue
    private let pipeline: MTLRenderPipelineState
    private var lastTime = CACurrentMediaTime()

    init?(view: MTKView, model: PhysicsModel) {
        guard let device = MTLCreateSystemDefaultDevice(), let queue = device.makeCommandQueue() else { return nil }
        self.model = model
        self.commandQueue = queue
        do {
            let library = try device.makeLibrary(source: waveShader, options: nil)
            let descriptor = MTLRenderPipelineDescriptor()
            descriptor.vertexFunction = library.makeFunction(name: "fullscreen")
            descriptor.fragmentFunction = library.makeFunction(name: "wave")
            descriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat
            descriptor.colorAttachments[0].isBlendingEnabled = false
            self.pipeline = try device.makeRenderPipelineState(descriptor: descriptor)
        } catch {
            return nil
        }
        super.init()
        view.device = device
        view.delegate = self
        view.framebufferOnly = true
        view.enableSetNeedsDisplay = false
        view.isPaused = false
        view.preferredFramesPerSecond = 60
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    func draw(in view: MTKView) {
        guard let model, let drawable = view.currentDrawable, let pass = view.currentRenderPassDescriptor,
              let command = commandQueue.makeCommandBuffer(), let encoder = command.makeRenderCommandEncoder(descriptor: pass) else { return }
        let now = CACurrentMediaTime()
        let delta = min(Float(now - lastTime), 1.0 / 20.0)
        lastTime = now
        if model.playing { DispatchQueue.main.async { model.time += delta } }
        let uniforms = WaveUniforms(time: model.time, amplitude: model.amplitude, wavenumber: model.wavenumber,
                                   angularFrequency: model.angularFrequency, width: Float(view.drawableSize.width),
                                   height: Float(view.drawableSize.height))
        encoder.setRenderPipelineState(pipeline)
        var copy = uniforms
        encoder.setFragmentBytes(&copy, length: MemoryLayout<WaveUniforms>.stride, index: 0)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        encoder.endEncoding()
        command.present(drawable)
        command.commit()
    }
}

struct MetalWaveView: NSViewRepresentable {
    @ObservedObject var model: PhysicsModel

    func makeNSView(context: Context) -> MTKView {
        let view = MTKView(frame: .zero, device: MTLCreateSystemDefaultDevice())
        view.colorPixelFormat = .bgra8Unorm_srgb
        view.clearColor = MTLClearColor(red: 0.01, green: 0.02, blue: 0.06, alpha: 1.0)
        let renderer = MetalWaveRenderer(view: view, model: model)
        view.delegate = renderer
        objc_setAssociatedObject(view, &rendererAssociationKey, renderer, .OBJC_ASSOCIATION_RETAIN_NONATOMIC)
        return view
    }

    func updateNSView(_ view: MTKView, context: Context) {}
}

struct ContentView: View {
    @StateObject private var model = PhysicsModel()

    var body: some View {
        HSplitView {
            VStack(alignment: .leading, spacing: 18) {
                Text("VULKAX").font(.system(size: 24, weight: .bold, design: .rounded)).foregroundStyle(.mint)
                Text("PHYSICS STUDIO").font(.caption.weight(.semibold)).foregroundStyle(.secondary)
                Divider()
                Text("DIRECT GPU VIEWPORT").font(.caption.weight(.bold)).foregroundStyle(.secondary)
                Label("CAMetalLayer presentation", systemImage: "display")
                Label("Metal fragment pipeline", systemImage: "cpu")
                Label("No CPU image bridge", systemImage: "bolt.fill")
                Spacer()
            }
            .frame(minWidth: 210, idealWidth: 230)
            .padding(20)
            .background(Color(red: 0.045, green: 0.07, blue: 0.12))

            VStack(spacing: 0) {
                MetalWaveView(model: model)
                    .overlay(alignment: .topLeading) {
                        Text(model.playing ? "LIVE GPU" : "PAUSED")
                            .font(.caption.bold()).foregroundStyle(model.playing ? .mint : .orange)
                            .padding(8).background(.black.opacity(0.45), in: RoundedRectangle(cornerRadius: 4))
                            .padding(14)
                    }
                HStack {
                    Button(model.playing ? "Pause" : "Play") { model.playing.toggle() }
                    Button("Reset") { model.time = 0 }
                    Spacer()
                    Text(String(format: "t = %.2f s", model.time)).monospacedDigit().foregroundStyle(.secondary)
                }.padding(14).background(.thinMaterial)
            }
            .frame(minWidth: 640, minHeight: 540)

            VStack(alignment: .leading, spacing: 18) {
                Text("WAVE FIELD").font(.headline)
                parameter("Amplitude", value: $model.amplitude, range: 0...4)
                parameter("Wavenumber", value: $model.wavenumber, range: 0.1...18)
                parameter("Frequency", value: $model.angularFrequency, range: 0.1...18)
                Spacer()
                Text("The displayed field is shaded directly into the Metal drawable at its backing resolution.")
                    .font(.caption).foregroundStyle(.secondary)
            }
            .frame(minWidth: 260, idealWidth: 300)
            .padding(20)
            .background(Color(red: 0.045, green: 0.07, blue: 0.12))
        }
        .frame(minWidth: 1120, minHeight: 720)
        .preferredColorScheme(.dark)
    }

    @ViewBuilder private func parameter(_ label: String, value: Binding<Float>, range: ClosedRange<Float>) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack { Text(label).font(.subheadline.weight(.medium)); Spacer(); Text(String(format: "%.2f", value.wrappedValue)).monospacedDigit().foregroundStyle(.secondary) }
            Slider(value: value, in: range)
        }
    }
}

@main struct VulkaxPhysicsStudioMacApp: App {
    var body: some Scene { WindowGroup("Vulkax Physics Studio") { ContentView() } }
}
