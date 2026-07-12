#include "static_local_lowering.h"
#include "../semantic/local_type_state.h"

#include "static_data_initializer.h"
#include "../parser/local_registry.h"
#include "../parser/node_utils.h"
#include "../parser/symtab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int object_sequences[PSX_STATIC_LOCAL_KIND_COUNT];

void psx_static_local_lowering_reset(void) {
  memset(object_sequences, 0, sizeof(object_sequences));
}

void psx_static_local_prepare_global(global_var_t *global,
                                     const psx_type_t *type) {
  if (!global || !type) return;
  global->is_static = 1;
  if (!global->decl_type) ps_decl_set_gvar_decl_type(global, type);
}

static char *mangle_static_local_name(
    psx_static_local_kind_t kind,
    const char *function_name, int function_name_len,
    const char *name, int name_len, int *out_len) {
  static const char *const prefixes[PSX_STATIC_LOCAL_KIND_COUNT] = {
      "", "a", "ac", "s", "sa",
  };
  if (kind < 0 || kind >= PSX_STATIC_LOCAL_KIND_COUNT) return NULL;
  const char *prefix = prefixes[kind];
  const char *function = function_name && function_name_len > 0
                             ? function_name : "anon";
  int function_len = function_name && function_name_len > 0
                         ? function_name_len : 4;
  char sequence[16];
  int sequence_len = snprintf(
      sequence, sizeof(sequence), "%s%d", prefix, object_sequences[kind]++);
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
      !request->global || !request->type || request->alias_element_size <= 0)
    return NULL;
  int mangled_len = 0;
  char *mangled = mangle_static_local_name(
      request->kind, request->function_name, request->function_name_len,
      request->name, request->name_len, &mangled_len);
  if (!mangled) return NULL;

  global_var_t *global = request->global;
  psx_static_local_prepare_global(global, request->type);
  global->name = mangled;
  global->name_len = mangled_len;
  ps_register_global_var(global);

  lvar_t *alias = calloc(1, sizeof(*alias));
  if (!alias) return NULL;
  alias->name = request->name;
  alias->len = request->name_len;
  alias->size = request->alias_size;
  alias->elem_size = request->alias_element_size;
  alias->is_static_local = 1;
  alias->static_global_name = mangled;
  alias->static_global_name_len = mangled_len;
  ps_decl_attach_lvar_current_region(alias);
  psx_decl_set_lvar_decl_type(alias, request->type);
  ps_local_registry_add(alias);
  return alias;
}

int lower_static_local_declaration_storage(
    const psx_static_local_declaration_request_t *request,
    psx_static_local_declaration_result_t *result) {
  if (result) *result = (psx_static_local_declaration_result_t){0};
  if (!request || !request->name || request->name_len <= 0 ||
      !request->type || request->alias_element_size <= 0)
    return 0;

  global_var_t *global = calloc(1, sizeof(*global));
  if (!global) return 0;
  global->type_size = ps_type_sizeof(request->type);
  psx_static_local_prepare_global(global, request->type);

  lvar_t *alias = lower_static_local_object(
      &(psx_static_local_object_request_t){
          .kind = request->kind,
          .function_name = request->function_name,
          .function_name_len = request->function_name_len,
          .name = request->name,
          .name_len = request->name_len,
          .global = global,
          .alias_size = request->alias_size,
          .alias_element_size = request->alias_element_size,
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
    global_var_t *global,
    const psx_static_initializer_resolution_t *resolution,
    token_t *diag_tok, int *type_completed) {
  if (type_completed) *type_completed = 0;
  if (!global || !resolution) return 0;
  psx_static_declaration_initializer_result_t initializer_result = {0};
  if (!lower_resolved_static_initializer(
          global, resolution, diag_tok, &initializer_result)) {
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
          lowered.global, request->initializer_resolution,
          request->diag_tok, &lowered.type_completed)) {
    return 0;
  }
  if (result) *result = lowered;
  return 1;
}
