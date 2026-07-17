#include "identifier_resolution.h"

#include "../parser/decl.h"
#include "../parser/global_registry.h"
#include "../parser/gvar_public.h"
#include "../parser/lvar_public.h"
#include "../parser/semantic_ctx.h"

#include <string.h>

global_var_t *psx_resolve_global_object_symbol_in(
    psx_global_registry_t *global_registry,
    char *name, int name_len) {
  if (!name || name_len <= 0) return NULL;
  return ps_find_global_var_in(global_registry, name, name_len);
}

void psx_resolve_identifier(
    const psx_identifier_resolution_request_t *request,
    psx_identifier_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  if (!request || !request->semantic_context || !request->global_registry ||
      !request->local_registry || !request->name || request->name_len <= 0) {
    return;
  }
  psx_semantic_context_t *semantic_context = request->semantic_context;
  psx_local_registry_t *local_registry = request->local_registry;
  psx_global_registry_t *global_registry = request->global_registry;

  resolution->local = request->has_local_lookup_point
      ? ps_local_registry_find_visible_in(
            local_registry,
            request->name, request->name_len,
            request->local_lookup_point)
      : ps_decl_find_lvar_in(
            local_registry, request->name, request->name_len);
  if (resolution->local) {
    resolution->kind = PSX_IDENTIFIER_LOCAL;
    return;
  }
  int has_enum = request->has_local_lookup_point
      ? ps_ctx_find_enum_const_at_in_contexts(
            semantic_context, local_registry,
            request->name, request->name_len,
            request->local_lookup_point, &resolution->enum_value)
      : ps_ctx_find_enum_const_in(
            semantic_context,
            request->name, request->name_len,
            &resolution->enum_value);
  if (has_enum) {
    resolution->kind = PSX_IDENTIFIER_ENUM_CONSTANT;
    return;
  }

  resolution->global = psx_resolve_global_object_symbol_in(
      global_registry, request->name, request->name_len);
  const psx_function_symbol_t *function =
      ps_ctx_find_function_symbol_in(
          semantic_context,
          request->name, request->name_len);
  if (request->is_call) {
    if (resolution->global) {
      resolution->kind = PSX_IDENTIFIER_GLOBAL_OBJECT;
      return;
    }
    if (!function) {
      resolution->kind = PSX_IDENTIFIER_UNDECLARED_CALL;
      return;
    }
    resolution->kind = PSX_IDENTIFIER_FUNCTION;
    resolution->function = function;
    return;
  }

  if (function) {
    resolution->kind = PSX_IDENTIFIER_FUNCTION;
    resolution->function = function;
    return;
  }
  if (resolution->global) {
    resolution->kind = PSX_IDENTIFIER_GLOBAL_OBJECT;
    return;
  }
  resolution->kind = PSX_IDENTIFIER_UNDEFINED;
}

static psx_qual_type_t invalid_qual_type(void) {
  return (psx_qual_type_t){
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
}

void psx_resolve_identifier_expression(
    const psx_identifier_resolution_request_t *request,
    psx_identifier_expression_resolution_t *resolution) {
  if (!resolution) return;
  *resolution = (psx_identifier_expression_resolution_t){
      .declaration_qual_type = invalid_qual_type(),
      .expression_qual_type = invalid_qual_type(),
  };
  if (!request || !request->semantic_context) return;

  psx_resolve_identifier(request, &resolution->symbol);
  psx_semantic_context_t *semantic_context =
      request->semantic_context;
  switch (resolution->symbol.kind) {
    case PSX_IDENTIFIER_ENUM_CONSTANT: {
      resolution->declaration_qual_type =
          ps_ctx_intern_integer_qual_type_in(
              semantic_context, PSX_INTEGER_KIND_INT, 0, 0);
      break;
    }
    case PSX_IDENTIFIER_LOCAL:
      resolution->declaration_qual_type =
          ps_lvar_decl_qual_type(resolution->symbol.local);
      break;
    case PSX_IDENTIFIER_GLOBAL_OBJECT:
      resolution->declaration_qual_type =
          ps_gvar_decl_qual_type(resolution->symbol.global);
      break;
    case PSX_IDENTIFIER_FUNCTION:
      resolution->declaration_qual_type =
          ps_ctx_intern_qual_type_in(
              semantic_context,
              ps_function_symbol_type(
                  resolution->symbol.function));
      break;
    case PSX_IDENTIFIER_UNDECLARED_CALL:
    case PSX_IDENTIFIER_UNDEFINED:
      return;
  }

  psx_qual_type_t declared = resolution->declaration_qual_type;
  const psx_type_t *declared_type = ps_ctx_type_by_id_in(
      semantic_context, declared.type_id);
  if (!declared_type) {
    resolution->declaration_qual_type = invalid_qual_type();
    return;
  }
  if (resolution->symbol.kind == PSX_IDENTIFIER_FUNCTION) {
    resolution->expression_qual_type =
        ps_ctx_intern_pointer_to_qual_type_in(
            semantic_context, declared);
    resolution->decays_function_to_pointer = 1;
    return;
  }
  if (declared_type->kind == PSX_TYPE_ARRAY) {
    psx_qual_type_t element = psx_semantic_type_table_base(
        ps_ctx_semantic_type_table_in(semantic_context),
        declared.type_id);
    resolution->expression_qual_type =
        ps_ctx_intern_pointer_to_qual_type_in(
            semantic_context, element);
    resolution->decays_array_to_address = 1;
    return;
  }
  resolution->expression_qual_type = declared;
}
