#ifndef PARSER_TYPE_BUILDER_H
#define PARSER_TYPE_BUILDER_H

#include "type.h"
#include "declarator_shape.h"
#include "../semantic/record_decl.h"

typedef struct arena_context_t arena_context_t;
typedef const psx_type_t *(*psx_qual_type_view_resolver_t)(
    void *context, psx_qual_type_t qual_type);

/* Mutation is restricted to construction-owned types before publication. */
psx_type_t *ps_type_new_in(
    arena_context_t *arena_context, psx_type_kind_t kind);
psx_type_t *ps_type_new_integer_kind_in(
    arena_context_t *arena_context, psx_integer_kind_t integer_kind,
    int is_unsigned, int is_plain_char);
/* Syntax-boundary adapter. Semantic and lowering code should construct from
 * psx_integer_kind_t instead of retaining parser token kinds. */
psx_type_t *ps_type_new_integer_in(
    arena_context_t *arena_context, token_kind_t scalar_token_kind,
    int is_unsigned);
psx_type_t *ps_type_new_enum_in(
    arena_context_t *arena_context, char *tag_name, int tag_len,
    int tag_scope_depth_p1);
psx_type_t *ps_type_new_record_in(
    arena_context_t *arena_context, const psx_record_decl_t *record);
psx_type_t *ps_type_new_floating_in(
    arena_context_t *arena_context, psx_floating_kind_t floating_kind,
    int is_complex);
/* Syntax-boundary adapter for floating literals. Semantic and lowering code
 * should construct from psx_floating_kind_t. */
psx_type_t *ps_type_new_float_in(
    arena_context_t *arena_context, tk_float_kind_t fp_kind);
psx_type_t *ps_type_new_pointer_in(
    arena_context_t *arena_context, const psx_type_t *base);
psx_type_t *ps_type_new_function_in(
    arena_context_t *arena_context, const psx_type_t *return_type);
psx_type_t *ps_type_new_array_in(
    arena_context_t *arena_context, const psx_type_t *base,
    int array_len, int is_vla);
psx_type_t *ps_type_clone_in(
    arena_context_t *arena_context, const psx_type_t *src);
psx_type_t *ps_type_clone_persistent(const psx_type_t *src);
psx_type_t *ps_type_new_tag_in(
    arena_context_t *arena_context, token_kind_t tag_kind,
    char *tag_name, int tag_len, int tag_scope_depth_p1);
void ps_type_normalize_scalar_identity(psx_type_t *type);
void ps_type_set_function_params_in(
    arena_context_t *arena_context, psx_type_t *function_type,
    const psx_type_t *const *param_types,
    int param_count, int is_variadic);
psx_type_t *ps_type_wrap_array_dims_in(
    arena_context_t *arena_context, psx_type_t *base,
    const int *dims, int dim_count);
psx_type_t *ps_type_apply_declarator_shape_in(
    arena_context_t *arena_context, psx_type_t *base,
    const psx_declarator_shape_t *shape);
/* Canonical function parameters require an explicit semantic type view. */
psx_type_t *ps_type_apply_resolved_declarator_shape_in(
    arena_context_t *arena_context, psx_type_t *base,
    const psx_declarator_shape_t *shape,
    psx_qual_type_view_resolver_t resolve_qual_type,
    void *resolver_context);
psx_type_t *ps_type_adjust_parameter_type_in(
    arena_context_t *arena_context, psx_type_t *type);
int ps_type_complete_array(psx_type_t *type, int array_len);
void ps_type_set_decl_spec_qualifiers(psx_type_t *type,
                                      int is_const_qualified,
                                      int is_volatile_qualified);
void ps_type_add_qualifiers(psx_type_t *type,
                            psx_type_qualifiers_t qualifiers);
void ps_type_remove_qualifiers(psx_type_t *type,
                               psx_type_qualifiers_t qualifiers);
void ps_type_remove_all_qualifiers_recursive(psx_type_t *type);

#endif
