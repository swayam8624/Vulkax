#pragma once

namespace vulkax::cli {

// Returns -1 when argv does not name this command; otherwise returns an exit code.
int transferEnergyCycleCommand(int argc, char** argv);

} // namespace vulkax::cli
