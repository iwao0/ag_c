#include "resolved_function.h"

psx_qual_type_t ps_function_definition_signature_qual_type(
    const node_function_definition_t *function) {
  return function
             ? function->signature_qual_type
             : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                                 PSX_TYPE_QUALIFIER_NONE};
}

const psx_type_t *ps_function_definition_return_type(
    const node_function_definition_t *function) {
  if (!function || !function->signature ||
      function->signature->kind != PSX_TYPE_FUNCTION)
    return NULL;
  return function->signature->base;
}
