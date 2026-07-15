#ifndef PARSER_TYPE_H
#define PARSER_TYPE_H

#include "core.h"
#include "type_fwd.h"
#include <stddef.h>

typedef struct ag_target_info_t ag_target_info_t;

typedef enum {
  PSX_TYPE_INVALID = 0,
  PSX_TYPE_VOID,
  PSX_TYPE_BOOL,
  PSX_TYPE_INTEGER,
  PSX_TYPE_FLOAT,
  PSX_TYPE_POINTER,
  PSX_TYPE_ARRAY,
  PSX_TYPE_FUNCTION,
  PSX_TYPE_STRUCT,
  PSX_TYPE_UNION,
  PSX_TYPE_COMPLEX,
} psx_type_kind_t;

typedef enum {
  PSX_TYPE_BINARY_COMMA = 0,
  PSX_TYPE_BINARY_ADD,
  PSX_TYPE_BINARY_SUB,
  PSX_TYPE_BINARY_MUL,
  PSX_TYPE_BINARY_DIV,
  PSX_TYPE_BINARY_MOD,
  PSX_TYPE_BINARY_BITAND,
  PSX_TYPE_BINARY_BITXOR,
  PSX_TYPE_BINARY_BITOR,
  PSX_TYPE_BINARY_SHL,
  PSX_TYPE_BINARY_SHR,
  PSX_TYPE_BINARY_COMPARE,
  PSX_TYPE_BINARY_LOGICAL,
} psx_type_binary_op_t;

struct tag_member_info_t;
typedef struct arena_context_t arena_context_t;

typedef unsigned int psx_record_id_t;
typedef unsigned int psx_type_qualifiers_t;

#define PSX_RECORD_ID_INVALID ((psx_record_id_t)0)

enum {
  PSX_TYPE_QUALIFIER_NONE = 0,
  PSX_TYPE_QUALIFIER_CONST = 1u << 0,
  PSX_TYPE_QUALIFIER_VOLATILE = 1u << 1,
  PSX_TYPE_QUALIFIER_ATOMIC = 1u << 2,
};

typedef struct psx_record_decl_t {
  psx_record_id_t record_id;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  unsigned char is_complete;
  int member_count;
  const struct tag_member_info_t *members;
} psx_record_decl_t;

struct psx_type_t {
  psx_type_kind_t kind;
  const psx_type_t *base;

  int array_len;

  token_kind_t scalar_kind;
  tk_float_kind_t fp_kind;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int tag_scope_depth_p1;
  psx_record_id_t record_id;

  psx_type_qualifiers_t qualifiers;
  unsigned int is_unsigned : 1;
  unsigned int is_long_long : 1;
  unsigned int is_plain_char : 1;
  unsigned int is_vla : 1;

  const psx_type_t *const *param_types;
  int param_count;
  unsigned char is_variadic_function;

};

const psx_type_t *ps_type_usual_arithmetic_result_for_target_in(
    arena_context_t *arena_context, const ag_target_info_t *target,
    const psx_type_t *lhs, const psx_type_t *rhs,
    tk_float_kind_t fallback_fp_kind, int force_complex);
int ps_type_integer_promotion_is_unsigned_for_target(
    const psx_type_t *type, const ag_target_info_t *target);
int ps_type_usual_arithmetic_result_is_unsigned_for_target(
    const psx_type_t *lhs, const psx_type_t *rhs,
    const ag_target_info_t *target);
const psx_type_t *ps_type_binary_result_for_target_in(
    arena_context_t *arena_context, const ag_target_info_t *target,
    psx_type_binary_op_t op,
    const psx_type_t *lhs, const psx_type_t *rhs);
const psx_type_t *ps_type_conditional_result_for_target_in(
    arena_context_t *arena_context, const ag_target_info_t *target,
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
int ps_type_format_canonical_signature_for_target(
    const psx_type_t *type, const ag_target_info_t *target,
    char *out, size_t out_size);
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

#endif
