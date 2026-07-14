#include "function_declaration_plan.h"

#include <string.h>

int psx_plan_function_declaration(
    const psx_function_declaration_request_t *request,
    psx_function_declaration_plan_t *plan) {
  if (!request || !plan || !request->function_type ||
      request->function_type->kind != PSX_TYPE_FUNCTION ||
      !request->function_type->base ||
      !ps_type_is_well_formed(request->function_type)) {
    return 0;
  }
  memset(plan, 0, sizeof(*plan));
  plan->function_type = ps_type_clone(request->function_type);
  return plan->function_type != NULL;
}
