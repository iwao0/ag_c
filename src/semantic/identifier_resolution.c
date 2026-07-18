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

static psx_scope_graph_t *shared_identifier_scope_graph(
    const psx_identifier_resolution_request_t *request) {
  if (!request) return NULL;
  psx_scope_graph_t *graph = ps_ctx_scope_graph(
      request->semantic_context);
  return graph &&
                 graph == ps_local_registry_scope_graph(
                              request->local_registry) &&
                 graph == ps_global_registry_scope_graph(
                              request->global_registry)
             ? graph
             : NULL;
}

static int resolve_identifier_from_scope_graph(
    const psx_identifier_resolution_request_t *request,
    psx_identifier_resolution_t *resolution) {
  psx_scope_graph_t *graph = shared_identifier_scope_graph(request);
  if (!graph) return 0;
  int has_valid_lookup_point =
      request->has_local_lookup_point &&
      request->local_lookup_point.scope_seq != PSX_SCOPE_ID_INVALID;
  psx_scope_lookup_point_t point = has_valid_lookup_point
      ? (psx_scope_lookup_point_t){
            .scope_id = request->local_lookup_point.scope_seq,
            .declaration_order =
                request->local_lookup_point.declaration_seq,
        }
      : psx_scope_graph_capture_lookup_point(graph);
  psx_decl_id_t id = psx_scope_graph_lookup(
      graph, PSX_NAMESPACE_ORDINARY,
      request->name, request->name_len, point);
  const psx_scope_declaration_t *declaration =
      psx_scope_graph_declaration(graph, id);
  if (!declaration) {
    resolution->kind = request->is_call
                           ? PSX_IDENTIFIER_UNDECLARED_CALL
                           : PSX_IDENTIFIER_UNDEFINED;
    return 1;
  }
  switch (declaration->kind) {
    case PSX_DECL_LOCAL_OBJECT:
      resolution->kind = PSX_IDENTIFIER_LOCAL;
      resolution->local = declaration->payload;
      return 1;
    case PSX_DECL_GLOBAL_OBJECT:
      resolution->kind = PSX_IDENTIFIER_GLOBAL_OBJECT;
      resolution->global = declaration->payload;
      return 1;
    case PSX_DECL_FUNCTION:
      resolution->kind = PSX_IDENTIFIER_FUNCTION;
      resolution->function = declaration->payload;
      return 1;
    case PSX_DECL_ENUM_CONSTANT:
      if (ps_ctx_enum_const_value_by_declaration_id_in(
              request->semantic_context, id,
              &resolution->enum_value)) {
        resolution->kind = PSX_IDENTIFIER_ENUM_CONSTANT;
        return 1;
      }
      break;
    default:
      break;
  }
  resolution->kind = request->is_call
                         ? PSX_IDENTIFIER_UNDECLARED_CALL
                         : PSX_IDENTIFIER_UNDEFINED;
  return 1;
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

  if (!request->is_call && request->name_len == 13 &&
      memcmp(request->name, "__va_arg_area", 13) == 0) {
    resolution->kind = PSX_IDENTIFIER_BUILTIN_VA_ARG_AREA;
    return;
  }

  if (resolve_identifier_from_scope_graph(request, resolution)) return;

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
      resolution->static_storage_global =
          ps_lvar_static_storage_global(resolution->symbol.local);
      resolution->local_has_static_storage =
          ps_lvar_is_static_local(resolution->symbol.local);
      resolution->local_is_vla =
          ps_lvar_is_vla(resolution->symbol.local);
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
    case PSX_IDENTIFIER_BUILTIN_VA_ARG_AREA: {
      resolution->declaration_qual_type =
          ps_ctx_intern_pointer_to_qual_type_in(
              semantic_context,
              ps_ctx_intern_void_qual_type_in(
                  semantic_context));
      break;
    }
    case PSX_IDENTIFIER_UNDECLARED_CALL: {
      if (!request->is_call) return;
      resolution->declaration_qual_type =
          ps_ctx_intern_implicit_function_qual_type_in(
              semantic_context);
      break;
    }
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
  resolution->local_is_vla_object =
      resolution->symbol.kind == PSX_IDENTIFIER_LOCAL &&
      resolution->local_is_vla &&
      declared_type->kind == PSX_TYPE_ARRAY;
  if (resolution->symbol.kind == PSX_IDENTIFIER_FUNCTION ||
      resolution->symbol.kind == PSX_IDENTIFIER_UNDECLARED_CALL) {
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
    resolution->decays_array_to_address =
        !resolution->local_is_vla;
    return;
  }
  resolution->expression_qual_type = declared;
}
