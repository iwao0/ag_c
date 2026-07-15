#include "cast_lowering.h"
#include "local_storage.h"
#include "runtime_context.h"
#include "../diag/diag.h"
#include "../parser/diag.h"
#include "../parser/local_registry.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"
#include "../parser/type_builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static node_t *annotate(node_t *node, const psx_type_t *type) {
  if (node && type) ps_node_bind_type(node, type);
  return node;
}

static node_t *fp_to_int(
    psx_lowering_context_t *lowering_context,
    node_t *operand, const psx_type_t *type) {
  if (!operand || ps_node_value_fp_kind(operand) == TK_FLOAT_KIND_NONE)
    return operand;
  if (!type)
    type = ps_type_new_integer_in(
        ps_lowering_arena(lowering_context), TK_INT, 4, 0);
  return ps_node_new_fp_to_int_cast_in(
      ps_lowering_arena(lowering_context), operand, type);
}

static node_t *lower_value_to_fp(arena_context_t *arena_context,
                                 node_t *operand,
                                 const psx_type_t *target_type) {
  if (!operand) return NULL;
  if (!target_type) return operand;
  tk_float_kind_t source = ps_node_value_fp_kind(operand);
  if (source == target_type->fp_kind &&
      ps_type_shape_matches(ps_node_get_type(operand), target_type))
    return operand;
  return ps_node_new_int_to_fp_cast_in(
      arena_context, operand, target_type);
}

typedef struct {
  const psx_type_t *target;
  const psx_type_t *value;
  token_kind_t kind;
  token_kind_t tag_kind;
  int is_pointer;
  int elem_size;
  int is_unsigned;
  int is_long_long;
  int is_plain_char;
} cast_target_view_t;

static const psx_type_t *target_value_type(const psx_type_t *type) {
  const psx_type_t *value = type;
  while (value &&
         (value->kind == PSX_TYPE_POINTER || value->kind == PSX_TYPE_ARRAY))
    value = value->base;
  if (value && value->kind == PSX_TYPE_FUNCTION) value = value->base;
  return value;
}

static cast_target_view_t target_view(const psx_type_t *target) {
  cast_target_view_t view = {0};
  view.target = target;
  view.value = target_value_type(target);
  view.kind = view.value ? view.value->scalar_kind : TK_EOF;
  view.tag_kind = view.value ? view.value->tag_kind : TK_EOF;
  view.is_pointer = ps_type_is_pointer(target);
  view.elem_size = view.value ? ps_type_sizeof(view.value) : 0;
  view.is_unsigned = view.value ? ps_type_is_unsigned(view.value) : 0;
  view.is_long_long = view.value ? view.value->is_long_long : 0;
  view.is_plain_char = view.value ? view.value->is_plain_char : 0;
  if (view.value) {
    if (view.value->kind == PSX_TYPE_VOID) view.kind = TK_VOID;
    else if (view.value->kind == PSX_TYPE_BOOL) view.kind = TK_BOOL;
    else if (view.value->kind == PSX_TYPE_FLOAT ||
             view.value->kind == PSX_TYPE_COMPLEX)
      view.kind = view.value->fp_kind == TK_FLOAT_KIND_FLOAT
                      ? TK_FLOAT : TK_DOUBLE;
    else if (ps_type_is_tag_aggregate(view.value))
      view.kind = view.value->tag_kind;
  }
  return view;
}

static int same_tag_value(node_t *expr, const cast_target_view_t *view) {
  if (!expr || !view || !view->value) return 0;
  node_t *value = expr;
  while (value && value->kind == ND_COMMA) value = value->rhs;
  if (!value) return 0;
  if (value->kind == ND_TERNARY) {
    node_ctrl_t *ternary = (node_ctrl_t *)value;
    return same_tag_value(ternary->base.rhs, view) &&
           same_tag_value(ternary->els, view);
  }
  const psx_type_t *value_type = ps_node_get_type(value);
  return ps_type_is_tag_aggregate(value_type) &&
         ps_type_tag_identity_matches(value_type, view->value);
}

static int size_compatible_tag_value(node_t *expr,
                                     const cast_target_view_t *view) {
  if (!expr || !view) return 0;
  node_t *value = expr;
  while (value && value->kind == ND_COMMA) value = value->rhs;
  if (!value) return 0;
  if (value->kind == ND_TERNARY) {
    node_ctrl_t *ternary = (node_ctrl_t *)value;
    return size_compatible_tag_value(ternary->base.rhs, view) &&
           size_compatible_tag_value(ternary->els, view);
  }
  const psx_type_t *value_type = ps_node_get_type(value);
  int value_size = ps_type_sizeof(value_type);
  return ps_type_is_tag_aggregate(value_type) &&
         value_type->tag_kind == view->tag_kind &&
         value_size > 0 && view->elem_size > 0 &&
         value_size == view->elem_size;
}

static char *new_aggregate_temp_name(
    psx_lowering_context_t *lowering_context) {
  int seq = lowering_context->aggregate_cast_temp_sequence++;
  int len = snprintf(NULL, 0, "__aggregate_cast_%d", seq);
  char *name = calloc((size_t)len + 1, 1);
  snprintf(name, (size_t)len + 1, "__aggregate_cast_%d", seq);
  return name;
}

static node_t *lower_aggregate_cast(
    psx_lowering_context_t *lowering_context,
    psx_local_registry_t *local_registry,
    node_t *operand, cast_target_view_t view,
    token_t *diag_tok,
    const ag_compilation_options_t *options) {
  if (same_tag_value(operand, &view) ||
      (options->enable_size_compatible_nonscalar_cast &&
       size_compatible_tag_value(operand, &view))) {
    return ps_node_new_aggregate_cast_result_in(
        ps_lowering_arena(lowering_context), operand, view.target);
  }
  if (!lowering_context || !local_registry) return operand;

  const psx_type_t *operand_type = ps_node_get_type(operand);
  if (ps_type_is_tag_aggregate(operand_type)) {
    ps_diag_ctx(diag_tok, "cast",
                 diag_message_for(
                     DIAG_ERR_PARSER_CAST_NONSCALAR_TYPE_MISMATCH),
                 ps_ctx_tag_kind_spelling(view.tag_kind));
  }

  if (view.tag_kind == TK_STRUCT &&
      !options->enable_struct_scalar_pointer_cast) {
    ps_diag_ctx(diag_tok, "cast", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_CAST_STRUCT_SCALAR_POINTER_DISABLED));
  }
  if (view.tag_kind == TK_UNION &&
      !options->enable_union_scalar_pointer_cast) {
    ps_diag_ctx(diag_tok, "cast", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_CAST_UNION_SCALAR_POINTER_DISABLED));
  }
  if (view.tag_kind != TK_STRUCT && view.tag_kind != TK_UNION) {
    ps_diag_ctx(diag_tok, "cast",
                 diag_message_for(
                     DIAG_ERR_PARSER_CAST_NONSCALAR_UNSUPPORTED),
                 ps_ctx_tag_kind_spelling(view.tag_kind));
  }

  tag_member_info_t member = {0};
  int member_found = 0;
  const psx_aggregate_definition_t *definition =
      view.value->aggregate_definition;
  if (definition) {
    for (int i = 0; i < definition->member_count; i++) {
      if (definition->members[i].len <= 0) continue;
      member = definition->members[i];
      member_found = 1;
      break;
    }
  }
  if (!member_found) {
    ps_diag_ctx(diag_tok, "cast", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
  }

  int object_size = view.elem_size > 0 ? view.elem_size : 8;
  char *temp_name = new_aggregate_temp_name(lowering_context);
  int offset = local_storage_allocate(
      lowering_context, object_size, object_size);
  lvar_t *temp = ps_local_registry_create_storage_object_in(
      local_registry, temp_name, (int)strlen(temp_name), offset,
      object_size, object_size, view.target);

  node_t *member_ref =
      ps_node_new_tag_member_lvar_ref_for_in(
          ps_lowering_arena(lowering_context),
          temp, member.offset, &member);
  node_t *assign = ps_node_new_assign_in(
      ps_lowering_arena(lowering_context), member_ref, operand);
  node_t *result = ps_node_new_lvar_expr_ref_for_in(
      ps_lowering_arena(lowering_context), temp);
  return ps_node_new_binary_in(
      ps_lowering_arena(lowering_context), ND_COMMA, assign, result);
}

static node_t *pointer_result(arena_context_t *arena_context,
                              node_t *operand, cast_target_view_t view) {
  return ps_node_new_pointer_cast_result_in(
      arena_context, operand, view.target);
}

static node_t *integer_result(arena_context_t *arena_context,
                              node_t *operand, cast_target_view_t view) {
  return ps_node_new_integer_cast_result_in(
      arena_context, operand, view.target);
}

static node_t *integer_result_ex(arena_context_t *arena_context,
                                 node_t *operand, cast_target_view_t view,
                                 int widen_zext_i64) {
  return ps_node_new_integer_cast_result_ex_in(
      arena_context, operand, view.target, widen_zext_i64);
}

static node_t *lower_cast(
    psx_lowering_context_t *lowering_context,
    psx_local_registry_t *local_registry,
    node_t *operand, cast_target_view_t view,
    token_t *diag_tok,
    const ag_compilation_options_t *options) {
  arena_context_t *arena_context = ps_lowering_arena(lowering_context);
  if (!view.is_pointer && ps_ctx_is_tag_aggregate_kind(view.tag_kind))
    return lower_aggregate_cast(
        lowering_context, local_registry, operand, view, diag_tok, options);

  if (operand && operand->kind == ND_NUM &&
      ps_node_value_fp_kind(operand) != TK_FLOAT_KIND_NONE &&
      !view.is_pointer &&
      (view.kind == TK_INT || view.kind == TK_LONG ||
       view.kind == TK_SHORT || view.kind == TK_CHAR ||
       view.kind == TK_ENUM || view.kind == TK_SIGNED ||
       view.kind == TK_UNSIGNED || view.kind == TK_BOOL)) {
    operand = ps_node_new_num_in(
        arena_context, (long long)((node_num_t *)operand)->fval);
  }

  if (view.is_pointer || view.kind == TK_LONG) {
    operand = fp_to_int(
        lowering_context, operand,
        view.is_pointer ? NULL : view.target);
    if (!view.is_pointer && view.kind == TK_LONG) {
      if (operand->kind == ND_NUM) {
        return annotate(operand, view.target);
      }
      if (!ps_node_value_is_pointer_like(operand)) {
        int zext = ps_node_integer_value_is_unsigned(operand) &&
                   ps_node_type_size(operand) >= 1 &&
                   ps_node_type_size(operand) < 8;
        return integer_result_ex(arena_context, operand, view, zext);
      }
    }
    const psx_type_t *operand_pointee =
        ps_type_pointee_value_type(ps_node_get_type(operand));
    if (view.is_pointer &&
        (ps_ctx_is_tag_aggregate_kind(view.tag_kind) ||
         view.kind == TK_FLOAT || view.kind == TK_DOUBLE ||
         (operand_pointee && operand_pointee->kind == PSX_TYPE_VOID)))
      return pointer_result(arena_context, operand, view);

    if (view.is_pointer && view.elem_size > 0 &&
        ps_node_value_is_pointer_like(operand) &&
        ps_type_pointer_depth(
            ps_node_get_type(operand)) <= 1)
      return pointer_result(arena_context, operand, view);
    if (!view.is_pointer && view.kind == TK_LONG &&
        ps_node_value_is_pointer_like(operand))
      return integer_result(arena_context, operand, view);
    if (view.is_pointer)
      return pointer_result(arena_context, operand, view);
    return annotate(operand, view.target);
  }

  if (view.kind == TK_FLOAT)
    return lower_value_to_fp(arena_context, operand, view.target);
  if (view.kind == TK_DOUBLE)
    return lower_value_to_fp(arena_context, operand, view.target);

  if (view.kind == TK_INT || view.kind == TK_ENUM ||
      view.kind == TK_SIGNED || view.kind == TK_UNSIGNED) {
    operand = fp_to_int(lowering_context, operand, view.target);
    int target_unsigned = view.kind != TK_ENUM &&
                          (view.is_unsigned || view.kind == TK_UNSIGNED);
    if (operand->kind == ND_NUM) {
      long long value = ((node_num_t *)operand)->val;
      node_t *number = ps_node_new_num_in(
          arena_context,
          target_unsigned ? (long long)(unsigned)value
                          : (long long)(int)value);
      return annotate(number, view.target);
    }
    if (ps_node_type_size(operand) > 4 && !ps_node_value_is_pointer_like(operand))
      return ps_node_new_i64_to_i32_trunc_cast_in(
          arena_context, operand, view.target);
    int size = ps_node_type_size(operand);
    if (target_unsigned && size >= 1 && size < 4 &&
        ps_node_value_fp_kind(operand) == TK_FLOAT_KIND_NONE &&
        !ps_node_value_is_pointer_like(operand)) {
      node_t *masked = ps_node_new_binary_in(
          arena_context, ND_BITAND, operand,
          ps_node_new_num_in(arena_context, 0xffffffffLL));
      return integer_result(arena_context, masked, view);
    }
    return integer_result(arena_context, operand, view);
  }

  if (view.kind == TK_BOOL)
    return annotate(ps_node_new_binary_in(
                        arena_context, ND_NE, operand,
                        ps_node_new_num_in(arena_context, 0)),
                    view.target);
  if (view.kind == TK_VOID)
    return ps_node_new_void_cast_result_in(
        arena_context, operand, view.target);

  if (view.kind == TK_SHORT || view.kind == TK_CHAR) {
    operand = fp_to_int(lowering_context, operand, view.target);
    int width = view.kind == TK_SHORT ? 16 : 8;
    long long mask = view.kind == TK_SHORT ? 0xffffLL : 0xffLL;
    if (operand->kind == ND_NUM) {
      long long value = ((node_num_t *)operand)->val;
      long long truncated = view.kind == TK_SHORT
          ? (view.is_unsigned ? (long long)(unsigned short)value
                              : (long long)(short)value)
          : (view.is_unsigned ? (long long)(unsigned char)value
                              : (long long)(signed char)value);
      node_t *number = ps_node_new_num_in(arena_context, truncated);
      return annotate(number, view.target);
    }
    if (view.is_unsigned) {
      node_t *masked = ps_node_new_binary_in(
          arena_context, ND_BITAND, operand,
          ps_node_new_num_in(arena_context, mask));
      return integer_result(arena_context, masked, view);
    }
    int source_width = ps_node_type_size(operand) >= 8 ? 64 : 32;
    node_t *truncated = ps_node_new_shift_trunc_extend_in(
        arena_context, operand, source_width - width, 0);
    return integer_result_ex(arena_context, truncated, view, 0);
  }

  ps_diag_ctx(diag_tok, "cast", "%s",
               diag_message_for(
                   DIAG_ERR_PARSER_CAST_TYPE_RESOLVE_FAILED));
  return annotate(operand, view.target);
}

node_t *lower_implicit_value_conversion(
                                        psx_lowering_context_t *lowering_context,
                                        node_t *operand,
                                        const psx_type_t *target_type,
                                        token_t *fallback_diag_tok,
                                        const ag_compilation_options_t *options) {
  if (!operand || !target_type || !options ||
      ps_type_is_tag_aggregate(target_type))
    return operand;
  if (target_type->kind == PSX_TYPE_INTEGER &&
      target_type->scalar_kind == TK_EOF &&
      ps_node_value_fp_kind(operand) != TK_FLOAT_KIND_NONE) {
    return ps_node_new_fp_to_int_cast_in(
        ps_lowering_arena(lowering_context), operand, target_type);
  }
  if (target_type->kind == PSX_TYPE_BOOL)
    return annotate(ps_node_new_binary_in(
                        ps_lowering_arena(lowering_context), ND_NE, operand,
                        ps_node_new_num_in(
                            ps_lowering_arena(lowering_context), 0)),
                    target_type);
  if (target_type->kind == PSX_TYPE_INTEGER &&
      ps_node_value_fp_kind(operand) != TK_FLOAT_KIND_NONE) {
    return ps_node_new_fp_to_int_cast_in(
        ps_lowering_arena(lowering_context), operand, target_type);
  }
  if (target_type->kind == PSX_TYPE_FLOAT)
    return lower_value_to_fp(
        ps_lowering_arena(lowering_context), operand, target_type);
  cast_target_view_t view = target_view(target_type);
  if (view.kind == TK_EOF ||
      (target_type->kind != PSX_TYPE_BOOL &&
       target_type->kind != PSX_TYPE_INTEGER &&
       target_type->kind != PSX_TYPE_FLOAT &&
       target_type->kind != PSX_TYPE_POINTER)) {
    return operand;
  }
  const psx_type_t *source_type = ps_node_get_type(operand);
  if (source_type && ps_type_is_pointer_like(source_type) &&
      ps_type_is_pointer(target_type)) {
    return operand;
  }
  if (source_type &&
      (source_type->kind == PSX_TYPE_INTEGER ||
       source_type->kind == PSX_TYPE_BOOL) &&
      (target_type->kind == PSX_TYPE_INTEGER ||
       target_type->kind == PSX_TYPE_BOOL) &&
      ps_type_sizeof(source_type) == ps_type_sizeof(target_type) &&
      ps_type_is_unsigned(source_type) == ps_type_is_unsigned(target_type)) {
    return operand;
  }
  if (source_type && ps_type_shape_matches(source_type, target_type))
    return operand;
  return lower_cast(
      lowering_context, NULL, operand, view, fallback_diag_tok, options);
}

node_t *lower_source_cast_expression(
    psx_lowering_context_t *lowering_context,
    psx_local_registry_t *local_registry,
    node_t *node, token_t *fallback_diag_tok,
    const ag_compilation_options_t *options) {
  if (!lowering_context || !local_registry || !node ||
      node->kind != ND_CAST || !node->is_source_cast || !options)
    return node;
  const psx_type_t *target = node->type;
  node_t *operand = node->lhs;
  node_t *lowered = lower_cast(
      lowering_context, local_registry, operand, target_view(target),
      node->tok ? node->tok : fallback_diag_tok, options);
  if (!lowered) return node;
  token_t *source_tok = node->tok;
  if (lowered == operand && lowered->kind != ND_NUM) {
    node->is_source_cast = 0;
    ps_node_bind_type(node, target);
    return node;
  }
  lowered->tok = source_tok;
  lowered->is_source_cast = 0;
  return lowered;
}

node_t *lower_aggregate_address_expression(
    psx_lowering_context_t *lowering_context, node_t *node) {
  if (!node || node->kind != ND_ADDR || !node->lhs) return node;
  node_t *value = node->lhs;
  token_t *source_tok = node->tok;

  if (value->kind == ND_CAST && value->type &&
      ps_type_is_tag_aggregate(value->type) && value->lhs) {
    node_t *address = ps_node_new_unary_addr_for_in(
        ps_lowering_arena(lowering_context), value->lhs);
    if (node->type) ps_node_bind_type(address, node->type);
    if (!address->tok) address->tok = source_tok;
    return address;
  }
  if (value->kind != ND_COMMA || !value->rhs) return node;

  node_t *address = ps_node_new_addr_value_for_in(
      ps_lowering_arena(lowering_context), value->rhs);
  node_t *lowered = ps_node_new_binary_in(
      ps_lowering_arena(lowering_context), ND_COMMA, value->lhs, address);
  if (!lowered) return node;
  if (!lowered->tok) lowered->tok = source_tok;
  return lowered;
}
