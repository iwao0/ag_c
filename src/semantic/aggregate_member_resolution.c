#include "aggregate_member_resolution.h"

#include "../parser/semantic_ctx.h"

#include <string.h>

static int align_up(int value, int alignment) {
  if (alignment <= 1) return value;
  return (value + alignment - 1) / alignment * alignment;
}

static int is_aggregate_kind(token_kind_t kind) {
  return kind == TK_STRUCT || kind == TK_UNION;
}

static int collect_array_shape(const psx_type_t *type, int *dims,
                               int *dim_count, int *element_size) {
  int count = 0;
  while (type && type->kind == PSX_TYPE_ARRAY) {
    if (dims && count < 8) dims[count] = type->array_len;
    count++;
    type = type->base;
  }
  if (dim_count) *dim_count = count;
  if (element_size) {
    *element_size = ps_type_sizeof(type);
    if (*element_size <= 0 && type) *element_size = type->elem_size;
  }
  return count > 0;
}

psx_type_t *psx_resolve_aggregate_member_type(
    const psx_aggregate_member_type_request_t *request) {
  if (!request) return NULL;
  psx_decl_type_request_t declaration = request->declaration;
  if (!declaration.base_decl_type &&
      !psx_ctx_is_tag_aggregate_kind(declaration.tag_kind) &&
      declaration.base_kind == TK_EOF) {
    declaration.base_kind = TK_INT;
  }
  if (!declaration.base_decl_type &&
      !psx_ctx_is_tag_aggregate_kind(declaration.tag_kind) &&
      declaration.elem_size <= 0)
    declaration.elem_size = 4;
  psx_type_t *type = psx_resolve_decl_type(&declaration);
  if (!type) return NULL;
  return type;
}

void psx_plan_aggregate_member_storage(
    const psx_type_t *type, psx_aggregate_member_storage_plan_t *plan) {
  if (!plan) return;
  memset(plan, 0, sizeof(*plan));
  plan->status = PSX_AGGREGATE_MEMBER_INVALID;
  if (!type) return;

  plan->storage_size = ps_type_sizeof(type);
  plan->alignment = type->align;
  if (plan->alignment <= 0) plan->alignment = 1;
  const psx_type_t *value = type;
  long long element_count = 1;
  while (value && value->kind == PSX_TYPE_ARRAY) {
    if (plan->array_dim_count < 8)
      plan->array_dims[plan->array_dim_count] = value->array_len;
    plan->array_dim_count++;
    if (value->array_len <= 0) {
      plan->is_flexible_array = 1;
      element_count = 0;
    } else if (element_count > 0) {
      element_count *= value->array_len;
      if (element_count > 0x7fffffffLL) return;
    }
    value = value->base;
  }
  plan->array_element_count = (int)element_count;
  plan->value_size = ps_type_sizeof(value);
  if (plan->value_size <= 0 && value) plan->value_size = value->elem_size;
  plan->is_pointer_object = value && value->kind == PSX_TYPE_POINTER;
  plan->pointer_depth = psx_type_pointer_depth(value);
  if (plan->is_pointer_object) {
    plan->deref_size = ps_type_deref_size(value);
    if (plan->deref_size <= 0 && value->base)
      plan->deref_size = ps_type_sizeof(value->base);
    if (collect_array_shape(
            value->base, plan->pointee_array_dims,
            &plan->pointee_array_dim_count,
            &plan->pointee_array_element_size)) {
      plan->pointee_array_size = ps_type_sizeof(value->base);
    }
  }
  const psx_type_t *scalar = value;
  while (scalar && (scalar->kind == PSX_TYPE_POINTER ||
                    scalar->kind == PSX_TYPE_ARRAY ||
                    scalar->kind == PSX_TYPE_FUNCTION)) {
    scalar = scalar->base;
  }
  plan->scalar_size = ps_type_sizeof(scalar);
  if (plan->scalar_size <= 0 && scalar) plan->scalar_size = scalar->elem_size;
  plan->is_unsigned = scalar ? ps_type_is_unsigned(scalar) : 0;
  plan->fp_kind = scalar ? scalar->fp_kind : TK_FLOAT_KIND_NONE;
  plan->scalar_kind = scalar ? scalar->scalar_kind : TK_EOF;
  plan->is_bool = scalar && scalar->kind == PSX_TYPE_BOOL;
  plan->is_complex = scalar && scalar->kind == PSX_TYPE_COMPLEX;
  plan->is_atomic = scalar && scalar->is_atomic;
  if (plan->storage_size < 0 || plan->value_size < 0) return;
  plan->status = PSX_AGGREGATE_MEMBER_OK;
}

void psx_resolve_aggregate_member_base_type(
    const psx_aggregate_member_base_resolution_request_t *request,
    psx_aggregate_member_base_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_AGGREGATE_MEMBER_INVALID;
  if (!request) return;
  resolution->type = psx_resolve_aggregate_member_type(
      &(psx_aggregate_member_type_request_t){
          .declaration = request->declaration,
      });
  if (!resolution->type) return;
  psx_plan_aggregate_member_storage(
      resolution->type, &resolution->storage);
  if (resolution->storage.status != PSX_AGGREGATE_MEMBER_OK) return;
  resolution->status = PSX_AGGREGATE_MEMBER_OK;
}

psx_aggregate_member_status_t psx_validate_aggregate_member_type(
    const psx_type_t *type) {
  if (!type) return PSX_AGGREGATE_MEMBER_INVALID;
  const psx_type_t *stored = type;
  while (stored && stored->kind == PSX_TYPE_ARRAY) stored = stored->base;
  if (!stored) return PSX_AGGREGATE_MEMBER_INVALID;
  if (stored->kind == PSX_TYPE_POINTER)
    return PSX_AGGREGATE_MEMBER_OK;
  if (stored->kind == PSX_TYPE_FUNCTION)
    return PSX_AGGREGATE_MEMBER_FUNCTION_TYPE;
  if (ps_type_is_tag_aggregate(stored) && ps_type_sizeof(stored) <= 0 &&
      (!stored->aggregate_definition ||
       stored->aggregate_definition->align <= 0)) {
    return PSX_AGGREGATE_MEMBER_INCOMPLETE_TYPE;
  }
  return PSX_AGGREGATE_MEMBER_OK;
}

void psx_aggregate_layout_init(
    psx_aggregate_layout_state_t *state, token_kind_t kind) {
  if (!state) return;
  memset(state, 0, sizeof(*state));
  state->kind = kind;
  state->alignment = 1;
  state->bitfield_storage_offset = -1;
}

void psx_resolve_aggregate_bitfield_placement(
    psx_aggregate_layout_state_t *state,
    const psx_aggregate_bitfield_request_t *request,
    psx_aggregate_bitfield_resolution_t *resolution) {
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
  int storage_size = ps_type_sizeof(type);
  if (storage_size <= 0) return;
  if (storage_size > 8) storage_size = 8;
  resolution->storage_size = storage_size;
  resolution->bit_is_signed =
      type->kind != PSX_TYPE_BOOL && !ps_type_is_unsigned(type) &&
      !request->is_enum_type;
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

void psx_resolve_aggregate_object_placement(
    psx_aggregate_layout_state_t *state,
    const psx_aggregate_object_placement_request_t *request,
    psx_aggregate_object_placement_t *placement) {
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

void psx_resolve_aggregate_member(
    const psx_aggregate_member_resolution_request_t *request,
    psx_aggregate_member_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_AGGREGATE_MEMBER_INVALID;
  if (!request || !is_aggregate_kind(request->tag_kind) ||
      !request->tag_name || request->tag_name_len <= 0 ||
      !request->member_name || request->member_name_len < 0 ||
      request->offset < 0 || !request->type) {
    return;
  }
  tag_member_info_t member = {
      .name = request->member_name,
      .len = request->member_name_len,
      .offset = request->offset,
      .bit_width = request->bit_width,
      .bit_offset = request->bit_offset,
      .bit_is_signed = request->bit_is_signed,
      .decl_type = request->type,
  };
  int created = 0;
  if (!psx_ctx_register_tag_member(
          request->tag_kind, request->tag_name, request->tag_name_len,
          &member, &created)) {
    resolution->status = request->member_name_len > 0
                             ? PSX_AGGREGATE_MEMBER_DUPLICATE
                             : PSX_AGGREGATE_MEMBER_INVALID;
    return;
  }
  resolution->created = created;
  resolution->scope_depth = psx_ctx_current_tag_scope_depth();
  resolution->status = PSX_AGGREGATE_MEMBER_OK;
}

void psx_promote_aggregate_members(
    const psx_aggregate_member_promotion_request_t *request,
    psx_aggregate_member_promotion_t *promotion) {
  if (!promotion) return;
  memset(promotion, 0, sizeof(*promotion));
  promotion->status = PSX_AGGREGATE_MEMBER_INVALID;
  if (!request || !is_aggregate_kind(request->target_tag_kind) ||
      !request->target_tag_name || request->target_tag_name_len <= 0 ||
      !is_aggregate_kind(request->source_tag_kind) ||
      !request->source_tag_name || request->source_tag_name_len <= 0 ||
      request->base_offset < 0) {
    return;
  }

  int count = ps_ctx_get_tag_member_count(
      request->source_tag_kind, request->source_tag_name,
      request->source_tag_name_len);
  for (int i = 0; i < count; i++) {
    tag_member_info_t source = {0};
    if (!ps_ctx_get_tag_member_info(
            request->source_tag_kind, request->source_tag_name,
            request->source_tag_name_len, i, &source)) {
      return;
    }
    if (source.len == 0) continue;
    psx_aggregate_member_resolution_t member_resolution;
    psx_resolve_aggregate_member(
        &(psx_aggregate_member_resolution_request_t){
            .tag_kind = request->target_tag_kind,
            .tag_name = request->target_tag_name,
            .tag_name_len = request->target_tag_name_len,
            .member_name = source.name,
            .member_name_len = source.len,
            .offset = request->base_offset + source.offset,
            .type = source.decl_type,
            .bit_width = source.bit_width,
            .bit_offset = source.bit_offset,
            .bit_is_signed = source.bit_is_signed,
        },
        &member_resolution);
    if (member_resolution.status != PSX_AGGREGATE_MEMBER_OK) {
      promotion->status = member_resolution.status;
      promotion->conflicting_name = source.name;
      promotion->conflicting_name_len = source.len;
      return;
    }
    promotion->promoted_count++;
  }
  promotion->status = PSX_AGGREGATE_MEMBER_OK;
}

void psx_resolve_aggregate_member_declaration(
    psx_aggregate_layout_state_t *layout,
    const psx_aggregate_member_declaration_request_t *request,
    psx_aggregate_member_declaration_resolution_t *resolution) {
  if (!resolution) return;
  memset(resolution, 0, sizeof(*resolution));
  resolution->status = PSX_AGGREGATE_MEMBER_INVALID;
  if (!layout || !request ||
      !is_aggregate_kind(request->target_tag_kind) ||
      !request->target_tag_name || request->target_tag_name_len <= 0 ||
      !request->base_type || !request->declarator_shape ||
      request->member_name_len < 0 || request->pack_alignment < 0 ||
      request->requested_alignment < 0) {
    return;
  }
  psx_aggregate_layout_state_t working_layout = *layout;

  int has_name = request->member_name != NULL;
  int source_is_aggregate = is_aggregate_kind(request->source_tag_kind);
  if (!has_name && !source_is_aggregate && !request->has_bitfield) {
    resolution->status = PSX_AGGREGATE_MEMBER_MISSING_NAME;
    return;
  }

  resolution->type = psx_resolve_aggregate_member_type(
      &(psx_aggregate_member_type_request_t){
          .declaration = {
              .base_decl_type = request->base_type,
              .declarator_shape = request->declarator_shape,
          },
      });
  if (!resolution->type) return;

  if (request->has_bitfield) {
    psx_aggregate_bitfield_resolution_t bitfield;
    psx_resolve_aggregate_bitfield_placement(
        &working_layout,
        &(psx_aggregate_bitfield_request_t){
            .type = resolution->type,
            .is_enum_type = request->is_enum_type,
            .bit_width = request->bit_width,
        },
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
    resolution->status = psx_validate_aggregate_member_type(resolution->type);
    if (resolution->status != PSX_AGGREGATE_MEMBER_OK) return;
    psx_aggregate_member_storage_plan_t storage;
    psx_plan_aggregate_member_storage(resolution->type, &storage);
    if (storage.status != PSX_AGGREGATE_MEMBER_OK) {
      resolution->status = storage.status;
      return;
    }
    psx_aggregate_object_placement_t placement;
    psx_resolve_aggregate_object_placement(
        &working_layout,
        &(psx_aggregate_object_placement_request_t){
            .storage_size = storage.storage_size,
            .natural_alignment = storage.alignment,
            .pack_alignment = request->pack_alignment,
            .requested_alignment = request->requested_alignment,
        },
        &placement);
    resolution->status = placement.status;
    resolution->offset = placement.offset;
    resolution->storage_size = storage.storage_size;
    if (placement.status != PSX_AGGREGATE_MEMBER_OK) return;
  }

  if (has_name || source_is_aggregate) {
    char *name = has_name ? request->member_name : (char *)"";
    int name_len = has_name ? request->member_name_len : 0;
    psx_aggregate_member_resolution_t member;
    psx_resolve_aggregate_member(
        &(psx_aggregate_member_resolution_request_t){
            .tag_kind = request->target_tag_kind,
            .tag_name = request->target_tag_name,
            .tag_name_len = request->target_tag_name_len,
            .member_name = name,
            .member_name_len = name_len,
            .offset = resolution->offset,
            .type = resolution->type,
            .bit_width = request->has_bitfield ? request->bit_width : 0,
            .bit_offset = resolution->bit_offset,
            .bit_is_signed = resolution->bit_is_signed,
        },
        &member);
    if (member.status != PSX_AGGREGATE_MEMBER_OK) {
      resolution->status = member.status;
      resolution->conflicting_name = name;
      resolution->conflicting_name_len = name_len;
      return;
    }
    resolution->registered_member_count++;
  }

  psx_aggregate_member_storage_plan_t completed_storage;
  psx_plan_aggregate_member_storage(resolution->type, &completed_storage);
  if (completed_storage.status != PSX_AGGREGATE_MEMBER_OK) {
    resolution->status = completed_storage.status;
    return;
  }
  if (!has_name && source_is_aggregate &&
      !completed_storage.is_pointer_object && request->source_tag_name) {
    psx_aggregate_member_promotion_t promotion;
    psx_promote_aggregate_members(
        &(psx_aggregate_member_promotion_request_t){
            .target_tag_kind = request->target_tag_kind,
            .target_tag_name = request->target_tag_name,
            .target_tag_name_len = request->target_tag_name_len,
            .source_tag_kind = request->source_tag_kind,
            .source_tag_name = request->source_tag_name,
            .source_tag_name_len = request->source_tag_name_len,
            .base_offset = resolution->offset,
        },
        &promotion);
    if (promotion.status != PSX_AGGREGATE_MEMBER_OK) {
      resolution->status = promotion.status;
      resolution->conflicting_name = promotion.conflicting_name;
      resolution->conflicting_name_len = promotion.conflicting_name_len;
      return;
    }
    resolution->registered_member_count += promotion.promoted_count;
  }
  *layout = working_layout;
  resolution->status = PSX_AGGREGATE_MEMBER_OK;
}
