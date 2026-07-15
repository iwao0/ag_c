#include "local_registry.h"

#include "decl.h"
#include "diag.h"
#include "global_registry.h"
#include "lvar_internal.h"
#include "node_utils.h"
#include "type.h"
#include "type_builder.h"
#include "../semantic/type_identity.h"
#include <stdlib.h>
#include <string.h>

#define LVAR_SCOPE_STACK_MAX 256
#define LVAR_HASH_BUCKETS 256u

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
  lvar_t *locals;
  lvar_t *all_locals;
  lvar_t *all_bindings;
  lvar_t *lvar_scope_stack[LVAR_SCOPE_STACK_MAX];
  unsigned lvar_scope_seq_stack[LVAR_SCOPE_STACK_MAX];
  int lvar_scope_depth;
  unsigned next_scope_seq;
  unsigned current_scope_seq;
  unsigned next_declaration_seq;
  unsigned *scope_parent_by_seq;
  size_t scope_parent_capacity;
  lvar_t *lvars_by_bucket[LVAR_HASH_BUCKETS];
  lvar_t *lvars_by_offset[LVAR_HASH_BUCKETS];
  lvar_usage_event_t *usage_events_head;
  lvar_usage_event_t *usage_events_tail;
  psx_lvar_usage_region_t *current_usage_region;
  char *current_function_name;
  int current_function_name_len;
};

psx_local_registry_t *ps_local_registry_create(
    ag_diagnostic_context_t *diagnostic_context) {
  psx_local_registry_t *registry =
      calloc(1, sizeof(psx_local_registry_t));
  if (registry) registry->diagnostic_context = diagnostic_context;
  return registry;
}

void ps_local_registry_destroy(psx_local_registry_t *registry) {
  if (!registry) return;
  free(registry->scope_parent_by_seq);
  free(registry);
}

void ps_local_registry_bind_semantic_types(
    psx_local_registry_t *registry,
    const psx_semantic_type_table_t *semantic_types) {
  if (registry) registry->semantic_types = semantic_types;
}

static int resolve_local_decl_type(
    const psx_local_registry_t *registry, const psx_type_t *type,
    const psx_type_t **canonical_type, psx_qual_type_t *qual_type) {
  if (!registry || !registry->semantic_types || !type ||
      !canonical_type || !qual_type)
    return 0;
  *qual_type = psx_semantic_type_table_find(
      registry->semantic_types, type);
  if (qual_type->type_id == PSX_TYPE_ID_INVALID) return 0;
  *canonical_type = psx_semantic_type_table_lookup(
      registry->semantic_types, qual_type->type_id);
  return *canonical_type != NULL;
}

static unsigned name_hash(const char *name, int len) {
  unsigned hash = 2166136261u;
  for (int i = 0; i < len; i++)
    hash = (hash ^ (unsigned char)name[i]) * 16777619u;
  return hash & (LVAR_HASH_BUCKETS - 1u);
}

static unsigned offset_hash(int offset) {
  return (((unsigned)offset) * 2654435761u) >> 24;
}

static void ensure_scope_parent_capacity(
    psx_local_registry_t *registry, unsigned scope_seq) {
  if (!registry ||
      (size_t)scope_seq < registry->scope_parent_capacity) return;
  size_t capacity = registry->scope_parent_capacity
      ? registry->scope_parent_capacity : 16;
  while (capacity <= (size_t)scope_seq) capacity *= 2;
  unsigned *grown = realloc(
      registry->scope_parent_by_seq,
      capacity * sizeof(*registry->scope_parent_by_seq));
  if (!grown) {
    ps_diag_ctx_in(
        registry->diagnostic_context, NULL, "scope",
        "local scope ancestry allocation failed");
  }
  memset(grown + registry->scope_parent_capacity, 0,
         (capacity - registry->scope_parent_capacity) * sizeof(*grown));
  registry->scope_parent_by_seq = grown;
  registry->scope_parent_capacity = capacity;
}

int ps_local_registry_scope_is_visible_from_in(
    const psx_local_registry_t *registry,
    unsigned declaration_scope, unsigned reference_scope) {
  if (!registry) return 0;
  for (;;) {
    if (reference_scope == declaration_scope) return 1;
    if (reference_scope == 0 ||
        (size_t)reference_scope >= registry->scope_parent_capacity)
      return 0;
    reference_scope = registry->scope_parent_by_seq[reference_scope];
  }
}

static void index_add(psx_local_registry_t *registry, lvar_t *var) {
  var->scope_seq = registry->current_scope_seq;
  var->declaration_seq =
      ps_local_registry_register_binding_event_in(registry);
  unsigned name_bucket = name_hash(var->name, var->len);
  var->next_hash = registry->lvars_by_bucket[name_bucket];
  registry->lvars_by_bucket[name_bucket] = var;
  unsigned offset_bucket = offset_hash(var->offset);
  var->next_offhash = registry->lvars_by_offset[offset_bucket];
  registry->lvars_by_offset[offset_bucket] = var;
}

static void index_remove(psx_local_registry_t *registry, lvar_t *var) {
  unsigned bucket = name_hash(var->name, var->len);
  lvar_t **cursor = &registry->lvars_by_bucket[bucket];
  while (*cursor) {
    if (*cursor == var) {
      *cursor = var->next_hash;
      return;
    }
    cursor = &(*cursor)->next_hash;
  }
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

unsigned ps_local_registry_current_scope_seq_in(
    const psx_local_registry_t *registry) {
  return registry ? registry->current_scope_seq : 0;
}

unsigned ps_local_registry_register_binding_event_in(
    psx_local_registry_t *registry) {
  return registry ? ++registry->next_declaration_seq : 0;
}

psx_local_lookup_point_t ps_local_registry_capture_lookup_point_in(
    const psx_local_registry_t *registry) {
  if (!registry) return (psx_local_lookup_point_t){0};
  return (psx_local_lookup_point_t){
      .scope_seq = registry->current_scope_seq,
      .declaration_seq = registry->next_declaration_seq,
  };
}

lvar_t *ps_local_registry_find_visible_in(
    const psx_local_registry_t *registry,
    char *name, int name_len, psx_local_lookup_point_t point) {
  if (!registry || !name || name_len <= 0) return NULL;
  for (lvar_t *var = registry->all_bindings;
       var; var = var->next_binding) {
    if (var->declaration_seq > point.declaration_seq ||
        var->len != name_len ||
        memcmp(var->name, name, (size_t)name_len) != 0)
      continue;
    if (ps_local_registry_scope_is_visible_from_in(
            registry,
            var->scope_seq, point.scope_seq))
      return var;
  }
  return NULL;
}

void psx_local_registry_add_in(
    psx_local_registry_t *registry, lvar_t *var) {
  if (!registry || !var) return;
  var->next = registry->locals;
  var->next_all = registry->all_locals;
  var->next_binding = registry->all_bindings;
  registry->all_locals = var;
  registry->all_bindings = var;
  registry->locals = var;
  index_add(registry, var);
}

lvar_t *ps_local_registry_create_storage_object_in(
    psx_local_registry_t *registry,
    char *name, int name_len, int offset, int storage_size,
    int alignment, const psx_type_t *decl_type,
    token_t *diagnostic_token) {
  if (!registry || !decl_type) return NULL;
  const psx_type_t *canonical_type = NULL;
  psx_qual_type_t qual_type = {0};
  if (!resolve_local_decl_type(
          registry, decl_type, &canonical_type, &qual_type))
    return NULL;
  lvar_t *previous = ps_decl_find_lvar_in(registry, name, name_len);
  if (previous &&
      previous->scope_seq ==
          ps_local_registry_current_scope_seq_in(registry)) {
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
  var->decl_type = canonical_type;
  var->decl_qual_type = qual_type;
  psx_decl_attach_lvar_current_region_in(registry, var);
  psx_local_registry_add_in(registry, var);
  return var;
}

lvar_t *ps_local_registry_create_type_binding_in(
    psx_local_registry_t *registry,
    char *name, int name_len, const psx_type_t *type,
    token_t *diagnostic_token) {
  if (!registry || !name || name_len <= 0 || !type) return NULL;
  const psx_type_t *canonical_type = NULL;
  psx_qual_type_t qual_type = {0};
  if (!resolve_local_decl_type(
          registry, type, &canonical_type, &qual_type))
    return NULL;
  lvar_t *previous = ps_decl_find_lvar_in(registry, name, name_len);
  if (previous &&
      previous->scope_seq ==
          ps_local_registry_current_scope_seq_in(registry)) {
    ps_diag_duplicate_with_name_in(
        registry->diagnostic_context, diagnostic_token,
        "parameter", name, name_len);
  }
  lvar_t *var = calloc(1, sizeof(*var));
  if (!var) return NULL;
  var->name = name;
  var->len = name_len;
  var->scope_seq = registry->current_scope_seq;
  var->declaration_seq =
      ps_local_registry_register_binding_event_in(registry);
  var->decl_type_table = registry->semantic_types;
  var->decl_type = canonical_type;
  var->decl_qual_type = qual_type;
  var->is_param = 1;
  var->next = registry->locals;
  var->next_binding = registry->all_bindings;
  registry->all_bindings = var;
  registry->locals = var;
  unsigned bucket = name_hash(name, name_len);
  var->next_hash = registry->lvars_by_bucket[bucket];
  registry->lvars_by_bucket[bucket] = var;
  return var;
}

lvar_t *ps_local_registry_create_static_alias_in(
    psx_local_registry_t *registry,
    global_var_t *global,
    char *name, int name_len, char *global_name, int global_name_len,
    const psx_type_t *type) {
  if (!registry || !name || name_len <= 0 ||
      !global_name || global_name_len <= 0 ||
      !type)
    return NULL;
  const psx_type_t *canonical_type = NULL;
  psx_qual_type_t qual_type = {0};
  if (!resolve_local_decl_type(
          registry, type, &canonical_type, &qual_type))
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
  var->decl_type = canonical_type;
  var->decl_qual_type = qual_type;
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
  const psx_type_t *current = ps_lvar_get_decl_type(var);
  if (!ps_type_is_incomplete_array(current) || !complete_type ||
      complete_type->kind != PSX_TYPE_ARRAY ||
      complete_type->array_len <= 0 || complete_type->is_vla ||
      !current->base || !complete_type->base) {
    return 0;
  }
  const psx_type_t *replacement = NULL;
  psx_qual_type_t qual_type = {0};
  if (!resolve_local_decl_type(
          registry, complete_type, &replacement, &qual_type))
    return 0;
  psx_qual_type_t current_base = psx_semantic_type_table_base(
      registry->semantic_types, var->decl_qual_type.type_id);
  psx_qual_type_t replacement_base = psx_semantic_type_table_base(
      registry->semantic_types, qual_type.type_id);
  if (current_base.type_id == PSX_TYPE_ID_INVALID ||
      current_base.type_id != replacement_base.type_id ||
      current_base.qualifiers != replacement_base.qualifiers)
    return 0;
  var->decl_type = replacement;
  var->decl_type_table = registry->semantic_types;
  var->decl_qual_type = qual_type;
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

lvar_t *ps_lvar_next_all(const lvar_t *var) {
  return var ? var->next_all : NULL;
}

lvar_t *ps_lvar_find_owner(lvar_t *head, int offset) {
  for (lvar_t *var = head; var; var = var->next_all) {
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
      .scope_seq = var->scope_seq,
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

void ps_local_registry_reset_in(psx_local_registry_t *registry) {
  if (!registry) return;
  for (lvar_t *var = registry->all_locals;
       var; var = var->next_all) {
    free(var->vla_runtime.param_inner_dim_consts);
    free(var->vla_runtime.param_inner_dim_src_offsets);
    var->vla_runtime.param_inner_dim_consts = NULL;
    var->vla_runtime.param_inner_dim_src_offsets = NULL;
    var->vla_runtime.param_inner_dim_count = 0;
  }
  registry->locals = NULL;
  registry->all_locals = NULL;
  registry->all_bindings = NULL;
  registry->lvar_scope_depth = 0;
  memset(registry->lvars_by_bucket, 0,
         sizeof(registry->lvars_by_bucket));
  memset(registry->lvars_by_offset, 0,
         sizeof(registry->lvars_by_offset));
  registry->next_scope_seq = 0;
  registry->current_scope_seq = 0;
  registry->next_declaration_seq = 0;
  ensure_scope_parent_capacity(registry, 0);
  registry->scope_parent_by_seq[0] = 0;
  registry->usage_events_head = NULL;
  registry->usage_events_tail = NULL;
  registry->current_usage_region = NULL;
  registry->current_function_name = NULL;
  registry->current_function_name_len = 0;
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

void ps_decl_enter_scope_in(psx_local_registry_t *registry) {
  if (!registry) return;
  if (registry->lvar_scope_depth < LVAR_SCOPE_STACK_MAX) {
    registry->lvar_scope_stack[registry->lvar_scope_depth] =
        registry->locals;
    registry->lvar_scope_seq_stack[registry->lvar_scope_depth] =
        registry->current_scope_seq;
  }
  registry->lvar_scope_depth++;
  unsigned parent_scope_seq = registry->current_scope_seq;
  registry->current_scope_seq = ++registry->next_scope_seq;
  ensure_scope_parent_capacity(registry, registry->current_scope_seq);
  registry->scope_parent_by_seq[registry->current_scope_seq] =
      parent_scope_seq;
}

void ps_decl_leave_scope_in(psx_local_registry_t *registry) {
  if (!registry || registry->lvar_scope_depth <= 0) return;
  registry->lvar_scope_depth--;
  if (registry->lvar_scope_depth < LVAR_SCOPE_STACK_MAX) {
    lvar_t *restore =
        registry->lvar_scope_stack[registry->lvar_scope_depth];
    for (lvar_t *var = registry->locals;
         var != restore; var = var->next)
      index_remove(registry, var);
    registry->locals = restore;
    registry->current_scope_seq =
        registry->lvar_scope_seq_stack[registry->lvar_scope_depth];
  }
}

lvar_t *ps_decl_get_locals_in(const psx_local_registry_t *registry) {
  return registry ? registry->all_locals : NULL;
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
    psx_local_registry_t *registry, lvar_t *all) {
  if (!registry) return;
  for (lvar_t *var = all; var; var = var->next_all) {
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

lvar_t *ps_decl_find_lvar_in(
    const psx_local_registry_t *registry, char *name, int len) {
  if (!registry || !name || len <= 0) return NULL;
  unsigned bucket = name_hash(name, len);
  for (lvar_t *var = registry->lvars_by_bucket[bucket];
       var; var = var->next_hash) {
    if (var->len == len && memcmp(var->name, name, (size_t)len) == 0)
      return var;
  }
  return NULL;
}
