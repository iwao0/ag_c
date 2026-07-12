#include "global_registry.h"
#include "parser_public.h"
#include "symtab.h"
#include <string.h>

static string_lit_t *string_literals;
static float_lit_t *float_literals;
static global_var_t *global_vars;

#define GVAR_HASH_BUCKETS 256u
static global_var_t *gvars_by_bucket[GVAR_HASH_BUCKETS];

static unsigned gvar_name_hash(const char *name, int len) {
  unsigned h = 2166136261u;
  for (int i = 0; i < len; i++)
    h = (h ^ (unsigned char)name[i]) * 16777619u;
  return h & (GVAR_HASH_BUCKETS - 1u);
}

void ps_register_global_var(global_var_t *gv) {
  gv->next = global_vars;
  global_vars = gv;
  unsigned h = gvar_name_hash(gv->name, gv->name_len);
  gv->next_hash = gvars_by_bucket[h];
  gvars_by_bucket[h] = gv;
}

void psx_register_string_lit(string_lit_t *lit) {
  lit->next = string_literals;
  string_literals = lit;
}

void psx_register_float_lit(float_lit_t *lit) {
  lit->next = float_literals;
  float_literals = lit;
}

global_var_t *ps_find_global_var(char *name, int len) {
  unsigned h = gvar_name_hash(name, len);
  for (global_var_t *gv = gvars_by_bucket[h]; gv; gv = gv->next_hash) {
    if (gv->name_len == len && memcmp(gv->name, name, (size_t)len) == 0)
      return gv;
  }
  return NULL;
}

string_lit_t *ps_find_string_lit_by_label(char *label) {
  if (!label) return NULL;
  for (string_lit_t *lit = string_literals; lit; lit = lit->next) {
    if (strcmp(lit->label, label) == 0) return lit;
  }
  return NULL;
}

void ps_iter_globals(global_var_visitor_t fn, void *user) {
  for (global_var_t *gv = global_vars; gv; gv = gv->next) fn(gv, user);
}

bool ps_iter_string_literals(string_lit_visitor_t fn, void *user) {
  if (!string_literals) return false;
  for (string_lit_t *lit = string_literals; lit; lit = lit->next) fn(lit, user);
  return true;
}

bool ps_iter_float_literals(float_lit_visitor_t fn, void *user) {
  if (!float_literals) return false;
  for (float_lit_t *lit = float_literals; lit; lit = lit->next) fn(lit, user);
  return true;
}

bool ps_has_string_literals(void) { return string_literals != NULL; }
bool ps_has_float_literals(void) { return float_literals != NULL; }

void ps_global_registry_reset_translation_unit(void) {
  global_vars = NULL;
  string_literals = NULL;
  float_literals = NULL;
  memset(gvars_by_bucket, 0, sizeof(gvars_by_bucket));
}

void psx_global_registry_reset_diag_state(void) {
  for (global_var_t *gv = global_vars; gv; gv = gv->next) gv->has_init = 0;
}
