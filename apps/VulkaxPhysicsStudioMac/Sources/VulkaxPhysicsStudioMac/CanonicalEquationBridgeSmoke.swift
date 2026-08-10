import Foundation
import Metal

func runCanonicalEquationBridgeSmoke() -> Bool {
    do {
        let compiled = try ScalarEquationCompiler.compile("gain * sin(k*x - omega*t) + bias")
        guard compiled.parameterNames == ["bias", "gain", "k", "omega"] else {
            throw NSError(
                domain: "VulkaxCanonicalEquationBridgeSmoke",
                code: 1,
                userInfo: [NSLocalizedDescriptionKey:
                    "unexpected canonical parameters: \(compiled.parameterNames)"])
        }
        guard compiled.sourceHash != 0,
              compiled.metalSource.contains("renderCompiledEquation"),
              compiled.metalSource.contains("canonical Physics IR hash") else {
            throw NSError(
                domain: "VulkaxCanonicalEquationBridgeSmoke",
                code: 2,
                userInfo: [NSLocalizedDescriptionKey: "canonical Metal emission is incomplete"])
        }
        guard let device = MTLCreateSystemDefaultDevice() else {
            throw NSError(
                domain: "VulkaxCanonicalEquationBridgeSmoke",
                code: 3,
                userInfo: [NSLocalizedDescriptionKey: "no Metal device"])
        }
        _ = try device.makeLibrary(source: compiled.metalSource, options: nil)

        do {
            _ = try ScalarEquationCompiler.compile("sin((x)")
            throw NSError(
                domain: "VulkaxCanonicalEquationBridgeSmoke",
                code: 4,
                userInfo: [NSLocalizedDescriptionKey: "malformed equation unexpectedly compiled"])
        } catch {
            guard error.localizedDescription.lowercased().contains("equation parse error") else {
                throw NSError(
                    domain: "VulkaxCanonicalEquationBridgeSmoke",
                    code: 5,
                    userInfo: [NSLocalizedDescriptionKey:
                        "Swift did not surface the canonical parser diagnostic: \(error.localizedDescription)"])
            }
        }
        print("Vulkax canonical C++ equation bridge smoke passed · hash=\(compiled.sourceHash)")
        return true
    } catch {
        FileHandle.standardError.write(Data("Canonical equation bridge smoke failed: \(error)\n".utf8))
        return false
    }
}
