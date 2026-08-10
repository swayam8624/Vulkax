import Foundation
import VulkaxEquationBridge

struct CompiledScalarEquation {
    let metalExpression: String
    let parameterNames: [String]
    let metalSource: String
    let sourceHash: UInt64
}

enum ScalarEquationError: LocalizedError {
    case canonicalCompiler(String)

    var errorDescription: String? {
        switch self {
        case let .canonicalCompiler(message): return message
        }
    }
}

enum ScalarEquationCompiler {
    static func compile(_ source: String) throws -> CompiledScalarEquation {
        guard let handle = source.withCString({ vulkax_compile_scalar_equation($0) }) else {
            throw ScalarEquationError.canonicalCompiler("Canonical C++ equation compiler allocation failed")
        }
        defer { vulkax_destroy_compiled_equation(handle) }
        guard vulkax_compiled_equation_success(handle) != 0 else {
            let message = vulkax_compiled_equation_diagnostic(handle).map(String.init(cString:))
                ?? "Canonical C++ equation compilation failed"
            throw ScalarEquationError.canonicalCompiler(message)
        }
        guard let sourcePointer = vulkax_compiled_equation_metal_source(handle),
              let parameterPointer = vulkax_compiled_equation_parameter_names(handle) else {
            throw ScalarEquationError.canonicalCompiler("Canonical compiler returned incomplete output")
        }
        let parameterBlob = String(cString: parameterPointer)
        return CompiledScalarEquation(
            metalExpression: "",
            parameterNames: parameterBlob.isEmpty ? [] : parameterBlob.split(separator: "\n").map(String.init),
            metalSource: String(cString: sourcePointer),
            sourceHash: vulkax_compiled_equation_canonical_hash(handle))
    }
}
