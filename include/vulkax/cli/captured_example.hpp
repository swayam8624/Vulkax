#pragma once

namespace vulkax::cli {

// Generates the deterministic synthetic captured-deformable dataset used for
// end-to-end material-calibration regression and reproducible CLI examples.
// Returns -1 when argv does not request this command.
int capturedExampleCommand(int argc, char** argv);

} // namespace vulkax::cli
