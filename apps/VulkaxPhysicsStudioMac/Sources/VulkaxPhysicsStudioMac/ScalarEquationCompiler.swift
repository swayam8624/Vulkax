import Foundation

struct CompiledScalarEquation {
    let metalExpression: String
    let parameterNames: [String]
    let metalSource: String
    let sourceHash: UInt64
}

enum ScalarEquationError: LocalizedError {
    case syntax(column: Int, message: String)
    case unsupportedFunction(String)
    case invalidArity(function: String, expected: String)
    case tooManyParameters(Int)

    var errorDescription: String? {
        switch self {
        case let .syntax(column, message): return "Column \(column): \(message)"
        case let .unsupportedFunction(name): return "Unsupported function '\(name)'"
        case let .invalidArity(function, expected): return "Function '\(function)' expects \(expected)"
        case let .tooManyParameters(count): return "The GPU runtime supports at most 16 parameters; this equation uses \(count)"
        }
    }
}

private enum ScalarToken: Equatable {
    case number(String)
    case identifier(String)
    case symbol(Character)
    case end
}

private struct ScalarLexer {
    private let characters: [Character]
    private(set) var position = 0

    init(_ source: String) { characters = Array(source) }

    mutating func next() throws -> ScalarToken {
        while position < characters.count && characters[position].isWhitespace { position += 1 }
        guard position < characters.count else { return .end }
        let character = characters[position]
        if character.isNumber || character == "." {
            let begin = position
            var sawExponent = false
            position += 1
            while position < characters.count {
                let candidate = characters[position]
                if candidate.isNumber || candidate == "." {
                    position += 1
                } else if (candidate == "e" || candidate == "E") && !sawExponent {
                    sawExponent = true
                    position += 1
                    if position < characters.count && (characters[position] == "+" || characters[position] == "-") {
                        position += 1
                    }
                } else {
                    break
                }
            }
            let literal = String(characters[begin..<position])
            guard Double(literal)?.isFinite == true else {
                throw ScalarEquationError.syntax(column: begin + 1, message: "invalid numeric literal")
            }
            return .number(literal)
        }
        if character.isLetter || character == "_" {
            let begin = position
            position += 1
            while position < characters.count && (characters[position].isLetter || characters[position].isNumber || characters[position] == "_") {
                position += 1
            }
            return .identifier(String(characters[begin..<position]))
        }
        if "+-*/^(),".contains(character) {
            position += 1
            return .symbol(character)
        }
        throw ScalarEquationError.syntax(column: position + 1, message: "unexpected character '\(character)'")
    }
}

private indirect enum ScalarNode {
    case number(String)
    case variable(String)
    case unary(ScalarNode)
    case binary(Character, ScalarNode, ScalarNode)
    case call(String, [ScalarNode])
}

private struct ScalarParser {
    private var tokens: [ScalarToken] = []
    private var index = 0

    init(_ source: String) throws {
        var lexer = ScalarLexer(source)
        while true {
            let token = try lexer.next()
            tokens.append(token)
            if token == .end { break }
        }
    }

    mutating func parse() throws -> ScalarNode {
        let result = try expression()
        guard current == .end else { throw syntax("unexpected token") }
        return result
    }

    private var current: ScalarToken { tokens[index] }

    private mutating func expression() throws -> ScalarNode {
        var left = try term()
        while case let .symbol(operation) = current, operation == "+" || operation == "-" {
            advance()
            left = .binary(operation, left, try term())
        }
        return left
    }

    private mutating func term() throws -> ScalarNode {
        var left = try unary()
        while case let .symbol(operation) = current, operation == "*" || operation == "/" {
            advance()
            left = .binary(operation, left, try unary())
        }
        return left
    }

    private mutating func unary() throws -> ScalarNode {
        if consume("-") { return .unary(try unary()) }
        if consume("+") { return try unary() }
        return try power()
    }

    private mutating func power() throws -> ScalarNode {
        let left = try primary()
        return consume("^") ? .binary("^", left, try unary()) : left
    }

    private mutating func primary() throws -> ScalarNode {
        switch current {
        case let .number(value):
            advance()
            return .number(value)
        case let .identifier(name):
            advance()
            if consume("(") {
                var arguments: [ScalarNode] = []
                if !consume(")") {
                    repeat { arguments.append(try expression()) } while consume(",")
                    guard consume(")") else { throw syntax("expected ')'") }
                }
                return .call(name, arguments)
            }
            return .variable(name)
        case .symbol("("):
            advance()
            let nested = try expression()
            guard consume(")") else { throw syntax("expected ')'") }
            return nested
        default:
            throw syntax("expected a number, symbol, function, or parenthesized expression")
        }
    }

    private mutating func consume(_ symbol: Character) -> Bool {
        guard current == .symbol(symbol) else { return false }
        advance()
        return true
    }

    private mutating func advance() { index = min(index + 1, tokens.count - 1) }
    private func syntax(_ message: String) -> ScalarEquationError { .syntax(column: index + 1, message: message) }
}

enum ScalarEquationCompiler {
    static func compile(_ source: String) throws -> CompiledScalarEquation {
        var parser = try ScalarParser(source)
        let root = try parser.parse()
        var symbols = Set<String>()
        collectSymbols(root, into: &symbols)
        let builtins: Set<String> = ["x", "y", "z", "t", "pi", "e"]
        let parameters = symbols.subtracting(builtins).sorted()
        guard parameters.count <= 16 else { throw ScalarEquationError.tooManyParameters(parameters.count) }
        let indices = Dictionary(uniqueKeysWithValues: parameters.enumerated().map { ($1, $0) })
        let expression = try emit(root, parameterIndices: indices)
        let hash = fnv1a(source)
        return CompiledScalarEquation(
            metalExpression: expression,
            parameterNames: parameters,
            metalSource: metalKernel(expression: expression, sourceHash: hash),
            sourceHash: hash)
    }

    private static func collectSymbols(_ node: ScalarNode, into symbols: inout Set<String>) {
        switch node {
        case .number: break
        case let .variable(name): symbols.insert(name)
        case let .unary(child): collectSymbols(child, into: &symbols)
        case let .binary(_, left, right):
            collectSymbols(left, into: &symbols)
            collectSymbols(right, into: &symbols)
        case let .call(_, arguments):
            arguments.forEach { collectSymbols($0, into: &symbols) }
        }
    }

    private static func emit(_ node: ScalarNode, parameterIndices: [String: Int]) throws -> String {
        switch node {
        case let .number(value): return value.contains(".") || value.lowercased().contains("e") ? value : value + ".0"
        case let .variable(name):
            if name == "pi" { return "M_PI_F" }
            if name == "e" { return "M_E_F" }
            if ["x", "y", "z", "t"].contains(name) { return name }
            guard let index = parameterIndices[name] else { throw ScalarEquationError.syntax(column: 1, message: "unbound symbol '\(name)'") }
            return "parameters[\(index)]"
        case let .unary(child): return "(-\(try emit(child, parameterIndices: parameterIndices)))"
        case let .binary(operation, left, right):
            let lhs = try emit(left, parameterIndices: parameterIndices)
            let rhs = try emit(right, parameterIndices: parameterIndices)
            return operation == "^" ? "pow(\(lhs), \(rhs))" : "(\(lhs) \(operation) \(rhs))"
        case let .call(name, arguments):
            let arity: [String: Int] = ["sin": 1, "cos": 1, "tan": 1, "exp": 1, "sqrt": 1,
                                        "abs": 1, "log": 1, "min": 2, "max": 2, "clamp": 3]
            guard let expected = arity[name] else { throw ScalarEquationError.unsupportedFunction(name) }
            guard arguments.count == expected else {
                throw ScalarEquationError.invalidArity(function: name, expected: "\(expected) argument\(expected == 1 ? "" : "s")")
            }
            return "\(name)(\(try arguments.map { try emit($0, parameterIndices: parameterIndices) }.joined(separator: ", ")))"
        }
    }

    private static func fnv1a(_ value: String) -> UInt64 {
        value.utf8.reduce(1_469_598_103_934_665_603) { hash, byte in
            (hash ^ UInt64(byte)) &* 1_099_511_628_211
        }
    }

    private static func metalKernel(expression: String, sourceHash: UInt64) -> String {
        """
        #include <metal_stdlib>
        using namespace metal;
        struct Uniforms {
            float time; float amplitude; float wavenumber; float angularFrequency;
            float width; float height; float4 padding; float4 control; float4 renderParameters;
        };
        float3 equationPalette(float value) {
            float shadow = pow(clamp(value, 0.0, 1.0), 0.72);
            return float3(0.025 + 0.91 * pow(shadow, 1.55),
                          0.055 + 0.54 * sin(shadow * 1.47),
                          0.13 + 0.70 * (1.0 - shadow) * (1.0 - shadow));
        }
        kernel void renderCompiledEquation(
            texture2d<half, access::write> outputRadiance [[texture(0)]],
            constant Uniforms& u [[buffer(0)]],
            device const float* parameters [[buffer(1)]],
            uint2 pixel [[thread_position_in_grid]]) {
            if (pixel.x >= uint(u.width) || pixel.y >= uint(u.height)) return;
            float2 uv = (float2(pixel) + 0.5) / float2(u.width, u.height);
            float aspect = u.width / max(1.0, u.height);
            float x = (uv.x - 0.5) * 8.0 * aspect;
            float y = (0.5 - uv.y) * 8.0;
            float z = 0.0;
            float t = u.time;
            float field = \(expression);
            if (!isfinite(field)) field = 0.0;
            float3 radiance = equationPalette(0.5 + 0.5 * tanh(field));
            outputRadiance.write(half4(half3(radiance * 2.25), 1.0h), pixel);
        }
        // canonical source hash: \(sourceHash)
        """
    }
}
