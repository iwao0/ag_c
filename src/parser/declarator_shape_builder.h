#ifndef PARSER_DECLARATOR_SHAPE_BUILDER_H
#define PARSER_DECLARATOR_SHAPE_BUILDER_H

#include "declarator_shape.h"

typedef struct arena_context_t arena_context_t;

void ps_declarator_shape_init(psx_declarator_shape_t *shape);
int ps_declarator_shape_copy_in(
    arena_context_t *arena_context, psx_declarator_shape_t *dst,
    const psx_declarator_shape_t *src);
int ps_declarator_shape_append_shape_in(
    arena_context_t *arena_context, psx_declarator_shape_t *shape,
    const psx_declarator_shape_t *suffix);
int ps_declarator_shape_append_pointer_in(
    arena_context_t *arena_context, psx_declarator_shape_t *shape,
    int is_const_qualified, int is_volatile_qualified,
    int is_restrict_qualified);
int ps_declarator_shape_append_pointer_qualified_in(
    arena_context_t *arena_context, psx_declarator_shape_t *shape,
    int is_const_qualified, int is_volatile_qualified,
    int is_restrict_qualified, int is_atomic_qualified);
int ps_declarator_shape_append_array_in(
    arena_context_t *arena_context, psx_declarator_shape_t *shape,
    int array_len);
int ps_declarator_shape_append_array_ex_in(
    arena_context_t *arena_context, psx_declarator_shape_t *shape,
    int array_len, int is_incomplete);
int ps_declarator_shape_append_vla_array_in(
    arena_context_t *arena_context, psx_declarator_shape_t *shape);
int ps_declarator_shape_append_function_in(
    arena_context_t *arena_context, psx_declarator_shape_t *shape);
int ps_declarator_op_set_function_param_qual_types_in(
    arena_context_t *arena_context, psx_declarator_op_t *op,
    const psx_qual_type_t *param_qual_types,
    int param_count, int is_variadic, int has_prototype);
int ps_declarator_shape_set_array_bound(
    psx_declarator_shape_t *shape, int op_index,
    int array_len, int is_vla);
int ps_declarator_op_set_variadic(
    psx_declarator_op_t *op, int is_variadic);
int ps_declarator_shape_count_ops(
    const psx_declarator_shape_t *shape, psx_declarator_op_kind_t kind);

#endif
