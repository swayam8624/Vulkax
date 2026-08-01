#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vulkax::physics {

// Exponents for the SI base dimensions L, M, T, I, temperature, amount, and
// luminous intensity. A dimensionless quantity is the zero value.
struct Dimension {
  std::array<int8_t, 7> exponents{};

  [[nodiscard]] static constexpr Dimension dimensionless() { return {}; }
  [[nodiscard]] static constexpr Dimension length() { return {{{1, 0, 0, 0, 0, 0, 0}}}; }
  [[nodiscard]] static constexpr Dimension time() { return {{{0, 0, 1, 0, 0, 0, 0}}}; }
  [[nodiscard]] static constexpr Dimension mass() { return {{{0, 1, 0, 0, 0, 0, 0}}}; }

  [[nodiscard]] constexpr Dimension multipliedBy(const Dimension& other) const {
    Dimension result{};
    for (size_t index = 0; index < exponents.size(); ++index) {
      result.exponents[index] = static_cast<int8_t>(exponents[index] + other.exponents[index]);
    }
    return result;
  }
  [[nodiscard]] constexpr Dimension dividedBy(const Dimension& other) const {
    Dimension result{};
    for (size_t index = 0; index < exponents.size(); ++index) {
      result.exponents[index] = static_cast<int8_t>(exponents[index] - other.exponents[index]);
    }
    return result;
  }
  [[nodiscard]] constexpr bool operator==(const Dimension&) const = default;
};

enum class ValueType : uint8_t {
  Scalar,
  Vector2,
  Vector3,
};

enum class FieldPlacement : uint8_t {
  CellCenter,
  FaceX,
  FaceY,
  FaceZ,
};

struct FieldDeclaration {
  std::string name;
  ValueType valueType = ValueType::Scalar;
  Dimension dimension{};
  FieldPlacement placement = FieldPlacement::CellCenter;
};

struct Domain3D {
  std::array<double, 3> minimum{-1.0, -1.0, -1.0};
  std::array<double, 3> maximum{1.0, 1.0, 1.0};
  std::array<uint32_t, 3> resolution{64, 64, 64};
};

enum class BoundaryKind : uint8_t {
  Open,
  Periodic,
  NoSlip,
  FixedValue,
};

struct BoundaryCondition {
  std::string field;
  BoundaryKind kind = BoundaryKind::Open;
  std::optional<double> fixedValue;
};

// A spatially uniform initial value. This deliberately covers the common
// zero/ambient initialization contract without pretending that an arbitrary
// expression has already been lowered to a backend kernel.
struct InitialCondition {
  std::string field;
  double value = 0.0;
};

enum class DifferentialOperator : uint8_t {
  Identity,
  TimeDerivative,
  Gradient,
  Divergence,
  Curl,
  Laplacian,
};

struct OperatorTerm {
  DifferentialOperator operation = DifferentialOperator::Identity;
  std::string field;
};

enum class EquationRelation : uint8_t {
  Equality,
};

struct Equation {
  std::string name;
  OperatorTerm lhs;
  std::vector<OperatorTerm> rhs;
  EquationRelation relation = EquationRelation::Equality;
};

enum class AdvectionScheme : uint8_t {
  SemiLagrangian,
  MacCormack,
  Bfecc,
};

enum class PressureSolve : uint8_t {
  Jacobi,
  Multigrid,
  ConjugateGradient,
};

struct SolverSettings {
  AdvectionScheme advection = AdvectionScheme::MacCormack;
  PressureSolve pressure = PressureSolve::Multigrid;
  bool adaptiveCfl = true;
  double timestepSeconds = 1.0 / 60.0;
};

struct VisualizationSettings {
  bool volume = false;
  double extinction = 0.0;
  double scattering = 0.0;
  double phaseAnisotropy = 0.0;
};

enum class SolverPassKind : uint8_t {
  AdvectVelocity,
  AdvectScalar,
  ApplyForces,
  ComputeDivergence,
  SolvePressure,
  ProjectVelocity,
  ComputeCurl,
  ApplyVorticityConfinement,
  VoxelizeObstacles,
  ApplySolidBoundary,
  IntegrateFluidForces,
  AdvanceRigidBodies,
  DeriveOpticalProperties,
};

struct SolverPass {
  SolverPassKind kind = SolverPassKind::AdvectVelocity;
  std::string name;
  std::vector<std::string> reads;
  std::vector<std::string> writes;
};

struct PhysicsModel {
  std::string name;
  Domain3D domain{};
  std::vector<FieldDeclaration> fields;
  std::vector<Equation> equations;
  std::vector<Equation> constraints;
  std::vector<BoundaryCondition> boundaries;
  std::vector<InitialCondition> initialConditions;
  SolverSettings solver{};
  VisualizationSettings visualization{};
  // An explicit pass graph overrides built-in equation lowering. This makes
  // serialized projects executable graphs rather than preset dispatch keys.
  std::vector<SolverPass> passes;
};

struct TypedTerm {
  ValueType valueType = ValueType::Scalar;
  Dimension dimension{};
};

struct ValidationIssue {
  std::string message;
  bool error = true;
};

struct ValidationResult {
  std::vector<ValidationIssue> issues;
  [[nodiscard]] bool valid() const;
};

struct ParseDiagnostic {
  uint32_t line = 0;
  std::string message;
};

struct ParseResult {
  std::optional<PhysicsModel> model;
  std::vector<ParseDiagnostic> diagnostics;
  [[nodiscard]] bool valid() const { return model.has_value() && diagnostics.empty(); }
};

struct SolverGraph {
  std::vector<SolverPass> passes;
  std::vector<InitialCondition> initialConditions;
  uint64_t canonicalHash = 0;
};

enum class ResourceFormat : uint8_t {
  R16Float,
  R32Float,
  Rgba16Float,
  R32Uint,
  StructuredBuffer,
};

enum class ResourceAccess : uint8_t {
  ReadOnly,
  WriteOnly,
  ReadWrite,
};

struct ReflectedResource {
  std::string name;
  ResourceFormat format = ResourceFormat::R32Float;
  ResourceAccess access = ResourceAccess::ReadOnly;
  std::array<uint32_t, 3> extent{1, 1, 1};
  uint32_t descriptorSet = 0;
  uint32_t binding = 0;
  uint32_t historyLength = 1;
  uint64_t estimatedBytes = 0;
};

struct ReflectedPass {
  std::string name;
  SolverPassKind kind = SolverPassKind::AdvectVelocity;
  std::vector<uint32_t> readBindings;
  std::vector<uint32_t> writeBindings;
};

struct ResourceLayout {
  std::vector<ReflectedResource> resources;
  std::vector<ReflectedPass> passes;
  uint64_t canonicalHash = 0;
  uint64_t estimatedBytes = 0;
};

// Resolves the dimensional and vector type of a field operator. It deliberately
// contains no renderer backend detail, so Vulkan and Metal lowering share the
// same physics contract.
[[nodiscard]] std::optional<TypedTerm> typeOf(
    const PhysicsModel& model, const OperatorTerm& term, std::string* error = nullptr);
[[nodiscard]] ValidationResult validate(const PhysicsModel& model);

// Parses the compact, line-oriented Physics Studio DSL. The grammar is kept
// deliberately explicit while solver lowering is still evolving:
//
//   model smoke
//   field velocity: vector3 [m/s] face_x
//   domain -2 0 -2 2 6 2
//   resolution 128 192 128
//   constraint incompressibility: div(velocity) = divergence_source
//   initial density = 0
//   boundary velocity: no_slip
//   solver advection maccormack pressure multigrid timestep 0.0166667
//   pass advect_density advect_scalar reads velocity,density writes density_next
//   visualize volume extinction 1.7 scattering 0.6 phase 0.25
[[nodiscard]] ParseResult parsePhysicsDsl(const std::string& source);

// Lowers a validated physics model to an explicit resource-dependency graph.
// Backends choose their own buffers/images and kernels, but must honor the
// graph's read/write ordering. Unsupported models fail here instead of being
// silently redirected to a handwritten preset implementation.
[[nodiscard]] std::optional<SolverGraph> lowerToSolverGraph(
    const PhysicsModel& model, std::vector<ValidationIssue>* issues = nullptr);

// Generates stable descriptor bindings and allocation requirements from the
// graph. Backends consume this contract instead of maintaining handwritten
// resource tables for each preset.
[[nodiscard]] ResourceLayout reflectResourceLayout(
    const PhysicsModel& model, const SolverGraph& graph);

// A canonical, backend-independent fluid declaration. It is intentionally a
// real typed model rather than a preset identifier used to select handwritten
// code; lowering backends may choose different numerical kernels from it.
[[nodiscard]] PhysicsModel makeIncompressibleSmokeModel();
// Extends the incompressible equations with a voxelized solid and dynamic
// rigid-body state. The lowered graph includes boundary and force-coupling
// passes suitable for imported watertight triangle meshes.
[[nodiscard]] PhysicsModel makeFluidRigidInteractionModel();

}  // namespace vulkax::physics
