#pragma once

namespace vulkax::cli {

// Returns -1 when argv does not request captured-world-run.
[[nodiscard]] int capturedWorldRunCommand(int argc, char** argv);

} // namespace vulkax::cli
