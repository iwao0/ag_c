#include "aggregate_member_resolution.h"

#include "../parser/semantic_ctx.h"
#include "../type_layout.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int align_up(int value, int alignment) {
  if (alignment <= 1) return value;
  return (value + alignment - 1) / alignment * alignment;
}

static int is_aggregate_kind(psx_type_kind_t kind) {
  return kind == PSX_TYPE_STRUCT || kind == PSX_TYPE_UNION;
}

typedef struct {
  const psx_semantic_type_table_t *semantic_types;
  const psx_record_layout_table_t *record_layouts;
  psx_type_id_t type_id;
  int bit_width;
  int pack_alignment;
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
    psx_semantic_context_t *semantic_context, psx_type_id_t type_id) {
  const psx_semantic_type_table_t *semantic_types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t type = {0};
  if (!psx_semantic_type_table_describe(semantic_types, type_id, &type))
    return PSX_AGGREGATE_MEMBER_INVALID;
  psx_qual_type_t stored_identity = psx_semantic_type_table_array_leaf(
      semantic_types, type_id);
  psx_type_shape_t stored = {0};
  if (!psx_semantic_type_table_describe(
          semantic_types, stored_identity.type_id, &stored))
    return PSX_AGGREGATE_MEMBER_INVALID;
  if (stored.kind == PSX_TYPE_POINTER)
    return PSX_AGGREGATE_MEMBER_OK;
  if (stored.kind == PSX_TYPE_FUNCTION)
    return PSX_AGGREGATE_MEMBER_FUNCTION_TYPE;
  if (stored.kind == PSX_TYPE_STRUCT || stored.kind == PSX_TYPE_UNION) {
    const psx_record_decl_t *record = ps_ctx_get_record_decl_in(
        semantic_context, stored.record_id);
    if (!record || !record->is_complete)
      return PSX_AGGREGATE_MEMBER_INCOMPLETE_TYPE;
  }
  return PSX_AGGREGATE_MEMBER_OK;
}

void psx_aggregate_layout_init(
    psx_aggregate_layout_state_t *state,
    const psx_record_decl_t *record) {
  if (!state) return;
  memset(state, 0, sizeof(*state));
  state->record_kind = record ? record->record_kind : PSX_TYPE_INVALID;
  state->record_id = record ? record->record_id : PSX_RECORD_ID_INVALID;
  state->alignment = 1;
  state->bitfield_storage_offset = -1;
}

static void finish_aggregate_bitfield_run(
    psx_aggregate_layout_state_t *state) {
  if (!state) return;
  if (state->record_kind != PSX_TYPE_UNION &&
      state->bitfield_storage_offset >= 0 &&
      state->bitfield_bits_used > 0) {
    int occupied_bytes = (state->bitfield_bits_used + 7) / 8;
    state->current_offset =
        state->bitfield_storage_offset + occupied_bytes;
  }
  state->bitfield_storage_offset = -1;
  state->bitfield_storage_size = 0;
  state->bitfield_bits_used = 0;
}

static void resolve_aggregate_bitfield_placement(
    psx_aggregate_layout_state_t *state,
    const aggregate_bitfield_request_t *request,
    const ag_data_layout_t *data_layout,
    aggregate_bitfield_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_AGGREGATE_MEMBER_INVALID;
  if (!state || !request || !is_aggregate_kind(state->record_kind) ||
      !request->semantic_types ||
      !request->record_layouts ||
      request->type_id == PSX_TYPE_ID_INVALID ||
      request->bit_width < 0 || request->pack_alignment < 0) {
    return;
  }
  psx_type_shape_t type = {0};
  if (!psx_semantic_type_table_describe(
          request->semantic_types, request->type_id, &type))
    return;
  if (type.kind != PSX_TYPE_INTEGER && type.kind != PSX_TYPE_BOOL) {
    resolution->status = PSX_AGGREGATE_MEMBER_INVALID_BITFIELD_TYPE;
    return;
  }
  int storage_size =
      psx_type_layout_sizeof(request->semantic_types, request->record_layouts,
                        request->type_id, data_layout);
  if (storage_size <= 0) return;
  if (storage_size > 8) storage_size = 8;
  resolution->storage_size = storage_size;
  resolution->bit_is_signed =
      type.kind != PSX_TYPE_BOOL && !type.is_unsigned &&
      type.integer_kind != PSX_INTEGER_KIND_ENUM;
  int storage_bits = storage_size * 8;
  int member_alignment = storage_size;
  if (request->pack_alignment > 0 &&
      request->pack_alignment < member_alignment)
    member_alignment = request->pack_alignment;
  if (request->bit_width > storage_bits) {
    resolution->status = PSX_AGGREGATE_MEMBER_BIT_WIDTH_EXCEEDS_STORAGE;
    return;
  }
  if (request->bit_width == 0) {
    finish_aggregate_bitfield_run(state);
    if (state->record_kind != PSX_TYPE_UNION)
      state->current_offset = align_up(state->current_offset, storage_size);
    resolution->storage_size = storage_size;
    resolution->offset = state->record_kind == PSX_TYPE_UNION
                             ? 0 : state->current_offset;
    resolution->status = PSX_AGGREGATE_MEMBER_OK;
    return;
  }

  if (state->record_kind == PSX_TYPE_UNION) {
    state->bitfield_storage_offset = 0;
    state->bitfield_storage_size = storage_size;
    state->bitfield_bits_used = request->bit_width;
    int occupied_size = (request->bit_width + 7) / 8;
    if (occupied_size > state->union_size)
      state->union_size = occupied_size;
    if (member_alignment > state->alignment)
      state->alignment = member_alignment;
    resolution->offset = 0;
    resolution->storage_size = storage_size;
    resolution->status = PSX_AGGREGATE_MEMBER_OK;
    return;
  }

  long long next_bit =
      state->bitfield_storage_offset >= 0
          ? (long long)state->bitfield_storage_offset * 8 +
                state->bitfield_bits_used
          : (long long)state->current_offset * 8;
  long long container_bit_start = 0;
  int bits_before = 0;
  if (request->pack_alignment > 0) {
    container_bit_start =
        state->bitfield_storage_offset >= 0
            ? (long long)state->bitfield_storage_offset * 8
            : next_bit / 8 * 8;
    bits_before = (int)(next_bit - container_bit_start);
    if (bits_before + request->bit_width > storage_bits) {
      container_bit_start = next_bit / 8 * 8;
      bits_before = (int)(next_bit - container_bit_start);
      if (bits_before + request->bit_width > storage_bits) {
        container_bit_start = (next_bit + 7) / 8 * 8;
        bits_before = 0;
      }
    }
  } else {
    container_bit_start = next_bit / storage_bits * storage_bits;
    bits_before = (int)(next_bit - container_bit_start);
    if (bits_before + request->bit_width > storage_bits) {
      long long next_byte = (next_bit + 7) / 8;
      long long aligned_byte =
          (next_byte + storage_size - 1) / storage_size * storage_size;
      container_bit_start = aligned_byte * 8;
      bits_before = 0;
    }
  }
  long long container_start = container_bit_start / 8;
  if (container_start < 0 ||
      container_start > INT_MAX - storage_size)
    return;
  state->bitfield_storage_offset = (int)container_start;
  state->bitfield_storage_size = storage_size;
  state->bitfield_bits_used = bits_before;
  state->current_offset = (int)container_start + storage_size;
  if (member_alignment > state->alignment)
    state->alignment = member_alignment;
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
  if (!state || !request || !is_aggregate_kind(state->record_kind) ||
      request->storage_size < 0 || request->natural_alignment <= 0 ||
      request->pack_alignment < 0 || request->requested_alignment < 0) {
    return;
  }
  finish_aggregate_bitfield_run(state);

  int alignment = request->natural_alignment;
  if (request->pack_alignment > 0 && request->pack_alignment < alignment)
    alignment = request->pack_alignment;
  if (request->requested_alignment > alignment)
    alignment = request->requested_alignment;
  if (alignment > state->alignment) state->alignment = alignment;

  placement->offset = 0;
  if (state->record_kind == PSX_TYPE_UNION) {
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
  if (!state || !is_aggregate_kind(state->record_kind)) return 0;
  int size = state->record_kind == PSX_TYPE_UNION
                 ? state->union_size : state->current_offset;
  if (state->record_kind != PSX_TYPE_UNION &&
      state->bitfield_storage_offset >= 0 &&
      state->bitfield_bits_used > 0) {
    int occupied_bytes = (state->bitfield_bits_used + 7) / 8;
    size = state->bitfield_storage_offset + occupied_bytes;
  }
  return align_up(size, state->alignment);
}

int psx_aggregate_layout_alignment(const psx_aggregate_layout_state_t *state) {
  return state && state->alignment > 0 ? state->alignment : 1;
}

static int collect_promoted_aggregate_members(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t source_type, int base_offset,
    psx_record_member_decl_t **out_declarations,
    psx_record_member_layout_t **out_layouts,
    int *out_member_count) {
  *out_declarations = NULL;
  *out_layouts = NULL;
  *out_member_count = 0;
  const psx_semantic_type_table_t *semantic_types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t source_shape = {0};
  if (base_offset < 0 ||
      !psx_semantic_type_table_describe(
          semantic_types, source_type.type_id, &source_shape) ||
      !is_aggregate_kind(source_shape.kind))
    return 0;
  psx_record_id_t record_id = source_shape.record_id;
  if (record_id == PSX_RECORD_ID_INVALID) return 0;
  const psx_record_decl_t *record = ps_ctx_get_record_decl_in(
      semantic_context, record_id);
  const psx_record_layout_t *record_layout = psx_record_layout_table_lookup(
      ps_ctx_record_layout_table_in(semantic_context), record_id,
      ps_ctx_data_layout(semantic_context));
  if (!record || !record_layout || !record->is_complete ||
      (record->member_count > 0 && !record->members) ||
      record_layout->member_count < record->member_count)
    return 0;
  int source_count = record->member_count;
  psx_record_member_decl_t *declarations = source_count > 0
      ? calloc((size_t)source_count, sizeof(*declarations))
      : NULL;
  psx_record_member_layout_t *layouts = source_count > 0
      ? calloc((size_t)source_count, sizeof(*layouts))
      : NULL;
  if (source_count > 0 && (!declarations || !layouts)) {
    free(declarations);
    free(layouts);
    return 0;
  }
  int member_count = 0;
  for (int i = 0; i < source_count; i++) {
    const psx_record_member_decl_t *source_declaration =
        &record->members[i];
    const psx_record_member_layout_t *source_layout =
        psx_record_layout_member(record_layout, i);
    if (source_declaration->len == 0) continue;
    if (!source_layout || source_layout->offset < 0 ||
        base_offset > INT_MAX - source_layout->offset) {
      free(declarations);
      free(layouts);
      return 0;
    }
    declarations[member_count] = *source_declaration;
    layouts[member_count] = *source_layout;
    layouts[member_count].offset = base_offset + source_layout->offset;
    member_count++;
  }
  *out_declarations = declarations;
  *out_layouts = layouts;
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
      layout->record_id == PSX_RECORD_ID_INVALID ||
      !is_aggregate_kind(layout->record_kind) ||
      request->base_qual_type.type_id == PSX_TYPE_ID_INVALID ||
      !request->declarator_shape ||
      request->member_name_len < 0 || request->pack_alignment < 0 ||
      request->requested_alignment < 0) {
    return;
  }
  psx_semantic_context_t *semantic_context = request->semantic_context;
  psx_aggregate_layout_state_t working_layout = *layout;

  int has_name = request->member_name != NULL;
  psx_qual_type_t identity = psx_resolve_decl_qual_type(
      &(psx_decl_type_request_t){
          .semantic_context = semantic_context,
          .base_qual_type = request->base_qual_type,
          .declarator_shape = request->declarator_shape,
      });
  if (identity.type_id == PSX_TYPE_ID_INVALID) return;
  psx_type_shape_t type_shape = {0};
  if (!psx_semantic_type_table_describe(
          ps_ctx_semantic_type_table_in(semantic_context),
          identity.type_id, &type_shape))
    return;
  int is_anonymous_aggregate =
      !has_name && is_aggregate_kind(type_shape.kind);
  if (!has_name && !is_anonymous_aggregate && !request->has_bitfield) {
    resolution->status = PSX_AGGREGATE_MEMBER_MISSING_NAME;
    return;
  }
  resolution->type_id = identity.type_id;

  if (request->has_bitfield) {
    aggregate_bitfield_resolution_t bitfield;
    resolve_aggregate_bitfield_placement(
        &working_layout,
        &(aggregate_bitfield_request_t){
            .semantic_types = ps_ctx_semantic_type_table_in(semantic_context),
            .record_layouts = ps_ctx_record_layout_table_in(semantic_context),
            .type_id = identity.type_id,
            .bit_width = request->bit_width,
            .pack_alignment = request->pack_alignment,
        },
        ps_ctx_data_layout(semantic_context), &bitfield);
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
    const ag_data_layout_t *data_layout = ps_ctx_data_layout(semantic_context);
    const psx_semantic_type_table_t *semantic_types =
        ps_ctx_semantic_type_table_in(semantic_context);
    resolution->status = validate_aggregate_member_type(
        semantic_context, identity.type_id);
    if (resolution->status != PSX_AGGREGATE_MEMBER_OK) return;
    const psx_record_layout_table_t *record_layouts =
        ps_ctx_record_layout_table_in(semantic_context);
    int storage_size = psx_qual_type_layout_sizeof(
        semantic_types, record_layouts, identity, data_layout);
    int storage_alignment = psx_qual_type_layout_alignof(
        semantic_types, record_layouts, identity, data_layout);
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

  psx_record_member_decl_t *promoted_declarations = NULL;
  psx_record_member_layout_t *promoted_layouts = NULL;
  int promoted_count = 0;
  if (is_anonymous_aggregate) {
    if (!collect_promoted_aggregate_members(
            semantic_context, identity, resolution->offset,
            &promoted_declarations, &promoted_layouts,
            &promoted_count))
      return;
  }

  int own_member_count = has_name || is_anonymous_aggregate ? 1 : 0;
  int batch_count = own_member_count + promoted_count;
  psx_record_member_decl_t *batch_declarations = batch_count > 0
      ? calloc((size_t)batch_count, sizeof(*batch_declarations))
      : NULL;
  psx_record_member_layout_t *batch_layouts = batch_count > 0
      ? calloc((size_t)batch_count, sizeof(*batch_layouts))
      : NULL;
  if (batch_count > 0 && (!batch_declarations || !batch_layouts)) {
    free(promoted_declarations);
    free(promoted_layouts);
    free(batch_declarations);
    free(batch_layouts);
    return;
  }
  if (own_member_count) {
    batch_declarations[0] = (psx_record_member_decl_t){
        .name = has_name ? request->member_name : (char *)"",
        .len = has_name ? request->member_name_len : 0,
        .bit_width = request->has_bitfield ? request->bit_width : 0,
        .bit_is_signed = resolution->bit_is_signed,
        .decl_qual_type = identity,
    };
    batch_layouts[0] = (psx_record_member_layout_t){
        .offset = resolution->offset,
        .bit_offset = resolution->bit_offset,
    };
  }
  if (promoted_count > 0) {
    memcpy(batch_declarations + own_member_count, promoted_declarations,
           (size_t)promoted_count * sizeof(*batch_declarations));
    memcpy(batch_layouts + own_member_count, promoted_layouts,
           (size_t)promoted_count * sizeof(*batch_layouts));
  }
  free(promoted_declarations);
  free(promoted_layouts);

  int conflict_index = -1;
  int registered =
      batch_count <= 0 ||
      ps_ctx_register_record_members_in(
          semantic_context, layout->record_id,
          batch_declarations, batch_layouts, batch_count,
          &conflict_index);
  if (!registered) {
    resolution->status = PSX_AGGREGATE_MEMBER_DUPLICATE;
    if (conflict_index >= 0 && conflict_index < batch_count) {
      resolution->conflicting_name =
          batch_declarations[conflict_index].name;
      resolution->conflicting_name_len =
          batch_declarations[conflict_index].len;
    }
    free(batch_declarations);
    free(batch_layouts);
    return;
  }
  resolution->registered_member_count = batch_count;
  free(batch_declarations);
  free(batch_layouts);
  *layout = working_layout;
  resolution->status = PSX_AGGREGATE_MEMBER_OK;
}
