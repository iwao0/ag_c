#include "local_registry.h"

#include "decl.h"
#include "diag.h"
#include "lvar_internal.h"
#include "node_utils.h"
#include "type.h"
#include "../tokenizer/tokenizer.h"
#include <stdlib.h>
#include <string.h>

#define LVAR_SCOPE_STACK_MAX 256
#define LVAR_HASH_BUCKETS 256u

static lvar_t *locals;
static lvar_t *all_locals;
static lvar_t *all_bindings;
static lvar_t *lvar_scope_stack[LVAR_SCOPE_STACK_MAX];
static unsigned lvar_scope_seq_stack[LVAR_SCOPE_STACK_MAX];
static int lvar_scope_depth;
static unsigned next_scope_seq;
static unsigned current_scope_seq;
static unsigned next_declaration_seq;
static unsigned *scope_parent_by_seq;
static size_t scope_parent_capacity;
static lvar_t *lvars_by_bucket[LVAR_HASH_BUCKETS];
static lvar_t *lvars_by_offset[LVAR_HASH_BUCKETS];

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

static lvar_usage_event_t *usage_events_head;
static lvar_usage_event_t *usage_events_tail;
static psx_lvar_usage_region_t *current_usage_region;

static unsigned name_hash(const char *name, int len) {
  unsigned hash = 2166136261u;
  for (int i = 0; i < len; i++)
    hash = (hash ^ (unsigned char)name[i]) * 16777619u;
  return hash & (LVAR_HASH_BUCKETS - 1u);
}

static unsigned offset_hash(int offset) {
  return (((unsigned)offset) * 2654435761u) >> 24;
}

static void ensure_scope_parent_capacity(unsigned scope_seq) {
  if ((size_t)scope_seq < scope_parent_capacity) return;
  size_t capacity = scope_parent_capacity ? scope_parent_capacity : 16;
  while (capacity <= (size_t)scope_seq) capacity *= 2;
  unsigned *grown = realloc(
      scope_parent_by_seq, capacity * sizeof(*scope_parent_by_seq));
  if (!grown) {
    ps_diag_ctx(tk_get_current_token(), "scope",
                "local scope ancestry allocation failed");
  }
  memset(grown + scope_parent_capacity, 0,
         (capacity - scope_parent_capacity) * sizeof(*grown));
  scope_parent_by_seq = grown;
  scope_parent_capacity = capacity;
}

int ps_local_registry_scope_is_visible_from(
    unsigned declaration_scope, unsigned reference_scope) {
  for (;;) {
    if (reference_scope == declaration_scope) return 1;
    if (reference_scope == 0 ||
        (size_t)reference_scope >= scope_parent_capacity)
      return 0;
    reference_scope = scope_parent_by_seq[reference_scope];
  }
}

static void index_add(lvar_t *var) {
  var->scope_seq = current_scope_seq;
  var->declaration_seq = ps_local_registry_register_binding_event();
  unsigned name_bucket = name_hash(var->name, var->len);
  var->next_hash = lvars_by_bucket[name_bucket];
  lvars_by_bucket[name_bucket] = var;
  unsigned offset_bucket = offset_hash(var->offset);
  var->next_offhash = lvars_by_offset[offset_bucket];
  lvars_by_offset[offset_bucket] = var;
}

static void index_remove(lvar_t *var) {
  unsigned bucket = name_hash(var->name, var->len);
  lvar_t **cursor = &lvars_by_bucket[bucket];
  while (*cursor) {
    if (*cursor == var) {
      *cursor = var->next_hash;
      return;
    }
    cursor = &(*cursor)->next_hash;
  }
}

static void offset_index_remove(lvar_t *var) {
  unsigned bucket = offset_hash(var->offset);
  lvar_t **cursor = &lvars_by_offset[bucket];
  while (*cursor) {
    if (*cursor == var) {
      *cursor = var->next_offhash;
      return;
    }
    cursor = &(*cursor)->next_offhash;
  }
}

unsigned ps_local_registry_current_scope_seq(void) {
  return current_scope_seq;
}

unsigned ps_local_registry_register_binding_event(void) {
  return ++next_declaration_seq;
}

psx_local_lookup_point_t ps_local_registry_capture_lookup_point(void) {
  return (psx_local_lookup_point_t){
      .scope_seq = current_scope_seq,
      .declaration_seq = next_declaration_seq,
  };
}

lvar_t *ps_local_registry_find_visible(
    char *name, int name_len, psx_local_lookup_point_t point) {
  if (!name || name_len <= 0) return NULL;
  for (lvar_t *var = all_bindings; var; var = var->next_binding) {
    if (var->declaration_seq > point.declaration_seq ||
        var->len != name_len ||
        memcmp(var->name, name, (size_t)name_len) != 0)
      continue;
    if (ps_local_registry_scope_is_visible_from(
            var->scope_seq, point.scope_seq))
      return var;
  }
  return NULL;
}

void psx_local_registry_add(lvar_t *var) {
  if (!var) return;
  var->next = locals;
  var->next_all = all_locals;
  var->next_binding = all_bindings;
  all_locals = var;
  all_bindings = var;
  locals = var;
  index_add(var);
}

lvar_t *ps_local_registry_create_storage_object(
    char *name, int name_len, int offset, int storage_size,
    int element_size, int is_array, int alignment) {
  lvar_t *previous = ps_decl_find_lvar(name, name_len);
  if (previous &&
      previous->scope_seq == ps_local_registry_current_scope_seq()) {
    ps_diag_duplicate_with_name(
        tk_get_current_token(), "variable", name, name_len);
  }

  lvar_t *var = calloc(1, sizeof(*var));
  if (!var) return NULL;
  var->name = name;
  var->len = name_len;
  var->offset = offset;
  var->size = storage_size;
  var->elem_size = element_size;
  var->is_array = is_array ? 1 : 0;
  var->align_bytes = alignment;
  var->decl_type = psx_type_new_storage_object(
      storage_size, element_size, is_array, TK_FLOAT_KIND_NONE, 0,
      TK_EOF, NULL, 0, 0, 0);
  psx_decl_attach_lvar_current_region(var);
  psx_local_registry_add(var);
  return var;
}

lvar_t *ps_local_registry_create_type_binding(
    char *name, int name_len, const psx_type_t *type) {
  if (!name || name_len <= 0 || !type) return NULL;
  lvar_t *previous = ps_decl_find_lvar(name, name_len);
  if (previous &&
      previous->scope_seq == ps_local_registry_current_scope_seq()) {
    ps_diag_duplicate_with_name(
        tk_get_current_token(), "parameter", name, name_len);
  }
  lvar_t *var = calloc(1, sizeof(*var));
  if (!var) return NULL;
  var->name = name;
  var->len = name_len;
  var->scope_seq = current_scope_seq;
  var->declaration_seq = ps_local_registry_register_binding_event();
  var->decl_type = ps_type_clone_persistent(type);
  var->size = ps_type_sizeof(type);
  var->elem_size = ps_type_deref_size(type);
  var->is_param = 1;
  var->next = locals;
  var->next_binding = all_bindings;
  all_bindings = var;
  locals = var;
  unsigned bucket = name_hash(name, name_len);
  var->next_hash = lvars_by_bucket[bucket];
  lvars_by_bucket[bucket] = var;
  return var;
}

lvar_t *ps_local_registry_create_static_alias(
    char *name, int name_len, int storage_size, int element_size,
    char *global_name, int global_name_len) {
  if (!name || name_len <= 0 || !global_name || global_name_len <= 0)
    return NULL;
  lvar_t *var = calloc(1, sizeof(*var));
  if (!var) return NULL;
  var->name = name;
  var->len = name_len;
  var->size = storage_size;
  var->elem_size = element_size;
  var->is_static_local = 1;
  var->static_global_name = global_name;
  var->static_global_name_len = global_name_len;
  psx_decl_attach_lvar_current_region(var);
  psx_local_registry_add(var);
  return var;
}

void ps_local_registry_update_storage_object(
    lvar_t *var, int offset, int storage_size,
    int element_size, int is_array, int alignment) {
  if (!var) return;
  offset_index_remove(var);
  var->offset = offset;
  var->size = storage_size;
  var->elem_size = element_size;
  var->is_array = is_array ? 1 : 0;
  var->align_bytes = alignment;
  unsigned bucket = offset_hash(offset);
  var->next_offhash = lvars_by_offset[bucket];
  lvars_by_offset[bucket] = var;
}

void ps_local_registry_mark_parameter(lvar_t *var, int is_byref) {
  if (!var) return;
  var->is_param = 1;
  var->is_byref_param = is_byref ? 1 : 0;
}

static void sync_type_cache(lvar_t *var) {
  if (!var || !var->decl_type) return;
  const psx_type_t *type = var->decl_type;
  var->is_array = type->kind == PSX_TYPE_ARRAY;
  var->is_vla = type->is_vla ? 1 : 0;
}

void ps_local_registry_set_decl_type(
    lvar_t *var, const psx_type_t *decl_type) {
  if (!var || !decl_type) return;
  var->decl_type = ps_type_clone_persistent(decl_type);
  sync_type_cache(var);
}

void ps_local_registry_set_vla_descriptor(
    lvar_t *var, int outer_stride, int row_stride_frame_off,
    int strides_remaining, int row_stride_src_offset,
    int row_stride_elem_size) {
  if (!var) return;
  psx_type_t *type = ps_lvar_get_decl_type(var);
  var->is_vla = 1;
  var->vla_row_stride_frame_off = row_stride_frame_off;
  var->vla_strides_remaining = strides_remaining;
  var->vla_row_stride_src_offset = row_stride_src_offset;
  var->vla_row_stride_elem_size = (short)row_stride_elem_size;
  if (!type) return;
  if (var->is_array && type->kind != PSX_TYPE_POINTER) {
    psx_type_t *vla = ps_type_new_vla_object_view(
        type, outer_stride, row_stride_frame_off, strides_remaining);
    if (vla) {
      var->decl_type = vla;
      type = vla;
    }
  } else if (!var->is_array && type->kind == PSX_TYPE_POINTER &&
             row_stride_frame_off != 0 && type->base &&
             type->base->kind != PSX_TYPE_ARRAY) {
    int elem_size = row_stride_elem_size > 0
                        ? row_stride_elem_size
                        : ps_type_sizeof(type->base);
    if (elem_size > 0) {
      psx_type_t *row = ps_type_new_array(
          type->base, 0, 0, elem_size, 1);
      row->vla_runtime_strides.outer_stride = elem_size;
      type->base = row;
    }
  }
  type->vla_runtime_strides.outer_stride = outer_stride;
  ps_type_set_vla_runtime_descriptor(
      type, row_stride_frame_off, strides_remaining,
      row_stride_src_offset, row_stride_elem_size);
}

void ps_local_registry_set_vla_param_inner_dims(
    lvar_t *var, const int *inner_dim_consts,
    const int *inner_dim_src_offsets, int inner_dim_count) {
  if (!var) return;
  if (inner_dim_count < 0) inner_dim_count = 0;
  if (inner_dim_count > 7) inner_dim_count = 7;
  var->vla_param_inner_dim_count = (unsigned char)inner_dim_count;
  for (int i = 0; i < 7; i++) {
    var->vla_param_inner_dim_consts[i] =
        (i < inner_dim_count && inner_dim_consts)
            ? (short)inner_dim_consts[i]
            : 0;
    var->vla_param_inner_dim_src_offsets[i] =
        (i < inner_dim_count && inner_dim_src_offsets)
            ? inner_dim_src_offsets[i]
            : 0;
  }
  ps_type_set_vla_param_inner_dims(
      ps_lvar_get_decl_type(var), inner_dim_consts,
      inner_dim_src_offsets, inner_dim_count);
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
      .is_array = var->is_array,
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

void ps_local_registry_reset(void) {
  locals = NULL;
  all_locals = NULL;
  all_bindings = NULL;
  lvar_scope_depth = 0;
  memset(lvars_by_bucket, 0, sizeof(lvars_by_bucket));
  memset(lvars_by_offset, 0, sizeof(lvars_by_offset));
  next_scope_seq = 0;
  current_scope_seq = 0;
  next_declaration_seq = 0;
  ensure_scope_parent_capacity(0);
  scope_parent_by_seq[0] = 0;
  usage_events_head = NULL;
  usage_events_tail = NULL;
  current_usage_region = NULL;
}

void ps_decl_enter_scope(void) {
  if (lvar_scope_depth < LVAR_SCOPE_STACK_MAX) {
    lvar_scope_stack[lvar_scope_depth] = locals;
    lvar_scope_seq_stack[lvar_scope_depth] = current_scope_seq;
  }
  lvar_scope_depth++;
  unsigned parent_scope_seq = current_scope_seq;
  current_scope_seq = ++next_scope_seq;
  ensure_scope_parent_capacity(current_scope_seq);
  scope_parent_by_seq[current_scope_seq] = parent_scope_seq;
}

void ps_decl_leave_scope(void) {
  if (lvar_scope_depth <= 0) return;
  lvar_scope_depth--;
  if (lvar_scope_depth < LVAR_SCOPE_STACK_MAX) {
    lvar_t *restore = lvar_scope_stack[lvar_scope_depth];
    for (lvar_t *var = locals; var != restore; var = var->next)
      index_remove(var);
    locals = restore;
    current_scope_seq = lvar_scope_seq_stack[lvar_scope_depth];
  }
}

lvar_t *ps_decl_get_locals(void) {
  return all_locals;
}

psx_lvar_usage_region_t *psx_decl_begin_lvar_usage_region(void) {
  psx_lvar_usage_region_t *region = calloc(1, sizeof(*region));
  region->prev = current_usage_region;
  current_usage_region = region;
  return region;
}

void psx_decl_end_lvar_usage_region(psx_lvar_usage_region_t *region) {
  if (!region) return;
  current_usage_region = region->prev;
}

void ps_decl_suppress_lvar_usage_region(psx_lvar_usage_region_t *region) {
  if (region) region->suppress_warnings = 1;
}

void psx_decl_attach_lvar_current_region(lvar_t *var) {
  if (var) var->decl_region = current_usage_region;
}

void ps_decl_record_lvar_usage_in_region(
    lvar_t *var, psx_lvar_usage_kind_t kind,
    psx_lvar_usage_region_t *region) {
  if (!var) return;
  lvar_usage_event_t *event = calloc(1, sizeof(*event));
  event->var = var;
  event->kind = kind;
  event->region = region;
  if (!usage_events_head)
    usage_events_head = event;
  else
    usage_events_tail->next = event;
  usage_events_tail = event;
}

void ps_decl_replay_lvar_usage_events(lvar_t *all) {
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
  for (lvar_usage_event_t *event = usage_events_head;
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

lvar_t *psx_decl_find_lvar_by_offset(int offset) {
  unsigned bucket = offset_hash(offset);
  for (lvar_t *var = lvars_by_offset[bucket];
       var; var = var->next_offhash) {
    if (var->offset == offset) return var;
  }
  return NULL;
}

lvar_t *ps_decl_find_lvar(char *name, int len) {
  unsigned bucket = name_hash(name, len);
  for (lvar_t *var = lvars_by_bucket[bucket]; var; var = var->next_hash) {
    if (var->len == len && memcmp(var->name, name, (size_t)len) == 0)
      return var;
  }
  return NULL;
}
