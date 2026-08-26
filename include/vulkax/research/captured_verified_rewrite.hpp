#pragma once

#include "vulkax/research/captured_operator_influence.hpp"
#include "vulkax/world/verified_rewrite.hpp"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace vulkax::research {

// Controlled adapter from the captured APIC/MPM material-influence oracle to
// the central verified world transaction envelope. The physical bindings of
// the rewritten entity are interpreted as stable captured MPM particle IDs.
struct CapturedMaterialRewriteVerifierSettings {
    std::vector<std::size_t> activeGaussianIndices;
    capture::CapturedDeformableDataset dataset;
    solvers::MpmGridSettings grid;
    NonlinearDeformableWorldSettings worldSettings;
    CapturedMaterialInfluenceSettings influenceSettings;
    std::filesystem::path evidenceDirectory;

    double maximumRelativeLinearizationError{0.25};
    double maximumAdjointAbsoluteError{1.0e-8};
    double maximumAdjointRelativeError{5.0e-3};
    double minimumReferenceDerivativeForRelativeCheck{1.0e-7};
    double rewriteScaleTolerance{1.0e-10};
};

// The returned verifier supports one transaction-local `young_modulus` edit.
// It requires the old WorldIR value to match worldSettings.material.youngModulus
// and the requested new/old scale delta to match influenceSettings'
// verificationScaleDelta. This prevents evidence from a different perturbation
// from being attached to the transaction being committed.
[[nodiscard]] world::PhysicalRewriteVerifier makeCapturedMaterialRewriteVerifier(
    CapturedMaterialRewriteVerifierSettings settings);

} // namespace vulkax::research
