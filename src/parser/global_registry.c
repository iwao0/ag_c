#include "global_registry.h"
#include "literal_public.h"
#include "symtab.h"
#include "type.h"
#include "type_builder.h"
#include "../semantic/type_identity.h"
#include "../semantic/scope_graph.h"
#include <stdlib.h>
#include <string.h>

typedef struct psx_global_registry_name_t {
  struct psx_global_registry_name_t *next;
  char bytes[];
} psx_global_registry_name_t;

typedef struct psx_global_registry_global_snapshot_t {
  global_var_t *global;
  global_var_t value;
  struct psx_global_registry_global_snapshot_t *next;
} psx_global_registry_global_snapshot_t;

typedef struct {
  psx_global_registry_t *registry;
  global_var_t *global_vars;
  string_lit_t *string_literals;
  float_lit_t *float_literals;
  int next_string_literal_id;
  int next_float_literal_id;
  psx_global_registry_name_t *owned_names;
  psx_global_registry_global_snapshot_t *global_snapshots;
  psx_scope_graph_checkpoint_t scope_graph_checkpoint;
} psx_global_registry_transaction_t;

struct psx_global_registry_t {
  const psx_semantic_type_table_t *semantic_types;
  psx_scope_graph_t *scope_graph;
  string_lit_t *string_literals;
  float_lit_t *float_literals;
  global_var_t *global_vars;
  int next_string_literal_id;
  int next_float_literal_id;
  psx_global_registry_name_t *owned_names;
  psx_global_registry_transaction_t *active_transaction;
};

static void free_global_registry_transaction(
    psx_global_registry_transaction_t *transaction) {
  if (!transaction) return;
  psx_scope_graph_checkpoint_commit(&transaction->scope_graph_checkpoint);
  psx_global_registry_global_snapshot_t *snapshot =
      transaction->global_snapshots;
  while (snapshot) {
    psx_global_registry_global_snapshot_t *next = snapshot->next;
    free(snapshot);
    snapshot = next;
  }
  free(transaction);
}

static void free_owned_names_until(
    psx_global_registry_t *registry,
    psx_global_registry_name_t *stop) {
  if (!registry) return;
  while (registry->owned_names != stop) {
    psx_global_registry_name_t *owned = registry->owned_names;
    if (!owned) break;
    registry->owned_names = owned->next;
    free(owned);
  }
}

psx_global_registry_t *ps_global_registry_create(
    const psx_semantic_type_table_t *semantic_types,
    psx_scope_graph_t *scope_graph) {
  if (!semantic_types || !scope_graph) return NULL;
  psx_global_registry_t *registry =
      calloc(1, sizeof(psx_global_registry_t));
  if (registry) {
    registry->semantic_types = semantic_types;
    registry->scope_graph = scope_graph;
  }
  return registry;
}

void ps_global_registry_destroy(psx_global_registry_t *registry) {
  if (!registry) return;
  free_global_registry_transaction(registry->active_transaction);
  free_owned_names_until(registry, NULL);
  free(registry);
}

const psx_semantic_type_table_t *ps_global_registry_semantic_types(
    const psx_global_registry_t *registry) {
  return registry ? registry->semantic_types : NULL;
}

int psx_global_registry_checkpoint_begin(
    psx_global_registry_t *registry,
    psx_global_registry_checkpoint_t *checkpoint) {
  if (!registry || !checkpoint || registry->active_transaction)
    return 0;
  *checkpoint = (psx_global_registry_checkpoint_t){0};
  psx_global_registry_transaction_t *transaction =
      calloc(1, sizeof(*transaction));
  if (!transaction) return 0;
  transaction->registry = registry;
  transaction->global_vars = registry->global_vars;
  transaction->string_literals = registry->string_literals;
  transaction->float_literals = registry->float_literals;
  transaction->next_string_literal_id = registry->next_string_literal_id;
  transaction->next_float_literal_id = registry->next_float_literal_id;
  transaction->owned_names = registry->owned_names;
  if (registry->scope_graph &&
      !psx_scope_graph_checkpoint_begin(
          registry->scope_graph, &transaction->scope_graph_checkpoint)) {
    free(transaction);
    return 0;
  }
  registry->active_transaction = transaction;
  checkpoint->state = transaction;
  return 1;
}

int psx_global_registry_checkpoint_is_active(
    const psx_global_registry_t *registry) {
  return registry && registry->active_transaction;
}

static int transaction_contains_original_global(
    const psx_global_registry_transaction_t *transaction,
    const global_var_t *global) {
  if (!transaction || !global) return 0;
  for (const global_var_t *current = transaction->global_vars;
       current; current = current->next) {
    if (current == global) return 1;
  }
  return 0;
}

int psx_global_registry_note_global_mutation(
    psx_global_registry_t *registry, global_var_t *global) {
  if (!registry || !global) return 0;
  psx_global_registry_transaction_t *transaction =
      registry->active_transaction;
  if (!transaction ||
      !transaction_contains_original_global(transaction, global))
    return 1;
  for (psx_global_registry_global_snapshot_t *snapshot =
           transaction->global_snapshots;
       snapshot; snapshot = snapshot->next) {
    if (snapshot->global == global) return 1;
  }
  psx_global_registry_global_snapshot_t *snapshot =
      malloc(sizeof(*snapshot));
  if (!snapshot) return 0;
  snapshot->global = global;
  snapshot->value = *global;
  snapshot->next = transaction->global_snapshots;
  transaction->global_snapshots = snapshot;
  return 1;
}

static psx_global_registry_transaction_t *checkpoint_transaction(
    psx_global_registry_t *registry,
    psx_global_registry_checkpoint_t *checkpoint) {
  psx_global_registry_transaction_t *transaction =
      checkpoint ? checkpoint->state : NULL;
  if (!registry || !transaction || transaction->registry != registry ||
      registry->active_transaction != transaction)
    return NULL;
  return transaction;
}

void psx_global_registry_checkpoint_commit(
    psx_global_registry_t *registry,
    psx_global_registry_checkpoint_t *checkpoint) {
  psx_global_registry_transaction_t *transaction =
      checkpoint_transaction(registry, checkpoint);
  if (!transaction) return;
  registry->active_transaction = NULL;
  checkpoint->state = NULL;
  psx_scope_graph_checkpoint_commit(&transaction->scope_graph_checkpoint);
  free_global_registry_transaction(transaction);
}

void psx_global_registry_checkpoint_rollback(
    psx_global_registry_t *registry,
    psx_global_registry_checkpoint_t *checkpoint) {
  psx_global_registry_transaction_t *transaction =
      checkpoint_transaction(registry, checkpoint);
  if (!transaction) return;
  for (psx_global_registry_global_snapshot_t *snapshot =
           transaction->global_snapshots;
       snapshot; snapshot = snapshot->next)
    *snapshot->global = snapshot->value;
  registry->global_vars = transaction->global_vars;
  registry->string_literals = transaction->string_literals;
  registry->float_literals = transaction->float_literals;
  registry->next_string_literal_id = transaction->next_string_literal_id;
  registry->next_float_literal_id = transaction->next_float_literal_id;
  free_owned_names_until(registry, transaction->owned_names);
  registry->active_transaction = NULL;
  checkpoint->state = NULL;
  psx_scope_graph_checkpoint_rollback(
      registry->scope_graph, &transaction->scope_graph_checkpoint);
  free_global_registry_transaction(transaction);
}

psx_scope_graph_t *ps_global_registry_scope_graph(
    const psx_global_registry_t *registry) {
  return registry ? registry->scope_graph : NULL;
}

static psx_qual_type_t resolve_global_decl_type(
    const psx_global_registry_t *registry, const psx_type_t *type) {
  if (!registry || !registry->semantic_types || !type)
    return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                             PSX_TYPE_QUALIFIER_NONE};
  psx_qual_type_t qual_type = psx_semantic_type_table_find(
      registry->semantic_types, type);
  return psx_semantic_type_table_lookup_qual_type(
             registry->semantic_types, qual_type)
             ? qual_type
             : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                                 PSX_TYPE_QUALIFIER_NONE};
}

int ps_global_registry_bind_decl_type(
    psx_global_registry_t *registry, global_var_t *global,
    const psx_type_t *type) {
  if (!global ||
      global->decl_qual_type.type_id != PSX_TYPE_ID_INVALID || !type)
    return 0;
  psx_qual_type_t qual_type = resolve_global_decl_type(registry, type);
  if (qual_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  return ps_global_registry_bind_decl_qual_type(
      registry, global, qual_type);
}

int ps_global_registry_bind_decl_qual_type(
    psx_global_registry_t *registry, global_var_t *global,
    psx_qual_type_t type) {
  if (!registry || !registry->semantic_types || !global ||
      global->decl_qual_type.type_id != PSX_TYPE_ID_INVALID ||
      type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  if (!psx_semantic_type_table_lookup_qual_type(
          registry->semantic_types, type))
    return 0;
  if (!psx_global_registry_note_global_mutation(registry, global))
    return 0;
  global->decl_type_table = registry->semantic_types;
  global->decl_qual_type = type;
  return 1;
}

int ps_global_registry_complete_array_type(
    psx_global_registry_t *registry, global_var_t *global,
    const psx_type_t *complete_type) {
  psx_qual_type_t qual_type = resolve_global_decl_type(
      registry, complete_type);
  if (qual_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  return ps_global_registry_complete_array_qual_type(
      registry, global, qual_type);
}

int ps_global_registry_complete_array_qual_type(
    psx_global_registry_t *registry, global_var_t *global,
    psx_qual_type_t complete_type) {
  const psx_type_t *current = global && registry
      ? psx_semantic_type_table_lookup(
            registry->semantic_types, global->decl_qual_type.type_id)
      : NULL;
  const psx_type_t *replacement = registry
      ? psx_semantic_type_table_lookup(
            registry->semantic_types, complete_type.type_id)
      : NULL;
  psx_qual_type_t current_base = global && registry
      ? psx_semantic_type_table_base(
            registry->semantic_types, global->decl_qual_type.type_id)
      : (psx_qual_type_t){0};
  psx_qual_type_t replacement_base = registry
      ? psx_semantic_type_table_base(
            registry->semantic_types, complete_type.type_id)
      : (psx_qual_type_t){0};
  if (!ps_type_is_incomplete_array(current) || !replacement ||
      replacement->kind != PSX_TYPE_ARRAY ||
      replacement->array_len <= 0 || replacement->is_vla ||
      current_base.type_id == PSX_TYPE_ID_INVALID ||
      current_base.type_id != replacement_base.type_id ||
      current_base.qualifiers != replacement_base.qualifiers)
    return 0;
  if (!psx_global_registry_note_global_mutation(registry, global))
    return 0;
  global->decl_type_table = registry->semantic_types;
  global->decl_qual_type = complete_type;
  return 1;
}

void ps_register_global_var_in(
    psx_global_registry_t *registry, global_var_t *gv) {
  if (!registry || !gv) return;
  if (registry->scope_graph) {
    if (gv->is_compiler_generated) {
      (void)psx_scope_graph_declare_synthetic_at(
          registry->scope_graph, PSX_SCOPE_ID_TRANSLATION_UNIT,
          PSX_NAMESPACE_ORDINARY, PSX_DECL_GLOBAL_OBJECT,
          gv->name, gv->name_len, gv);
    } else {
      (void)psx_scope_graph_declare_at(
          registry->scope_graph, PSX_SCOPE_ID_TRANSLATION_UNIT,
          PSX_NAMESPACE_ORDINARY, PSX_DECL_GLOBAL_OBJECT,
          gv->name, gv->name_len, gv);
    }
  }
  gv->next = registry->global_vars;
  registry->global_vars = gv;
}

char *ps_global_registry_copy_name_in(
    psx_global_registry_t *registry, const char *name, int len) {
  if (!registry || !name || len <= 0) return NULL;
  psx_global_registry_name_t *owned = malloc(
      sizeof(*owned) + (size_t)len + 1);
  if (!owned) return NULL;
  memcpy(owned->bytes, name, (size_t)len);
  owned->bytes[len] = '\0';
  owned->next = registry->owned_names;
  registry->owned_names = owned;
  return owned->bytes;
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
  psx_decl_id_t id = psx_scope_graph_lookup_in_scope(
      registry->scope_graph, PSX_SCOPE_ID_TRANSLATION_UNIT,
      PSX_NAMESPACE_ORDINARY, name, len);
  const psx_scope_declaration_t *declaration =
      psx_scope_graph_declaration(registry->scope_graph, id);
  return declaration && declaration->kind == PSX_DECL_GLOBAL_OBJECT
             ? declaration->payload
             : NULL;
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
  free_owned_names_until(registry, NULL);
  registry->global_vars = NULL;
  registry->string_literals = NULL;
  registry->float_literals = NULL;
  registry->next_string_literal_id = 0;
  registry->next_float_literal_id = 0;
}

void ps_global_registry_reset_diag_state_in(
    psx_global_registry_t *registry) {
  if (!registry) return;
  for (global_var_t *gv = registry->global_vars; gv; gv = gv->next)
    gv->has_init = 0;
}
