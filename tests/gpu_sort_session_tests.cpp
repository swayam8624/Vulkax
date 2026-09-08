#include "vulkax/viewer/gpu_sort_session.hpp"

#include <cassert>
#include <string>

int main() {
    using vulkax::viewer::GpuSortAuthority;
    using vulkax::viewer::GpuSortSession;

    GpuSortSession session;
    session.reset(true);
    assert(session.authority() == GpuSortAuthority::Validating);
    assert(session.parityPasses() == 0U);
    assert(session.requiredParityPasses() == 3U);

    session.recordParity(true);
    assert(session.validating());
    assert(session.parityPasses() == 1U);
    session.recordParity(true);
    assert(session.validating());
    assert(session.parityPasses() == 2U);
    session.recordParity(true);
    assert(session.gpuTrusted());
    assert(session.parityPasses() == 3U);
    assert(session.note().empty());

    // A late runtime failure must permanently return the session to CPU fallback.
    session.recordRuntimeFailure("command buffer failed");
    assert(session.cpuFallback());
    assert(session.note() == "command buffer failed");
    session.recordParity(true);
    assert(session.cpuFallback());

    // Reloading a scene re-enters validation when the GPU backend exists.
    session.reset(true);
    assert(session.validating());
    session.recordParity(false, "order mismatch");
    assert(session.cpuFallback());
    assert(session.note() == "order mismatch");

    // Unavailable GPU starts directly on CPU and preserves the diagnostic.
    session.reset(false, "no Metal device");
    assert(session.cpuFallback());
    assert(session.note() == "no Metal device");

    // A zero requested pass count is normalized to one.
    GpuSortSession onePass(0U);
    onePass.reset(true);
    onePass.recordParity(true);
    assert(onePass.gpuTrusted());

    return 0;
}
