#ifndef PARSER_TYPE_H
#define PARSER_TYPE_H

#include "../type_system/type_ids.h"
#include "../type_system/type_operators.h"
#include "../type_system/type_shape.h"
#include "core.h"
#include "type_fwd.h"
#include <stddef.h>

typedef struct ag_data_layout_t ag_data_layout_t;

typedef struct arena_context_t arena_context_t;
typedef struct psx_semantic_type_table_t psx_semantic_type_table_t;

typedef struct psx_record_member_decl_t {
  char *name;
  int len;
  int bit_width;
  int bit_is_signed;
  const psx_semantic_type_table_t *decl_type_table;
  psx_qual_type_t decl_qual_type;
} psx_record_member_decl_t;

const psx_type_t *psx_record_member_decl_type(
    const psx_record_member_decl_t *member);

typedef struct psx_record_decl_t {
  psx_record_id_t record_id;
  psx_type_kind_t record_kind;
  char *tag_name;
  int tag_len;
  unsigned char is_complete;
  int member_count;
  const psx_record_member_decl_t *members;
} psx_record_decl_t;

struct psx_type_t {
  psx_type_kind_t kind;
  const psx_type_t *base;

  int array_len;

  psx_integer_kind_t integer_kind;
  psx_floating_kind_t floating_kind;
  char *tag_name;
  int tag_len;
  int tag_scope_depth_p1;
  psx_record_id_t record_id;

  psx_type_qualifiers_t qualifiers;
  unsigned int is_unsigned : 1;
  unsigned int is_plain_char : 1;
  unsigned int is_vla : 1;

  const psx_type_t *const *param_types;
  int param_count;
  unsigned char has_function_prototype;
  unsigned char is_variadic_function;

};

const psx_type_t *ps_type_usual_arithmetic_result_for_data_layout_in(
    arena_context_t *arena_context, const ag_data_layout_t *data_layout,
    const psx_type_t *lhs, const psx_type_t *rhs,
    psx_floating_kind_t fallback_floating_kind, int force_complex);
int ps_type_integer_promotion_is_unsigned_for_data_layout(
    const psx_type_t *type, const ag_data_layout_t *data_layout);
int ps_type_usual_arithmetic_result_is_unsigned_for_data_layout(
    const psx_type_t *lhs, const psx_type_t *rhs,
    const ag_data_layout_t *data_layout);
const psx_type_t *ps_type_binary_result_for_data_layout_in(
    arena_context_t *arena_context, const ag_data_layout_t *data_layout,
    psx_type_binary_op_t op, const psx_type_t *lhs, const psx_type_t *rhs);
const psx_type_t *ps_type_conditional_result_for_data_layout_in(
    arena_context_t *arena_context, const ag_data_layout_t *data_layout,
    const psx_type_t *then_type, const psx_type_t *else_type);
/* Returns the function node contained in a pointer/array derivation chain.
 * This does not imply that the original expression type is callable. */
const psx_type_t *ps_type_derived_function(const psx_type_t *type);
const psx_type_t *ps_type_callable_function(const psx_type_t *type);
const psx_type_t *ps_type_function_return_type(const psx_type_t *type);
psx_type_kind_t ps_type_kind_from_tag_kind(token_kind_t tag_kind);

int ps_type_integer_rank(const psx_type_t *type);
int ps_type_character_code_unit_width(const psx_type_t *type);
int ps_type_is_incomplete_array(const psx_type_t *type);
const psx_type_t *ps_type_array_leaf_type(const psx_type_t *type);
const psx_type_t *ps_type_pointee_value_type(const psx_type_t *type);
const psx_type_t *ps_type_derived_leaf_type(const psx_type_t *type);
int ps_type_array_rank(const psx_type_t *type);
int ps_type_array_dimension(const psx_type_t *type, int index);
int ps_type_array_flat_element_count(const psx_type_t *type);
int ps_type_array_subscript_stride_elements(const psx_type_t *type,
                                             int depth);
const psx_type_t *ps_type_address_result_in(
    arena_context_t *arena_context, const psx_type_t *type);
const psx_type_t *ps_type_decay_array_in(
    arena_context_t *arena_context, const psx_type_t *type);
const psx_type_t *ps_type_dereference_result(const psx_type_t *type);
const psx_type_t *ps_type_subscript_result_in(
    arena_context_t *arena_context, const psx_type_t *type);
int ps_type_is_pointer(const psx_type_t *type);
int ps_type_is_pointer_like(const psx_type_t *type);
int ps_type_contains_vla_array(const psx_type_t *type);
int ps_type_is_unsigned(const psx_type_t *type);
int ps_type_is_scalar(const psx_type_t *type);
psx_type_qualifiers_t ps_type_qualifiers(const psx_type_t *type);
int ps_type_has_qualifier(const psx_type_t *type,
                          psx_type_qualifiers_t qualifier);
int ps_type_is_tag_aggregate(const psx_type_t *type);
token_kind_t ps_type_tag_token_kind(const psx_type_t *type);
tk_float_kind_t ps_type_floating_token_kind(const psx_type_t *type);
const psx_type_t *ps_type_find_aggregate_object_type(
    const psx_type_t *type);
int ps_type_tag_identity_matches(const psx_type_t *a,
                                 const psx_type_t *b);
psx_record_id_t ps_type_record_id(const psx_type_t *type);
int ps_type_is_well_formed(const psx_type_t *type);
int ps_type_shape_matches(const psx_type_t *a, const psx_type_t *b);
/* Compares C type meaning while excluding top-level qualifiers and target
 * layout. Recursive child qualifiers remain part of the containing type. */
int ps_type_unqualified_semantic_matches(
    const psx_type_t *a, const psx_type_t *b);
int ps_type_format_canonical_signature_for_data_layout(
    const psx_type_t *type, const ag_data_layout_t *data_layout, char *out,
    size_t out_size);
int ps_type_generic_matches(const psx_type_t *control,
                            const psx_type_t *association);
const psx_type_t *ps_type_generic_control_in(
    arena_context_t *arena_context, const psx_type_t *control);
int ps_type_generic_select_index_in(
    arena_context_t *arena_context, const psx_type_t *control,
    const psx_type_t *const *association_types,
    const unsigned char *is_default, int association_count);
int ps_type_generic_select_index(
    const psx_type_t *control,
    const psx_type_t *const *association_types,
    const unsigned char *is_default, int association_count);
int ps_type_pointer_depth(const psx_type_t *type);
int psx_record_member_decl_is_tag_aggregate(
    const psx_record_member_decl_t *member);
int psx_record_member_decl_is_unnamed_struct(
    const psx_record_member_decl_t *member);
int psx_record_member_decl_is_unnamed_union(
    const psx_record_member_decl_t *member);
int psx_record_member_decl_is_unnamed_aggregate(
    const psx_record_member_decl_t *member);
tk_float_kind_t psx_record_member_decl_fp_kind(
    const psx_record_member_decl_t *member);
int psx_record_member_decl_is_bool(
    const psx_record_member_decl_t *member);

#endif
