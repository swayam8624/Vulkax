from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise SystemExit(f'missing marker: {label}')
    return text.replace(old, new, 1)

header = Path('src/vulkax/physics/compute_ir.hpp')
text = header.read_text()
marker = '[[nodiscard]] std::string emitScalarProgramMsl(const ScalarComputeProgram& program);\n'
addition = marker + '''// Emits the canonical scalar program into the native Physics Studio texture\n// contract. Swift never reparses or re-lowers equations; the native editor\n// consumes this source through the C bridge below.\n[[nodiscard]] std::string emitScalarProgramNativeMetalTexture(\n    const ScalarComputeProgram& program);\n'''
if 'emitScalarProgramNativeMetalTexture' not in text:
    text = replace_once(text, marker, addition, 'native Metal emitter declaration')
header.write_text(text)

cpp = Path('src/vulkax/physics/compute_ir.cpp')
text = cpp.read_text()
helper_marker = '''std::string instructionExpression(const ScalarInstruction& instruction,
                                  const std::vector<std::string>& parameters) {
'''
helper_end = '''}

}  // namespace

ComputeLoweringResult lowerScalarFieldProgram(
'''
if 'nativeMetalInstructionExpression' not in text:
    helper = '''std::string nativeMetalInstructionExpression(const ScalarInstruction& instruction) {
  const auto reg = [&](uint8_t index) {
    return "r" + std::to_string(instruction.operands[index]);
  };
  switch (instruction.opcode) {
    case ScalarOpcode::Constant: return "float(" + number(instruction.immediate) + ")";
    case ScalarOpcode::CoordinateX: return "x";
    case ScalarOpcode::CoordinateY: return "y";
    case ScalarOpcode::CoordinateZ: return "z";
    case ScalarOpcode::Time: return "t";
    case ScalarOpcode::Parameter:
      return "parameters[" + std::to_string(instruction.parameterIndex) + "]";
    case ScalarOpcode::Add: return "(" + reg(0) + " + " + reg(1) + ")";
    case ScalarOpcode::Subtract: return "(" + reg(0) + " - " + reg(1) + ")";
    case ScalarOpcode::Multiply: return "(" + reg(0) + " * " + reg(1) + ")";
    case ScalarOpcode::Divide: return "(" + reg(0) + " / " + reg(1) + ")";
    case ScalarOpcode::Negate: return "(-" + reg(0) + ")";
    default: {
      const char* name = functionName(instruction.opcode);
      if (name == nullptr) throw std::runtime_error("opcode cannot be emitted to native Metal");
      std::string expression = std::string{name} + "(";
      for (uint8_t index = 0; index < instruction.operandCount; ++index) {
        if (index != 0) expression += ", ";
        expression += reg(index);
      }
      return expression + ")";
    }
  }
}

'''
    if helper_end not in text:
        raise SystemExit('compute IR anonymous namespace marker missing')
    text = text.replace(helper_end, '}\n\n' + helper + '}  // namespace\n\nComputeLoweringResult lowerScalarFieldProgram(\n', 1)

namespace_end = '\n}  // namespace vulkax::physics\n'
if 'std::string emitScalarProgramNativeMetalTexture' not in text:
    function = r'''
std::string emitScalarProgramNativeMetalTexture(const ScalarComputeProgram& program) {
  std::ostringstream shader;
  shader << "#include <metal_stdlib>\nusing namespace metal;\n"
            "struct Uniforms {\n"
            "  float time; float amplitude; float wavenumber; float angularFrequency;\n"
            "  float width; float height; float4 padding; float4 control; float4 renderParameters;\n"
            "  float4 cameraPositionExposure; float4 cameraTarget; float4 cameraUpFov;\n"
            "};\n"
            "float3 equationPalette(float value) {\n"
            "  float shadow = pow(clamp(value, 0.0f, 1.0f), 0.72f);\n"
            "  return float3(0.025f + 0.91f * pow(shadow, 1.55f),\n"
            "                0.055f + 0.54f * sin(shadow * 1.47f),\n"
            "                0.13f + 0.70f * (1.0f - shadow) * (1.0f - shadow));\n"
            "}\n"
            "kernel void renderCompiledEquation(\n"
            "    texture2d<half, access::write> outputRadiance [[texture(0)]],\n"
            "    constant Uniforms& u [[buffer(0)]],\n"
            "    device const float* parameters [[buffer(1)]],\n"
            "    uint2 pixel [[thread_position_in_grid]]) {\n"
            "  if (pixel.x >= uint(u.width) || pixel.y >= uint(u.height)) return;\n"
            "  float2 uv = (float2(pixel) + 0.5f) / float2(u.width, u.height);\n"
            "  float aspect = u.width / max(1.0f, u.height);\n"
         << "  float x = mix(float(" << number(program.domain.minimum[0]) << ") * aspect, float("
         << number(program.domain.maximum[0]) << ") * aspect, uv.x);\n"
         << "  float y = mix(float(" << number(program.domain.maximum[1]) << "), float("
         << number(program.domain.minimum[1]) << "), uv.y);\n"
         << "  float z = float(" << number(0.5 * (program.domain.minimum[2] + program.domain.maximum[2])) << ");\n"
            "  float t = u.time;\n";
  for (size_t index = 0; index < program.instructions.size(); ++index) {
    shader << "  float r" << index << " = "
           << nativeMetalInstructionExpression(program.instructions[index]) << ";\n";
  }
  shader << "  float field = r" << program.outputRegister << ";\n"
            "  if (!isfinite(field)) field = 0.0f;\n"
            "  float3 radiance = equationPalette(0.5f + 0.5f * tanh(field));\n"
            "  outputRadiance.write(half4(half3(radiance * 2.25f), 1.0h), pixel);\n"
            "}\n"
         << "// canonical Physics IR hash: " << program.canonicalHash << "\n";
  return shader.str();
}
'''
    if namespace_end not in text:
        raise SystemExit('compute IR namespace close missing')
    text = text.replace(namespace_end, '\n' + function + namespace_end, 1)
cpp.write_text(text)

package = Path('apps/VulkaxPhysicsStudioMac/Package.swift')
text = package.read_text()
text = replace_once(text, '''        .target(
            name: "VulkaxRuntimeContract",
            path: "Sources/VulkaxRuntimeContract",
            publicHeadersPath: "include"
        ),
''', '''        .target(
            name: "VulkaxRuntimeContract",
            path: "Sources/VulkaxRuntimeContract",
            publicHeadersPath: "include"
        ),
        .target(
            name: "VulkaxEquationBridge",
            path: "Sources/VulkaxEquationBridge",
            publicHeadersPath: "include"
        ),
''', 'SwiftPM bridge target')
text = text.replace(
    'dependencies: ["VulkaxRuntimeContract"],',
    'dependencies: ["VulkaxRuntimeContract", "VulkaxEquationBridge"],', 1)
text = text.replace(
    '    swiftLanguageModes: [.v5]\n)',
    '    cxxLanguageStandard: .cxx20,\n    swiftLanguageModes: [.v5]\n)', 1)
package.write_text(text)

compiler = Path('apps/VulkaxPhysicsStudioMac/Sources/VulkaxPhysicsStudioMac/ScalarEquationCompiler.swift')
compiler.write_text('''import Foundation\nimport VulkaxEquationBridge\n\nstruct CompiledScalarEquation {\n    let metalExpression: String\n    let parameterNames: [String]\n    let metalSource: String\n    let sourceHash: UInt64\n}\n\nenum ScalarEquationError: LocalizedError {\n    case canonicalCompiler(String)\n\n    var errorDescription: String? {\n        switch self {\n        case let .canonicalCompiler(message): return message\n        }\n    }\n}\n\nenum ScalarEquationCompiler {\n    static func compile(_ source: String) throws -> CompiledScalarEquation {\n        guard let handle = source.withCString({ vulkax_compile_scalar_equation($0) }) else {\n            throw ScalarEquationError.canonicalCompiler("Canonical equation compiler allocation failed")\n        }\n        defer { vulkax_destroy_compiled_equation(handle) }\n        guard vulkax_compiled_equation_success(handle) != 0 else {\n            let diagnostic = vulkax_compiled_equation_diagnostic(handle).map(String.init(cString:)) ??\n                "Canonical equation compilation failed"\n            throw ScalarEquationError.canonicalCompiler(diagnostic)\n        }\n        guard let metalPointer = vulkax_compiled_equation_metal_source(handle),\n              let parameterPointer = vulkax_compiled_equation_parameter_names(handle) else {\n            throw ScalarEquationError.canonicalCompiler("Canonical compiler returned incomplete output")\n        }\n        let parameterBlob = String(cString: parameterPointer)\n        let parameters = parameterBlob.isEmpty\n            ? []\n            : parameterBlob.split(separator: "\\n").map(String.init)\n        return CompiledScalarEquation(\n            metalExpression: "",\n            parameterNames: parameters,\n            metalSource: String(cString: metalPointer),\n            sourceHash: vulkax_compiled_equation_canonical_hash(handle))\n    }\n}\n''')

main = Path('apps/VulkaxPhysicsStudioMac/Sources/VulkaxPhysicsStudioMac/main.swift')
text = main.read_text()
marker = '''        if CommandLine.arguments.contains("--native-cinematic-capture-smoke") {
            exit(runCinematicCaptureWriterSmoke() ? EXIT_SUCCESS : EXIT_FAILURE)
        }
'''
addition = marker + '''        if CommandLine.arguments.contains("--native-canonical-equation-bridge-smoke") {
            exit(runCanonicalEquationBridgeSmoke() ? EXIT_SUCCESS : EXIT_FAILURE)
        }
'''
if '--native-canonical-equation-bridge-smoke' not in text:
    text = replace_once(text, marker, addition, 'canonical bridge CLI')
main.write_text(text)

Path('scripts/phase13_canonical_bridge.py').unlink()
''