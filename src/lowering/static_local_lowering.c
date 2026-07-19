#include "static_local_lowering.h"

#include "static_data_initializer.h"
#include "runtime_context.h"
#include "../parser/global_registry.h"
#include "../parser/local_registry.h"
#include "../parser/node_utils.h"
#include "../parser/symtab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void psx_static_local_lowering_reset_in(
    psx_lowering_context_t *lowering_context) {
  if (!lowering_context) return;
  memset(lowering_context->static_local_sequences, 0,
         sizeof(lowering_context->static_local_sequences));
}

int psx_static_local_prepare_global(
    psx_global_registry_t *global_registry, global_var_t *global,
    const psx_type_t *type) {
  if (!global || !type) return 0;
  global->is_static = 1;
  if (ps_gvar_get_decl_type(global)) return 1;
  return ps_global_registry_bind_decl_type(
      global_registry, global, type);
}

static char *mangle_static_local_name(
    psx_lowering_context_t *lowering_context,
    psx_static_local_kind_t kind,
    const char *function_name, int function_name_len,
    const char *name, int name_len, int *out_len) {
  static const char *const prefixes[PSX_STATIC_LOCAL_KIND_COUNT] = {
      "", "a", "ac", "s", "sa",
  };
  if (!lowering_context || kind < 0 ||
      kind >= PSX_STATIC_LOCAL_KIND_COUNT) return NULL;
  const char *prefix = prefixes[kind];
  const char *function = function_name && function_name_len > 0
                             ? function_name : "anon";
  int function_len = function_name && function_name_len > 0
                         ? function_name_len : 4;
  char sequence[16];
  int sequence_len = snprintf(
      sequence, sizeof(sequence), "%s%d", prefix,
      lowering_context->static_local_sequences[kind]++);
  int total_len = function_len + 1 + name_len + 1 + sequence_len;
  char *mangled = malloc((size_t)total_len + 1);
  if (!mangled) return NULL;
  int offset = 0;
  memcpy(mangled + offset, function, (size_t)function_len);
  offset += function_len;
  mangled[offset++] = '.';
  memcpy(mangled + offset, name, (size_t)name_len);
  offset += name_len;
  mangled[offset++] = '.';
  memcpy(mangled + offset, sequence, (size_t)sequence_len);
  offset += sequence_len;
  mangled[offset] = '\0';
  if (out_len) *out_len = total_len;
  return mangled;
}

lvar_t *lower_static_local_object(
    const psx_static_local_object_request_t *request) {
  if (!request || !request->name || request->name_len <= 0 ||
      !request->global || !request->type || !request->global_registry ||
      !request->local_registry || !request->lowering_context)
    return NULL;
  int mangled_len = 0;
  char *mangled = mangle_static_local_name(
      request->lowering_context, request->kind,
      request->function_name, request->function_name_len,
      request->name, request->name_len, &mangled_len);
  if (!mangled) return NULL;

  global_var_t *global = request->global;
  if (!psx_static_local_prepare_global(
          request->global_registry, global, request->type)) {
    free(mangled);
    return NULL;
  }
  global->name = mangled;
  global->name_len = mangled_len;
  global->is_compiler_generated = 1;
  ps_register_global_var_in(request->global_registry, global);

  lvar_t *alias = ps_local_registry_create_static_alias_in(
      request->local_registry,
      global,
      request->name, request->name_len, mangled, mangled_len,
      request->type);
  if (!alias) return NULL;
  return alias;
}

int lower_static_local_declaration_storage(
    const psx_static_local_declaration_request_t *request,
    psx_static_local_declaration_result_t *result) {
  if (result) *result = (psx_static_local_declaration_result_t){0};
  if (!request || !request->name || request->name_len <= 0 ||
      !request->type || !request->global_registry ||
      !request->local_registry || !request->lowering_context)
    return 0;

  global_var_t *global = calloc(1, sizeof(*global));
  if (!global) return 0;
  if (!psx_static_local_prepare_global(
          request->global_registry, global, request->type)) {
    free(global);
    return 0;
  }

  lvar_t *alias = lower_static_local_object(
      &(psx_static_local_object_request_t){
          .global_registry = request->global_registry,
          .local_registry = request->local_registry,
          .lowering_context = request->lowering_context,
          .kind = request->kind,
          .function_name = request->function_name,
          .function_name_len = request->function_name_len,
          .name = request->name,
          .name_len = request->name_len,
          .global = global,
          .type = ps_gvar_get_decl_type(global),
      });
  if (!alias) return 0;
  if (result) {
    result->global = global;
    result->alias = alias;
  }
  return 1;
}

int lower_static_local_declaration_initializer(
    psx_global_registry_t *global_registry,
    psx_lowering_context_t *lowering_context, global_var_t *global,
    const psx_static_initializer_resolution_t *resolution,
    token_t *diag_tok, int *type_completed) {
  if (type_completed) *type_completed = 0;
  if (!global || !resolution) return 0;
  psx_static_declaration_initializer_result_t initializer_result = {0};
  if (!lower_resolved_static_initializer(
          global_registry, lowering_context, global,
          resolution, diag_tok,
          &initializer_result)) {
    return 0;
  }
  if (type_completed)
    *type_completed = initializer_result.type_completed;
  return 1;
}

int lower_static_local_declaration(
    const psx_static_local_declaration_request_t *request,
    psx_static_local_declaration_result_t *result) {
  psx_static_local_declaration_result_t lowered = {0};
  if (!lower_static_local_declaration_storage(request, &lowered)) return 0;
  if (request->initializer_resolution &&
      !lower_static_local_declaration_initializer(
          request->global_registry, request->lowering_context,
          lowered.global,
          request->initializer_resolution,
          request->diag_tok, &lowered.type_completed)) {
    return 0;
  }
  if (lowered.type_completed &&
      !ps_local_registry_complete_array_type(
          request->local_registry, lowered.alias,
          psx_semantic_type_table_lookup_qual_type(
              ps_lowering_semantic_types(request->lowering_context),
              request->initializer_resolution->object_qual_type))) {
    return 0;
  }
  if (result) *result = lowered;
  return 1;
}
