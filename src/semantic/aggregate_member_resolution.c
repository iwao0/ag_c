#include "aggregate_member_resolution.h"
#include "declaration_type_builder.h"

#include "../parser/semantic_ctx.h"
#include "../type_layout.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int align_up(int value, int alignment) {
  if (alignment <= 1) return value;
  return (value + alignment - 1) / alignment * alignment;
}

static int is_aggregate_kind(token_kind_t kind) {
  return kind == TK_STRUCT || kind == TK_UNION;
}

typedef struct {
  const psx_type_t *type;
  const psx_semantic_type_table_t *semantic_types;
  psx_type_id_t type_id;
  int bit_width;
} aggregate_bitfield_request_t;

typedef struct {
  psx_aggregate_member_status_t status;
  int offset;
  int bit_offset;
  int storage_size;
  int bit_is_signed;
} aggregate_bitfield_resolution_t;

typedef struct {
  int storage_size;
  int natural_alignment;
  int pack_alignment;
  int requested_alignment;
} aggregate_object_placement_request_t;

typedef struct {
  psx_aggregate_member_status_t status;
  int offset;
  int alignment;
} aggregate_object_placement_t;

static psx_aggregate_member_status_t validate_aggregate_member_type(
    const psx_semantic_type_table_t *semantic_types,
    psx_type_id_t type_id) {
  const psx_type_t *type = psx_semantic_type_table_lookup(
      semantic_types, type_id);
  if (!type) return PSX_AGGREGATE_MEMBER_INVALID;
  const psx_type_t *stored = ps_type_array_leaf_type(type);
  if (!stored) return PSX_AGGREGATE_MEMBER_INVALID;
  if (stored->kind == PSX_TYPE_POINTER)
    return PSX_AGGREGATE_MEMBER_OK;
  if (stored->kind == PSX_TYPE_FUNCTION)
    return PSX_AGGREGATE_MEMBER_FUNCTION_TYPE;
  if (ps_type_is_tag_aggregate(stored) &&
      (!stored->aggregate_definition ||
       !stored->aggregate_definition->is_complete)) {
    return PSX_AGGREGATE_MEMBER_INCOMPLETE_TYPE;
  }
  return PSX_AGGREGATE_MEMBER_OK;
}

void psx_aggregate_layout_init(
    psx_aggregate_layout_state_t *state, token_kind_t kind,
    psx_record_id_t record_id) {
  if (!state) return;
  memset(state, 0, sizeof(*state));
  state->kind = kind;
  state->record_id = record_id;
  state->alignment = 1;
  state->bitfield_storage_offset = -1;
}

static void resolve_aggregate_bitfield_placement(
    psx_aggregate_layout_state_t *state,
    const aggregate_bitfield_request_t *request,
    const ag_target_info_t *target,
    aggregate_bitfield_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_AGGREGATE_MEMBER_INVALID;
  if (!state || !request || !is_aggregate_kind(state->kind) ||
      !request->type || request->bit_width < 0) {
    return;
  }
  const psx_type_t *type = request->type;
  if (type->kind != PSX_TYPE_INTEGER && type->kind != PSX_TYPE_BOOL) {
    resolution->status = PSX_AGGREGATE_MEMBER_INVALID_BITFIELD_TYPE;
    return;
  }
  int storage_size = ps_type_sizeof_id_for_target(
      request->semantic_types, request->type_id, target);
  if (storage_size <= 0) return;
  if (storage_size > 8) storage_size = 8;
  resolution->storage_size = storage_size;
  resolution->bit_is_signed =
      type->kind != PSX_TYPE_BOOL && !ps_type_is_unsigned(type) &&
      type->scalar_kind != TK_ENUM;
  int storage_bits = storage_size * 8;
  if (request->bit_width > storage_bits) {
    resolution->status = PSX_AGGREGATE_MEMBER_BIT_WIDTH_EXCEEDS_STORAGE;
    return;
  }
  if (request->bit_width == 0) {
    state->bitfield_storage_offset = -1;
    state->bitfield_storage_size = 0;
    state->bitfield_bits_used = 0;
    if (state->kind != TK_UNION)
      state->current_offset = align_up(state->current_offset, storage_size);
    resolution->storage_size = storage_size;
    resolution->offset = state->kind == TK_UNION ? 0 : state->current_offset;
    resolution->status = PSX_AGGREGATE_MEMBER_OK;
    return;
  }

  if (state->kind == TK_UNION) {
    state->bitfield_storage_offset = 0;
    state->bitfield_storage_size = storage_size;
    state->bitfield_bits_used = request->bit_width;
    if (storage_size > state->union_size) state->union_size = storage_size;
    if (storage_size > state->alignment) state->alignment = storage_size;
    resolution->offset = 0;
    resolution->storage_size = storage_size;
    resolution->status = PSX_AGGREGATE_MEMBER_OK;
    return;
  }

  if (state->bitfield_storage_offset < 0 ||
      state->bitfield_storage_size != storage_size ||
      state->bitfield_bits_used + request->bit_width > storage_bits) {
    int container_start =
        state->current_offset - (state->current_offset % storage_size);
    int bits_before = (state->current_offset - container_start) * 8;
    if (state->bitfield_storage_offset < 0 &&
        bits_before + request->bit_width <= storage_bits) {
      state->bitfield_storage_offset = container_start;
      state->bitfield_storage_size = storage_size;
      state->bitfield_bits_used = bits_before;
      state->current_offset = container_start + storage_size;
    } else {
      state->current_offset = align_up(state->current_offset, storage_size);
      state->bitfield_storage_offset = state->current_offset;
      state->bitfield_storage_size = storage_size;
      state->bitfield_bits_used = 0;
      state->current_offset += storage_size;
    }
    if (storage_size > state->alignment) state->alignment = storage_size;
  }
  resolution->offset = state->bitfield_storage_offset;
  resolution->bit_offset = state->bitfield_bits_used;
  resolution->storage_size = state->bitfield_storage_size;
  state->bitfield_bits_used += request->bit_width;
  resolution->status = PSX_AGGREGATE_MEMBER_OK;
}

static void resolve_aggregate_object_placement(
    psx_aggregate_layout_state_t *state,
    const aggregate_object_placement_request_t *request,
    aggregate_object_placement_t *placement) {
  if (!placement) return;
  memset(placement, 0, sizeof(*placement));
  placement->status = PSX_AGGREGATE_MEMBER_INVALID;
  if (!state || !request || !is_aggregate_kind(state->kind) ||
      request->storage_size < 0 || request->natural_alignment <= 0 ||
      request->pack_alignment < 0 || request->requested_alignment < 0) {
    return;
  }
  state->bitfield_storage_offset = -1;
  state->bitfield_storage_size = 0;
  state->bitfield_bits_used = 0;

  int alignment = request->natural_alignment;
  if (alignment > 8) alignment = 8;
  if (request->pack_alignment > 0 && request->pack_alignment < alignment)
    alignment = request->pack_alignment;
  if (request->requested_alignment > alignment)
    alignment = request->requested_alignment;
  if (alignment > state->alignment) state->alignment = alignment;

  placement->offset = 0;
  if (state->kind == TK_UNION) {
    if (request->storage_size > state->union_size)
      state->union_size = request->storage_size;
  } else {
    state->current_offset = align_up(state->current_offset, alignment);
    placement->offset = state->current_offset;
    state->current_offset += request->storage_size;
  }
  placement->alignment = alignment;
  placement->status = PSX_AGGREGATE_MEMBER_OK;
}

int psx_aggregate_layout_size(const psx_aggregate_layout_state_t *state) {
  if (!state || !is_aggregate_kind(state->kind)) return 0;
  int size = state->kind == TK_UNION ? state->union_size
                                    : state->current_offset;
  return align_up(size, state->alignment);
}

int psx_aggregate_layout_alignment(const psx_aggregate_layout_state_t *state) {
  return state && state->alignment > 0 ? state->alignment : 1;
}

static int collect_promoted_aggregate_members(
    psx_semantic_context_t *semantic_context,
    const psx_type_t *source_type, int base_offset,
    tag_member_info_t **out_members, int *out_member_count) {
  *out_members = NULL;
  *out_member_count = 0;
  if (!ps_type_is_tag_aggregate(source_type) || !source_type->tag_name ||
      source_type->tag_len <= 0 || base_offset < 0)
    return 0;
  int source_count = ps_ctx_get_tag_member_count_in(
      semantic_context, source_type->tag_kind, source_type->tag_name,
      source_type->tag_len);
  if (source_count < 0) return 0;
  tag_member_info_t *members = source_count > 0
                                   ? calloc((size_t)source_count,
                                            sizeof(*members))
                                   : NULL;
  if (source_count > 0 && !members) return 0;
  int member_count = 0;
  for (int i = 0; i < source_count; i++) {
    tag_member_info_t source = {0};
    if (!ps_ctx_get_tag_member_info_in(
            semantic_context, source_type->tag_kind, source_type->tag_name,
            source_type->tag_len, i, &source)) {
      free(members);
      return 0;
    }
    if (source.len == 0) continue;
    if (source.offset < 0 || base_offset > INT_MAX - source.offset) {
      free(members);
      return 0;
    }
    members[member_count] = source;
    members[member_count].offset = base_offset + source.offset;
    member_count++;
  }
  *out_members = members;
  *out_member_count = member_count;
  return 1;
}

void psx_resolve_aggregate_member_declaration(
    psx_aggregate_layout_state_t *layout,
    const psx_aggregate_member_declaration_request_t *request,
    psx_aggregate_member_declaration_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_AGGREGATE_MEMBER_INVALID;
  if (!layout || !request || !request->semantic_context ||
      (layout->record_id == PSX_RECORD_ID_INVALID &&
       (!is_aggregate_kind(request->target_tag_kind) ||
        !request->target_tag_name || request->target_tag_name_len <= 0)) ||
      !request->base_type || !request->declarator_shape ||
      request->member_name_len < 0 || request->pack_alignment < 0 ||
      request->requested_alignment < 0) {
    return;
  }
  psx_semantic_context_t *semantic_context = request->semantic_context;
  psx_aggregate_layout_state_t working_layout = *layout;

  int has_name = request->member_name != NULL;
  psx_type_t *type = psx_build_decl_type(
      &(psx_decl_type_request_t){
          .semantic_context = semantic_context,
          .base_type = request->base_type,
          .declarator_shape = request->declarator_shape,
      });
  if (!type) return;
  resolution->type = type;
  int is_anonymous_aggregate =
      !has_name && ps_type_is_tag_aggregate(type);
  if (!has_name && !is_anonymous_aggregate && !request->has_bitfield) {
    resolution->status = PSX_AGGREGATE_MEMBER_MISSING_NAME;
    return;
  }
  psx_qual_type_t identity = ps_ctx_intern_qual_type_in(
      semantic_context, type);
  if (identity.type_id == PSX_TYPE_ID_INVALID) return;
  resolution->type_id = identity.type_id;

  if (request->has_bitfield) {
    aggregate_bitfield_resolution_t bitfield;
    resolve_aggregate_bitfield_placement(
        &working_layout,
        &(aggregate_bitfield_request_t){
            .type = type,
            .semantic_types = ps_ctx_semantic_type_table_in(
                semantic_context),
            .type_id = identity.type_id,
            .bit_width = request->bit_width,
        },
        ps_ctx_target_info(semantic_context),
        &bitfield);
    resolution->status = bitfield.status;
    resolution->offset = bitfield.offset;
    resolution->storage_size = bitfield.storage_size;
    resolution->bit_offset = bitfield.bit_offset;
    resolution->bit_is_signed = bitfield.bit_is_signed;
    if (bitfield.status != PSX_AGGREGATE_MEMBER_OK) return;
    if (!has_name) {
      *layout = working_layout;
      return;
    }
  } else {
    const ag_target_info_t *target = ps_ctx_target_info(semantic_context);
    const psx_semantic_type_table_t *semantic_types =
        ps_ctx_semantic_type_table_in(semantic_context);
    resolution->status = validate_aggregate_member_type(
        semantic_types, identity.type_id);
    if (resolution->status != PSX_AGGREGATE_MEMBER_OK) return;
    int storage_size = ps_type_sizeof_id_for_target(
        semantic_types, identity.type_id, target);
    int storage_alignment = ps_type_alignof_id_for_target(
        semantic_types, identity.type_id, target);
    if (storage_size < 0) return;
    if (storage_alignment <= 0) storage_alignment = 1;
    aggregate_object_placement_t placement;
    resolve_aggregate_object_placement(
        &working_layout,
        &(aggregate_object_placement_request_t){
            .storage_size = storage_size,
            .natural_alignment = storage_alignment,
            .pack_alignment = request->pack_alignment,
            .requested_alignment = request->requested_alignment,
        },
        &placement);
    resolution->status = placement.status;
    resolution->offset = placement.offset;
    resolution->storage_size = storage_size;
    if (placement.status != PSX_AGGREGATE_MEMBER_OK) return;
  }

  tag_member_info_t *promoted_members = NULL;
  int promoted_count = 0;
  if (is_anonymous_aggregate) {
    if (!collect_promoted_aggregate_members(
            semantic_context, type, resolution->offset,
            &promoted_members, &promoted_count))
      return;
  }

  int own_member_count = has_name || is_anonymous_aggregate ? 1 : 0;
  int batch_count = own_member_count + promoted_count;
  tag_member_info_t *batch = batch_count > 0
                                 ? calloc((size_t)batch_count, sizeof(*batch))
                                 : NULL;
  if (batch_count > 0 && !batch) {
    free(promoted_members);
    return;
  }
  if (own_member_count) {
    batch[0] = (tag_member_info_t){
        .name = has_name ? request->member_name : (char *)"",
        .len = has_name ? request->member_name_len : 0,
        .offset = resolution->offset,
        .bit_width = request->has_bitfield ? request->bit_width : 0,
        .bit_offset = resolution->bit_offset,
        .bit_is_signed = resolution->bit_is_signed,
        .decl_type = type,
    };
  }
  if (promoted_count > 0) {
    memcpy(batch + own_member_count, promoted_members,
           (size_t)promoted_count * sizeof(*batch));
  }
  free(promoted_members);

  int conflict_index = -1;
  int registered = batch_count <= 0;
  if (batch_count > 0 && layout->record_id != PSX_RECORD_ID_INVALID) {
    registered = ps_ctx_register_record_members_in(
        semantic_context, layout->record_id, batch, batch_count,
        &conflict_index);
  } else if (batch_count > 0) {
    registered = ps_ctx_register_tag_members_in(
        semantic_context, request->target_tag_kind,
        request->target_tag_name, request->target_tag_name_len,
        batch, batch_count, &conflict_index);
  }
  if (!registered) {
    resolution->status = PSX_AGGREGATE_MEMBER_DUPLICATE;
    if (conflict_index >= 0 && conflict_index < batch_count) {
      resolution->conflicting_name = batch[conflict_index].name;
      resolution->conflicting_name_len = batch[conflict_index].len;
    }
    free(batch);
    return;
  }
  resolution->registered_member_count = batch_count;
  free(batch);
  *layout = working_layout;
  resolution->status = PSX_AGGREGATE_MEMBER_OK;
}
