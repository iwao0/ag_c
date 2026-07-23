#include "global_object_lowering.h"

#include "../parser/global_registry.h"
#include "../parser/symtab.h"
#include <stdlib.h>
#include <string.h>

int lower_resolved_global_object_declaration(
    const psx_resolved_global_object_request_t *request,
    psx_global_object_result_t *result) {
  if (!request || !result || !request->global_registry || !request->name ||
      request->name_len <= 0 || !request->resolution ||
      request->resolution->status != PSX_GLOBAL_DECLARATION_OK) return 0;
  memset(result, 0, sizeof(*result));
  psx_global_registry_t *global_registry = request->global_registry;

  global_var_t *existing = request->resolution->existing;
  if (existing) {
    if (!psx_global_registry_note_global_mutation(
            global_registry, existing))
      return 0;
    if (request->resolution->clear_existing_extern)
      existing->is_extern_decl = 0;
    if (request->resolution->complete_existing_array &&
        !ps_global_registry_complete_array_qual_type(
            global_registry, existing,
            request->resolution->declaration_qual_type))
      return 0;
    if (request->resolution->adopt_composite_type &&
        !ps_global_registry_adopt_composite_qual_type(
            global_registry, existing,
            request->resolution->declaration_qual_type))
      return 0;
    result->global = existing;
    return 1;
  }

  global_var_t *global = calloc(1, sizeof(*global));
  if (!global) return 0;
  global->name_len = request->name_len;
  global->is_extern_decl = request->is_extern_decl ? 1 : 0;
  global->is_static = request->is_extern_decl ? 0
                                               : (request->is_static ? 1 : 0);
  global->is_compiler_generated =
      request->is_compiler_generated ? 1 : 0;
  global->is_compound_literal =
      request->is_compound_literal ? 1 : 0;
  if (!ps_global_registry_bind_decl_qual_type(
          global_registry, global,
          request->resolution->declaration_qual_type)) {
    free(global);
    return 0;
  }
  global->name = ps_global_registry_copy_name_in(
      global_registry, request->name, request->name_len);
  if (!global->name) {
    free(global);
    return 0;
  }
  ps_register_global_var_in(global_registry, global);
  result->global = global;
  result->created = 1;
  return 1;
}

int lower_resolved_global_declaration_initializer(
    psx_global_registry_t *global_registry,
    psx_lowering_context_t *lowering_context, global_var_t *global,
    const psx_static_initializer_lowering_input_t *initializer) {
  return lower_resolved_static_initializer(
      global_registry, lowering_context, global,
      initializer, NULL);
}
