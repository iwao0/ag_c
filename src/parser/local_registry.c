#include "local_registry.h"

#include "decl.h"
#include "diag.h"
#include "global_registry.h"
#include "lvar_internal.h"
#include "node_utils.h"
#include "type.h"
#include "type_builder.h"
#include "../semantic/scope_graph.h"
#include "../semantic/type_identity.h"
#include "../semantic/type_compatibility_view.h"
#include <stdlib.h>
#include <string.h>

#define LVAR_OFFSET_BUCKETS 256u

struct psx_lvar_usage_region_t {
  psx_lvar_usage_region_t *prev;
  unsigned int suppress_warnings : 1;
};

typedef struct lvar_usage_event_t lvar_usage_event_t;
struct lvar_usage_event_t {
  lvar_usage_event_t *next;
  lvar_t *var;
  psx_lvar_usage_kind_t kind;
  psx_lvar_usage_region_t *region;
};

struct psx_local_registry_t {
  ag_diagnostic_context_t *diagnostic_context;
  const psx_semantic_type_table_t *semantic_types;
  /* Name lookup and declaration identity live in scope_graph; this list is
   * only storage and analysis order. */
  lvar_t *storage_objects;
  psx_scope_graph_t *scope_graph;
  lvar_t *lvars_by_offset[LVAR_OFFSET_BUCKETS];
  lvar_usage_event_t *usage_events_head;
  lvar_usage_event_t *usage_events_tail;
  psx_lvar_usage_region_t *current_usage_region;
  char *current_function_name;
  int current_function_name_len;
  void *active_transaction;
};

typedef struct {
  psx_local_registry_t *registry;
  lvar_t *storage_objects;
  psx_scope_graph_checkpoint_t scope_graph_checkpoint;
  lvar_t *lvars_by_offset[LVAR_OFFSET_BUCKETS];
  lvar_usage_event_t *usage_events_head;
  lvar_usage_event_t *usage_events_tail;
  psx_lvar_usage_region_t *current_usage_region;
  char *current_function_name;
  int current_function_name_len;
} psx_local_registry_transaction_t;

static int local_transaction_contains_original(
    const psx_local_registry_transaction_t *transaction,
    const lvar_t *var) {
  if (!transaction || !var) return 0;
  for (const lvar_t *current = transaction->storage_objects;
       current; current = current->next_storage) {
    if (current == var) return 1;
  }
  return 0;
}

psx_local_registry_t *ps_local_registry_create(
    ag_diagnostic_context_t *diagnostic_context,
    const psx_semantic_type_table_t *semantic_types,
    psx_scope_graph_t *scope_graph) {
  if (!diagnostic_context || !semantic_types || !scope_graph) return NULL;
  psx_local_registry_t *registry =
      calloc(1, sizeof(psx_local_registry_t));
  if (registry) {
    registry->diagnostic_context = diagnostic_context;
    registry->semantic_types = semantic_types;
    registry->scope_graph = scope_graph;
  }
  return registry;
}

void ps_local_registry_destroy(psx_local_registry_t *registry) {
  if (!registry) return;
  psx_local_registry_transaction_t *transaction =
      registry->active_transaction;
  if (transaction)
    psx_scope_graph_checkpoint_commit(&transaction->scope_graph_checkpoint);
  free(registry->active_transaction);
  free(registry);
}

ag_diagnostic_context_t *ps_local_registry_diagnostics(
    const psx_local_registry_t *registry) {
  return registry ? registry->diagnostic_context : NULL;
}

const psx_semantic_type_table_t *ps_local_registry_semantic_types(
    const psx_local_registry_t *registry) {
  return registry ? registry->semantic_types : NULL;
}

int psx_local_registry_checkpoint_begin(
    psx_local_registry_t *registry,
    psx_local_registry_checkpoint_t *checkpoint) {
  if (!registry || !checkpoint || registry->active_transaction)
    return 0;
  *checkpoint = (psx_local_registry_checkpoint_t){0};
  psx_local_registry_transaction_t *transaction =
      calloc(1, sizeof(*transaction));
  if (!transaction) return 0;
  transaction->registry = registry;
  transaction->storage_objects = registry->storage_objects;
  if (!psx_scope_graph_checkpoint_begin(
          registry->scope_graph, &transaction->scope_graph_checkpoint)) {
    free(transaction);
    return 0;
  }
  memcpy(transaction->lvars_by_offset, registry->lvars_by_offset,
         sizeof(transaction->lvars_by_offset));
  transaction->usage_events_head = registry->usage_events_head;
  transaction->usage_events_tail = registry->usage_events_tail;
  transaction->current_usage_region = registry->current_usage_region;
  transaction->current_function_name = registry->current_function_name;
  transaction->current_function_name_len =
      registry->current_function_name_len;
  registry->active_transaction = transaction;
  checkpoint->state = transaction;
  return 1;
}

int psx_local_registry_checkpoint_is_active(
    const psx_local_registry_t *registry) {
  return registry && registry->active_transaction;
}

static psx_local_registry_transaction_t *local_checkpoint_transaction(
    psx_local_registry_t *registry,
    psx_local_registry_checkpoint_t *checkpoint) {
  psx_local_registry_transaction_t *transaction =
      checkpoint ? checkpoint->state : NULL;
  if (!registry || !transaction || transaction->registry != registry ||
      registry->active_transaction != transaction)
    return NULL;
  return transaction;
}

void psx_local_registry_checkpoint_commit(
    psx_local_registry_t *registry,
    psx_local_registry_checkpoint_t *checkpoint) {
  psx_local_registry_transaction_t *transaction =
      local_checkpoint_transaction(registry, checkpoint);
  if (!transaction) return;
  registry->active_transaction = NULL;
  checkpoint->state = NULL;
  psx_scope_graph_checkpoint_commit(&transaction->scope_graph_checkpoint);
  free(transaction);
}

void psx_local_registry_checkpoint_rollback(
    psx_local_registry_t *registry,
    psx_local_registry_checkpoint_t *checkpoint) {
  psx_local_registry_transaction_t *transaction =
      local_checkpoint_transaction(registry, checkpoint);
  if (!transaction) return;
  if (transaction->usage_events_tail)
    transaction->usage_events_tail->next = NULL;
  registry->storage_objects = transaction->storage_objects;
  memcpy(registry->lvars_by_offset, transaction->lvars_by_offset,
         sizeof(registry->lvars_by_offset));
  registry->usage_events_head = transaction->usage_events_head;
  registry->usage_events_tail = transaction->usage_events_tail;
  registry->current_usage_region = transaction->current_usage_region;
  registry->current_function_name = transaction->current_function_name;
  registry->current_function_name_len =
      transaction->current_function_name_len;
  registry->active_transaction = NULL;
  checkpoint->state = NULL;
  psx_scope_graph_checkpoint_rollback(
      registry->scope_graph, &transaction->scope_graph_checkpoint);
  free(transaction);
}

psx_scope_graph_t *ps_local_registry_scope_graph(
    const psx_local_registry_t *registry) {
  return registry ? registry->scope_graph : NULL;
}

static psx_qual_type_t resolve_local_decl_type(
    const psx_local_registry_t *registry, const psx_type_t *type) {
  if (!registry || !registry->semantic_types || !type)
    return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                             PSX_TYPE_QUALIFIER_NONE};
  psx_qual_type_t qual_type = psx_semantic_type_table_find(
      registry->semantic_types, type);
  return psx_semantic_type_table_qual_type_is_valid(
             registry->semantic_types, qual_type)
             ? qual_type
             : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                                 PSX_TYPE_QUALIFIER_NONE};
}

static unsigned offset_hash(int offset) {
  return (((unsigned)offset) * 2654435761u) >> 24;
}

static int has_local_object_in_current_scope(
    const psx_local_registry_t *registry, const char *name, int name_len) {
  if (!registry || !registry->scope_graph || !name || name_len <= 0)
    return 0;
  const psx_scope_declaration_t *declaration =
      psx_scope_graph_lookup_declaration_in_scope(
          registry->scope_graph,
          psx_scope_graph_current_scope(registry->scope_graph),
          PSX_NAMESPACE_ORDINARY, name, name_len);
  return declaration && declaration->kind == PSX_DECL_LOCAL_OBJECT;
}

static void index_add(psx_local_registry_t *registry, lvar_t *var) {
  (void)psx_scope_graph_declare(
      registry->scope_graph, PSX_NAMESPACE_ORDINARY,
      PSX_DECL_LOCAL_OBJECT, var->name, var->len, var);
  unsigned offset_bucket = offset_hash(var->offset);
  var->next_offhash = registry->lvars_by_offset[offset_bucket];
  registry->lvars_by_offset[offset_bucket] = var;
}

static void offset_index_remove(
    psx_local_registry_t *registry, lvar_t *var) {
  unsigned bucket = offset_hash(var->offset);
  lvar_t **cursor = &registry->lvars_by_offset[bucket];
  while (*cursor) {
    if (*cursor == var) {
      *cursor = var->next_offhash;
      return;
    }
    cursor = &(*cursor)->next_offhash;
  }
}

void psx_local_registry_add_in(
    psx_local_registry_t *registry, lvar_t *var) {
  if (!registry || !var) return;
  var->next_storage = registry->storage_objects;
  registry->storage_objects = var;
  index_add(registry, var);
}

lvar_t *ps_local_registry_create_storage_object_in(
    psx_local_registry_t *registry,
    char *name, int name_len, int offset, int storage_size,
    int alignment, const psx_type_t *decl_type,
    token_t *diagnostic_token) {
  if (!registry || !decl_type) return NULL;
  psx_qual_type_t qual_type = resolve_local_decl_type(
      registry, decl_type);
  if (qual_type.type_id == PSX_TYPE_ID_INVALID)
    return NULL;
  return ps_local_registry_create_storage_object_qual_type_in(
      registry, name, name_len, offset, storage_size, alignment,
      qual_type, diagnostic_token);
}

lvar_t *ps_local_registry_create_storage_object_qual_type_in(
    psx_local_registry_t *registry,
    char *name, int name_len, int offset, int storage_size,
    int alignment, psx_qual_type_t decl_qual_type,
    token_t *diagnostic_token) {
  psx_type_shape_t shape = {0};
  if (!registry || !name || name_len <= 0 ||
      !psx_semantic_type_table_describe(
          registry->semantic_types, decl_qual_type.type_id, &shape))
    return NULL;
  if (has_local_object_in_current_scope(registry, name, name_len)) {
    ps_diag_duplicate_with_name_in(
        registry->diagnostic_context, diagnostic_token,
        "variable", name, name_len);
  }

  lvar_t *var = calloc(1, sizeof(*var));
  if (!var) return NULL;
  var->name = name;
  var->len = name_len;
  var->offset = offset;
  var->size = storage_size;
  var->align_bytes = alignment;
  var->decl_type_table = registry->semantic_types;
  var->decl_qual_type = decl_qual_type;
  psx_decl_attach_lvar_current_region_in(registry, var);
  psx_local_registry_add_in(registry, var);
  return var;
}

lvar_t *ps_local_registry_create_internal_storage_object_in(
    psx_local_registry_t *registry,
    char *name, int name_len, int offset, int storage_size,
    int alignment, const psx_type_t *decl_type) {
  if (!registry || !decl_type) return NULL;
  psx_qual_type_t qual_type = resolve_local_decl_type(
      registry, decl_type);
  return ps_local_registry_create_internal_storage_object_qual_type_in(
      registry, name, name_len, offset, storage_size, alignment, qual_type);
}

lvar_t *ps_local_registry_create_internal_storage_object_qual_type_in(
    psx_local_registry_t *registry,
    char *name, int name_len, int offset, int storage_size,
    int alignment, psx_qual_type_t decl_qual_type) {
  psx_type_shape_t shape = {0};
  if (!registry || !psx_semantic_type_table_describe(
          registry->semantic_types, decl_qual_type.type_id, &shape))
    return NULL;
  lvar_t *var = calloc(1, sizeof(*var));
  if (!var) return NULL;
  var->name = name;
  var->len = name_len;
  var->offset = offset;
  var->size = storage_size;
  var->align_bytes = alignment;
  var->decl_type_table = registry->semantic_types;
  var->decl_qual_type = decl_qual_type;
  var->next_storage = registry->storage_objects;
  registry->storage_objects = var;
  unsigned bucket = offset_hash(offset);
  var->next_offhash = registry->lvars_by_offset[bucket];
  registry->lvars_by_offset[bucket] = var;
  return var;
}

lvar_t *ps_local_registry_create_static_alias_in(
    psx_local_registry_t *registry,
    global_var_t *global,
    char *name, int name_len, char *global_name, int global_name_len,
    const psx_type_t *type) {
  if (!registry || !global || !name || name_len <= 0 ||
      !global_name || global_name_len <= 0 ||
      !type)
    return NULL;
  psx_qual_type_t qual_type = resolve_local_decl_type(registry, type);
  if (qual_type.type_id == PSX_TYPE_ID_INVALID)
    return NULL;
  return ps_local_registry_create_static_alias_qual_type_in(
      registry, global, name, name_len, global_name, global_name_len,
      qual_type);
}

lvar_t *ps_local_registry_create_static_alias_qual_type_in(
    psx_local_registry_t *registry,
    global_var_t *global,
    char *name, int name_len, char *global_name, int global_name_len,
    psx_qual_type_t type) {
  psx_type_shape_t shape = {0};
  if (!registry || !name || name_len <= 0 ||
      !global_name || global_name_len <= 0 ||
      !psx_semantic_type_table_describe(
          registry->semantic_types, type.type_id, &shape))
    return NULL;
  lvar_t *var = calloc(1, sizeof(*var));
  if (!var) return NULL;
  var->name = name;
  var->len = name_len;
  var->is_static_local = 1;
  var->static_global = global;
  var->static_global_name = global_name;
  var->static_global_name_len = global_name_len;
  var->decl_type_table = registry->semantic_types;
  var->decl_qual_type = type;
  psx_decl_attach_lvar_current_region_in(registry, var);
  psx_local_registry_add_in(registry, var);
  return var;
}

void ps_local_registry_update_storage_object_in(
    psx_local_registry_t *registry,
    lvar_t *var, int offset, int storage_size, int alignment) {
  if (!registry || !var) return;
  offset_index_remove(registry, var);
  var->offset = offset;
  var->size = storage_size;
  var->align_bytes = alignment;
  unsigned bucket = offset_hash(offset);
  var->next_offhash = registry->lvars_by_offset[bucket];
  registry->lvars_by_offset[bucket] = var;
}

void ps_local_registry_mark_parameter(lvar_t *var, int is_byref) {
  if (!var) return;
  var->is_param = 1;
  var->is_byref_param = is_byref ? 1 : 0;
}

int ps_local_registry_complete_array_type(
    psx_local_registry_t *registry, lvar_t *var,
    const psx_type_t *complete_type) {
  psx_qual_type_t qual_type = resolve_local_decl_type(
      registry, complete_type);
  if (qual_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  return ps_local_registry_complete_array_qual_type(
      registry, var, qual_type);
}

int ps_local_registry_complete_array_qual_type(
    psx_local_registry_t *registry, lvar_t *var,
    psx_qual_type_t complete_type) {
  psx_type_shape_t current = {0};
  psx_type_shape_t replacement = {0};
  if (!registry || !var ||
      !psx_semantic_type_table_describe(
          registry->semantic_types, var->decl_qual_type.type_id, &current) ||
      current.kind != PSX_TYPE_ARRAY || current.array_len > 0 ||
      current.is_vla ||
      !psx_semantic_type_table_describe(
          registry->semantic_types, complete_type.type_id, &replacement) ||
      replacement.kind != PSX_TYPE_ARRAY || replacement.array_len <= 0 ||
      replacement.is_vla)
    return 0;
  psx_qual_type_t current_base = psx_semantic_type_table_base(
      registry->semantic_types, var->decl_qual_type.type_id);
  psx_qual_type_t replacement_base = psx_semantic_type_table_base(
      registry->semantic_types, complete_type.type_id);
  if (current_base.type_id == PSX_TYPE_ID_INVALID ||
      current_base.type_id != replacement_base.type_id ||
      current_base.qualifiers != replacement_base.qualifiers)
    return 0;
  var->decl_type_table = registry->semantic_types;
  var->decl_qual_type = complete_type;
  return 1;
}

void ps_local_registry_set_vla_descriptor(
    lvar_t *var, int row_stride_frame_off, int strides_remaining,
    int row_stride_src_offset,
    int row_stride_elem_size) {
  if (!var) return;
  var->vla_runtime.view.row_stride_frame_off =
      row_stride_frame_off > 0 ? row_stride_frame_off : 0;
  var->vla_runtime.view.strides_remaining =
      row_stride_frame_off > 0 && strides_remaining > 0
          ? strides_remaining
          : 0;
  var->vla_runtime.row_stride_src_offset = row_stride_src_offset;
  var->vla_runtime.row_stride_elem_size =
      row_stride_elem_size > 0 ? row_stride_elem_size : 0;
}

void ps_local_registry_set_vla_param_inner_dims(
    psx_local_registry_t *registry, lvar_t *var,
    const int *inner_dim_consts,
    const int *inner_dim_src_offsets, int inner_dim_count,
    token_t *diagnostic_token) {
  if (!registry || !var) return;
  if (inner_dim_count < 0) inner_dim_count = 0;
  int *constants = NULL;
  int *source_offsets = NULL;
  if (inner_dim_count > 0) {
    constants = calloc((size_t)inner_dim_count, sizeof(*constants));
    source_offsets = calloc((size_t)inner_dim_count, sizeof(*source_offsets));
    if (!constants || !source_offsets) {
      free(constants);
      free(source_offsets);
      ps_diag_ctx_in(
          registry->diagnostic_context, diagnostic_token, "vla",
          "VLA runtime dimension allocation failed");
      return;
    }
    if (inner_dim_consts)
      memcpy(constants, inner_dim_consts,
             (size_t)inner_dim_count * sizeof(*constants));
    if (inner_dim_src_offsets)
      memcpy(source_offsets, inner_dim_src_offsets,
             (size_t)inner_dim_count * sizeof(*source_offsets));
  }
  free(var->vla_runtime.param_inner_dim_consts);
  free(var->vla_runtime.param_inner_dim_src_offsets);
  var->vla_runtime.param_inner_dim_consts = constants;
  var->vla_runtime.param_inner_dim_src_offsets = source_offsets;
  var->vla_runtime.param_inner_dim_count = inner_dim_count;
}

lvar_t *ps_lvar_next_storage(const lvar_t *var) {
  return var ? var->next_storage : NULL;
}

lvar_t *ps_lvar_find_owner(lvar_t *head, int offset) {
  for (lvar_t *var = head; var; var = var->next_storage) {
    if (var->is_static_local) continue;
    int size = var->size > 0 ? var->size : 1;
    if (var->offset <= offset && offset < var->offset + size) return var;
  }
  return NULL;
}

psx_lvar_registry_view_t ps_lvar_registry_view(const lvar_t *var) {
  if (!var) return (psx_lvar_registry_view_t){0};
  return (psx_lvar_registry_view_t){
      .name = var->name,
      .name_len = var->len,
      .is_used = var->is_used,
      .is_unevaluated_used = var->is_unevaluated_used,
      .is_address_taken = var->is_address_taken,
      .is_initialized = var->is_initialized,
      .suppress_unreachable_warnings =
          var->suppress_unreachable_warnings,
      .is_param = var->is_param,
      .is_array = ps_lvar_is_array(var),
      .is_static_local = var->is_static_local,
      .decl_region = var->decl_region,
  };
}

int ps_lvar_offset(const lvar_t *var) {
  return var ? var->offset : 0;
}

const char *ps_lvar_name(const lvar_t *var) {
  return var ? var->name : NULL;
}

int ps_lvar_name_len(const lvar_t *var) {
  return var ? var->len : 0;
}

static void clear_local_registry_state(psx_local_registry_t *registry) {
  if (!registry) return;
  const psx_local_registry_transaction_t *transaction =
      registry->active_transaction;
  for (lvar_t *var = registry->storage_objects;
       var; var = var->next_storage) {
    if (local_transaction_contains_original(transaction, var))
      continue;
    free(var->vla_runtime.param_inner_dim_consts);
    free(var->vla_runtime.param_inner_dim_src_offsets);
    var->vla_runtime.param_inner_dim_consts = NULL;
    var->vla_runtime.param_inner_dim_src_offsets = NULL;
    var->vla_runtime.param_inner_dim_count = 0;
  }
  registry->storage_objects = NULL;
  memset(registry->lvars_by_offset, 0,
         sizeof(registry->lvars_by_offset));
  registry->usage_events_head = NULL;
  registry->usage_events_tail = NULL;
  registry->current_usage_region = NULL;
  registry->current_function_name = NULL;
  registry->current_function_name_len = 0;
}

void ps_local_registry_reset_in(psx_local_registry_t *registry) {
  if (!registry) return;
  clear_local_registry_state(registry);
  while (psx_scope_graph_leave_scope(registry->scope_graph)) {}
  if (psx_scope_graph_enter_scope(
          registry->scope_graph, PSX_SCOPE_FUNCTION) == PSX_SCOPE_ID_INVALID)
    ps_diag_ctx_in(
        registry->diagnostic_context, NULL, "scope",
        "function scope graph allocation failed");
}

void ps_local_registry_reset_translation_unit_in(
    psx_local_registry_t *registry) {
  if (!registry) return;
  clear_local_registry_state(registry);
  while (psx_scope_graph_leave_scope(registry->scope_graph)) {}
}

void ps_local_registry_prepare_function_resolution_in(
    psx_local_registry_t *registry) {
  if (!registry) return;
  psx_scope_id_t current =
      psx_scope_graph_current_scope(registry->scope_graph);
  if (current == PSX_SCOPE_ID_TRANSLATION_UNIT ||
      psx_scope_graph_scope_kind(registry->scope_graph, current) !=
          PSX_SCOPE_FUNCTION) {
    ps_local_registry_reset_in(registry);
    return;
  }
  clear_local_registry_state(registry);
}

static void enter_local_scope(
    psx_local_registry_t *registry, psx_scope_kind_t kind) {
  if (!registry) return;
  if (psx_scope_graph_enter_scope(
          registry->scope_graph, kind) == PSX_SCOPE_ID_INVALID)
    ps_diag_ctx_in(
        registry->diagnostic_context, NULL, "scope",
        "scope graph allocation failed");
}

void ps_local_registry_enter_translation_unit_in(
    psx_local_registry_t *registry) {
  if (!registry) return;
  while (psx_scope_graph_leave_scope(registry->scope_graph)) {}
}

void ps_local_registry_set_current_function_in(
    psx_local_registry_t *registry, char *name, int len) {
  if (!registry) return;
  registry->current_function_name = name;
  registry->current_function_name_len = len;
}

void ps_local_registry_get_current_function_in(
    const psx_local_registry_t *registry,
    char **out_name, int *out_len) {
  if (out_name)
    *out_name = registry ? registry->current_function_name : NULL;
  if (out_len)
    *out_len = registry ? registry->current_function_name_len : 0;
}

psx_lvar_usage_region_t *
ps_local_registry_set_current_usage_region_in(
    psx_local_registry_t *registry,
    psx_lvar_usage_region_t *region) {
  if (!registry) return NULL;
  psx_lvar_usage_region_t *previous =
      registry->current_usage_region;
  registry->current_usage_region = region;
  return previous;
}

void ps_decl_enter_scope_in(psx_local_registry_t *registry) {
  enter_local_scope(registry, PSX_SCOPE_BLOCK);
}

void ps_decl_leave_scope_in(psx_local_registry_t *registry) {
  if (!registry || !registry->scope_graph) return;
  psx_scope_id_t current =
      psx_scope_graph_current_scope(registry->scope_graph);
  psx_scope_kind_t kind =
      psx_scope_graph_scope_kind(registry->scope_graph, current);
  if (kind != PSX_SCOPE_BLOCK &&
      kind != PSX_SCOPE_FUNCTION_PROTOTYPE)
    return;
  psx_scope_graph_leave_scope(registry->scope_graph);
}

lvar_t *ps_decl_get_storage_objects_in(
    const psx_local_registry_t *registry) {
  return registry ? registry->storage_objects : NULL;
}

psx_lvar_usage_region_t *psx_decl_begin_lvar_usage_region_in(
    psx_local_registry_t *registry) {
  if (!registry) return NULL;
  psx_lvar_usage_region_t *region = calloc(1, sizeof(*region));
  if (!region) return NULL;
  region->prev = registry->current_usage_region;
  registry->current_usage_region = region;
  return region;
}

void psx_decl_end_lvar_usage_region_in(
    psx_local_registry_t *registry,
    psx_lvar_usage_region_t *region) {
  if (!registry || !region) return;
  registry->current_usage_region = region->prev;
}

void ps_decl_suppress_lvar_usage_region(psx_lvar_usage_region_t *region) {
  if (region) region->suppress_warnings = 1;
}

void psx_decl_attach_lvar_current_region_in(
    const psx_local_registry_t *registry, lvar_t *var) {
  if (registry && var) var->decl_region = registry->current_usage_region;
}

void ps_decl_record_lvar_usage_in_region_in(
    psx_local_registry_t *registry,
    lvar_t *var, psx_lvar_usage_kind_t kind,
    psx_lvar_usage_region_t *region) {
  if (!registry || !var) return;
  lvar_usage_event_t *event = calloc(1, sizeof(*event));
  if (!event) return;
  event->var = var;
  event->kind = kind;
  event->region = region;
  if (!registry->usage_events_head)
    registry->usage_events_head = event;
  else
    registry->usage_events_tail->next = event;
  registry->usage_events_tail = event;
}

void ps_decl_replay_lvar_usage_events_in(
    psx_local_registry_t *registry, lvar_t *storage_objects) {
  if (!registry) return;
  for (lvar_t *var = storage_objects;
       var; var = var->next_storage) {
    var->is_used = 0;
    var->is_unevaluated_used = 0;
    var->is_address_taken = 0;
    var->is_initialized = 0;
    var->suppress_unreachable_warnings = 0;
    var->used_count = 0;
    if (var->decl_region)
      var->suppress_unreachable_warnings =
          var->decl_region->suppress_warnings ? 1 : 0;
  }
  for (lvar_usage_event_t *event = registry->usage_events_head;
       event; event = event->next) {
    lvar_t *var = event->var;
    if (!var || (event->region && event->region->suppress_warnings)) continue;
    switch (event->kind) {
      case PSX_LVAR_USAGE_EVALUATED:
        var->used_count++;
        var->is_used = 1;
        break;
      case PSX_LVAR_USAGE_UNEVALUATED:
        var->is_unevaluated_used = 1;
        break;
      case PSX_LVAR_USAGE_ADDRESS_TAKEN:
        if (var->used_count > 0) var->used_count--;
        var->is_used = var->used_count > 0;
        var->is_address_taken = 1;
        break;
      case PSX_LVAR_USAGE_INITIALIZED:
        var->is_initialized = 1;
        break;
    }
  }
}

lvar_t *psx_decl_find_lvar_by_offset_in(
    const psx_local_registry_t *registry, int offset) {
  if (!registry) return NULL;
  unsigned bucket = offset_hash(offset);
  for (lvar_t *var = registry->lvars_by_offset[bucket];
       var; var = var->next_offhash) {
    if (var->offset == offset) return var;
  }
  return NULL;
}
