// Compile the repository's canonical equation implementation as a SwiftPM
// translation unit. Keeping it in its own TU avoids anonymous-namespace
// collisions and means the native editor cannot drift to a second parser.
#include "../../../../src/vulkax/equation/equation.cpp"
