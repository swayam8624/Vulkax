#pragma once

namespace vulkax::cli {

// Returns -1 when argv does not name this command; otherwise returns an exit code.
int deformableBackendCompareCommand(int argc, char** argv);
int deformableTimestepSweepCommand(int argc, char** argv);

} // namespace vulkax::cli
