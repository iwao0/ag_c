#include "function_type_lowering.h"

#include "../semantic/type_identity.h"
#include "../type_signature.h"

#include <stdlib.h>

int ir_function_type_from_type_id(
    const psx_semantic_type_table_t *semantic_types,
    psx_type_id_t type_id, ir_function_type_t *out) {
  if (!semantic_types || !out) return 0;
  ir_function_type_dispose(out);

  psx_type_shape_t function = {0};
  int has_function = psx_semantic_type_table_describe(
      semantic_types, type_id, &function);
  while (has_function && (function.kind == PSX_TYPE_POINTER ||
                          function.kind == PSX_TYPE_ARRAY)) {
    type_id = psx_semantic_type_table_base(
        semantic_types, type_id).type_id;
    has_function = psx_semantic_type_table_describe(
        semantic_types, type_id, &function);
  }
  if (!has_function || function.kind != PSX_TYPE_FUNCTION ||
      function.parameter_count < 0)
    return 0;

  size_t param_count = (size_t)function.parameter_count;
  psx_qual_type_t *params = NULL;
  if (param_count > 0) {
    params = calloc(param_count, sizeof(*params));
    if (!params) return 0;
    for (size_t i = 0; i < param_count; i++) {
      params[i] = psx_semantic_type_table_parameter(
          semantic_types, type_id, (int)i);
      if (params[i].type_id == PSX_TYPE_ID_INVALID) {
        free(params);
        return 0;
      }
    }
  }

  psx_qual_type_t result = psx_semantic_type_table_base(
      semantic_types, type_id);
  int ok = ir_function_type_set(
      out, type_id, result, params, param_count,
      function.is_variadic_function,
      function.has_function_prototype);
  free(params);
  return ok;
}

int ir_function_type_format_c_signature(
    const psx_semantic_type_table_t *semantic_types,
    const ir_function_type_t *function_type,
    const ag_data_layout_t *data_layout,
    char **out, int *out_length) {
  if (out) *out = NULL;
  if (out_length) *out_length = 0;
  if (!semantic_types || !function_type || !data_layout || !out ||
      !out_length ||
      function_type->type_id == PSX_TYPE_ID_INVALID)
    return 0;
  int length = psx_format_canonical_type_signature(
      semantic_types,
      (psx_qual_type_t){
          function_type->type_id, PSX_TYPE_QUALIFIER_NONE},
      data_layout, NULL, 0);
  if (length <= 0) return 0;
  char *signature = malloc((size_t)length + 1);
  if (!signature) return 0;
  if (psx_format_canonical_type_signature(
          semantic_types,
          (psx_qual_type_t){
              function_type->type_id, PSX_TYPE_QUALIFIER_NONE},
          data_layout, signature, (size_t)length + 1) != length) {
    free(signature);
    return 0;
  }
  *out = signature;
  *out_length = length;
  return 1;
}
