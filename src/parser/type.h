#ifndef PARSER_TYPE_H
#define PARSER_TYPE_H

#include "core.h"
#include <stddef.h>

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

typedef struct psx_type_t psx_type_t;
struct tag_member_info_t;

typedef struct psx_aggregate_definition_t {
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int size;
  int align;
  int member_count;
  struct tag_member_info_t *members;
} psx_aggregate_definition_t;

typedef enum {
  PSX_DECL_OP_POINTER = 0,
  PSX_DECL_OP_ARRAY,
  PSX_DECL_OP_FUNCTION,
} psx_declarator_op_kind_t;

typedef struct {
  psx_declarator_op_kind_t kind;
  int array_len;
  unsigned int is_const_qualified : 1;
  unsigned int is_volatile_qualified : 1;
  unsigned int is_incomplete_array : 1;
  unsigned int is_vla_array : 1;
  unsigned int has_canonical_function_params : 1;
  psx_type_t *function_param_types[16];
  int function_param_count;
  int function_is_variadic;
} psx_declarator_op_t;

typedef struct {
  psx_declarator_op_t ops[24];
  int count;
} psx_declarator_shape_t;

typedef struct {
  int outer_stride;
  int mid_stride;
  int extra_strides[5];
  unsigned char extra_strides_count;
} psx_vla_runtime_strides_t;

/* Operators are stored from the declared identifier outward. Applying them in
 * reverse order preserves parenthesized declarator binding without flattening
 * pointer/array/function placement into independent flags. */

struct psx_type_t {
  psx_type_kind_t kind;
  psx_type_t *base;

  int size;
  int align;
  int elem_size;
  int array_len;

  token_kind_t scalar_kind;
  tk_float_kind_t fp_kind;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int tag_scope_depth_p1;
  psx_aggregate_definition_t *aggregate_definition;

  unsigned int is_unsigned : 1;
  unsigned int is_const_qualified : 1;
  unsigned int is_volatile_qualified : 1;
  unsigned int is_atomic : 1;
  unsigned int is_long_long : 1;
  unsigned int is_plain_char : 1;
  unsigned int is_long_double : 1;
  unsigned int is_vla : 1;

  int pointer_qual_levels;
  unsigned int pointer_const_qual_mask;
  unsigned int pointer_volatile_qual_mask;

  int vla_row_stride_frame_off;
  int vla_strides_remaining;
  int vla_row_stride_src_offset;
  short vla_row_stride_elem_size;
  short vla_param_inner_dim_consts[7];
  int vla_param_inner_dim_src_offsets[7];
  unsigned char vla_param_inner_dim_count;
  psx_vla_runtime_strides_t vla_runtime_strides;

  psx_type_t *param_types[16];
  int param_count;
  unsigned char is_variadic_function;

};

psx_type_t *ps_type_new(psx_type_kind_t kind);
void ps_type_normalize_integer_identity(psx_type_t *type);
psx_type_t *ps_type_new_integer(token_kind_t scalar_kind, int size, int is_unsigned);
psx_type_t *ps_type_new_enum(char *tag_name, int tag_len,
                              int tag_scope_depth_p1, int size);
psx_type_t *ps_type_new_float(tk_float_kind_t fp_kind, int size);
psx_type_t *ps_type_usual_arithmetic_result(
    const psx_type_t *lhs, const psx_type_t *rhs,
    tk_float_kind_t fallback_fp_kind, int force_complex);
int ps_type_integer_promotion_is_unsigned(const psx_type_t *type);
psx_type_t *ps_type_binary_result(
    psx_type_binary_op_t op, const psx_type_t *lhs,
    const psx_type_t *rhs);
psx_type_t *ps_type_conditional_result(
    const psx_type_t *then_type, const psx_type_t *else_type);
psx_type_t *ps_type_new_pointer(psx_type_t *base);
psx_type_t *ps_type_new_function(psx_type_t *return_type);
void ps_type_set_function_params(psx_type_t *function_type,
                                  psx_type_t *const *param_types,
                                  int param_count, int is_variadic);
const psx_type_t *ps_type_find_function(const psx_type_t *type);
const psx_type_t *ps_type_function_return_type(const psx_type_t *type);
psx_type_t *psx_type_new_storage_object(
    int object_size, int elem_size, int is_array,
    tk_float_kind_t fp_kind, int is_unsigned,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int tag_scope_depth_p1, int is_pointer);
psx_type_t *ps_type_wrap_pointer_levels(psx_type_t *base, int levels,
                                          unsigned int const_mask,
                                          unsigned int volatile_mask);
psx_type_t *ps_type_wrap_array_dims(psx_type_t *base,
                                     const int *dims, int dim_count);
void ps_declarator_shape_init(psx_declarator_shape_t *shape);
int ps_declarator_shape_append_pointer(
    psx_declarator_shape_t *shape, int is_const_qualified,
    int is_volatile_qualified);
int ps_declarator_shape_append_pointer_levels(
    psx_declarator_shape_t *shape, int levels,
    unsigned int const_mask, unsigned int volatile_mask);
int ps_declarator_shape_append_array(
    psx_declarator_shape_t *shape, int array_len);
int ps_declarator_shape_append_array_ex(
    psx_declarator_shape_t *shape, int array_len, int is_incomplete);
int ps_declarator_shape_append_vla_array(
    psx_declarator_shape_t *shape);
int psx_declarator_shape_append_array_dims(
    psx_declarator_shape_t *shape, const int *dims, int dim_count);
int ps_declarator_shape_append_function(psx_declarator_shape_t *shape);
int ps_declarator_shape_append_shape(
    psx_declarator_shape_t *shape, const psx_declarator_shape_t *suffix);
int ps_declarator_shape_count_ops(
    const psx_declarator_shape_t *shape, psx_declarator_op_kind_t kind);
psx_type_t *ps_type_apply_declarator_shape(
    psx_type_t *base, const psx_declarator_shape_t *shape);
psx_type_t *ps_type_adjust_parameter_type(psx_type_t *type);
psx_type_t *psx_type_rebase_declarator(
    const psx_type_t *derived_type, const psx_type_t *canonical_base,
    int *out_rebased);
psx_type_t *ps_type_new_array(psx_type_t *base, int array_len, int size, int elem_size, int is_vla);
int ps_type_complete_array(psx_type_t *type, int array_len);
psx_type_t *ps_type_clone(const psx_type_t *src);
psx_type_t *ps_type_clone_persistent(const psx_type_t *src);
psx_type_t *psx_type_rebuild_array_dims(psx_type_t *type,
                                        const int *dims, int dim_count,
                                        int leaf_size);
psx_type_t *psx_type_new_runtime_vla_row_view(
    const psx_type_t *source, int row_size, int elem_size,
    int row_stride_frame_off, int strides_remaining);
psx_type_t *ps_type_new_vla_object_view(
    const psx_type_t *source, int outer_stride,
    int row_stride_frame_off, int strides_remaining);
void ps_type_set_vla_runtime_descriptor(
    psx_type_t *type, int row_stride_frame_off, int strides_remaining,
    int row_stride_src_offset, int row_stride_elem_size);
void ps_type_copy_vla_runtime_metadata(psx_type_t *dst,
                                        const psx_type_t *src);
void ps_type_set_vla_param_inner_dims(
    psx_type_t *type, const int *inner_dim_consts,
    const int *inner_dim_src_offsets, int inner_dim_count);
psx_type_t *ps_type_new_tag(token_kind_t tag_kind, char *tag_name, int tag_len,
                             int tag_scope_depth_p1, int size);

psx_type_kind_t ps_type_kind_from_tag_kind(token_kind_t tag_kind);

int ps_type_sizeof(const psx_type_t *type);
int ps_type_deref_size(const psx_type_t *type);
int ps_type_is_pointer(const psx_type_t *type);
int ps_type_is_unsigned(const psx_type_t *type);
int ps_type_is_scalar(const psx_type_t *type);
int ps_type_is_tag_aggregate(const psx_type_t *type);
const struct tag_member_info_t *ps_type_find_aggregate_member(
    const psx_type_t *type, token_kind_t tag_kind,
    const char *tag_name, int tag_len,
    const char *member_name, int member_len);
int ps_type_shape_matches(const psx_type_t *a, const psx_type_t *b);
int ps_type_format_canonical_signature(const psx_type_t *type,
                                       char *out, size_t out_size);
int ps_type_generic_matches(const psx_type_t *control,
                            const psx_type_t *association);
psx_type_t *ps_type_generic_control(const psx_type_t *control);
int ps_type_generic_select_index(
    const psx_type_t *control, psx_type_t *const *association_types,
    const unsigned char *is_default, int association_count);
int ps_type_pointer_depth(const psx_type_t *type);
int ps_type_pointer_view_structural_qual_levels(const psx_type_t *type);
unsigned int ps_type_pointer_view_structural_qual_mask(
    const psx_type_t *type, int is_volatile);
int ps_type_pointer_view_structural_base_deref_size(const psx_type_t *type);
int ps_type_pointer_view_structural_ptr_array_pointee_bytes(
    const psx_type_t *type);
int psx_type_copy_runtime_vla_stride_metadata(psx_type_t *dst,
                                              const psx_type_t *src);
int ps_type_pointer_view_stride_metadata(const psx_type_t *type,
                                          int *inner_stride,
                                          int *next_stride,
                                          int *extra_strides,
                                          int *extra_strides_count);
int ps_type_decl_array_stride_metadata(const psx_type_t *type,
                                        int *outer_stride,
                                        int *mid_stride,
                                        int *extra_strides,
                                        int *extra_strides_count);
int psx_type_pointer_view_vla_row_stride_frame_off(const psx_type_t *type);
int ps_type_pointer_view_vla_strides_remaining(const psx_type_t *type);
int psx_type_vla_row_stride_src_offset(const psx_type_t *type);
int ps_type_vla_row_stride_elem_size(const psx_type_t *type);
int ps_type_vla_param_inner_dim_count(const psx_type_t *type);
int ps_type_vla_param_inner_dim_const(const psx_type_t *type, int index);
int ps_type_vla_param_inner_dim_src_offset(const psx_type_t *type, int index);

void psx_type_copy_common_qualifiers(psx_type_t *dst, const psx_type_t *src);
void ps_type_set_decl_spec_qualifiers(psx_type_t *type,
                                       int is_const_qualified,
                                       int is_volatile_qualified);
void psx_type_copy_pointer_metadata(psx_type_t *dst, const psx_type_t *src);

#endif
