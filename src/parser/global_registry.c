#include "global_registry.h"
#include "literal_public.h"
#include "symtab.h"
#include "type.h"
#include "type_builder.h"
#include <stdlib.h>
#include <string.h>

#define GVAR_HASH_BUCKETS 256u

struct psx_global_registry_t {
  string_lit_t *string_literals;
  float_lit_t *float_literals;
  global_var_t *global_vars;
  global_var_t *gvars_by_bucket[GVAR_HASH_BUCKETS];
};

static psx_global_registry_t default_global_registry;
static psx_global_registry_t *active_global_registry =
    &default_global_registry;

psx_global_registry_t *ps_global_registry_create(void) {
  return calloc(1, sizeof(psx_global_registry_t));
}

void ps_global_registry_destroy(psx_global_registry_t *registry) {
  if (!registry || registry == &default_global_registry) return;
  if (active_global_registry == registry)
    active_global_registry = &default_global_registry;
  free(registry);
}

psx_global_registry_t *ps_global_registry_activate(
    psx_global_registry_t *registry) {
  psx_global_registry_t *previous = active_global_registry;
  active_global_registry = registry ? registry : &default_global_registry;
  return previous;
}

psx_global_registry_t *ps_global_registry_active(void) {
  return active_global_registry;
}

int ps_global_registry_bind_decl_type(
    global_var_t *global, const psx_type_t *type) {
  if (!global || global->decl_type || !type) return 0;
  global->decl_type = ps_type_clone_persistent(type);
  return global->decl_type != NULL;
}

int ps_global_registry_complete_array_type(
    global_var_t *global, const psx_type_t *complete_type) {
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

void ps_register_global_var(global_var_t *gv) {
  ps_register_global_var_in(active_global_registry, gv);
}

void psx_register_string_lit_in(
    psx_global_registry_t *registry, string_lit_t *lit) {
  if (!registry || !lit) return;
  lit->next = registry->string_literals;
  registry->string_literals = lit;
}

void psx_register_string_lit(string_lit_t *lit) {
  psx_register_string_lit_in(active_global_registry, lit);
}

void psx_register_float_lit_in(
    psx_global_registry_t *registry, float_lit_t *lit) {
  if (!registry || !lit) return;
  lit->next = registry->float_literals;
  registry->float_literals = lit;
}

void psx_register_float_lit(float_lit_t *lit) {
  psx_register_float_lit_in(active_global_registry, lit);
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

global_var_t *ps_find_global_var(char *name, int len) {
  return ps_find_global_var_in(active_global_registry, name, len);
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

string_lit_t *ps_find_string_lit_by_label(char *label) {
  return ps_find_string_lit_by_label_in(active_global_registry, label);
}

void ps_iter_globals_in(
    psx_global_registry_t *registry,
    global_var_visitor_t fn, void *user) {
  if (!registry || !fn) return;
  for (global_var_t *gv = registry->global_vars; gv; gv = gv->next)
    fn(gv, user);
}

void ps_iter_globals(global_var_visitor_t fn, void *user) {
  ps_iter_globals_in(active_global_registry, fn, user);
}

bool ps_iter_string_literals(string_lit_visitor_t fn, void *user) {
  if (!active_global_registry->string_literals) return false;
  for (string_lit_t *lit = active_global_registry->string_literals;
       lit; lit = lit->next) fn(lit, user);
  return true;
}

bool ps_iter_float_literals(float_lit_visitor_t fn, void *user) {
  if (!active_global_registry->float_literals) return false;
  for (float_lit_t *lit = active_global_registry->float_literals;
       lit; lit = lit->next) fn(lit, user);
  return true;
}

bool ps_has_string_literals(void) {
  return active_global_registry->string_literals != NULL;
}
bool ps_has_float_literals(void) {
  return active_global_registry->float_literals != NULL;
}

void ps_global_registry_reset_translation_unit_in(
    psx_global_registry_t *registry) {
  if (!registry) return;
  registry->global_vars = NULL;
  registry->string_literals = NULL;
  registry->float_literals = NULL;
  memset(registry->gvars_by_bucket, 0,
         sizeof(registry->gvars_by_bucket));
}

void ps_global_registry_reset_translation_unit(void) {
  ps_global_registry_reset_translation_unit_in(active_global_registry);
}

void ps_global_registry_reset_diag_state_in(
    psx_global_registry_t *registry) {
  if (!registry) return;
  for (global_var_t *gv = registry->global_vars; gv; gv = gv->next)
    gv->has_init = 0;
}

void ps_global_registry_reset_diag_state(void) {
  ps_global_registry_reset_diag_state_in(active_global_registry);
}
