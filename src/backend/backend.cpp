#include "vulkax/backend/backend.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vulkax::backend {

PlatformKind currentPlatform() noexcept {
#if defined(__APPLE__)
    return PlatformKind::MacOS;
#elif defined(_WIN32)
    return PlatformKind::Windows;
#elif defined(__linux__)
    return PlatformKind::Linux;
#else
    return PlatformKind::Other;
#endif
}

std::string_view toString(BackendKind kind) noexcept {
    switch (kind) {
    case BackendKind::Vulkan:
        return "Vulkan";
    case BackendKind::Metal:
        return "Metal";
    case BackendKind::OpenGL:
        return "OpenGL";
    }
    return "Unknown";
}

bool supports(const BackendCapabilities& capability, Feature feature) noexcept {
    return std::find(capability.features.begin(), capability.features.end(), feature) !=
           capability.features.end();
}

BackendSelection selectBackend(const std::vector<BackendCapabilities>& candidates,
                               const WorkloadRequirements& requirements, PlatformKind platform) {
    BackendSelection best;
    best.score = -std::numeric_limits<double>::infinity();

    for (const auto& candidate : candidates) {
        if (!candidate.available) {
            continue;
        }
        if (requirements.requireCompute && !supports(candidate, Feature::Compute)) {
            continue;
        }
        if (std::any_of(requirements.requiredFeatures.begin(), requirements.requiredFeatures.end(),
                        [&](Feature feature) { return !supports(candidate, feature); })) {
            continue;
        }

        double score = 0.0;
        switch (candidate.kind) {
        case BackendKind::Vulkan:
            score += 100.0;
            break;
        case BackendKind::Metal:
            score += 95.0;
            break;
        case BackendKind::OpenGL:
            score += 55.0;
            break;
        }

        if (platform == PlatformKind::MacOS) {
            if (candidate.kind == BackendKind::Metal) {
                score += 35.0;
            } else if (candidate.kind == BackendKind::Vulkan) {
                score += 10.0;
            }
        } else if (platform == PlatformKind::Linux || platform == PlatformKind::Windows) {
            if (candidate.kind == BackendKind::Vulkan) {
                score += 35.0;
            } else if (candidate.kind == BackendKind::OpenGL) {
                score += 10.0;
            } else if (candidate.kind == BackendKind::Metal) {
                score -= 100.0;
            }
        }

        if (candidate.nativePlatformBackend) {
            score += 10.0;
        }
        if (candidate.dedicatedGpu) {
            score += 8.0;
        }
        score += std::clamp(candidate.driverQuality, 0.0, 1.0) * 20.0;
        if (candidate.deviceMemoryBytes >= (8ull << 30u)) {
            score += 5.0;
        } else if (candidate.deviceMemoryBytes >= (4ull << 30u)) {
            score += 2.0;
        }

        if (!best.kind || score > best.score) {
            best.kind = candidate.kind;
            best.score = score;
            best.reasons.clear();
            best.reasons.push_back(std::string(toString(candidate.kind)) + " satisfies workload features");
            if (candidate.nativePlatformBackend) {
                best.reasons.push_back("native platform backend");
            }
            if (candidate.dedicatedGpu) {
                best.reasons.push_back("dedicated GPU");
            }
        }
    }

    if (!best.kind) {
        best.score = 0.0;
        best.reasons.push_back("no available backend satisfies the workload requirements");
    }
    return best;
}

} // namespace vulkax::backend
