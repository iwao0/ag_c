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

static int ensure_scope_capacity(
    psx_parser_name_environment_t *environment, int needed) {
  if (needed <= environment->scope_stack_capacity) return 1;
  int capacity = environment->scope_stack_capacity
                     ? environment->scope_stack_capacity * 2 : 16;
  while (capacity < needed) capacity *= 2;
  unsigned *stack = realloc(
      environment->scope_stack,
      (size_t)capacity * sizeof(*stack));
  if (!stack) return 0;
  environment->scope_stack = stack;
  environment->scope_stack_capacity = capacity;
  return 1;
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
  ps_parser_name_environment_reset_at(
      environment, outer_names, 0, 0, 0);
}

void ps_parser_name_environment_reset_at(
    psx_parser_name_environment_t *environment,
    psx_name_classifier_t outer_names,
    unsigned scope_seq, unsigned next_scope_seq,
    unsigned declaration_seq) {
  if (!environment) return;
  clear_entries(environment);
  environment->outer_names = outer_names;
  environment->scope_depth = 0;
  environment->current_scope_seq = scope_seq;
  environment->next_scope_seq =
      next_scope_seq >= scope_seq ? next_scope_seq : scope_seq;
  environment->next_declaration_seq = declaration_seq;
}

void ps_parser_name_environment_dispose(
    psx_parser_name_environment_t *environment) {
  if (!environment) return;
  clear_entries(environment);
  free(environment->scope_stack);
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
  environment->next_declaration_seq++;
}

static void environment_enter_scope(void *context) {
  psx_parser_name_environment_t *environment = context;
  if (!environment ||
      !ensure_scope_capacity(
          environment, environment->scope_depth + 1))
    return;
  environment->scope_stack[environment->scope_depth++] =
      environment->current_scope_seq;
  environment->current_scope_seq = ++environment->next_scope_seq;
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
  environment->current_scope_seq =
      environment->scope_stack[environment->scope_depth];
}

static void environment_capture_lookup_point(
    void *context, unsigned *scope_seq,
    unsigned *declaration_seq) {
  psx_parser_name_environment_t *environment = context;
  if (scope_seq)
    *scope_seq = environment
                     ? environment->current_scope_seq : 0;
  if (declaration_seq)
    *declaration_seq = environment
                           ? environment->next_declaration_seq : 0;
}

static void environment_record_binding_event(void *context) {
  psx_parser_name_environment_t *environment = context;
  if (environment) environment->next_declaration_seq++;
}

static void environment_reserve_scope(void *context) {
  psx_parser_name_environment_t *environment = context;
  if (environment) environment->next_scope_seq++;
}

psx_name_classifier_t ps_parser_name_environment_classifier(
    psx_parser_name_environment_t *environment) {
  return (psx_name_classifier_t){
      .context = environment,
      .is_typedef_name = environment_is_typedef_name,
      .declare_name = environment_declare_name,
      .enter_scope = environment_enter_scope,
      .leave_scope = environment_leave_scope,
      .record_binding_event = environment_record_binding_event,
      .reserve_scope = environment_reserve_scope,
      .capture_lookup_point = environment_capture_lookup_point,
  };
}
