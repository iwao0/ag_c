#include "global_registry.h"
#include "literal_public.h"
#include "symtab.h"
#include "type.h"
#include "type_builder.h"
#include "../semantic/type_identity.h"
#include <stdlib.h>
#include <string.h>

#define GVAR_HASH_BUCKETS 256u

struct psx_global_registry_t {
  const psx_semantic_type_table_t *semantic_types;
  string_lit_t *string_literals;
  float_lit_t *float_literals;
  global_var_t *global_vars;
  int next_string_literal_id;
  int next_float_literal_id;
  global_var_t *gvars_by_bucket[GVAR_HASH_BUCKETS];
};

psx_global_registry_t *ps_global_registry_create(void) {
  return calloc(1, sizeof(psx_global_registry_t));
}

void ps_global_registry_destroy(psx_global_registry_t *registry) {
  if (!registry) return;
  free(registry);
}

void ps_global_registry_bind_semantic_types(
    psx_global_registry_t *registry,
    const psx_semantic_type_table_t *semantic_types) {
  if (registry) registry->semantic_types = semantic_types;
}

static psx_type_id_t global_decl_type_id(
    const psx_global_registry_t *registry, const psx_type_t *type) {
  return psx_semantic_type_table_find(
      registry ? registry->semantic_types : NULL, type).type_id;
}

int ps_global_registry_bind_decl_type(
    psx_global_registry_t *registry, global_var_t *global,
    const psx_type_t *type) {
  if (!global || global->decl_type || !type) return 0;
  global->decl_type = ps_type_clone_persistent(type);
  global->decl_type_id = global_decl_type_id(registry, type);
  return global->decl_type != NULL;
}

int ps_global_registry_complete_array_type(
    psx_global_registry_t *registry, global_var_t *global,
    const psx_type_t *complete_type) {
  const psx_type_t *current = global ? global->decl_type : NULL;
  if (!ps_type_is_incomplete_array(current) || !complete_type ||
      complete_type->kind != PSX_TYPE_ARRAY ||
      complete_type->array_len <= 0 || complete_type->is_vla ||
      !current->base || !complete_type->base ||
      !ps_type_shape_matches(current->base, complete_type->base)) {
    return 0;
  }
  psx_type_t *replacement = ps_type_clone_persistent(complete_type);
  if (!replacement) return 0;
  global->decl_type = replacement;
  global->decl_type_id = global_decl_type_id(registry, complete_type);
  return 1;
}

static unsigned gvar_name_hash(const char *name, int len) {
  unsigned h = 2166136261u;
  for (int i = 0; i < len; i++)
    h = (h ^ (unsigned char)name[i]) * 16777619u;
  return h & (GVAR_HASH_BUCKETS - 1u);
}

void ps_register_global_var_in(
    psx_global_registry_t *registry, global_var_t *gv) {
  if (!registry || !gv) return;
  gv->next = registry->global_vars;
  registry->global_vars = gv;
  unsigned h = gvar_name_hash(gv->name, gv->name_len);
  gv->next_hash = registry->gvars_by_bucket[h];
  registry->gvars_by_bucket[h] = gv;
}

void psx_register_string_lit_in(
    psx_global_registry_t *registry, string_lit_t *lit) {
  if (!registry || !lit) return;
  lit->next = registry->string_literals;
  registry->string_literals = lit;
}

void psx_register_float_lit_in(
    psx_global_registry_t *registry, float_lit_t *lit) {
  if (!registry || !lit) return;
  lit->next = registry->float_literals;
  registry->float_literals = lit;
}

global_var_t *ps_find_global_var_in(
    psx_global_registry_t *registry, char *name, int len) {
  if (!registry || !name || len <= 0) return NULL;
  unsigned h = gvar_name_hash(name, len);
  for (global_var_t *gv = registry->gvars_by_bucket[h];
       gv; gv = gv->next_hash) {
    if (gv->name_len == len && memcmp(gv->name, name, (size_t)len) == 0)
      return gv;
  }
  return NULL;
}

string_lit_t *ps_find_string_lit_by_label_in(
    psx_global_registry_t *registry, char *label) {
  if (!registry || !label) return NULL;
  for (string_lit_t *lit = registry->string_literals;
       lit; lit = lit->next) {
    if (strcmp(lit->label, label) == 0) return lit;
  }
  return NULL;
}

int ps_global_registry_next_string_literal_id(
    psx_global_registry_t *registry) {
  return registry ? registry->next_string_literal_id++ : -1;
}

int ps_global_registry_next_float_literal_id(
    psx_global_registry_t *registry) {
  return registry ? registry->next_float_literal_id++ : -1;
}

void ps_iter_globals_in(
    psx_global_registry_t *registry,
    global_var_visitor_t fn, void *user) {
  if (!registry || !fn) return;
  for (global_var_t *gv = registry->global_vars; gv; gv = gv->next)
    fn(gv, user);
}

bool ps_iter_string_literals_in(
    psx_global_registry_t *registry,
    string_lit_visitor_t fn, void *user) {
  if (!registry || !registry->string_literals || !fn) return false;
  for (string_lit_t *lit = registry->string_literals;
       lit; lit = lit->next) fn(lit, user);
  return true;
}

bool ps_iter_float_literals_in(
    psx_global_registry_t *registry,
    float_lit_visitor_t fn, void *user) {
  if (!registry || !registry->float_literals || !fn) return false;
  for (float_lit_t *lit = registry->float_literals;
       lit; lit = lit->next) fn(lit, user);
  return true;
}

void ps_global_registry_reset_translation_unit_in(
    psx_global_registry_t *registry) {
  if (!registry) return;
  registry->global_vars = NULL;
  registry->string_literals = NULL;
  registry->float_literals = NULL;
  registry->next_string_literal_id = 0;
  registry->next_float_literal_id = 0;
  memset(registry->gvars_by_bucket, 0,
         sizeof(registry->gvars_by_bucket));
}

void ps_global_registry_reset_diag_state_in(
    psx_global_registry_t *registry) {
  if (!registry) return;
  for (global_var_t *gv = registry->global_vars; gv; gv = gv->next)
    gv->has_init = 0;
}
