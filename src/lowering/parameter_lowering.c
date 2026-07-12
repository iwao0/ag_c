#include "parameter_lowering.h"

#include "local_storage.h"
#include "../parser/decl.h"
#include "../parser/local_registry.h"
#include "../semantic/local_type_state.h"
#include <string.h>

int lower_parameter_declaration(
    const psx_parameter_lowering_request_t *request,
    psx_parameter_lowering_result_t *result) {
  if (!request || !result || !request->name || request->name_len <= 0 ||
      !request->type) return 0;
  memset(result, 0, sizeof(*result));
  if (!psx_plan_parameter_storage(request->type, &result->storage))
    return 0;

  int offset = local_storage_allocate(
      result->storage.storage_size, result->storage.alignment);
  result->var = psx_local_registry_create_storage_object(
      request->name, request->name_len, offset,
      result->storage.storage_size, result->storage.element_size,
      0, result->storage.alignment);
  if (!result->var) return 0;
  result->var->is_param = 1;
  result->var->is_byref_param = result->storage.is_byref ? 1 : 0;
  psx_decl_set_lvar_decl_type(result->var, request->type);
  result->type_attached = 1;
  return 1;
}
