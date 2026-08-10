#ifndef VULKAX_EQUATION_BRIDGE_H
#define VULKAX_EQUATION_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque result owned by the bridge. The result exists even when compilation
// fails so Swift can surface the canonical C++ diagnostic verbatim.
typedef void* VulkaxCompiledEquationHandle;

VulkaxCompiledEquationHandle vulkax_compile_scalar_equation(const char* source);
int32_t vulkax_compiled_equation_success(VulkaxCompiledEquationHandle handle);
const char* vulkax_compiled_equation_metal_source(VulkaxCompiledEquationHandle handle);
const char* vulkax_compiled_equation_parameter_names(VulkaxCompiledEquationHandle handle);
const char* vulkax_compiled_equation_diagnostic(VulkaxCompiledEquationHandle handle);
uint64_t vulkax_compiled_equation_canonical_hash(VulkaxCompiledEquationHandle handle);
void vulkax_destroy_compiled_equation(VulkaxCompiledEquationHandle handle);

#ifdef __cplusplus
}
#endif

#endif
