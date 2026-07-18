#ifndef PARSER_GLOBAL_REGISTRY_H
#define PARSER_GLOBAL_REGISTRY_H

#include "gvar_public.h"
#include "literal_public.h"

typedef struct psx_type_t psx_type_t;
typedef struct string_lit_t string_lit_t;
typedef struct float_lit_t float_lit_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_semantic_type_table_t psx_semantic_type_table_t;

typedef struct {
  void *state;
} psx_global_registry_checkpoint_t;

psx_global_registry_t *ps_global_registry_create(void);
void ps_global_registry_destroy(psx_global_registry_t *registry);
void ps_global_registry_bind_semantic_types(
    psx_global_registry_t *registry,
    const psx_semantic_type_table_t *semantic_types);

void ps_global_registry_reset_translation_unit_in(
    psx_global_registry_t *registry);
void ps_global_registry_reset_diag_state_in(
    psx_global_registry_t *registry);
void ps_register_global_var_in(
    psx_global_registry_t *registry, global_var_t *global);
char *ps_global_registry_copy_name_in(
    psx_global_registry_t *registry, const char *name, int len);
global_var_t *ps_find_global_var_in(
    psx_global_registry_t *registry, char *name, int len);
void ps_iter_globals_in(
    psx_global_registry_t *registry,
    global_var_visitor_t visitor, void *user);
bool ps_iter_string_literals_in(
    psx_global_registry_t *registry,
    string_lit_visitor_t visitor, void *user);
bool ps_iter_float_literals_in(
    psx_global_registry_t *registry,
    float_lit_visitor_t visitor, void *user);
void psx_register_string_lit_in(
    psx_global_registry_t *registry, string_lit_t *literal);
void psx_register_float_lit_in(
    psx_global_registry_t *registry, float_lit_t *literal);
string_lit_t *ps_find_string_lit_by_label_in(
    psx_global_registry_t *registry, char *label);
int ps_global_registry_next_string_literal_id(
    psx_global_registry_t *registry);
int ps_global_registry_next_float_literal_id(
    psx_global_registry_t *registry);

int psx_global_registry_checkpoint_begin(
    psx_global_registry_t *registry,
    psx_global_registry_checkpoint_t *checkpoint);
int psx_global_registry_checkpoint_is_active(
    const psx_global_registry_t *registry);
int psx_global_registry_note_global_mutation(
    psx_global_registry_t *registry, global_var_t *global);
void psx_global_registry_checkpoint_commit(
    psx_global_registry_t *registry,
    psx_global_registry_checkpoint_t *checkpoint);
void psx_global_registry_checkpoint_rollback(
    psx_global_registry_t *registry,
    psx_global_registry_checkpoint_t *checkpoint);

int ps_global_registry_bind_decl_type(
    psx_global_registry_t *registry, global_var_t *global,
    const psx_type_t *type);
int ps_global_registry_bind_decl_qual_type(
    psx_global_registry_t *registry, global_var_t *global,
    psx_qual_type_t type);
int ps_global_registry_complete_array_type(
    psx_global_registry_t *registry, global_var_t *global,
    const psx_type_t *complete_type);
int ps_global_registry_complete_array_qual_type(
    psx_global_registry_t *registry, global_var_t *global,
    psx_qual_type_t complete_type);

#endif
