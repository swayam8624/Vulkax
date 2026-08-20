#pragma once

namespace vulkax::cli {

// Returns -1 when argv does not name this command; otherwise returns an exit code.
int transferAblationCommand(int argc, char** argv);
int transferDiagnosticsCommand(int argc, char** argv);

} // namespace vulkax::cli
