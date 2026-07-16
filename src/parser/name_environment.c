#include "name_environment.h"

#include <stdlib.h>
#include <string.h>

struct psx_parser_name_entry_t {
  psx_parser_name_entry_t *next;
  char *name;
  int name_len;
  int scope_depth;
  unsigned char is_typedef_name;
};

static void clear_entries(
    psx_parser_name_environment_t *environment) {
  if (!environment) return;
  psx_parser_name_entry_t *entry = environment->entries;
  while (entry) {
    psx_parser_name_entry_t *next = entry->next;
    free(entry);
    entry = next;
  }
  environment->entries = NULL;
}

void ps_parser_name_environment_init(
    psx_parser_name_environment_t *environment,
    psx_name_classifier_t outer_names) {
  if (!environment) return;
  *environment = (psx_parser_name_environment_t){
      .outer_names = outer_names,
  };
}

void ps_parser_name_environment_reset(
    psx_parser_name_environment_t *environment,
    psx_name_classifier_t outer_names) {
  if (!environment) return;
  clear_entries(environment);
  environment->outer_names = outer_names;
  environment->scope_depth = 0;
}

void ps_parser_name_environment_dispose(
    psx_parser_name_environment_t *environment) {
  if (!environment) return;
  clear_entries(environment);
  *environment = (psx_parser_name_environment_t){0};
}

static int environment_is_typedef_name(
    void *context, const token_t *token) {
  psx_parser_name_environment_t *environment = context;
  if (!environment || !token || token->kind != TK_IDENT) return 0;
  const token_ident_t *identifier = (const token_ident_t *)token;
  for (psx_parser_name_entry_t *entry = environment->entries;
       entry; entry = entry->next) {
    if (entry->name_len == identifier->len &&
        memcmp(
            entry->name, identifier->str,
            (size_t)identifier->len) == 0)
      return entry->is_typedef_name ? 1 : 0;
  }
  return ps_name_classifier_is_typedef_name(
      &environment->outer_names, token);
}

static void environment_declare_name(
    void *context, const token_t *token, int is_typedef_name) {
  psx_parser_name_environment_t *environment = context;
  if (!environment || !token || token->kind != TK_IDENT) return;
  const token_ident_t *identifier = (const token_ident_t *)token;
  psx_parser_name_entry_t *entry = calloc(1, sizeof(*entry));
  if (!entry) return;
  *entry = (psx_parser_name_entry_t){
      .next = environment->entries,
      .name = identifier->str,
      .name_len = identifier->len,
      .scope_depth = environment->scope_depth,
      .is_typedef_name = is_typedef_name ? 1 : 0,
  };
  environment->entries = entry;
}

static void environment_enter_scope(void *context) {
  psx_parser_name_environment_t *environment = context;
  if (environment) environment->scope_depth++;
}

static void environment_leave_scope(void *context) {
  psx_parser_name_environment_t *environment = context;
  if (!environment || environment->scope_depth <= 0) return;
  while (environment->entries &&
         environment->entries->scope_depth >=
             environment->scope_depth) {
    psx_parser_name_entry_t *entry = environment->entries;
    environment->entries = entry->next;
    free(entry);
  }
  environment->scope_depth--;
}

psx_name_classifier_t ps_parser_name_environment_classifier(
    psx_parser_name_environment_t *environment) {
  return (psx_name_classifier_t){
      .context = environment,
      .is_typedef_name = environment_is_typedef_name,
      .declare_name = environment_declare_name,
      .enter_scope = environment_enter_scope,
      .leave_scope = environment_leave_scope,
  };
}
