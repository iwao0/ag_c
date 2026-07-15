#include "declaration_resolution.h"
#include "declaration_type_builder.h"

#include "constant_expression.h"

#include "../parser/semantic_ctx.h"
#include "../parser/tag_member_public.h"
#include "../parser/type_builder.h"

#include <limits.h>

static psx_type_t *resolve_tag_base_type(
    psx_semantic_context_t *semantic_context,
    token_kind_t kind, char *name, int name_len) {
  int scope_depth = ps_ctx_get_tag_scope_depth_in(
      semantic_context, kind, name, name_len);
  if (kind == TK_ENUM) {
    return ps_type_new_enum_in(
        ps_ctx_arena(semantic_context), name, name_len,
        scope_depth >= 0 ? scope_depth + 1 : 0, 4);
  }
  if (!ps_ctx_is_tag_aggregate_kind(kind)) return NULL;
  int size = ps_ctx_get_tag_size_in(
      semantic_context, kind, name, name_len);
  if (size < 0) size = 0;
  psx_type_t *type = ps_type_new_tag_in(
      ps_ctx_arena(semantic_context), kind, name, name_len,
      scope_depth >= 0 ? scope_depth + 1 : 0, size);
  type->aggregate_definition = ps_ctx_get_tag_definition_in(
      semantic_context, kind, name, name_len);
  if (type->aggregate_definition && type->aggregate_definition->align > 0)
    type->align = type->aggregate_definition->align;
  return type;
}

static psx_type_t *resolve_builtin_base_type(
    psx_semantic_context_t *semantic_context,
    token_kind_t kind, const psx_type_spec_result_t *specifier) {
  int elem_size = ps_ctx_scalar_type_size(kind);
  if (specifier->is_complex) elem_size *= 2;
  tk_float_kind_t fp_kind = TK_FLOAT_KIND_NONE;
  if (kind == TK_FLOAT)
    fp_kind = TK_FLOAT_KIND_FLOAT;
  else if (kind == TK_DOUBLE)
    fp_kind = TK_FLOAT_KIND_DOUBLE;
  if (specifier->is_complex) {
    psx_type_t *type = ps_type_new_in(
        ps_ctx_arena(semantic_context), PSX_TYPE_COMPLEX);
    type->fp_kind = fp_kind != TK_FLOAT_KIND_NONE
                        ? fp_kind
                        : TK_FLOAT_KIND_DOUBLE;
    type->size = elem_size;
    type->align = elem_size >= 8 ? 8 : elem_size;
    return type;
  }
  if (fp_kind != TK_FLOAT_KIND_NONE) {
    return ps_type_new_float_in(
        ps_ctx_arena(semantic_context), fp_kind, elem_size);
  }
  if (kind == TK_VOID) {
    psx_type_t *type = ps_type_new_in(
        ps_ctx_arena(semantic_context), PSX_TYPE_VOID);
    type->scalar_kind = TK_VOID;
    return type;
  }
  return ps_type_new_integer_in(
      ps_ctx_arena(semantic_context), kind, elem_size,
      specifier->is_unsigned);
}

static void apply_decl_specifier_type_properties(
    psx_type_t *type, const psx_type_spec_result_t *specifier,
    int override_plain_char) {
  if (!type || !specifier) return;
  ps_type_set_decl_spec_qualifiers(
      type,
      type->is_const_qualified || specifier->is_const_qualified,
      type->is_volatile_qualified || specifier->is_volatile_qualified);
  if (specifier->is_atomic) type->is_atomic = 1;
  if (specifier->is_long_long) type->is_long_long = 1;
  if (override_plain_char)
    type->is_plain_char = specifier->is_plain_char ? 1 : 0;
  if (specifier->is_long_double) type->is_long_double = 1;
}

psx_type_t *psx_build_decl_type(const psx_decl_type_request_t *request) {
  if (!request || !request->semantic_context || !request->base_type)
    return NULL;
  psx_semantic_context_t *semantic_context = request->semantic_context;
  psx_type_t *type = ps_type_clone_in(
      ps_ctx_arena(semantic_context), request->base_type);
  if (!type) return NULL;
  if (request->declarator_shape) {
    type = ps_type_apply_declarator_shape_in(
        ps_ctx_arena(semantic_context), type,
        request->declarator_shape);
  }
  ps_ctx_attach_aggregate_definitions_in(semantic_context, type);
  return type;
}

const psx_type_t *psx_resolve_decl_type(
    const psx_decl_type_request_t *request) {
  return psx_build_decl_type(request);
}

psx_type_t *psx_build_decl_specifier_type_in_context(
    psx_semantic_context_t *semantic_context,
    const psx_parsed_decl_specifier_t *specifier) {
  if (!specifier) return NULL;

  const psx_type_spec_result_t *syntax = &specifier->type_spec;
  psx_type_t *type = NULL;
  int override_plain_char = 0;
  switch (specifier->source) {
    case PSX_PARSED_DECL_TYPE_BUILTIN:
      type = resolve_builtin_base_type(
          semantic_context, syntax->kind, syntax);
      override_plain_char = syntax->kind == TK_CHAR;
      break;
    case PSX_PARSED_DECL_TYPE_TAG:
      type = resolve_tag_base_type(
          semantic_context,
          specifier->tag_action.kind,
          specifier->tag_action.name,
          specifier->tag_action.name_len);
      break;
    case PSX_PARSED_DECL_TYPEDEF_NAME: {
      const psx_type_t *typedef_type = NULL;
      if (!specifier->typedef_name ||
          !ps_ctx_find_typedef_decl_type_in(
              semantic_context,
              specifier->typedef_name->str,
              specifier->typedef_name->len,
              &typedef_type))
        return NULL;
      type = ps_type_clone_in(
          ps_ctx_arena(semantic_context), typedef_type);
      break;
    }
    case PSX_PARSED_DECL_TYPE_IMPLICIT_INT:
      type = resolve_builtin_base_type(
          semantic_context, TK_INT, syntax);
      break;
    default:
      return NULL;
  }
  apply_decl_specifier_type_properties(type, syntax, override_plain_char);
  ps_ctx_attach_aggregate_definitions_in(semantic_context, type);
  return type;
}

const psx_type_t *psx_resolve_decl_specifier_syntax_in_context(
    psx_semantic_context_t *semantic_context,
    const psx_parsed_decl_specifier_t *specifier) {
  return psx_build_decl_specifier_type_in_context(
      semantic_context, specifier);
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
