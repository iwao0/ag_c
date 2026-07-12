#include "function_declaration_plan.h"

#include <string.h>

int psx_plan_function_declaration(
    const psx_function_declaration_request_t *request,
    psx_function_declaration_plan_t *plan) {
  if (!request || !plan || !request->return_type ||
      request->parameter_count < 0) {
    return 0;
  }
  memset(plan, 0, sizeof(*plan));
  const psx_type_t *returned_function =
      ps_type_find_function(request->return_type);
  if (returned_function) {
    plan->returns_function_pointer = 1;
    plan->returned_funcptr_signature =
        ps_type_funcptr_signature(request->return_type);
  }
  psx_type_t *function_type = ps_type_new_function(
      ps_type_clone(request->return_type),
      plan->returned_funcptr_signature);
  if (!function_type) return 0;
  ps_type_set_function_params(
      function_type, request->parameter_types,
      request->parameter_count, request->is_variadic);
  plan->function_type = function_type;
  return 1;
}
