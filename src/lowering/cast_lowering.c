#include "cast_lowering.h"
#include "../diag/diag.h"
#include "../parser/config_runtime.h"
#include "../parser/decl.h"
#include "../parser/diag.h"
#include "../parser/local_registry.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int aggregate_temp_seq;

static node_t *annotate(node_t *node, psx_type_t *type) {
  if (node && type) node->type = type;
  return node;
}

static node_t *fp_to_int(node_t *operand, psx_type_t *type) {
  if (!operand || ps_node_value_fp_kind(operand) == TK_FLOAT_KIND_NONE)
    return operand;
  return ps_node_new_fp_to_int_cast(operand, 4, type);
}

static node_t *lower_value_to_fp(node_t *operand, tk_float_kind_t target) {
  if (!operand) return NULL;
  psx_type_t *type = ps_type_new_float(
      target, target == TK_FLOAT_KIND_FLOAT ? 4 : 8);
  tk_float_kind_t source = ps_node_value_fp_kind(operand);
  if (source == target ||
      (source != TK_FLOAT_KIND_NONE &&
       (source >= TK_FLOAT_KIND_DOUBLE) ==
           (target >= TK_FLOAT_KIND_DOUBLE))) {
    operand->fp_kind = target;
    return annotate(operand, type);
  }
  return ps_node_new_int_to_fp_cast(operand, target, type);
}

typedef struct {
  psx_type_t *target;
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

static cast_target_view_t target_view(psx_type_t *target) {
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
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int is_tag_pointer = 0;
  ps_node_get_tag_type(value, &tag_kind, &tag_name, &tag_len,
                       &is_tag_pointer);
  return !is_tag_pointer && tag_kind == view->tag_kind &&
         tag_len == view->value->tag_len &&
         strncmp(tag_name ? tag_name : "",
                 view->value->tag_name ? view->value->tag_name : "",
                 (size_t)tag_len) == 0;
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
  token_kind_t tag_kind = TK_EOF;
  int is_tag_pointer = 0;
  ps_node_get_tag_type(value, &tag_kind, NULL, NULL, &is_tag_pointer);
  int value_size = ps_node_type_size(value);
  return !is_tag_pointer && tag_kind == view->tag_kind &&
         value_size > 0 && view->elem_size > 0 &&
         value_size == view->elem_size;
}

static char *new_aggregate_temp_name(void) {
  int seq = aggregate_temp_seq++;
  int len = snprintf(NULL, 0, "__aggregate_cast_%d", seq);
  char *name = calloc((size_t)len + 1, 1);
  snprintf(name, (size_t)len + 1, "__aggregate_cast_%d", seq);
  return name;
}

static node_t *lower_aggregate_cast(node_t *operand,
                                    cast_target_view_t view,
                                    token_t *diag_tok) {
  if (same_tag_value(operand, &view) ||
      (ps_get_enable_size_compatible_nonscalar_cast() &&
       size_compatible_tag_value(operand, &view))) {
    return ps_node_new_aggregate_cast_result(operand, view.target);
  }

  token_kind_t operand_tag_kind = TK_EOF;
  int operand_is_tag_pointer = 0;
  ps_node_get_tag_type(operand, &operand_tag_kind, NULL, NULL,
                       &operand_is_tag_pointer);
  if (!operand_is_tag_pointer &&
      ps_ctx_is_tag_aggregate_kind(operand_tag_kind)) {
    ps_diag_ctx(diag_tok, "cast",
                 diag_message_for(
                     DIAG_ERR_PARSER_CAST_NONSCALAR_TYPE_MISMATCH),
                 ps_ctx_tag_kind_spelling(view.tag_kind));
  }

  if (view.tag_kind == TK_STRUCT &&
      !ps_get_enable_struct_scalar_pointer_cast()) {
    ps_diag_ctx(diag_tok, "cast", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_CAST_STRUCT_SCALAR_POINTER_DISABLED));
  }
  if (view.tag_kind == TK_UNION &&
      !ps_get_enable_union_scalar_pointer_cast()) {
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
  psx_aggregate_definition_t *definition =
      view.value->aggregate_definition;
  if (definition) {
    for (int i = 0; i < definition->member_count; i++) {
      if (definition->members[i].len <= 0) continue;
      member = definition->members[i];
      member_found = 1;
      break;
    }
  } else {
    member_found = ps_tag_first_named_member(
        view.tag_kind, view.value->tag_name, view.value->tag_len,
        &member, NULL);
  }
  if (!member_found) {
    ps_diag_ctx(diag_tok, "cast", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
  }

  int object_size = view.elem_size > 0 ? view.elem_size : 8;
  char *temp_name = new_aggregate_temp_name();
  lvar_t *temp = psx_decl_register_lvar_sized(
      temp_name, (int)strlen(temp_name), object_size, object_size, 0);
  ps_local_registry_set_decl_type(temp, view.target);

  node_t *member_ref =
      ps_node_new_tag_member_lvar_ref_for(temp, member.offset, &member);
  node_t *assign = ps_node_new_assign(member_ref, operand);
  node_t *result = ps_node_new_lvar_expr_ref_for(temp, 0);
  return ps_node_new_binary(ND_COMMA, assign, result);
}

static node_t *pointer_result(node_t *operand, cast_target_view_t view) {
  return ps_node_new_pointer_cast_result(
      operand, view.target, view.kind, view.tag_kind,
      view.value ? view.value->tag_name : NULL,
      view.value ? view.value->tag_len : 0,
      view.elem_size, view.is_unsigned);
}

static node_t *integer_result(node_t *operand, cast_target_view_t view,
                              int size, int is_unsigned, int is_long_long) {
  return ps_node_new_integer_cast_result(
      operand, view.target, size, is_unsigned, is_long_long);
}

static node_t *integer_result_ex(node_t *operand, cast_target_view_t view,
                                 int size, int is_unsigned, int is_long_long,
                                 int is_plain_char, int widen_zext_i64) {
  return ps_node_new_integer_cast_result_ex(
      operand, view.target, size, is_unsigned, is_long_long,
      is_plain_char, widen_zext_i64);
}

static node_t *lower_cast(node_t *operand, cast_target_view_t view,
                          token_t *diag_tok) {
  if (!view.is_pointer && ps_ctx_is_tag_aggregate_kind(view.tag_kind))
    return lower_aggregate_cast(operand, view, diag_tok);

  if (operand && operand->kind == ND_NUM &&
      ps_node_value_fp_kind(operand) != TK_FLOAT_KIND_NONE &&
      !view.is_pointer &&
      (view.kind == TK_INT || view.kind == TK_LONG ||
       view.kind == TK_SHORT || view.kind == TK_CHAR ||
       view.kind == TK_ENUM || view.kind == TK_SIGNED ||
       view.kind == TK_UNSIGNED || view.kind == TK_BOOL)) {
    operand = ps_node_new_num((long long)((node_num_t *)operand)->fval);
  }

  if (view.is_pointer || view.kind == TK_LONG) {
    operand = fp_to_int(operand, view.is_pointer ? NULL : view.target);
    operand->fp_kind = TK_FLOAT_KIND_NONE;
    if (!view.is_pointer && view.kind == TK_LONG) {
      if (operand->kind == ND_NUM) {
        ((node_num_t *)operand)->int_is_long = 1;
        ((node_num_t *)operand)->int_is_long_long = view.is_long_long;
        ps_node_set_unsigned(operand, view.is_unsigned);
        return annotate(operand, view.target);
      }
      if (!ps_node_is_pointer(operand)) {
        int zext = ps_node_integer_value_is_unsigned(operand) &&
                   ps_node_type_size(operand) >= 1 &&
                   ps_node_type_size(operand) < 8;
        return integer_result_ex(operand, view, 8, view.is_unsigned,
                                 view.is_long_long, 0, zext);
      }
    }
    if (view.is_pointer &&
        (ps_ctx_is_tag_aggregate_kind(view.tag_kind) ||
         view.kind == TK_FLOAT || view.kind == TK_DOUBLE ||
         ps_node_pointee_is_void(operand)))
      return pointer_result(operand, view);

    int operand_is_tag_pointer = 0;
    ps_node_get_tag_type(operand, NULL, NULL, NULL,
                         &operand_is_tag_pointer);
    if (view.is_pointer && view.elem_size > 0 &&
        (ps_node_is_pointer(operand) || operand_is_tag_pointer) &&
        ps_node_pointer_qual_levels(operand) <= 1)
      return pointer_result(operand, view);
    if (!view.is_pointer && view.kind == TK_LONG &&
        ps_node_is_pointer(operand))
      return integer_result(operand, view, 8, view.is_unsigned,
                            view.is_long_long);
    if (view.is_pointer) return pointer_result(operand, view);
    return annotate(operand, view.target);
  }

  if (view.kind == TK_FLOAT)
    return annotate(lower_value_to_fp(operand, TK_FLOAT_KIND_FLOAT),
                    view.target);
  if (view.kind == TK_DOUBLE)
    return annotate(lower_value_to_fp(operand, TK_FLOAT_KIND_DOUBLE),
                    view.target);

  if (view.kind == TK_INT || view.kind == TK_ENUM ||
      view.kind == TK_SIGNED || view.kind == TK_UNSIGNED) {
    operand = fp_to_int(operand, view.target);
    operand->fp_kind = TK_FLOAT_KIND_NONE;
    int target_unsigned = view.kind != TK_ENUM &&
                          (view.is_unsigned || view.kind == TK_UNSIGNED);
    if (operand->kind == ND_NUM) {
      long long value = ((node_num_t *)operand)->val;
      node_t *number = ps_node_new_num(
          target_unsigned ? (long long)(unsigned)value
                          : (long long)(int)value);
      if (target_unsigned) ps_node_set_unsigned(number, 1);
      return annotate(number, view.target);
    }
    if (ps_node_type_size(operand) > 4 && !ps_node_is_pointer(operand))
      return ps_node_new_i64_to_i32_trunc_cast(
          operand, view.target, target_unsigned);
    int size = ps_node_type_size(operand);
    if (target_unsigned && size >= 1 && size < 4 &&
        ps_node_value_fp_kind(operand) == TK_FLOAT_KIND_NONE &&
        !ps_node_is_pointer(operand)) {
      node_t *masked = ps_node_new_binary(
          ND_BITAND, operand, ps_node_new_num(0xffffffffLL));
      ps_node_set_unsigned(masked, 1);
      return annotate(masked, view.target);
    }
    return integer_result(operand, view, 4, target_unsigned, 0);
  }

  if (view.kind == TK_BOOL)
    return annotate(ps_node_new_binary(
                        ND_NE, operand, ps_node_new_num(0)),
                    view.target);
  if (view.kind == TK_VOID)
    return ps_node_new_void_cast_result(operand, view.target);

  if (view.kind == TK_SHORT || view.kind == TK_CHAR) {
    operand = fp_to_int(operand, view.target);
    operand->fp_kind = TK_FLOAT_KIND_NONE;
    int width = view.kind == TK_SHORT ? 16 : 8;
    long long mask = view.kind == TK_SHORT ? 0xffffLL : 0xffLL;
    if (operand->kind == ND_NUM) {
      long long value = ((node_num_t *)operand)->val;
      long long truncated = view.kind == TK_SHORT
          ? (view.is_unsigned ? (long long)(unsigned short)value
                              : (long long)(short)value)
          : (view.is_unsigned ? (long long)(unsigned char)value
                              : (long long)(signed char)value);
      node_t *number = ps_node_new_num(truncated);
      ((node_num_t *)number)->int_width =
          (unsigned char)(view.kind == TK_SHORT ? 2 : 1);
      if (view.is_plain_char)
        ((node_num_t *)number)->int_is_plain_char = 1;
      if (view.is_unsigned) ps_node_set_unsigned(number, 1);
      return annotate(number, view.target);
    }
    if (view.is_unsigned) {
      node_t *masked = ps_node_new_binary(
          ND_BITAND, operand, ps_node_new_num(mask));
      ps_node_set_unsigned(masked, 1);
      return integer_result(masked, view, width / 8, 1, 0);
    }
    int source_width = ps_node_type_size(operand) >= 8 ? 64 : 32;
    node_t *truncated = ps_node_new_shift_trunc_extend(
        operand, source_width - width, 0);
    return integer_result_ex(truncated, view, width / 8, 0, 0,
                             view.is_plain_char, 0);
  }

  ps_diag_ctx(diag_tok, "cast", "%s",
               diag_message_for(
                   DIAG_ERR_PARSER_CAST_TYPE_RESOLVE_FAILED));
  return annotate(operand, view.target);
}

node_t *lower_implicit_value_conversion(node_t *operand,
                                        psx_type_t *target_type,
                                        token_t *fallback_diag_tok) {
  if (!operand || !target_type || ps_type_is_tag_aggregate(target_type))
    return operand;
  if (target_type->kind == PSX_TYPE_INTEGER &&
      target_type->scalar_kind == TK_EOF &&
      ps_node_value_fp_kind(operand) != TK_FLOAT_KIND_NONE) {
    int width = ps_type_sizeof(target_type) >= 8 ? 8 : 4;
    return ps_node_new_fp_to_int_cast(operand, width, NULL);
  }
  if (target_type->kind == PSX_TYPE_BOOL)
    return annotate(ps_node_new_binary(
                        ND_NE, operand, ps_node_new_num(0)),
                    target_type);
  if (target_type->kind == PSX_TYPE_INTEGER &&
      ps_node_value_fp_kind(operand) != TK_FLOAT_KIND_NONE) {
    int width = ps_type_sizeof(target_type);
    if (width <= 0) width = 4;
    return ps_node_new_fp_to_int_cast(operand, width, target_type);
  }
  if (target_type->kind == PSX_TYPE_FLOAT)
    return annotate(lower_value_to_fp(operand, target_type->fp_kind),
                    target_type);
  cast_target_view_t view = target_view(target_type);
  if (view.kind == TK_EOF ||
      (target_type->kind != PSX_TYPE_BOOL &&
       target_type->kind != PSX_TYPE_INTEGER &&
       target_type->kind != PSX_TYPE_FLOAT &&
       target_type->kind != PSX_TYPE_POINTER)) {
    return operand;
  }
  psx_type_t *source_type = ps_node_get_type(operand);
  if (source_type && ps_type_is_pointer(source_type) &&
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
  return lower_cast(operand, view, fallback_diag_tok);
}

void lower_source_cast_expression(node_t *node, token_t *fallback_diag_tok) {
  if (!node || node->kind != ND_CAST || !node->is_source_cast) return;
  psx_type_t *target = node->type;
  node_t *operand = node->lhs;
  node_t *lowered = lower_cast(
      operand, target_view(target),
      node->tok ? node->tok : fallback_diag_tok);
  if (!lowered) return;
  token_t *source_tok = node->tok;
  if (lowered == operand && lowered->kind != ND_NUM) {
    node->is_source_cast = 0;
    node->type = target;
    node->fp_kind = target ? target->fp_kind : TK_FLOAT_KIND_NONE;
    return;
  }
  if (lowered->kind == ND_NUM)
    *(node_num_t *)node = *(node_num_t *)lowered;
  else
    *node = *lowered;
  node->tok = source_tok;
  node->is_source_cast = 0;
}

void lower_aggregate_address_expression(node_t *node) {
  if (!node || node->kind != ND_ADDR || !node->lhs) return;
  node_t *value = node->lhs;
  token_t *source_tok = node->tok;

  if (value->kind == ND_CAST && value->type &&
      ps_type_is_tag_aggregate(value->type) && value->lhs) {
    node->lhs = value->lhs;
    return;
  }
  if (value->kind != ND_COMMA || !value->rhs) return;

  node_t *address = ps_node_new_addr_value_for(value->rhs);
  node_t *lowered = ps_node_new_binary(ND_COMMA, value->lhs, address);
  *node = *lowered;
  node->tok = source_tok;
}
