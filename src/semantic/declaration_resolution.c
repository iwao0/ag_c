#include "declaration_resolution.h"

#include "constant_expression.h"

#include "../parser/semantic_ctx.h"
#include "../type_layout.h"

#include <limits.h>

static psx_qual_type_t invalid_qual_type(void) {
  return (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                           PSX_TYPE_QUALIFIER_NONE};
}

static psx_qual_type_t resolve_tag_base_qual_type(
    psx_semantic_context_t *semantic_context,
    token_kind_t kind, char *name, int name_len) {
  int scope_depth = ps_ctx_get_tag_scope_depth_in(
      semantic_context, kind, name, name_len);
  if (kind == TK_ENUM) {
    return ps_ctx_intern_enum_qual_type_in(
        semantic_context, name, name_len,
        scope_depth >= 0 ? scope_depth + 1 : 0);
  }
  if (!ps_ctx_is_tag_aggregate_kind(kind)) return invalid_qual_type();
  psx_record_id_t record_id = ps_ctx_resolve_tag_record_id_in(
      semantic_context, kind, name, name_len);
  return ps_ctx_intern_record_qual_type_in(semantic_context, record_id);
}

static psx_qual_type_t resolve_builtin_base_qual_type(
    psx_semantic_context_t *semantic_context,
    token_kind_t kind, const psx_type_spec_result_t *specifier) {
  psx_floating_kind_t floating_kind = PSX_FLOATING_KIND_NONE;
  if (specifier->is_long_double)
    floating_kind = PSX_FLOATING_KIND_LONG_DOUBLE;
  else if (kind == TK_FLOAT)
    floating_kind = PSX_FLOATING_KIND_FLOAT;
  else if (kind == TK_DOUBLE)
    floating_kind = PSX_FLOATING_KIND_DOUBLE;
  if (specifier->is_complex)
    return ps_ctx_intern_floating_qual_type_in(
        semantic_context,
        floating_kind == PSX_FLOATING_KIND_NONE
            ? PSX_FLOATING_KIND_DOUBLE
            : floating_kind,
        1);
  if (floating_kind != PSX_FLOATING_KIND_NONE)
    return ps_ctx_intern_floating_qual_type_in(
        semantic_context, floating_kind, 0);
  if (kind == TK_VOID)
    return ps_ctx_intern_void_qual_type_in(semantic_context);
  psx_integer_kind_t integer_kind = PSX_INTEGER_KIND_INT;
  if (specifier->is_long_long)
    integer_kind = PSX_INTEGER_KIND_LONG_LONG;
  else if (kind == TK_BOOL)
    integer_kind = PSX_INTEGER_KIND_BOOL;
  else if (kind == TK_CHAR)
    integer_kind = PSX_INTEGER_KIND_CHAR;
  else if (kind == TK_SHORT)
    integer_kind = PSX_INTEGER_KIND_SHORT;
  else if (kind == TK_LONG)
    integer_kind = PSX_INTEGER_KIND_LONG;
  return ps_ctx_intern_integer_qual_type_in(
      semantic_context, integer_kind,
      specifier->is_unsigned, specifier->is_plain_char);
}

static psx_qual_type_t apply_decl_specifier_cv_qualifiers(
    psx_semantic_context_t *semantic_context, psx_qual_type_t type,
    psx_type_qualifiers_t qualifiers) {
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t shape = {0};
  if (!psx_semantic_type_table_describe(types, type.type_id, &shape))
    return invalid_qual_type();
  if (shape.kind == PSX_TYPE_FUNCTION) return type;
  if (shape.kind != PSX_TYPE_ARRAY) {
    type.qualifiers |= qualifiers;
    return type;
  }
  psx_qual_type_t element = psx_semantic_type_table_base(
      types, type.type_id);
  element = apply_decl_specifier_cv_qualifiers(
      semantic_context, element, qualifiers);
  if (element.type_id == PSX_TYPE_ID_INVALID) return element;
  psx_qual_type_t qualified_array = ps_ctx_intern_array_of_qual_type_in(
      semantic_context, element, shape.array_len, shape.is_vla);
  qualified_array.qualifiers = type.qualifiers;
  return qualified_array;
}

static psx_qual_type_t apply_decl_specifier_qualifiers(
    psx_semantic_context_t *semantic_context, psx_qual_type_t type,
    const psx_type_spec_result_t *specifier) {
  if (type.type_id == PSX_TYPE_ID_INVALID || !specifier) return type;
  psx_type_qualifiers_t cv_qualifiers = PSX_TYPE_QUALIFIER_NONE;
  if (specifier->is_const_qualified)
    cv_qualifiers |= PSX_TYPE_QUALIFIER_CONST;
  if (specifier->is_volatile_qualified)
    cv_qualifiers |= PSX_TYPE_QUALIFIER_VOLATILE;
  type = apply_decl_specifier_cv_qualifiers(
      semantic_context, type, cv_qualifiers);
  if (specifier->is_atomic)
    type.qualifiers |= PSX_TYPE_QUALIFIER_ATOMIC;
  return type;
}

static psx_qual_type_t apply_declarator_shape(
    const psx_decl_type_request_t *request) {
  if (!request || !request->semantic_context ||
      request->base_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return invalid_qual_type();
  psx_semantic_context_t *semantic_context = request->semantic_context;
  psx_qual_type_t type = request->base_qual_type;
  const psx_declarator_shape_t *shape = request->declarator_shape;
  if (!shape) return type;
  for (int i = shape->count - 1; i >= 0; i--) {
    const psx_declarator_op_t *op = &shape->ops[i];
    switch (op->kind) {
      case PSX_DECL_OP_POINTER:
        type = ps_ctx_intern_pointer_to_qual_type_in(
            semantic_context, type);
        if (op->is_const_qualified)
          type.qualifiers |= PSX_TYPE_QUALIFIER_CONST;
        if (op->is_volatile_qualified)
          type.qualifiers |= PSX_TYPE_QUALIFIER_VOLATILE;
        break;
      case PSX_DECL_OP_ARRAY:
        type = ps_ctx_intern_array_of_qual_type_in(
            semantic_context, type, op->array_len, op->is_vla_array);
        break;
      case PSX_DECL_OP_FUNCTION:
        type = ps_ctx_intern_function_qual_type_in(
            semantic_context, type,
            op->has_canonical_function_params
                ? op->function_param_qual_types : NULL,
            op->has_canonical_function_params
                ? op->function_param_count : 0,
            op->has_canonical_function_params
                ? op->function_has_prototype : 0,
            op->has_canonical_function_params
                ? op->function_is_variadic : 0);
        break;
      default:
        return invalid_qual_type();
    }
    if (type.type_id == PSX_TYPE_ID_INVALID) return type;
  }
  return type;
}

psx_qual_type_t psx_resolve_decl_qual_type(
    const psx_decl_type_request_t *request) {
  return apply_declarator_shape(request);
}

static psx_qual_type_t resolve_decl_specifier_qual_type(
    psx_semantic_context_t *semantic_context,
    const psx_parsed_decl_specifier_t *specifier) {
  if (!semantic_context || !specifier) return invalid_qual_type();

  const psx_type_spec_result_t *syntax = &specifier->type_spec;
  psx_qual_type_t type = invalid_qual_type();
  switch (specifier->source) {
    case PSX_PARSED_DECL_TYPE_BUILTIN:
      type = resolve_builtin_base_qual_type(
          semantic_context, syntax->kind, syntax);
      break;
    case PSX_PARSED_DECL_TYPE_TAG:
      type = resolve_tag_base_qual_type(
          semantic_context,
          specifier->tag_action.kind,
          specifier->tag_action.name,
          specifier->tag_action.name_len);
      break;
    case PSX_PARSED_DECL_TYPEDEF_NAME: {
      psx_typedef_info_t typedef_info;
      if (!specifier->typedef_name ||
          !ps_ctx_find_typedef_name_in(
              semantic_context,
              specifier->typedef_name->str,
              specifier->typedef_name->len,
              &typedef_info))
        return invalid_qual_type();
      type = ps_ctx_typedef_decl_qual_type(&typedef_info);
      break;
    }
    case PSX_PARSED_DECL_TYPE_IMPLICIT_INT:
      type = resolve_builtin_base_qual_type(
          semantic_context, TK_INT, syntax);
      break;
    default:
      return invalid_qual_type();
  }
  return apply_decl_specifier_qualifiers(
      semantic_context, type, syntax);
}

psx_qual_type_t psx_resolve_decl_specifier_qual_type_in_context(
    psx_semantic_context_t *semantic_context,
    const psx_parsed_decl_specifier_t *specifier) {
  return resolve_decl_specifier_qual_type(
      semantic_context, specifier);
}

static int object_scalar_slots_by_id(
    psx_semantic_context_t *semantic_context, psx_type_id_t type_id) {
  const psx_semantic_type_table_t *semantic_types =
      ps_ctx_semantic_type_table_in(semantic_context);
  const psx_record_layout_table_t *record_layouts =
      ps_ctx_record_layout_table_in(semantic_context);
  const ag_data_layout_t *data_layout = ps_ctx_data_layout(semantic_context);
  psx_type_shape_t shape = {0};
  if (!psx_semantic_type_table_describe(
          semantic_types, type_id, &shape))
    return 0;
  if (shape.kind == PSX_TYPE_ARRAY) {
    if (shape.array_len <= 0) return 0;
    psx_type_id_t base_type_id =
        psx_semantic_type_table_base(semantic_types, type_id).type_id;
    int child = object_scalar_slots_by_id(
        semantic_context, base_type_id);
    if (child <= 0 || child > INT_MAX / shape.array_len) return 0;
    return child * shape.array_len;
  }
  if (!psx_type_kind_is_aggregate(shape.kind)) return 1;
  psx_record_id_t record_id = shape.record_id;
  const psx_record_decl_t *record = ps_ctx_get_record_decl_in(
      semantic_context, record_id);
  const psx_record_layout_t *layout =
      psx_record_layout_table_lookup(record_layouts, record_id, data_layout);
  if (!record || !layout || record->member_count <= 0 ||
      layout->member_count < record->member_count)
    return 0;
  if (shape.kind == PSX_TYPE_UNION) {
    int max_slots = 0;
    int max_bytes = -1;
    for (int i = 0; i < record->member_count; i++) {
      psx_type_id_t member_type_id =
          psx_semantic_type_table_record_member(
              semantic_types, type_id, i).type_id;
      int slots = object_scalar_slots_by_id(
          semantic_context, member_type_id);
      int bytes = psx_type_layout_sizeof(semantic_types, record_layouts,
                                    member_type_id, data_layout);
      if (bytes > max_bytes || (bytes == max_bytes && slots > max_slots)) {
        max_bytes = bytes;
        max_slots = slots;
      }
    }
    return max_slots > 0 ? max_slots : 1;
  }
  int slots = 0;
  int covered_end = -1;
  for (int i = 0; i < record->member_count; i++) {
    const psx_record_member_decl_t *member = &record->members[i];
    const psx_record_member_layout_t *member_layout =
        psx_record_layout_member(layout, i);
    if (!member_layout) return 0;
    if (member_layout->offset < covered_end) continue;
    psx_type_id_t member_type_id =
        psx_semantic_type_table_record_member(
            semantic_types, type_id, i).type_id;
    int member_slots = object_scalar_slots_by_id(
        semantic_context, member_type_id);
    if (member_slots <= 0 || slots > INT_MAX - member_slots) return 0;
    slots += member_slots;
    if (member->len <= 0) {
      int size = psx_type_layout_sizeof(semantic_types, record_layouts,
                                   member_type_id, data_layout);
      int end = member_layout->offset + size;
      if (size > 0 && end > covered_end) covered_end = end;
    }
  }
  return slots;
}

int psx_resolve_completed_incomplete_array_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t incomplete_type,
    const psx_incomplete_array_resolution_t *request,
    psx_qual_type_t *completed_type) {
  if (completed_type)
    *completed_type = (psx_qual_type_t){
        PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  if (!semantic_context || !request || !completed_type ||
      incomplete_type.type_id == PSX_TYPE_ID_INVALID ||
      request->initializer_count <= 0)
    return 0;
  const psx_semantic_type_table_t *semantic_types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_type_shape_t shape = {0};
  if (!psx_semantic_type_table_describe(
          semantic_types, incomplete_type.type_id, &shape) ||
      shape.kind != PSX_TYPE_ARRAY || shape.is_vla ||
      shape.array_len > 0)
    return 0;
  psx_qual_type_t element = psx_semantic_type_table_base(
      semantic_types, incomplete_type.type_id);
  if (element.type_id == PSX_TYPE_ID_INVALID) return 0;

  long long outer_count = request->initializer_count;
  psx_type_shape_t element_shape = {0};
  if (!request->entries_initialize_outer_elements &&
      psx_semantic_type_table_describe(
          semantic_types, element.type_id, &element_shape) &&
      (element_shape.kind == PSX_TYPE_ARRAY ||
       psx_type_kind_is_aggregate(element_shape.kind))) {
    int slots = object_scalar_slots_by_id(
        semantic_context, element.type_id);
    if (slots <= 0) return 0;
    outer_count = (outer_count + slots - 1) / slots;
  }
  if (outer_count <= 0 || outer_count > INT_MAX) return 0;
  *completed_type = ps_ctx_intern_array_of_qual_type_in(
      semantic_context, element, (int)outer_count, 0);
  completed_type->qualifiers = incomplete_type.qualifiers;
  return completed_type->type_id != PSX_TYPE_ID_INVALID;
}

static long long initializer_string_count(
    const psx_semantic_type_table_t *semantic_types,
    psx_qual_type_t array_type, const node_t *initializer) {
  psx_type_shape_t array_shape = {0};
  if (!initializer || initializer->kind != ND_STRING ||
      !psx_semantic_type_table_describe(
          semantic_types, array_type.type_id, &array_shape) ||
      array_shape.kind != PSX_TYPE_ARRAY)
    return 0;
  psx_qual_type_t element = psx_semantic_type_table_base(
      semantic_types, array_type.type_id);
  psx_type_shape_t element_shape = {0};
  if (!psx_semantic_type_table_describe(
          semantic_types, element.type_id, &element_shape))
    return 0;
  const node_string_t *string = (const node_string_t *)initializer;
  int width = (int)string->char_width;
  if (width <= 0) width = 1;
  if (psx_type_shape_character_code_unit_width(&element_shape) != width)
    return 0;
  return (long long)string->byte_len + 1;
}

static int legacy_incomplete_array_constant_index(
    void *context, const node_t *expression, long long *value) {
  psx_semantic_context_t *semantic_context = context;
  int ok = 1;
  long long resolved = psx_eval_const_int(
      ps_ctx_resolution_store(semantic_context),
      (node_t *)expression, &ok);
  if (!ok) return 0;
  if (value) *value = resolved;
  return 1;
}

static long long initializer_list_count(
    const node_init_list_t *list,
    psx_incomplete_array_constant_index_resolver_t resolve_index,
    void *resolve_index_context,
    int *entries_initialize_outer_elements) {
  if (!list || !resolve_index || !entries_initialize_outer_elements)
    return 0;
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
      long long index = 0;
      if (!entry->designators[0].index_expr ||
          !resolve_index(
              resolve_index_context,
              entry->designators[0].index_expr, &index) ||
          index < 0)
        return 0;
      long long end = index;
      if (entry->designators[0].is_range) {
        if (!entry->designators[0].range_end_expr ||
            !resolve_index(
                resolve_index_context,
                entry->designators[0].range_end_expr, &end) ||
            end < index)
          return 0;
      }
      cursor = end;
      *entries_initialize_outer_elements = 1;
    }
    if (cursor > max_index) max_index = cursor;
    cursor++;
  }
  return max_index + 1;
}

int psx_resolve_incomplete_array_initializer_shape_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t incomplete_type,
    psx_decl_init_kind_t initializer_kind,
    const node_t *initializer,
    psx_incomplete_array_constant_index_resolver_t resolve_index,
    void *resolve_index_context,
    psx_incomplete_array_resolution_t *resolution) {
  if (resolution)
    *resolution = (psx_incomplete_array_resolution_t){0};
  const psx_semantic_type_table_t *semantic_types = semantic_context
      ? ps_ctx_semantic_type_table_in(semantic_context) : NULL;
  psx_type_shape_t shape = {0};
  if (!semantic_types || !resolution || !initializer ||
      !psx_semantic_type_table_describe(
          semantic_types, incomplete_type.type_id, &shape) ||
      shape.kind != PSX_TYPE_ARRAY || shape.array_len > 0 ||
      shape.is_vla)
    return 0;

  long long count = 0;
  int entries_initialize_outer_elements = 0;
  if (initializer_kind == PSX_DECL_INIT_EXPR) {
    count = initializer_string_count(
        semantic_types, incomplete_type, initializer);
  } else if (initializer_kind == PSX_DECL_INIT_LIST &&
             initializer->kind == ND_INIT_LIST) {
    const node_init_list_t *list =
        (const node_init_list_t *)initializer;
    count = initializer_list_count(
        list, resolve_index, resolve_index_context,
        &entries_initialize_outer_elements);
    if (count < 0) {
      count = initializer_string_count(
          semantic_types, incomplete_type,
          list->entries[0].value);
      if (count <= 0) count = list->entry_count;
    }
  }
  if (count <= 0) return 0;
  *resolution = (psx_incomplete_array_resolution_t){
      .initializer_count = count,
      .entries_initialize_outer_elements =
          entries_initialize_outer_elements,
  };
  return 1;
}

int psx_resolve_incomplete_array_initializer_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t incomplete_type,
    psx_decl_init_kind_t initializer_kind,
    node_t *initializer,
    psx_qual_type_t *completed_type) {
  if (completed_type)
    *completed_type = (psx_qual_type_t){
        PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  if (!semantic_context || !completed_type ||
      incomplete_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  psx_incomplete_array_resolution_t resolution;
  if (!psx_resolve_incomplete_array_initializer_shape_in(
          semantic_context, incomplete_type,
          initializer_kind, initializer,
          legacy_incomplete_array_constant_index, semantic_context,
          &resolution))
    return 0;
  return psx_resolve_completed_incomplete_array_qual_type_in(
      semantic_context, incomplete_type, &resolution,
      completed_type);
}
