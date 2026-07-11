#include "declaration_resolution.h"

#include "../parser/semantic_ctx.h"
#include "../parser/tag_member_public.h"

#include <limits.h>

static psx_type_t *resolve_decl_base_type(
    const psx_decl_type_request_t *request) {
  if (request->base_decl_type) return psx_type_clone(request->base_decl_type);
  if (psx_ctx_is_tag_aggregate_kind(request->tag_kind)) {
    int scope_depth = ps_ctx_get_tag_scope_depth(
        request->tag_kind, request->tag_name, request->tag_len);
    psx_type_t *type = psx_type_new_tag(
        request->tag_kind, request->tag_name, request->tag_len,
        scope_depth >= 0 ? scope_depth + 1 : 0, request->elem_size);
    type->aggregate_definition = psx_ctx_get_tag_definition(
        request->tag_kind, request->tag_name, request->tag_len);
    return type;
  }
  if (request->is_complex) {
    psx_type_t *type = psx_type_new(PSX_TYPE_COMPLEX);
    type->fp_kind = request->fp_kind != TK_FLOAT_KIND_NONE
                        ? request->fp_kind
                        : TK_FLOAT_KIND_DOUBLE;
    type->size = request->elem_size;
    type->align = request->elem_size >= 8 ? 8 : request->elem_size;
    return type;
  }
  if (request->fp_kind != TK_FLOAT_KIND_NONE) {
    return psx_type_new_float(request->fp_kind, request->elem_size);
  }
  if (request->base_kind == TK_VOID) {
    psx_type_t *type = psx_type_new(PSX_TYPE_VOID);
    type->scalar_kind = TK_VOID;
    return type;
  }
  return psx_type_new_integer(
      request->base_kind, request->elem_size, request->is_unsigned);
}

psx_type_t *psx_resolve_decl_type(const psx_decl_type_request_t *request) {
  if (!request) return NULL;
  psx_type_t *type = resolve_decl_base_type(request);
  if (!type) return NULL;

  if (request->is_const_qualified) type->is_const_qualified = 1;
  if (request->is_volatile_qualified) type->is_volatile_qualified = 1;
  if (request->is_atomic) type->is_atomic = 1;
  if (request->is_long_long) type->is_long_long = 1;
  if (request->override_plain_char) {
    type->is_plain_char = request->is_plain_char ? 1 : 0;
  }
  if (request->is_long_double) type->is_long_double = 1;
  if (request->declarator_shape) {
    type = psx_type_apply_declarator_shape(
        type, request->declarator_shape);
  }
  psx_ctx_attach_aggregate_definitions(type);
  return type;
}

static int object_scalar_slots(const psx_type_t *type) {
  if (!type) return 0;
  if (type->kind == PSX_TYPE_ARRAY) {
    if (type->array_len <= 0) return 0;
    int child = object_scalar_slots(type->base);
    if (child <= 0 || child > INT_MAX / type->array_len) return 0;
    return child * type->array_len;
  }
  if (!ps_type_is_tag_aggregate(type)) return 1;
  const psx_aggregate_definition_t *definition = type->aggregate_definition;
  if (!definition || definition->member_count <= 0) return 0;
  if (type->kind == PSX_TYPE_UNION) {
    int max_slots = 0;
    int max_bytes = -1;
    for (int i = 0; i < definition->member_count; i++) {
      const tag_member_info_t *member = &definition->members[i];
      int slots = object_scalar_slots(ps_tag_member_decl_type(member));
      int bytes = ps_tag_member_decl_storage_size(member);
      if (bytes > max_bytes || (bytes == max_bytes && slots > max_slots)) {
        max_bytes = bytes;
        max_slots = slots;
      }
    }
    return max_slots > 0 ? max_slots : 1;
  }
  int slots = 0;
  int covered_end = -1;
  for (int i = 0; i < definition->member_count; i++) {
    const tag_member_info_t *member = &definition->members[i];
    if (member->offset < covered_end) continue;
    int member_slots = object_scalar_slots(ps_tag_member_decl_type(member));
    if (member_slots <= 0 || slots > INT_MAX - member_slots) return 0;
    slots += member_slots;
    if (member->len <= 0) {
      int size = ps_tag_member_decl_storage_size(member);
      int end = member->offset + size;
      if (size > 0 && end > covered_end) covered_end = end;
    }
  }
  return slots;
}

int psx_resolve_incomplete_array_type(
    psx_type_t *type, const psx_incomplete_array_resolution_t *request) {
  if (!type || !request || type->kind != PSX_TYPE_ARRAY || type->is_vla ||
      type->array_len > 0 || request->initializer_count <= 0) return 0;

  long long outer_count = request->initializer_count;
  if (!request->entries_initialize_outer_elements && type->base &&
      (type->base->kind == PSX_TYPE_ARRAY ||
       ps_type_is_tag_aggregate(type->base))) {
    int slots = object_scalar_slots(type->base);
    if (slots <= 0) return 0;
    outer_count = (outer_count + slots - 1) / slots;
  }
  if (outer_count <= 0 || outer_count > INT_MAX) return 0;
  return psx_type_complete_array(type, (int)outer_count);
}
