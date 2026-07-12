#include "declaration_resolution.h"

#include "constant_expression.h"

#include "../parser/semantic_ctx.h"
#include "../parser/tag_member_public.h"

#include <limits.h>

static psx_type_t *resolve_decl_base_type(
    const psx_decl_type_request_t *request) {
  if (request->base_decl_type) return ps_type_clone(request->base_decl_type);
  if (request->typedef_name) {
    const psx_type_t *typedef_type = NULL;
    if (!ps_ctx_find_typedef_decl_type(
            request->typedef_name, request->typedef_name_len,
            &typedef_type))
      return NULL;
    return ps_type_clone(typedef_type);
  }
  int elem_size = request->elem_size;
  if (request->tag_kind == TK_ENUM) {
    if (elem_size <= 0) elem_size = 4;
    int scope_depth = ps_ctx_get_tag_scope_depth(
        request->tag_kind, request->tag_name, request->tag_len);
    return ps_type_new_enum(
        request->tag_name, request->tag_len,
        scope_depth >= 0 ? scope_depth + 1 : 0, elem_size);
  }
  if (ps_ctx_is_tag_aggregate_kind(request->tag_kind)) {
    if (elem_size <= 0) {
      elem_size = ps_ctx_get_tag_size(
          request->tag_kind, request->tag_name, request->tag_len);
      if (elem_size < 0) elem_size = 0;
    }
    int scope_depth = ps_ctx_get_tag_scope_depth(
        request->tag_kind, request->tag_name, request->tag_len);
    psx_type_t *type = ps_type_new_tag(
        request->tag_kind, request->tag_name, request->tag_len,
        scope_depth >= 0 ? scope_depth + 1 : 0, elem_size);
    type->aggregate_definition = ps_ctx_get_tag_definition(
        request->tag_kind, request->tag_name, request->tag_len);
    if (type->aggregate_definition && type->aggregate_definition->align > 0)
      type->align = type->aggregate_definition->align;
    return type;
  }
  if (elem_size <= 0) {
    elem_size = ps_ctx_scalar_type_size(request->base_kind);
    if (request->is_complex) elem_size *= 2;
  }
  tk_float_kind_t fp_kind = request->fp_kind;
  if (fp_kind == TK_FLOAT_KIND_NONE) {
    if (request->base_kind == TK_FLOAT)
      fp_kind = TK_FLOAT_KIND_FLOAT;
    else if (request->base_kind == TK_DOUBLE)
      fp_kind = TK_FLOAT_KIND_DOUBLE;
  }
  if (request->is_complex) {
    psx_type_t *type = ps_type_new(PSX_TYPE_COMPLEX);
    type->fp_kind = fp_kind != TK_FLOAT_KIND_NONE
                        ? fp_kind
                        : TK_FLOAT_KIND_DOUBLE;
    type->size = elem_size;
    type->align = elem_size >= 8 ? 8 : elem_size;
    return type;
  }
  if (fp_kind != TK_FLOAT_KIND_NONE) {
    return ps_type_new_float(fp_kind, elem_size);
  }
  if (request->base_kind == TK_VOID) {
    psx_type_t *type = ps_type_new(PSX_TYPE_VOID);
    type->scalar_kind = TK_VOID;
    return type;
  }
  return ps_type_new_integer(
      request->base_kind, elem_size, request->is_unsigned);
}

psx_type_t *psx_resolve_decl_type(const psx_decl_type_request_t *request) {
  if (!request) return NULL;
  psx_type_t *type = resolve_decl_base_type(request);
  if (!type) return NULL;

  if (request->is_const_qualified || request->is_volatile_qualified) {
    ps_type_set_decl_spec_qualifiers(
        type,
        type->is_const_qualified || request->is_const_qualified,
        type->is_volatile_qualified || request->is_volatile_qualified);
  }
  if (request->is_atomic) type->is_atomic = 1;
  if (request->is_long_long) type->is_long_long = 1;
  if (request->override_plain_char) {
    type->is_plain_char = request->is_plain_char ? 1 : 0;
  }
  if (request->is_long_double) type->is_long_double = 1;
  if (request->declarator_shape) {
    type = ps_type_apply_declarator_shape(
        type, request->declarator_shape);
  }
  ps_ctx_attach_aggregate_definitions(type);
  return type;
}

psx_type_t *psx_resolve_decl_specifier_syntax(
    const psx_parsed_decl_specifier_t *specifier) {
  if (!specifier) return NULL;

  const psx_type_spec_result_t *syntax = &specifier->type_spec;
  psx_decl_type_request_t request = {
      .base_kind = TK_EOF,
      .fp_kind = TK_FLOAT_KIND_NONE,
      .tag_kind = TK_EOF,
      .is_unsigned = syntax->is_unsigned,
      .is_complex = syntax->is_complex,
      .is_const_qualified = syntax->is_const_qualified,
      .is_volatile_qualified = syntax->is_volatile_qualified,
      .is_atomic = syntax->is_atomic,
      .is_long_long = syntax->is_long_long,
      .is_plain_char = syntax->is_plain_char,
      .is_long_double = syntax->is_long_double,
  };
  switch (specifier->source) {
    case PSX_PARSED_DECL_TYPE_BUILTIN:
      request.base_kind = syntax->kind;
      request.override_plain_char = syntax->kind == TK_CHAR;
      break;
    case PSX_PARSED_DECL_TYPE_TAG:
      request.tag_kind = specifier->tag_action.kind;
      request.tag_name = specifier->tag_action.name;
      request.tag_len = specifier->tag_action.name_len;
      break;
    case PSX_PARSED_DECL_TYPEDEF_NAME:
      request.typedef_name = specifier->typedef_name->str;
      request.typedef_name_len = specifier->typedef_name->len;
      break;
    case PSX_PARSED_DECL_TYPE_IMPLICIT_INT:
      request.base_kind = TK_INT;
      break;
    default:
      return NULL;
  }
  return psx_resolve_decl_type(&request);
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
  return ps_type_complete_array(type, (int)outer_count);
}

static long long initializer_string_count(
    const psx_type_t *array_type, node_t *initializer) {
  if (!array_type || !array_type->base || !initializer ||
      initializer->kind != ND_STRING)
    return 0;
  const node_string_t *string = (const node_string_t *)initializer;
  int width = (int)string->char_width;
  if (width <= 0) width = 1;
  if (ps_type_sizeof(array_type->base) != width) return 0;
  return (long long)string->byte_len + 1;
}

static long long initializer_list_count(
    const node_init_list_t *list, int *entries_initialize_outer_elements) {
  if (!list || !entries_initialize_outer_elements) return 0;
  if (list->entry_count == 1 && list->entries[0].designator_count == 0) {
    node_t *value = list->entries[0].value;
    if (value && value->kind == ND_STRING) return -1;
  }

  long long cursor = 0;
  long long max_index = -1;
  for (int i = 0; i < list->entry_count; i++) {
    const psx_initializer_entry_t *entry = &list->entries[i];
    if (entry->value && entry->value->kind == ND_INIT_LIST)
      *entries_initialize_outer_elements = 1;
    if (entry->designator_count > 0 &&
        entry->designators[0].kind == PSX_INIT_DESIGNATOR_INDEX) {
      int ok = 1;
      long long index = psx_eval_const_int(
          entry->designators[0].index_expr, &ok);
      if (!ok || index < 0) return 0;
      long long end = index;
      if (entry->designators[0].is_range) {
        end = psx_eval_const_int(
            entry->designators[0].range_end_expr, &ok);
        if (!ok || end < index) return 0;
      }
      cursor = end;
      *entries_initialize_outer_elements = 1;
    }
    if (cursor > max_index) max_index = cursor;
    cursor++;
  }
  return max_index + 1;
}

int psx_resolve_incomplete_array_initializer(
    psx_type_t *type, psx_decl_init_kind_t initializer_kind,
    node_t *initializer) {
  if (!type || type->kind != PSX_TYPE_ARRAY || type->array_len > 0 ||
      type->is_vla || !initializer)
    return 0;

  long long count = 0;
  int entries_initialize_outer_elements = 0;
  if (initializer_kind == PSX_DECL_INIT_EXPR) {
    count = initializer_string_count(type, initializer);
  } else if (initializer_kind == PSX_DECL_INIT_LIST &&
             initializer->kind == ND_INIT_LIST) {
    node_init_list_t *list = (node_init_list_t *)initializer;
    count = initializer_list_count(
        list, &entries_initialize_outer_elements);
    if (count < 0) {
      count = initializer_string_count(type, list->entries[0].value);
      if (count <= 0) count = list->entry_count;
    }
  }
  return psx_resolve_incomplete_array_type(
      type, &(psx_incomplete_array_resolution_t){
                .initializer_count = count,
                .entries_initialize_outer_elements =
                    entries_initialize_outer_elements,
            });
}
