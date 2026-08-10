import Foundation

enum SimulationMedium: String, CaseIterable, Codable, Identifiable {
    case surface2D
    case volume3D
    case particleSet
    case rigidBody
    case vectorField
    case relativityRayBundle
    case trajectory
    case abstractField

    var id: String { rawValue }

    var title: String {
        switch self {
        case .surface2D: return "2D Surface"
        case .volume3D: return "3D Volume"
        case .particleSet: return "Particles"
        case .rigidBody: return "Rigid Body"
        case .vectorField: return "Vector Field"
        case .relativityRayBundle: return "Relativistic Rays"
        case .trajectory: return "Trajectory / ODE"
        case .abstractField: return "Abstract Field"
        }
    }

    var systemImage: String {
        switch self {
        case .surface2D: return "square.grid.2x2"
        case .volume3D: return "cube.transparent"
        case .particleSet: return "circle.hexagongrid"
        case .rigidBody: return "car.side"
        case .vectorField: return "arrow.up.and.down.and.arrow.left.and.right"
        case .relativityRayBundle: return "sparkles"
        case .trajectory: return "point.topleft.down.to.point.bottomright.curvepath"
        case .abstractField: return "waveform.path.ecg"
        }
    }

    var geometryRecommended: Bool {
        switch self {
        case .surface2D, .volume3D, .rigidBody: return true
        default: return false
        }
    }
}

struct MediumInferenceResult: Equatable {
    let medium: SimulationMedium
    let confidence: Double
    let reasons: [String]
    let spatialDimensions: Int

    var confidencePercent: Int { Int((confidence * 100).rounded()) }
}

enum SimulationMediumInference {
    static func infer(equation: String, visualizerMode: VisualizerMode) -> MediumInferenceResult {
        switch visualizerMode {
        case .schwarzschild:
            return .init(
                medium: .relativityRayBundle,
                confidence: 0.98,
                reasons: ["Relativity visualizer is active"],
                spatialDimensions: 3)
        case .volumeSmoke:
            return .init(
                medium: .volume3D,
                confidence: 0.98,
                reasons: ["Volume/MAC simulation graph is active"],
                spatialDimensions: 3)
        case .wave:
            break
        }

        let source = equation.lowercased()
        var scores: [SimulationMedium: Double] = [:]
        var reasons: [SimulationMedium: [String]] = [:]
        var dimensions: [SimulationMedium: Int] = [:]

        func add(_ medium: SimulationMedium, _ score: Double, _ reason: String, _ dimension: Int) {
            scores[medium, default: 0] += score
            reasons[medium, default: []].append(reason)
            dimensions[medium] = max(dimensions[medium, default: 0], dimension)
        }

        func containsAny(_ needles: [String]) -> Bool {
            needles.contains { source.contains($0) }
        }

        func hasVariable(_ character: Character) -> Bool {
            let chars = Array(source)
            for index in chars.indices where chars[index] == character {
                let leftIdentifier = index > 0 && (chars[index - 1].isLetter || chars[index - 1].isNumber || chars[index - 1] == "_")
                let right = index + 1
                let rightIdentifier = right < chars.count && (chars[right].isLetter || chars[right].isNumber || chars[right] == "_")
                if !leftIdentifier && !rightIdentifier { return true }
            }
            return false
        }

        let hasX = hasVariable("x")
        let hasY = hasVariable("y")
        let hasZ = hasVariable("z")
        let hasT = hasVariable("t")

        if containsAny(["kerr", "schwarzschild", "geodesic", "christoffel", "riemann", "metric", "event horizon"]) {
            add(.relativityRayBundle, 8, "Relativity/geodesic terms detected", 3)
        }
        if containsAny(["navier", "stokes", "pressure", "viscosity", "vorticity", "density", "temperature", "buoyancy", "incompressible"]) {
            add(.volume3D, 5, "Continuum/fluid state terms detected", 3)
        }
        if containsAny(["curl(", "div(", "divergence", "gradient", "grad(", "electric field", "magnetic field", "velocity field"]) {
            add(.vectorField, 4, "Vector-calculus operator or field detected", hasZ ? 3 : 2)
        }
        if containsAny(["particle", "n-body", "nbody", "r_ij", "r_ji", "sum_i", "sum_j", "pairwise", "softening"]) {
            add(.particleSet, 6, "Particle/pairwise interaction terms detected", 3)
        }
        if containsAny(["rigid", "inertia", "torque", "angular velocity", "quaternion", "restitution", "friction"]) {
            add(.rigidBody, 7, "Rigid-body state terms detected", 3)
        }
        if containsAny(["d2x/dt2", "d²x/dt²", "dx/dt", "dv/dt", "position(t)", "velocity(t)", "trajectory"]) {
            add(.trajectory, 5, "Time-parametric ODE/trajectory form detected", 0)
        }
        if hasZ {
            add(.volume3D, 3, "Equation references x/y/z coordinates", 3)
        } else if hasX && hasY {
            add(.surface2D, 3, "Equation references a two-dimensional x/y domain", 2)
        } else if hasX {
            add(.surface2D, 1.5, "Equation references a spatial coordinate", 2)
        }
        if containsAny(["laplacian", "nabla", "∇", "diffusion", "wave equation"]) {
            add(hasZ ? .volume3D : .surface2D, 2.5, "Spatial PDE operator detected", hasZ ? 3 : 2)
        }
        if hasT && !hasX && !hasY && !hasZ {
            add(.trajectory, 1.5, "Time is the only independent coordinate", 0)
        }

        guard let winner = scores.max(by: { $0.value < $1.value }) else {
            return .init(
                medium: .abstractField,
                confidence: 0.35,
                reasons: ["No reliable spatial or physical-domain cue found"],
                spatialDimensions: 0)
        }
        let runnerUp = scores.filter { $0.key != winner.key }.map(\.value).max() ?? 0
        let evidence = 1 - exp(-winner.value / 4)
        let separation = winner.value > 0 ? max(0, min(1, (winner.value - runnerUp) / winner.value)) : 0
        let confidence = min(0.95, max(0, 0.25 + 0.55 * evidence + 0.20 * separation))
        return .init(
            medium: winner.key,
            confidence: confidence,
            reasons: reasons[winner.key] ?? [],
            spatialDimensions: dimensions[winner.key] ?? 0)
    }
}

enum SceneEntityRole: String, CaseIterable, Codable, Identifiable {
    case visual
    case collider
    case fluidObstacle
    case source
    case probe
    case domainSurface

    var id: String { rawValue }
    var title: String {
        switch self {
        case .visual: return "Visual only"
        case .collider: return "Collider"
        case .fluidObstacle: return "Fluid obstacle"
        case .source: return "Source / emitter"
        case .probe: return "Probe / sensor"
        case .domainSurface: return "Simulation surface"
        }
    }

    var participatesInSimulation: Bool { self != .visual }
}

enum CollisionProxyKind: String, CaseIterable, Codable, Identifiable {
    case none
    case renderMesh
    case convexHull
    case box
    case sphere

    var id: String { rawValue }
    var title: String {
        switch self {
        case .none: return "None"
        case .renderMesh: return "Mesh"
        case .convexHull: return "Convex Hull"
        case .box: return "Box"
        case .sphere: return "Sphere"
        }
    }
}
