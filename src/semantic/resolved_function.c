#include "resolved_function.h"

#include "type_identity.h"

psx_qual_type_t ps_function_definition_signature_qual_type(
    const node_function_definition_t *function) {
  return function
             ? function->signature_qual_type
             : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                                 PSX_TYPE_QUALIFIER_NONE};
}

psx_qual_type_t ps_function_definition_return_qual_type(
    const psx_semantic_type_table_t *types,
    const node_function_definition_t *function) {
  if (!types || !function)
    return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                             PSX_TYPE_QUALIFIER_NONE};
  return psx_semantic_type_table_base(
      types, function->signature_qual_type.type_id);
}
