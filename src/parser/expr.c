#include "expr.h"
#include "arena.h"
#include "core.h"
#include "decl.h"
#include "diag.h"
#include "dynarray.h"
#include "initializer_syntax.h"
#include "node_utils.h"
#include "ret_pointee_array.h"
#include "semantic_ctx.h"
#include "stmt.h"
#include "config_runtime.h"
#include "type.h"
#include "type_name.h"
#include "../lowering/cast_lowering.h"
#include "../lowering/global_object_lowering.h"
#include "../lowering/local_object_lowering.h"
#include "../lowering/static_data_initializer.h"
#include "../semantic/declaration_resolution.h"
#include "../semantic/function_parameter_resolution.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include "../tokenizer/allocator.h"
#include "../tokenizer/literals.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int string_label_count = 0;
static int float_label_count = 0;
static int compound_lit_seq = 0;

#define PS_MAX_EXPR_NEST_DEPTH 1024
#define PS_MAX_PAREN_NEST_DEPTH 1024

static inline token_t *curtok(void) { return tk_get_current_token(); }
static inline void set_curtok(token_t *tok) { tk_set_current_token(tok); }

typedef struct {
  int unevaluated_operand_depth;
  int expr_nest_depth;
  int paren_nest_depth;
} expr_parse_ctx_t;

typedef struct {
  psx_type_name_t name;
  token_t *after_rparen;
} parsed_parenthesized_type_name_t;

typedef struct {
  int is_pointer;
  int is_funcptr;
  int typedef_base_is_pointer;
} type_name_parse_state_t;

typedef struct {
  psx_type_t *types[16];
  int count;
  int is_variadic;
  psx_funcptr_signature_t legacy;
} expr_function_parameters_t;

static void parse_expr_function_parameters(
    token_t *lparen, expr_function_parameters_t *out) {
  if (!lparen || !out) return;
  *out = (expr_function_parameters_t){0};
  token_t *saved = curtok();
  set_curtok(lparen);
  psx_parsed_function_parameters_t parameters = {0};
  psx_parse_function_parameters_syntax(&parameters);
  psx_declarator_op_t function_op = {.kind = PSX_DECL_OP_FUNCTION};
  psx_resolve_function_parameters_syntax(
      &parameters, &function_op, lparen);
  out->count = function_op.function_param_count;
  out->is_variadic = function_op.function_is_variadic;
  out->legacy = function_op.funcptr_sig.function.callable.signature;
  for (int i = 0; i < out->count && i < 16; i++)
    out->types[i] = function_op.function_param_types[i];
  psx_dispose_function_parameters_syntax(&parameters);
  set_curtok(saved);
}

static expr_parse_ctx_t expr_parse_ctx_default(void) {
  expr_parse_ctx_t ctx = {0};
  return ctx;
}

static expr_parse_ctx_t expr_parse_ctx_unevaluated_child(const expr_parse_ctx_t *parent) {
  expr_parse_ctx_t ctx = parent ? *parent : expr_parse_ctx_default();
  ctx.unevaluated_operand_depth++;
  return ctx;
}

static void consume_local_type_quals(token_t **cur);
static long long eval_const_expr_type_size(node_t *n, int *ok);

static void apply_array_abstract_suffix_size(int *sz);
static int is_type_name_start_token(token_t *t);
static char *new_compound_lit_name(void);
static int lvar_is_static_local_array(lvar_t *var);
static node_t *apply_postfix(node_t *node, expr_parse_ctx_t *ctx);
static node_t *parse_compound_literal_from_type(
    psx_type_t *parsed_type, token_t *after_rparen,
    int compound_addr_context, expr_parse_ctx_t *ctx);

static void enter_expr_nest_or_die(expr_parse_ctx_t *ctx) {
  if (!ctx) return;
  ctx->expr_nest_depth++;
  if (ctx->expr_nest_depth > PS_MAX_EXPR_NEST_DEPTH) {
    psx_diag_ctx(curtok(), "expr",
                 diag_message_for(DIAG_ERR_PARSER_EXPR_NEST_TOO_DEEP),
                 PS_MAX_EXPR_NEST_DEPTH);
  }
}

static void leave_expr_nest(expr_parse_ctx_t *ctx) {
  if (ctx && ctx->expr_nest_depth > 0) ctx->expr_nest_depth--;
}

static void enter_paren_nest_or_die(expr_parse_ctx_t *ctx) {
  if (!ctx) return;
  ctx->paren_nest_depth++;
  if (ctx->paren_nest_depth > PS_MAX_PAREN_NEST_DEPTH) {
    psx_diag_ctx(curtok(), "paren",
                 diag_message_for(DIAG_ERR_PARSER_PAREN_NEST_TOO_DEEP),
                 PS_MAX_PAREN_NEST_DEPTH);
  }
}

static void leave_paren_nest(expr_parse_ctx_t *ctx) {
  if (ctx && ctx->paren_nest_depth > 0) ctx->paren_nest_depth--;
}

static int in_unevaluated_operand(const expr_parse_ctx_t *ctx) {
  return ctx && ctx->unevaluated_operand_depth > 0;
}

static node_t *annotate_lvar_usage_node(node_t *node, lvar_t *var, const expr_parse_ctx_t *ctx) {
  if (!node || !var) return node;
  node->usage_lvar = var;
  node->records_lvar_usage = 1;
  node->lvar_usage_unevaluated = in_unevaluated_operand(ctx) ? 1 : 0;
  return node;
}

static node_t *annotate_lvar_sizeof_usage_node(node_t *node, lvar_t *var) {
  if (!node || !var) return node;
  node->usage_lvar = var;
  node->records_lvar_usage = 1;
  node->lvar_usage_unevaluated = 1;
  return node;
}

static node_t *new_binary_with_source_op(node_kind_t kind, node_t *lhs, node_t *rhs,
                                         token_kind_t source_op) {
  node_t *node = psx_node_new_binary(kind, lhs, rhs);
  if (node) node->source_op = source_op;
  return node;
}

static tk_float_kind_t expr_node_fp_kind(node_t *node) {
  if (!node) return TK_FLOAT_KIND_NONE;
  psx_type_t *type = ps_node_get_type(node);
  if (type && !psx_type_is_pointer(type) &&
      (type->kind == PSX_TYPE_FLOAT || type->kind == PSX_TYPE_COMPLEX)) {
    return type->fp_kind != TK_FLOAT_KIND_NONE ? type->fp_kind : TK_FLOAT_KIND_DOUBLE;
  }
  return TK_FLOAT_KIND_NONE;
}

static int sizeof_expr_node(node_t *node) {
  /* C11 6.5.3.4p2: sizeof は対象式が「配列型」のときは配列全体のサイズを返す。
   * `"hello"` の sizeof は `char[6]` の 6 であって、decay 後のポインタサイズ
   * (= 8) ではない。 */
  if (node && node->kind == ND_STRING) {
    node_string_t *s = (node_string_t *)node;
    int elem = s->char_width ? (int)s->char_width : 1;
    return (s->byte_len + 1) * elem;
  }
  int cl_array_size = psx_node_compound_literal_array_size(node);
  if (cl_array_size > 0) return cl_array_size;
  int sz = ps_node_type_size(node);
  if (sz) return sz;
  tk_float_kind_t fp_kind = expr_node_fp_kind(node);
  if (fp_kind == TK_FLOAT_KIND_FLOAT) return 4;
  if (fp_kind >= TK_FLOAT_KIND_DOUBLE) return 8;
  return 8;
}

static long long eval_const_expr_type_size(node_t *n, int *ok) {
  if (!n) { *ok = 0; return 0; }
  switch (n->kind) {
  case ND_NUM:
    return ((node_num_t *)n)->val;
  case ND_COMMA:
    (void)eval_const_expr_type_size(n->lhs, ok);
    if (!*ok) return 0;
    return eval_const_expr_type_size(n->rhs, ok);
  case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV:
    break;
  default:
    *ok = 0; return 0;
  }
  long long l = eval_const_expr_type_size(n->lhs, ok);
  if (!*ok) return 0;
  long long r = eval_const_expr_type_size(n->rhs, ok);
  if (!*ok) return 0;
  switch (n->kind) {
  case ND_ADD: return l + r;
  case ND_SUB: return l - r;
  case ND_MUL: return l * r;
  case ND_DIV:
    if (r == 0) { *ok = 0; return 0; }
    return l / r;
  default:     *ok = 0; return 0;
  }
}

static void apply_array_abstract_suffix_size(int *sz) {
  while (tk_consume('[')) {
    if (tk_consume(']')) {
      psx_diag_ctx(curtok(), "sizeof", "%s",
                   diag_message_for(DIAG_ERR_PARSER_ARRAY_SIZE_CONSTEXPR_REQUIRED));
    }
    node_t *dim_expr = psx_expr_assign();
    int ok = 1;
    long long dim = eval_const_expr_type_size(dim_expr, &ok);
    if (!ok) {
      psx_diag_ctx(curtok(), "sizeof", diag_message_for(DIAG_ERR_PARSER_NONNEG_CONSTEXPR_REQUIRED),
                   diag_text_for(DIAG_TEXT_ARRAY_SIZE));
    }
    if (dim <= 0) {
      psx_diag_ctx(curtok(), "sizeof", "%s",
                   diag_message_for(DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
    }
    tk_expect(']');
    *sz *= (int)dim;
  }
}

static token_t *skip_balanced_paren_token(token_t *start) {
  if (!start || start->kind != TK_LPAREN) return NULL;
  int depth = 0;
  for (token_t *t = start; t; t = t->next) {
    if (t->kind == TK_LPAREN) depth++;
    else if (t->kind == TK_RPAREN) {
      depth--;
      if (depth == 0) return t->next;
    }
    if (t->kind == TK_EOF) break;
  }
  return NULL;
}

static token_t *skip_balanced_bracket_token(token_t *start) {
  if (!start || start->kind != TK_LBRACKET) return NULL;
  int depth = 0;
  for (token_t *t = start; t; t = t->next) {
    if (t->kind == TK_LBRACKET) depth++;
    else if (t->kind == TK_RBRACKET) {
      depth--;
      if (depth == 0) return t->next;
    }
    if (t->kind == TK_EOF) break;
  }
  return NULL;
}

// "[N1][N2]..." の連続を読み、各 N (>0 の整数リテラル) を掛け合わせて *out_mul に入れる。
// 1 つも [...] が無いか不正なら false を返し、*pt は変更しない（失敗時の巻き戻しは呼び出し側が return 0 で行う想定）。
// 成功時は *pt を最後の ']' の次まで進める。
static bool consume_const_dim_brackets_ex(token_t **pt, int *out_mul,
                                          int *out_dims, int max_dims,
                                          int *out_dim_count) {
  token_t *t = *pt;
  if (!t || t->kind != TK_LBRACKET) return false;
  int array_mul = 1;
  int dim_count = 0;
  while (t && t->kind == TK_LBRACKET) {
    token_t *open = t;
    token_t *after = skip_balanced_bracket_token(open);
    if (!after) return false;
    token_t *dim_tok = open->next;
    if (!dim_tok || dim_tok->kind != TK_NUM || tk_as_num(dim_tok)->num_kind != TK_NUM_KIND_INT ||
        !dim_tok->next || dim_tok->next->kind != TK_RBRACKET) {
      return false;
    }
    int dim = (int)tk_as_num_int(dim_tok)->uval;
    if (dim <= 0) return false;
    if (out_dims && dim_count < max_dims) out_dims[dim_count] = dim;
    dim_count++;
    array_mul *= dim;
    t = after;
  }
  *pt = t;
  if (out_mul) *out_mul = array_mul;
  if (out_dim_count) *out_dim_count = dim_count < max_dims ? dim_count : max_dims;
  return true;
}

static bool consume_const_dim_brackets(token_t **pt, int *out_mul) {
  return consume_const_dim_brackets_ex(pt, out_mul, NULL, 0, NULL);
}

// "[...]+" の連続を読み飛ばす（次元サイズは見ない）。
// 1 つも [...] が無いか balanced ペアが壊れていれば false。
static bool skip_bracket_sequence(token_t **pt) {
  token_t *t = *pt;
  if (!t || t->kind != TK_LBRACKET) return false;
  while (t && t->kind == TK_LBRACKET) {
    t = skip_balanced_bracket_token(t);
    if (!t) return false;
  }
  *pt = t;
  return true;
}

static int parse_funcptr_abstract_decl(token_t **ptok, int *is_pointer,
                                       expr_function_parameters_t *out_params) {
  token_t *t = *ptok;
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  psx_skip_gnu_attributes_at(&t);
  if (!t || t->kind != TK_MUL) return 0;
  while (t && t->kind == TK_MUL) {
    *is_pointer = 1;
    t = t->next;
    consume_local_type_quals(&t);
  }
  if (t && t->kind == TK_IDENT) t = t->next; // named declaratorも許可
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LPAREN) return 0;
  token_t *param_lparen = t;
  token_t *after_params = skip_balanced_paren_token(t);
  if (!after_params) return 0;
  if (out_params) parse_expr_function_parameters(param_lparen, out_params);
  *ptok = after_params;
  *is_pointer = 1;
  return 1;
}

static int parse_array_of_funcptr_abstract_decl(token_t **ptok, int *out_array_mul) {
  token_t *t = *ptok;
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  int array_mul = 1;
  if (!consume_const_dim_brackets(&t, &array_mul)) return 0;
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LPAREN) return 0;
  token_t *after_params = skip_balanced_paren_token(t);
  if (!after_params) return 0;
  *ptok = after_params;
  if (out_array_mul) *out_array_mul = array_mul;
  return 1;
}

static int parse_ptr_to_array_abstract_decl(token_t **ptok, int *is_pointer,
                                            int *out_array_count,
                                            int *out_array_dims,
                                            int *out_array_dim_count,
                                            int *out_ptr_array_pointee_bytes,
                                            int elem_size) {
  token_t *t = *ptok;
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  while (t && t->kind == TK_MUL) {
    *is_pointer = 1;
    t = t->next;
    consume_local_type_quals(&t);
  }
  if (t && t->kind == TK_IDENT) t = t->next;
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  int array_mul = 0;
  int dim_count = 0;
  int dims[8] = {0};
  token_t *brackets = t;
  if (consume_const_dim_brackets_ex(&t, &array_mul, dims, 8, &dim_count)) {
    if (out_array_count) *out_array_count = array_mul;
    if (out_array_dims) {
      for (int i = 0; i < dim_count && i < 8; i++) out_array_dims[i] = dims[i];
    }
    if (out_array_dim_count) *out_array_dim_count = dim_count;
    if (out_ptr_array_pointee_bytes && elem_size > 0) {
      *out_ptr_array_pointee_bytes = array_mul * elem_size;
    }
  } else {
    t = brackets;
    if (!skip_bracket_sequence(&t)) return 0;
  }
  *ptok = t;
  *is_pointer = 1;
  return 1;
}

static int parse_array_of_ptr_to_array_abstract_decl_ex(token_t **ptok, int *out_array_mul,
                                                        int *out_pointee_mul) {
  token_t *t = *ptok;
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  int array_mul = 1;
  if (!consume_const_dim_brackets(&t, &array_mul)) return 0;
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  int pointee_mul = 1;
  if (!consume_const_dim_brackets(&t, &pointee_mul)) return 0;
  *ptok = t;
  if (out_array_mul) *out_array_mul = array_mul;
  if (out_pointee_mul) *out_pointee_mul = pointee_mul;
  return 1;
}

static int parse_array_of_ptr_to_array_abstract_decl(token_t **ptok, int *out_array_mul) {
  return parse_array_of_ptr_to_array_abstract_decl_ex(ptok, out_array_mul, NULL);
}

// Parse nested abstract declarator like: int (*(*[N])[M])
// and return the outer array multiplier N so sizeof(type) can be computed.
static int parse_array_of_ptr_to_array_of_ptr_abstract_decl(token_t **ptok, int *out_array_mul) {
  token_t *t = *ptok;
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_LBRACKET) return 0;
  if (!t->next || t->next->kind != TK_NUM || tk_as_num(t->next)->num_kind != TK_NUM_KIND_INT) return 0;
  int n = (int)tk_as_num_int(t->next)->uval;
  if (n <= 0) return 0;
  t = t->next->next;
  if (!t || t->kind != TK_RBRACKET) return 0;
  t = t->next;
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LBRACKET) return 0;
  token_t *after_inner_array = skip_balanced_bracket_token(t);
  if (!after_inner_array) return 0;
  t = after_inner_array;
  if (!t || t->kind != TK_RPAREN) return 0;
  *ptok = t->next;
  if (out_array_mul) *out_array_mul = n;
  return 1;
}

// Parse abstract declarator like: int (*(*)(void))[3]
static int parse_ptr_to_func_returning_ptr_to_array_abstract_decl(
    token_t **ptok, expr_function_parameters_t *out_params,
    psx_ret_pointee_array_t *out_ret_array, int elem_size) {
  token_t *t = *ptok;
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LPAREN) return 0;
  token_t *param_lparen = t;
  token_t *after_params = skip_balanced_paren_token(t);
  if (!after_params) return 0;
  t = after_params;
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  int array_mul = 0;
  int dim_count = 0;
  int dims[8] = {0};
  token_t *brackets = t;
  if (consume_const_dim_brackets_ex(&t, &array_mul, dims, 8, &dim_count)) {
    if (out_ret_array) {
      *out_ret_array = psx_ret_pointee_array_make(
          dim_count >= 1 ? dims[0] : 0,
          dim_count >= 2 ? dims[1] : 0,
          elem_size);
    }
  } else {
    t = brackets;
    if (!skip_bracket_sequence(&t)) return 0;
  }
  if (out_params) parse_expr_function_parameters(param_lparen, out_params);
  *ptok = t;
  return 1;
}

// Parse abstract declarator like: int (*(*[N])(void))[M]
static int parse_array_of_ptr_to_func_returning_ptr_to_array_abstract_decl(token_t **ptok,
                                                                            int *out_array_mul) {
  token_t *t = *ptok;
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_LBRACKET) return 0;
  if (!t->next || t->next->kind != TK_NUM || tk_as_num(t->next)->num_kind != TK_NUM_KIND_INT) return 0;
  int n = (int)tk_as_num_int(t->next)->uval;
  if (n <= 0) return 0;
  t = t->next->next;
  if (!t || t->kind != TK_RBRACKET) return 0;
  t = t->next;
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LPAREN) return 0;
  token_t *after_params = skip_balanced_paren_token(t);
  if (!after_params) return 0;
  t = after_params;
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!skip_bracket_sequence(&t)) return 0;
  *ptok = t;
  if (out_array_mul) *out_array_mul = n;
  return 1;
}

// Parse abstract declarator like: int (*(*)(void))(int)
static int parse_ptr_to_func_returning_ptr_to_func_abstract_decl(
    token_t **ptok, expr_function_parameters_t *out_params,
    expr_function_parameters_t *out_returned_params) {
  token_t *t = *ptok;
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LPAREN) return 0;
  token_t *outer_param_lparen = t;
  token_t *after_params = skip_balanced_paren_token(t);
  if (!after_params) return 0;
  t = after_params;
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LPAREN) return 0;
  token_t *returned_param_lparen = t;
  after_params = skip_balanced_paren_token(t);
  if (!after_params) return 0;
  if (out_params)
    parse_expr_function_parameters(outer_param_lparen, out_params);
  if (out_returned_params)
    parse_expr_function_parameters(
        returned_param_lparen, out_returned_params);
  *ptok = after_params;
  return 1;
}

// Parse abstract declarator like: int (*(*(*)(void))(int))[3]
static int parse_ptr_to_func_returning_ptr_to_func_returning_ptr_to_array_abstract_decl(token_t **ptok) {
  token_t *t = *ptok;
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_LPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_MUL) return 0;
  t = t->next;
  consume_local_type_quals(&t);
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LPAREN) return 0;
  token_t *after = skip_balanced_paren_token(t);
  if (!after) return 0;
  t = after;
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LPAREN) return 0;
  after = skip_balanced_paren_token(t);
  if (!after) return 0;
  t = after;
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!skip_bracket_sequence(&t)) return 0;
  *ptok = t;
  return 1;
}

static int is_type_name_start_token(token_t *t) {
  if (!t) return 0;
  if (t->kind == TK_CONST || t->kind == TK_VOLATILE || t->kind == TK_RESTRICT || t->kind == TK_ATOMIC) return 1;
  if (t->kind == TK_STRUCT || t->kind == TK_UNION || t->kind == TK_ENUM) return 1;
  if (psx_ctx_is_type_token(t->kind)) return 1;
  if (psx_ctx_is_typedef_name_token(t)) return 1;
  return 0;
}

static void consume_local_type_quals(token_t **cur) {
  while (*cur) {
    psx_skip_gnu_attributes_at(cur);
    if (!(*cur) || ((*cur)->kind != TK_CONST && (*cur)->kind != TK_VOLATILE &&
                    (*cur)->kind != TK_RESTRICT)) {
      break;
    }
    *cur = (*cur)->next;
  }
}

static void consume_cast_pointer_suffix(token_t **cur, int *is_pointer) {
  consume_local_type_quals(cur);
  while (*cur && (*cur)->kind == TK_MUL) {
    (*is_pointer)++;
    *cur = (*cur)->next;
    consume_local_type_quals(cur);
  }
}

static int parse_integer_cast_spec_sequence(token_t *start, token_kind_t *out_kind, int *out_elem_size,
                                            int *out_is_unsigned, token_t **out_next,
                                            int *out_is_long_long, int *out_is_plain_char) {
  if (out_is_long_long) *out_is_long_long = 0;
  if (out_is_plain_char) *out_is_plain_char = 0;
  if (!start) return 0;
  if (start->kind != TK_SIGNED && start->kind != TK_UNSIGNED &&
      start->kind != TK_SHORT && start->kind != TK_LONG &&
      start->kind != TK_INT && start->kind != TK_CHAR) {
    return 0;
  }

  int n_signed = 0, n_unsigned = 0, n_short = 0, n_long = 0, n_int = 0, n_char = 0;
  token_t *t = start;
  while (t && (t->kind == TK_SIGNED || t->kind == TK_UNSIGNED ||
               t->kind == TK_SHORT || t->kind == TK_LONG ||
               t->kind == TK_INT || t->kind == TK_CHAR)) {
    if (t->kind == TK_SIGNED) n_signed++;
    else if (t->kind == TK_UNSIGNED) n_unsigned++;
    else if (t->kind == TK_SHORT) n_short++;
    else if (t->kind == TK_LONG) n_long++;
    else if (t->kind == TK_INT) n_int++;
    else if (t->kind == TK_CHAR) n_char++;
    t = t->next;
  }

  /* `long double` は浮動小数型。`long` だけ消費して `double` を残すと、_Generic 関連型や
   * cast で「long の後に double」になり呼び出し側が ': 必要' (E2006) を出す。整数指定子の
   * 直後が `double` なら整数列ではない (= long double) と判断し、汎用型パーサに委ねる。 */
  if (t && t->kind == TK_DOUBLE) return 0;

  if (n_signed > 1 || n_unsigned > 1 || n_short > 1 || n_long > 2 || n_int > 1 || n_char > 1) {
    diag_emit_tokf(DIAG_ERR_PARSER_INVALID_TYPE_SPEC, start, "%s", diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
  }
  if (n_signed && n_unsigned) {
    diag_emit_tokf(DIAG_ERR_PARSER_INVALID_TYPE_SPEC, start, "%s", diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
  }
  if (n_short && n_long) {
    diag_emit_tokf(DIAG_ERR_PARSER_INVALID_TYPE_SPEC, start, "%s", diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
  }
  if (n_char && (n_short || n_long || n_int)) {
    diag_emit_tokf(DIAG_ERR_PARSER_INVALID_TYPE_SPEC, start, "%s", diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
  }

  token_kind_t kind = TK_INT;
  int elem = 4;
  if (n_char) {
    kind = TK_CHAR;
    elem = 1;
  } else if (n_short) {
    kind = TK_SHORT;
    elem = 2;
  } else if (n_long) {
    kind = TK_LONG;
    elem = 8;
  } else if (n_unsigned) {
    kind = TK_UNSIGNED;
    elem = 4;
  } else if (n_signed) {
    kind = TK_SIGNED;
    elem = 4;
  }

  if (out_kind) *out_kind = kind;
  if (out_elem_size) *out_elem_size = elem;
  if (out_is_unsigned) *out_is_unsigned = n_unsigned ? 1 : 0;
  if (out_next) *out_next = t;
  if (out_is_long_long) *out_is_long_long = (n_long >= 2) ? 1 : 0;
  if (out_is_plain_char) *out_is_plain_char = (n_char && !n_signed && !n_unsigned) ? 1 : 0;
  return 1;
}

static int parse_scalar_type_name_base(token_t **cursor,
                                       psx_type_name_t *out) {
  token_t *t = cursor ? *cursor : NULL;
  if (!t || !out) return 0;

  int complex_prefix =
      t->kind == TK_COMPLEX || t->kind == TK_IMAGINARY;
  if (complex_prefix) {
    out->is_complex = 1;
    t = t->next;
  }

  if (t && t->kind == TK_LONG && t->next &&
      t->next->kind == TK_DOUBLE) {
    out->base_kind = TK_DOUBLE;
    out->base_size = 8;
    out->fp_kind = TK_FLOAT_KIND_DOUBLE;
    out->is_long_double = 1;
    t = t->next->next;
    if (t && (t->kind == TK_COMPLEX || t->kind == TK_IMAGINARY)) {
      out->is_complex = 1;
      t = t->next;
    }
    *cursor = t;
    return 1;
  }

  if (t && (t->kind == TK_FLOAT || t->kind == TK_DOUBLE)) {
    out->base_kind = t->kind;
    psx_ctx_get_type_info(t->kind, NULL, &out->base_size);
    out->fp_kind = t->kind == TK_FLOAT ? TK_FLOAT_KIND_FLOAT
                                       : TK_FLOAT_KIND_DOUBLE;
    t = t->next;
    if (t && (t->kind == TK_COMPLEX || t->kind == TK_IMAGINARY)) {
      out->is_complex = 1;
      t = t->next;
    }
    *cursor = t;
    return 1;
  }

  if (complex_prefix) {
    diag_emit_tokf(
        DIAG_ERR_PARSER_INVALID_CONTEXT, *cursor, "%s",
        diag_message_for(
            DIAG_ERR_PARSER_COMPLEX_IMAGINARY_CAST_REQUIRES_FLOAT));
    return 0;
  }

  token_t *after = NULL;
  int is_unsigned = 0;
  int is_long_long = 0;
  int is_plain_char = 0;
  if (parse_integer_cast_spec_sequence(
          t, &out->base_kind, &out->base_size, &is_unsigned, &after,
          &is_long_long, &is_plain_char)) {
    out->is_unsigned = is_unsigned ? 1u : 0u;
    out->is_long_long = is_long_long ? 1u : 0u;
    out->is_plain_char = is_plain_char ? 1u : 0u;
    out->fp_kind = TK_FLOAT_KIND_NONE;
    *cursor = after;
    return 1;
  }

  bool is_type = false;
  psx_ctx_get_type_info(t->kind, &is_type, &out->base_size);
  if (!is_type) return 0;
  out->base_kind = t->kind;
  *cursor = t->next;
  return 1;
}

static int parse_named_type_name_base(token_t **cursor,
                                      psx_type_name_t *out,
                                      type_name_parse_state_t *state,
                                      int preserve_typedef_canonical,
                                      int copy_typedef_array_shape) {
  token_t *t = cursor ? *cursor : NULL;
  if (!t || !out || !state) return 0;

  if (psx_ctx_is_tag_keyword(t->kind)) {
    token_kind_t tag_kind = t->kind;
    token_t *tag_tok = t->next;
    if (!tag_tok || tag_tok->kind != TK_IDENT) return 0;
    token_ident_t *tag = (token_ident_t *)tag_tok;
    if (!psx_ctx_has_tag_type(tag_kind, tag->str, tag->len)) {
      psx_diag_undefined_with_name(tag_tok,
                                   diag_text_for(DIAG_TEXT_TAG_TYPE_SUFFIX),
                                   tag->str, tag->len);
    }
    out->base_kind = tag_kind;
    out->tag_kind = tag_kind;
    out->tag_name = tag->str;
    out->tag_len = tag->len;
    out->base_size =
        psx_ctx_get_tag_size(tag_kind, tag->str, tag->len);
    *cursor = tag_tok->next;
    return 1;
  }

  if (!psx_ctx_is_typedef_name_token(t)) return 0;
  token_ident_t *id = (token_ident_t *)t;
  psx_typedef_info_t info = {0};
  if (!psx_ctx_find_typedef_name(id->str, id->len, &info)) return 0;

  out->base_kind = info.tag_kind != TK_EOF ? info.tag_kind
                                            : info.base_kind;
  out->base_size = info.elem_size;
  out->fp_kind = info.fp_kind;
  out->tag_kind = info.tag_kind;
  out->tag_name = info.tag_name;
  out->tag_len = info.tag_len;
  out->is_unsigned = info.is_unsigned ? 1u : 0u;
  out->is_long_double = info.is_long_double ? 1u : 0u;
  out->pointee_const = info.pointee_const_qualified ? 1 : 0;
  out->pointee_volatile = info.pointee_volatile_qualified ? 1 : 0;
  out->funcptr_sig = psx_ctx_typedef_funcptr_sig(&info);
  state->is_pointer = info.is_pointer;
  state->is_funcptr = info.is_funcptr;
  state->typedef_base_is_pointer = info.is_pointer;
  if (preserve_typedef_canonical)
    out->canonical_base = psx_type_clone(psx_ctx_typedef_decl_type(&info));
  else
    out->pointer_levels = info.is_pointer;

  if (copy_typedef_array_shape && info.is_array &&
      info.array_dim_count > 0) {
    int total = 1;
    int dim_count = info.array_dim_count > 8 ? 8 : info.array_dim_count;
    for (int i = 0; i < dim_count; i++) {
      out->array_dims[i] = info.array_dims[i];
      if (info.array_dims[i] > 0) total *= info.array_dims[i];
    }
    out->array_count = total;
    out->array_dim_count = dim_count;
  }
  out->is_unspecified_array = info.is_array ? 1u : 0u;
  *cursor = t->next;
  return 1;
}

static psx_type_t *generic_control_type(node_t *control) {
  psx_type_t *type = psx_type_clone(ps_node_get_type(control));
  if (!type) return NULL;
  if (type->kind == PSX_TYPE_ARRAY) {
    int deref_size = ps_type_sizeof(type->base);
    type = psx_type_new_pointer(type->base, deref_size);
  } else if (type->kind == PSX_TYPE_FUNCTION) {
    type = psx_type_new_pointer(type, 0);
  }
  psx_type_normalize_integer_identity(type);
  return type;
}

// _Generic の関連型に出てくる CV 修飾子を読み飛ばしながら、
// const/volatile が現れたかどうかを out_const / out_volatile に反映する。
// include_restrict が true の場合は restrict も受理する（後置 cv 用）。
static void consume_type_name_cv_quals(token_t **cursor,
                                       int *out_const,
                                       int *out_volatile,
                                       bool include_restrict) {
  token_t *t = cursor ? *cursor : NULL;
  for (;;) {
    if (!t) break;
    token_t *before_attributes = t;
    psx_skip_gnu_attributes_at(&t);
    if (t != before_attributes) continue;
    token_kind_t k = t->kind;
    if (k == TK_CONST)        { *out_const = 1;    t = t->next; continue; }
    if (k == TK_VOLATILE)     { *out_volatile = 1; t = t->next; continue; }
    if (include_restrict && k == TK_RESTRICT) { t = t->next; continue; }
    break;
  }
  *cursor = t;
}

// _Generic 関連型のベース型 1 つを読む。typedef 名 / struct-or-union-or-enum タグ /
// スカラ型の 3 経路。out / 各 base_* に結果を埋め、未認識なら 0 を返す。
static int parse_assoc_base_type(psx_type_name_t *out, int *out_is_pointer,
                                 int *out_is_funcptr,
                                 int *base_elem_size, tk_float_kind_t *base_fp_kind,
                                 int *base_unsigned, int *base_const, int *base_volatile) {
  token_t *t = curtok();
  type_name_parse_state_t named_state = {0};
  if (parse_named_type_name_base(&t, out, &named_state, 1, 0)) {
    *out_is_pointer = named_state.is_pointer;
    *out_is_funcptr = named_state.is_funcptr;
    *base_elem_size = out->base_size;
    *base_fp_kind = out->fp_kind;
    *base_unsigned = out->is_unsigned;
    if (base_const) *base_const = out->pointee_const;
    if (base_volatile) *base_volatile = out->pointee_volatile;
    set_curtok(t);
    return 1;
  }
  t = curtok();
  if (!parse_scalar_type_name_base(&t, out)) return 0;
  *base_elem_size = out->base_size;
  *base_fp_kind = out->fp_kind;
  *base_unsigned = out->is_unsigned;
  set_curtok(t);
  return 1;
}

// `*` 列を読み、各レベルに const/volatile/restrict/_Atomic 修飾を反映する。
// _Atomic(T) 形式（次が '(') はポインタ修飾子ではないのでスキップ対象外。
static void parse_pointer_levels_with_quals(psx_type_name_t *out,
                                            int *out_is_pointer,
                                            token_t **pt) {
  token_t *t = *pt;
  while (t && t->kind == TK_MUL) {
    *out_is_pointer = 1;
    out->pointer_levels++;
    int level = out->pointer_levels;
    t = t->next;
    while (t && (t->kind == TK_CONST || t->kind == TK_VOLATILE || t->kind == TK_RESTRICT ||
                 (t->kind == TK_ATOMIC && !(t->next && t->next->kind == TK_LPAREN)))) {
      if (t->kind == TK_CONST) out->pointer_const_mask |= (1u << (level - 1));
      if (t->kind == TK_VOLATILE) out->pointer_volatile_mask |= (1u << (level - 1));
      t = t->next;
    }
  }
  *pt = t;
}

static void parse_type_name_abstract_declarators(
    token_t **cursor, psx_type_name_t *out,
    type_name_parse_state_t *state, int base_pointer_before_ptr_array) {
  token_t *t = *cursor;
  int pointer_flag = state->is_pointer;
  int ret_is_data_pointer = out->pointer_levels > 0 ? 1 : 0;
  int ret_is_funcptr = 0;
  int saw_funcptr_suffix = 0;
  psx_ret_pointee_array_t ret_pointee_array = {0};
  expr_function_parameters_t funcptr_parameters = {0};
  expr_function_parameters_t returned_funcptr_parameters = {0};

  if (parse_funcptr_abstract_decl(&t, &pointer_flag,
                                  &funcptr_parameters)) {
    state->is_funcptr = 1;
    saw_funcptr_suffix = 1;
  }

  int elem_store_size = base_pointer_before_ptr_array
                            ? 8
                            : out->base_size;
  (void)parse_ptr_to_array_abstract_decl(
      &t, &pointer_flag, &out->array_count, out->array_dims,
      &out->array_dim_count, &out->pointer_array_pointee_bytes,
      elem_store_size);
  if (out->pointer_array_pointee_bytes > 0) {
    out->pointer_array_element_is_pointer =
        base_pointer_before_ptr_array ? 1u : 0u;
  }

  int fp_array_mul = 0;
  if (parse_array_of_funcptr_abstract_decl(&t, &fp_array_mul) &&
      fp_array_mul > 0) {
    out->array_count = fp_array_mul;
  }

  int ptr_array_mul = 0;
  int ptr_array_pointee_mul = 0;
  if (parse_array_of_ptr_to_array_abstract_decl_ex(
          &t, &ptr_array_mul, &ptr_array_pointee_mul)) {
    if (ptr_array_mul > 0) {
      out->array_count = ptr_array_mul;
      out->array_dims[0] = ptr_array_mul;
      out->array_dim_count = 1;
    }
    if (ptr_array_pointee_mul > 0) {
      out->pointer_array_pointee_bytes =
          ptr_array_pointee_mul * elem_store_size;
      out->pointer_array_element_is_pointer =
          base_pointer_before_ptr_array ? 1u : 0u;
    }
    psx_declarator_shape_init(&out->declarator_shape);
    psx_declarator_shape_append_array(
        &out->declarator_shape, ptr_array_mul);
    psx_declarator_shape_append_pointer(
        &out->declarator_shape, 0, 0);
    psx_declarator_shape_append_array(
        &out->declarator_shape, ptr_array_pointee_mul);
    pointer_flag = 1;
  }

  (void)parse_array_of_ptr_to_array_of_ptr_abstract_decl(&t, NULL);
  if (parse_ptr_to_func_returning_ptr_to_array_abstract_decl(
          &t, &funcptr_parameters, &ret_pointee_array, out->base_size)) {
    pointer_flag = 1;
    state->is_funcptr = 1;
    saw_funcptr_suffix = 1;
    ret_is_data_pointer = 1;
  }
  (void)parse_array_of_ptr_to_func_returning_ptr_to_array_abstract_decl(
      &t, NULL);
  if (parse_ptr_to_func_returning_ptr_to_func_abstract_decl(
          &t, &funcptr_parameters, &returned_funcptr_parameters)) {
    pointer_flag = 1;
    state->is_funcptr = 1;
    saw_funcptr_suffix = 1;
    ret_is_funcptr = 1;
  }
  (void)parse_ptr_to_func_returning_ptr_to_func_returning_ptr_to_array_abstract_decl(
      &t);

  if (pointer_flag && out->pointer_levels == 0 && !out->canonical_base)
    out->pointer_levels = 1;
  state->is_pointer = pointer_flag;
  out->canonicalize_function = state->is_funcptr ? 1u : 0u;
  if (saw_funcptr_suffix) {
    out->funcptr_sig = psx_decl_make_funcptr_sig_from_kind(
        &funcptr_parameters.legacy, out->base_kind, out->fp_kind,
        ret_is_data_pointer, 0, out->is_complex, ret_pointee_array);
    out->has_canonical_function_params = 1;
    out->function_param_count = funcptr_parameters.count;
    out->function_is_variadic = funcptr_parameters.is_variadic;
    for (int i = 0; i < funcptr_parameters.count && i < 16; i++)
      out->function_param_types[i] = funcptr_parameters.types[i];
    if (ret_is_funcptr) {
      psx_decl_funcptr_sig_promote_return_to_funcptr(
          &out->funcptr_sig, &returned_funcptr_parameters.legacy);
      out->has_canonical_returned_function_params = 1;
      out->returned_function_param_count =
          returned_funcptr_parameters.count;
      out->returned_function_is_variadic =
          returned_funcptr_parameters.is_variadic;
      for (int i = 0;
           i < returned_funcptr_parameters.count && i < 16; i++) {
        out->returned_function_param_types[i] =
            returned_funcptr_parameters.types[i];
      }
    }
  }
  *cursor = t;
}

static int parse_atomic_type_name_wrapper(
    token_t **cursor, psx_type_name_t *out,
    type_name_parse_state_t *state, int preserve_typedef_canonical,
    int copy_typedef_array_shape) {
  token_t *t = cursor ? *cursor : NULL;
  if (!t || t->kind != TK_ATOMIC || !t->next ||
      t->next->kind != TK_LPAREN) {
    return 0;
  }

  int wrapper_count = 0;
  while (t && t->kind == TK_ATOMIC && t->next &&
         t->next->kind == TK_LPAREN) {
    wrapper_count++;
    t = t->next->next;
    int ignored_const = 0;
    int ignored_volatile = 0;
    consume_type_name_cv_quals(&t, &ignored_const, &ignored_volatile,
                               true);
  }
  if (t && t->kind == TK_ATOMIC &&
      !(t->next && t->next->kind == TK_LPAREN)) {
    t = t->next;
  }

  if (!parse_scalar_type_name_base(&t, out) &&
      !parse_named_type_name_base(
          &t, out, state, preserve_typedef_canonical,
          copy_typedef_array_shape)) {
    return 0;
  }

  int base_const = 0;
  int base_volatile = 0;
  consume_type_name_cv_quals(&t, &base_const, &base_volatile, true);
  int pointer_before_suffix = out->pointer_levels;
  parse_pointer_levels_with_quals(out, &state->is_pointer, &t);
  int base_pointer_before_ptr_array =
      state->typedef_base_is_pointer ||
      out->pointer_levels > pointer_before_suffix;
  parse_type_name_abstract_declarators(
      &t, out, state, base_pointer_before_ptr_array);
  out->pointee_const = base_const;
  out->pointee_volatile = base_volatile;

  while (wrapper_count-- > 0) {
    if (!t || t->kind != TK_RPAREN) return 0;
    t = t->next;
  }
  *cursor = t;
  return 1;
}

static int parse_generic_assoc_type_name(psx_type_name_t *out) {
  psx_type_name_init(out);
  out->base_size = 8;
  /* ストリーミングのカーソルを進めずに型全体を t->next で先読みするため、未生成境界 (NULL)
   * を踏まないよう先に窓を満たす (非ストリーム時 no-op)。長い派生型 (`int(*(*)(void))[3]`) が
   * 窓境界に跨ると抽象宣言子パーサが有効な型を誤って却下していた。 */
  tk_ensure_lookahead();
  /* _Generic 用: 型名トークン全体 (base + 宣言子) を文字列化するための開始位置。 */
  token_t *sig_start = curtok();
  int base_elem_size = 8;
  tk_float_kind_t base_fp_kind = TK_FLOAT_KIND_NONE;
  int base_unsigned = 0;
  int base_const = 0;
  int base_volatile = 0;
  int is_pointer = 0;
  int is_funcptr = 0;
  token_t *base_cursor = curtok();
  consume_type_name_cv_quals(&base_cursor, &base_const, &base_volatile,
                             false);
  set_curtok(base_cursor);
  type_name_parse_state_t atomic_state = {0};
  token_t *atomic_cursor = base_cursor;
  if (parse_atomic_type_name_wrapper(&atomic_cursor, out, &atomic_state,
                                     1, 0)) {
    is_pointer = atomic_state.is_pointer;
    is_funcptr = atomic_state.is_funcptr;
    base_elem_size = out->base_size;
    base_fp_kind = out->fp_kind;
    base_unsigned = out->is_unsigned;
    set_curtok(atomic_cursor);
  } else if (!parse_assoc_base_type(
                 out, &is_pointer, &is_funcptr, &base_elem_size,
                 &base_fp_kind, &base_unsigned, &base_const,
                 &base_volatile)) {
    return 0;
  }
  out->base_size = base_elem_size;
  out->is_unsigned = base_unsigned;
  out->fp_kind = base_fp_kind;
  token_t *t = curtok();
  consume_type_name_cv_quals(&t, &base_const, &base_volatile, true);
  parse_pointer_levels_with_quals(out, &is_pointer, &t);
  type_name_parse_state_t declarator_state = {
      .is_pointer = is_pointer,
      .is_funcptr = is_funcptr,
      .typedef_base_is_pointer = is_pointer && out->canonical_base != NULL,
  };
  parse_type_name_abstract_declarators(
      &t, out, &declarator_state,
      declarator_state.typedef_base_is_pointer || out->pointer_levels > 0);
  is_pointer = declarator_state.is_pointer;
  // 配列サフィックス: ポインタとして扱わない場合のみ '[' 列を読み飛ばす。
  if (!is_pointer) {
    while (t && t->kind == TK_LBRACKET) {
      token_t *after = skip_balanced_bracket_token(t);
      if (!after) break;
      out->is_unspecified_array = 1;
      t = after;
    }
  }
  if (is_pointer) {
    if (out->pointer_levels == 0 && !out->canonical_base)
      out->pointer_levels = 1;
    out->pointer_deref_size = base_elem_size;
    out->pointer_base_deref_size = base_elem_size;
    out->is_unsigned = base_unsigned;
    out->pointee_const = base_const;
    out->pointee_volatile = base_volatile;
  }
  /* 関数ポインタ / ネスト宣言子など '(' を含む複雑型のみ型シグネチャを作る。 */
  out->type_sig = psx_serialize_decl_type_tokens(sig_start, t, NULL);

  if (!out->canonical_base && out->pointer_levels > 0 &&
      out->fp_kind == TK_FLOAT_KIND_NONE &&
      !psx_ctx_is_tag_aggregate_kind(out->tag_kind)) {
    int size = out->pointer_levels >= 2 && out->pointer_base_deref_size > 0
                   ? out->pointer_base_deref_size
                   : out->pointer_deref_size;
    if (size > 0) out->base_size = size;
    if (out->base_kind != TK_BOOL && out->tag_kind != TK_ENUM) {
      if (out->base_size == 1) out->base_kind = TK_CHAR;
      else if (out->base_size == 2) out->base_kind = TK_SHORT;
      else if (out->base_size == 8) out->base_kind = TK_LONG;
      else out->base_kind = TK_INT;
    }
  }
  set_curtok(t);
  return 1;
}

static int parse_generic_assoc_type(psx_type_t **out) {
  psx_type_name_t name;
  if (!parse_generic_assoc_type_name(&name)) return 0;
  if (out) {
    *out = psx_type_name_build(&name);
    psx_type_normalize_integer_identity(*out);
  }
  return 1;
}

/* rvalue struct (`f().x`): 一時 lvar に代入してメンバアドレス取得可能にする。
 * 戻り値は `(tmp = base, tmp)` 形の ND_COMMA。 */
static node_t *materialize_struct_rvalue_funcall(node_t *base,
                                                  token_kind_t base_tag_kind,
                                                  char *base_tag_name, int base_tag_len) {
  int obj_size = psx_ctx_get_tag_size(base_tag_kind, base_tag_name, base_tag_len);
  if (obj_size <= 0) obj_size = ps_node_type_size(base);
  if (obj_size <= 0) obj_size = 8;
  char *tmp_name = new_compound_lit_name();
  lvar_t *var = psx_decl_register_lvar_sized(tmp_name, (int)strlen(tmp_name), obj_size, obj_size, 0);
  int scope_depth = ps_ctx_get_tag_scope_depth(
      base_tag_kind, base_tag_name, base_tag_len);
  psx_decl_set_lvar_decl_type(
      var, psx_type_new_tag(base_tag_kind, base_tag_name, base_tag_len,
                            scope_depth >= 0 ? scope_depth + 1 : 0,
                            obj_size));
  node_t *lhs_obj = psx_node_new_lvar_expr_ref_for(var, 0);
  node_t *assign_node = psx_node_new_assign(lhs_obj, base);
  return psx_node_new_binary(ND_COMMA, (node_t *)assign_node, psx_node_new_lvar_expr_ref_for(var, 0));
}

/* `(cond ? a : b).v` 形の struct ternary rvalue: 一時 lvar への代入を
 * 両分岐に挟み込み、`(cond ? (tmp=a) : (tmp=b), tmp)` 形の ND_COMMA に変換。 */
static node_t *materialize_struct_rvalue_ternary(node_t *base,
                                                  token_kind_t base_tag_kind,
                                                  char *base_tag_name, int base_tag_len) {
  node_ctrl_t *tern = (node_ctrl_t *)base;
  int obj_size = psx_ctx_get_tag_size(base_tag_kind, base_tag_name, base_tag_len);
  if (obj_size <= 0) obj_size = 8;
  char *tmp_name = new_compound_lit_name();
  lvar_t *var = psx_decl_register_lvar_sized(tmp_name, (int)strlen(tmp_name), obj_size, obj_size, 0);
  int scope_depth = ps_ctx_get_tag_scope_depth(
      base_tag_kind, base_tag_name, base_tag_len);
  psx_decl_set_lvar_decl_type(
      var, psx_type_new_tag(base_tag_kind, base_tag_name, base_tag_len,
                            scope_depth >= 0 ? scope_depth + 1 : 0,
                            obj_size));
  node_t *lhs_then = psx_node_new_lvar_expr_ref_for(var, 0);
  node_t *assign_then = psx_node_new_assign(lhs_then, tern->base.rhs);
  node_t *lhs_else = psx_node_new_lvar_expr_ref_for(var, 0);
  node_t *assign_else = psx_node_new_assign(lhs_else, tern->els);
  node_ctrl_t *select = arena_alloc(sizeof(node_ctrl_t));
  select->base.kind = ND_TERNARY;
  select->base.lhs = tern->base.lhs;
  select->base.rhs = (node_t *)assign_then;
  select->els = (node_t *)assign_else;
  return psx_node_new_binary(ND_COMMA, (node_t *)select, psx_node_new_lvar_expr_ref_for(var, 0));
}

/* `base.member` / `base->member` の deref node を組み立てる。
 * base アドレス + member offset を ADD して DEREF。
 * mem_info から type_size / deref_size / 配列メンバ / スカラポインタメンバ /
 * bitfield / fp_kind / _Bool 等の伝播を全て担う。
 * from_ptr=0 (struct .) のとき base 自身のアドレスを取って加算する。 */
static node_t *build_member_deref_node(node_t *base, int from_ptr,
                                        const tag_member_info_t *mem_info) {
  node_t *addr_base = base;
  if (!from_ptr) {
    if (base->kind == ND_COMMA && base->rhs) {
      addr_base = psx_node_new_binary(ND_COMMA, base->lhs,
                                      psx_node_new_addr_value_for(base->rhs));
    } else {
      addr_base = psx_node_new_addr_value_for(base);
    }
  }
  return psx_node_new_tag_member_deref_for(addr_base, base, mem_info);
}

static int node_is_single_tag_array_view(node_t *node) {
  psx_type_t *type = ps_node_get_type(node);
  return node && node->kind == ND_DEREF &&
         type && type->kind == PSX_TYPE_ARRAY &&
         type->base && ps_type_is_tag_aggregate(type->base);
}

static node_t *build_member_access(node_t *base, int from_ptr, token_t *op_tok) {
  token_ident_t *member = tk_consume_ident();
  if (!member) {
    psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
  }

  token_kind_t base_tag_kind = TK_EOF;
  char *base_tag_name = NULL;
  int base_tag_len = 0;
  int base_is_ptr = 0;
  ps_node_get_tag_type(base, &base_tag_kind, &base_tag_name, &base_tag_len, &base_is_ptr);
  if (!from_ptr && base_is_ptr &&
      (node_is_single_tag_array_view(base) || base->kind == ND_DEREF)) {
    base_is_ptr = 0;
  }
  if (base_tag_kind == TK_EOF || (!from_ptr && base_is_ptr) || (from_ptr && !base_is_ptr)) {
    diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, op_tok,
                   "%s",
                   diag_message_for(from_ptr
                                        ? DIAG_ERR_PARSER_ARROW_LHS_REQUIRES_STRUCT_PTR
                                        : DIAG_ERR_PARSER_DOT_LHS_REQUIRES_STRUCT));
  }

  // rvalue struct/union (e.g. f().x, (cond?a:b).v): materialize once into a tmp
  // so member address can be formed as an lvalue.
  if (!from_ptr && base->kind == ND_FUNCALL && !base_is_ptr) {
    base = materialize_struct_rvalue_funcall(base, base_tag_kind, base_tag_name, base_tag_len);
  }
  if (!from_ptr && base->kind == ND_TERNARY && !base_is_ptr &&
      base_tag_kind != TK_EOF) {
    base = materialize_struct_rvalue_ternary(base, base_tag_kind, base_tag_name, base_tag_len);
  }

  tag_member_info_t mem_info = {0};
  /* タグ shadow 応用形: 変数が宣言時に見ていた tag の scope_depth が分かれば、その scope
   * に固定してメンバを引く。これがないと find_tag_type が最も内側 tag を返してしまい、
   * 外側変数を内側 shadow 下から参照したときにメンバが見つからず E3064 になる。 */
  int base_scope = psx_node_get_tag_scope_depth(base);
  bool found = (base_scope >= 0)
      ? ps_ctx_find_tag_member_info_at_scope(base_tag_kind, base_tag_name, base_tag_len,
                                              base_scope, member->str, member->len, &mem_info)
      : ps_ctx_find_tag_member_info(base_tag_kind, base_tag_name, base_tag_len,
                                     member->str, member->len, &mem_info);
  if (!found) {
    psx_diag_ctx(op_tok, "member", diag_message_for(DIAG_ERR_PARSER_MEMBER_NOT_FOUND),
                 member->len, member->str);
  }
  return build_member_deref_node(base, from_ptr, &mem_info);
}

static node_t *parse_compound_literal_from_type(
    psx_type_t *parsed_type, token_t *after_rparen,
    int compound_addr_context, expr_parse_ctx_t *ctx) {
  set_curtok(after_rparen);
  char *current_funcname = NULL;
  int current_funcname_len = 0;
  psx_decl_get_current_funcname(&current_funcname, &current_funcname_len);
  (void)current_funcname_len;
  token_t *initializer_tok = curtok();
  node_t *initializer = psx_parse_initializer_syntax_list();
  psx_type_t *compound_object_type = psx_type_clone(parsed_type);
  psx_ctx_attach_aggregate_definitions(compound_object_type);
  if (compound_object_type && compound_object_type->kind == PSX_TYPE_ARRAY &&
      compound_object_type->array_len <= 0 && !compound_object_type->is_vla) {
    if (!psx_resolve_incomplete_array_initializer(
            compound_object_type, PSX_DECL_INIT_LIST, initializer)) {
      psx_diag_ctx(initializer_tok, "compound-literal",
                   "複合リテラル `(T[]){...}` の要素数を初期化子から推定できません");
    }
  }
  int is_arr = compound_object_type &&
               compound_object_type->kind == PSX_TYPE_ARRAY;
  int is_pointer = compound_object_type &&
                   compound_object_type->kind == PSX_TYPE_POINTER;
  char *tmp_name = new_compound_lit_name();
  if (current_funcname == NULL) {
    int want_addr = compound_addr_context;
    token_t *init_tok = initializer_tok;
    node_init_list_t *list = (node_init_list_t *)initializer;
    /* `&(int){5}` のように `&` のオペランドなら、ND_NUM への短絡 (アドレス取得不能)
     * を避けてgvar実体化経路へ進む。 */
    if (!is_arr && !want_addr &&
        !ps_type_is_tag_aggregate(compound_object_type) &&
        list->entry_count == 1 &&
        list->entries[0].designator_count == 0 &&
        list->entries[0].value &&
        list->entries[0].value->kind == ND_NUM) {
      return apply_postfix(list->entries[0].value, ctx);
    }
    psx_global_object_result_t object = {0};
    if (!lower_global_object_declaration(
            &(psx_global_object_request_t){
                .name = tmp_name,
                .name_len = (int)strlen(tmp_name),
                .type = compound_object_type,
                .is_static = 1,
                .diag_tok = init_tok,
            },
            &object) ||
        !lower_static_declaration_initializer(
            &(psx_static_declaration_initializer_request_t){
                .global = object.global,
                .type = psx_gvar_get_decl_type(object.global),
                .initializer_kind = PSX_DECL_INIT_LIST,
                .initializer = initializer,
                .diag_tok = init_tok,
            },
            NULL)) {
      psx_diag_ctx(init_tok, "compound-literal", "%s",
                   diag_message_for(
                       DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
    }
    global_var_t *gv = object.global;
    if (is_arr) {
      return apply_postfix(psx_node_new_gvar_array_addr_for(gv), ctx);
    }
    node_gvar_t *gvar_node = (node_gvar_t *)psx_node_new_gvar_for(gv);
    return apply_postfix((node_t *)gvar_node, ctx);
  }
  psx_local_object_result_t object = {0};
  if (!lower_complete_local_object(
          &(psx_local_object_request_t){
              .name = tmp_name,
              .name_len = (int)strlen(tmp_name),
              .type = compound_object_type,
          },
          &object)) {
    psx_diag_ctx(initializer_tok, "compound-literal",
                 "compound literal local storage lowering failed");
  }
  lvar_t *var = object.var;
  node_t *init = psx_decl_bind_initializer_for_var(
      var, is_pointer, initializer, PSX_DECL_INIT_LIST, initializer_tok);
  node_t *ref;
  if (is_arr) {
    ref = psx_node_new_lvar_array_addr_for(var, 0);
  } else {
    ref = psx_node_new_lvar_expr_ref_for(var, is_pointer);
  }
  return psx_node_new_binary(ND_COMMA, init, apply_postfix(ref, ctx));
}

static int parse_cast_array_suffixes_token(token_t **pt, int *out_dims, int max_dims, int *out_dim_count) {
  if (!pt || !*pt || (*pt)->kind != TK_LBRACKET) return 0;
  token_t *t = *pt;
  int dims[8] = {0};
  int dim_count = 0;
  int product = 1;
  while (t && t->kind == TK_LBRACKET) {
    t = t->next;
    int n = 0;
    if (t && t->kind == TK_RBRACKET) {
      n = -1;
    } else if (t && t->kind == TK_NUM && tk_as_num(t)->num_kind == TK_NUM_KIND_INT) {
      n = (int)tk_as_num_int(t)->uval;
      t = t->next;
    } else {
      return 0;
    }
    if (!t || t->kind != TK_RBRACKET) return 0;
    t = t->next;
    if (dim_count < 8) dims[dim_count] = n;
    dim_count++;
    if (n > 0) product *= n;
  }
  if (dim_count > 0 && dims[0] < 0) product = -1;
  if (out_dims && max_dims > 0) {
    int ncopy = dim_count < max_dims ? dim_count : max_dims;
    for (int i = 0; i < ncopy; i++) out_dims[i] = dims[i];
  }
  if (out_dim_count) *out_dim_count = dim_count < max_dims ? dim_count : max_dims;
  *pt = t;
  return product;
}

static int parse_parenthesized_type_name(
    token_t *tok, parsed_parenthesized_type_name_t *out) {
  if (!out) return 0;
  *out = (parsed_parenthesized_type_name_t){0};
  psx_type_name_init(&out->name);
  out->name.base_size = 8;

  psx_type_name_t *name = &out->name;
  token_kind_t *type_kind = &name->base_kind;
  int *is_pointer = &name->pointer_levels;
  token_t **after_rparen = &out->after_rparen;
  token_kind_t *out_tag_kind = &name->tag_kind;
  char **out_tag_name = &name->tag_name;
  int *out_tag_len = &name->tag_len;
  int *out_elem_size = &name->base_size;
  tk_float_kind_t *out_fp_kind = &name->fp_kind;
  int *out_array_count = &name->array_count;
  int *out_array_dims = name->array_dims;
  int *out_array_dim_count = &name->array_dim_count;
  int is_unsigned = 0;
  int is_long_long = 0;
  int is_plain_char = 0;
  int is_long_double = 0;
  int is_complex = 0;
  int pointee_const = 0;
  int pointee_volatile = 0;
  int pointer_array_element_is_pointer = 0;
  int *out_is_unsigned = &is_unsigned;
  int *out_is_long_long = &is_long_long;
  int *out_is_plain_char = &is_plain_char;
  int *out_is_long_double = &is_long_double;
  int *out_is_complex = &is_complex;
  int *out_ptr_array_pointee_bytes = &name->pointer_array_pointee_bytes;
  int *out_ptr_array_element_is_pointer =
      &pointer_array_element_is_pointer;
  psx_decl_funcptr_sig_t *out_funcptr_sig = &name->funcptr_sig;

  if (!tok || tok->kind != TK_LPAREN) return 0;
  tk_ensure_lookahead();
  token_t *t = tok->next;
  if (!t) return 0;
  *type_kind = TK_EOF;
  if (out_tag_kind) *out_tag_kind = TK_EOF;
  if (out_tag_name) *out_tag_name = NULL;
  if (out_tag_len) *out_tag_len = 0;
  if (out_elem_size) *out_elem_size = 8;
  if (out_fp_kind) *out_fp_kind = TK_FLOAT_KIND_NONE;
  if (out_array_count) *out_array_count = 0;
  if (out_array_dim_count) *out_array_dim_count = 0;
  if (out_is_unsigned) *out_is_unsigned = 0;
  if (out_is_long_long) *out_is_long_long = 0;
  if (out_is_plain_char) *out_is_plain_char = 0;
  if (out_is_long_double) *out_is_long_double = 0;
  if (out_is_complex) *out_is_complex = 0;
  if (out_ptr_array_pointee_bytes) *out_ptr_array_pointee_bytes = 0;
  if (out_ptr_array_element_is_pointer) *out_ptr_array_element_is_pointer = 0;
  if (out_funcptr_sig) *out_funcptr_sig = (psx_decl_funcptr_sig_t){0};
  int typedef_base_is_pointer = 0;

  consume_type_name_cv_quals(&t, &pointee_const, &pointee_volatile, true);
  if (t && (t->kind == TK_THREAD_LOCAL || t->kind == TK_EXTERN || t->kind == TK_STATIC ||
            t->kind == TK_AUTO || t->kind == TK_REGISTER || t->kind == TK_TYPEDEF)) {
    psx_diag_ctx(t, "cast", "%s",
                 diag_message_for(DIAG_ERR_PARSER_CAST_STORAGE_CLASS_FORBIDDEN));
  }
  if (t && t->kind == TK_ATOMIC && !(t->next && t->next->kind == TK_LPAREN)) {
    t = t->next;
    consume_local_type_quals(&t);
  }

  if (t->kind == TK_ATOMIC && t->next && t->next->kind == TK_LPAREN) {
    type_name_parse_state_t atomic_state = {0};
    if (!parse_atomic_type_name_wrapper(&t, name, &atomic_state, 0, 1))
      return 0;
    typedef_base_is_pointer = atomic_state.typedef_base_is_pointer;
    is_unsigned = name->is_unsigned;
    is_long_long = name->is_long_long;
    is_plain_char = name->is_plain_char;
    is_long_double = name->is_long_double;
    is_complex = name->is_complex;
    goto cast_parse_postfix;
  }

  if (parse_scalar_type_name_base(&t, name)) {
    is_unsigned = name->is_unsigned;
    is_long_long = name->is_long_long;
    is_plain_char = name->is_plain_char;
    is_long_double = name->is_long_double;
    is_complex = name->is_complex;
  } else {
    type_name_parse_state_t named_state = {0};
    if (!parse_named_type_name_base(&t, name, &named_state, 0, 1))
      return 0;
    typedef_base_is_pointer = named_state.typedef_base_is_pointer;
    is_unsigned = name->is_unsigned;
    is_long_double = name->is_long_double;
  }

cast_parse_postfix:
  if (*is_pointer < 0) *is_pointer = 0;
  int pointer_levels_before_type_suffix = *is_pointer;
  consume_type_name_cv_quals(&t, &pointee_const, &pointee_volatile, true);
  int parsed_is_pointer = *is_pointer > 0;
  parse_pointer_levels_with_quals(name, &parsed_is_pointer, &t);
  int explicit_base_pointer_suffix =
      *is_pointer > pointer_levels_before_type_suffix ? 1 : 0;
  int base_pointer_before_ptr_array =
      typedef_base_is_pointer || explicit_base_pointer_suffix;
  type_name_parse_state_t declarator_state = {
      .is_pointer = parsed_is_pointer,
      .is_funcptr = ps_decl_funcptr_sig_has_payload(name->funcptr_sig),
      .typedef_base_is_pointer = typedef_base_is_pointer,
  };
  parse_type_name_abstract_declarators(
      &t, name, &declarator_state, base_pointer_before_ptr_array);
  pointer_array_element_is_pointer =
      name->pointer_array_element_is_pointer;
  // 配列宣言子 [N][M]... を受理する。
  // 先頭の空 `[]` は -1 を返し、呼び出し側で初期化子から要素数を推定させる。
  if (t && t->kind == TK_LBRACKET) {
    int dims[8] = {0};
    int dim_count = 0;
    int n = parse_cast_array_suffixes_token(&t, dims, 8, &dim_count);
    if (n == 0 || dim_count <= 0) return 0;
    if (out_array_count) *out_array_count = n;
    if (out_array_dims) {
      for (int i = 0; i < dim_count && i < 8; i++) out_array_dims[i] = dims[i];
    }
    if (out_array_dim_count) *out_array_dim_count = dim_count;
    if (dim_count > 0 && dims[0] < 0)
      name->is_unspecified_array = 1;
    if (*is_pointer > 0)
      pointer_array_element_is_pointer = 1;
  }
  if (!t || t->kind != TK_RPAREN || !t->next) return 0;
  *after_rparen = t->next;
  name->is_unsigned = is_unsigned ? 1u : 0u;
  name->is_long_long = is_long_long ? 1u : 0u;
  name->is_plain_char = is_plain_char ? 1u : 0u;
  name->is_long_double = is_long_double ? 1u : 0u;
  name->is_complex = is_complex ? 1u : 0u;
  name->pointee_const = pointee_const;
  name->pointee_volatile = pointee_volatile;
  name->pointer_array_element_is_pointer =
      pointer_array_element_is_pointer ? 1u : 0u;
  return 1;
}

static node_t *expr_internal_ctx(expr_parse_ctx_t *ctx);
static node_t *assign_ctx(expr_parse_ctx_t *ctx);
static node_t *conditional_ctx(expr_parse_ctx_t *ctx);
static node_t *logical_or_ctx(expr_parse_ctx_t *ctx);
static node_t *logical_and_ctx(expr_parse_ctx_t *ctx);
static node_t *bit_or_ctx(expr_parse_ctx_t *ctx);
static node_t *bit_xor_ctx(expr_parse_ctx_t *ctx);
static node_t *bit_and_ctx(expr_parse_ctx_t *ctx);
static node_t *equality_ctx(expr_parse_ctx_t *ctx);
static node_t *relational_ctx(expr_parse_ctx_t *ctx);
static node_t *shift_ctx(expr_parse_ctx_t *ctx);
static node_t *add_ctx(expr_parse_ctx_t *ctx);
static node_t *mul_ctx(expr_parse_ctx_t *ctx);
static node_t *cast_ctx(expr_parse_ctx_t *ctx);
static node_t *cast_with_compound_addr_context(int compound_addr_context, expr_parse_ctx_t *ctx);
static node_t *unary_ctx(expr_parse_ctx_t *ctx);
static node_t *unary_with_compound_addr_context(int compound_addr_context, expr_parse_ctx_t *ctx);
static node_t *primary_with_compound_addr_context(int compound_addr_context, expr_parse_ctx_t *ctx);
static node_t *apply_postfix(node_t *node, expr_parse_ctx_t *ctx);

static char *new_compound_lit_name(void) {
  int n = compound_lit_seq++;
  int len = snprintf(NULL, 0, "__compound_lit_%d", n);
  char *name = calloc((size_t)len + 1, 1);
  snprintf(name, (size_t)len + 1, "__compound_lit_%d", n);
  return name;
}

// 型名トークン直後の共通サフィックス処理。
// 流れ: 後置 cv 修飾子 → ポインタ '*' → 各種抽象宣言子（関数ポインタ/配列等）
//      → 配列サフィックス '[N]' → 閉じ ')'。
// sz には呼び出し側で型の素サイズが入っており、ポインタ化されたら 8、
// 配列マルチプライヤがあれば乗算されたサイズに書き換えられる。
static int finish_parenthesized_type_size(token_t *t, int sz, int alignof_mode) {
  int decl_is_ptr = 0;
  consume_local_type_quals(&t);
  consume_cast_pointer_suffix(&t, &decl_is_ptr);
  if (decl_is_ptr) sz = 8;
  int fp_ptr = 0;
  int fp_array_mul = 1;
  if (parse_array_of_funcptr_abstract_decl(&t, &fp_array_mul)) {
    sz = 8 * fp_array_mul;
  }
  if (parse_array_of_ptr_to_array_abstract_decl(&t, &fp_array_mul)) {
    sz = 8 * fp_array_mul;
  }
  if (parse_array_of_ptr_to_array_of_ptr_abstract_decl(&t, &fp_array_mul)) {
    sz = 8 * fp_array_mul;
  }
  if (parse_ptr_to_func_returning_ptr_to_array_abstract_decl(&t, NULL, NULL, sz)) {
    sz = 8;
  }
  if (parse_array_of_ptr_to_func_returning_ptr_to_array_abstract_decl(&t, &fp_array_mul)) {
    sz = 8 * fp_array_mul;
  }
  if (parse_ptr_to_func_returning_ptr_to_func_abstract_decl(&t, NULL, NULL)) {
    sz = 8;
  }
  if (parse_ptr_to_func_returning_ptr_to_func_returning_ptr_to_array_abstract_decl(&t)) {
    sz = 8;
  }
  if (parse_funcptr_abstract_decl(&t, &fp_ptr, NULL)) {
    sz = 8;
  }
  if (parse_ptr_to_array_abstract_decl(&t, &fp_ptr, NULL, NULL, NULL, NULL, 0)) {
    sz = 8;
  }
  set_curtok(t);
  /* _Alignof では配列のアラインメント = 要素のアラインメントなので、要素数を掛けない。 */
  if (!alignof_mode) {
    apply_array_abstract_suffix_size(&sz);
  } else {
    /* 配列添字は消費だけして size には反映しない (要素アラインメントを保つ)。 */
    int dummy = 1;
    apply_array_abstract_suffix_size(&dummy);
  }
  tk_expect(')');
  return sz;
}

static int parse_parenthesized_type_size(int alignof_mode) {
  token_t *t = curtok();
  if (t->kind == TK_LPAREN && is_type_name_start_token(t->next)) {
    /* `sizeof((int) 1)` 等の cast 式は、`(` の直後が type-name でも閉じ `)` の後ろに
     * 式が続く。内側で sz を取得しても curtok が `)` でない場合は type-name 解釈は失敗で、
     * トークンを巻き戻して -1 を返し、呼び出し側 (parse_sizeof_operand 等) の式パース経路に
     * 任せる。これがないと `(int)` を type-name として消費し、残った `1)` で E2006 になる。 */
    token_t *save = curtok();
    set_curtok(t->next);
    int sz = parse_parenthesized_type_size(alignof_mode);
    if (sz < 0 || curtok()->kind != TK_RPAREN) {
      set_curtok(save);
      return -1;
    }
    tk_expect(')');
    return sz;
  }

  // Minimal support for C11 complex/imaginary spellings in sizeof/alignof:
  //   _Complex float, _Imaginary double, float _Complex, double _Imaginary
  if (t->kind == TK_COMPLEX || t->kind == TK_IMAGINARY) {
    t = t->next;
    int sz = 0;
    if (t->kind == TK_FLOAT) {
      sz = 4 * 2; // _Complex float = 8B
      t = t->next;
    } else if (t->kind == TK_DOUBLE) {
      sz = 8 * 2; // _Complex double = 16B
      t = t->next;
    } else if (t->kind == TK_LONG && t->next && t->next->kind == TK_DOUBLE) {
      sz = 8 * 2; // _Complex long double = 16B (lowering)
      t = t->next->next;
    } else {
      return -1;
    }
    return finish_parenthesized_type_size(t, sz, alignof_mode);
  }
  if ((t->kind == TK_FLOAT || t->kind == TK_DOUBLE) &&
      t->next && (t->next->kind == TK_COMPLEX || t->next->kind == TK_IMAGINARY)) {
    int base_sz = (t->kind == TK_FLOAT) ? 4 : 8;
    int sz = base_sz * 2; // _Complex: 基底型の2倍
    t = t->next->next;
    return finish_parenthesized_type_size(t, sz, alignof_mode);
  }
  if (t->kind == TK_LONG && t->next && t->next->kind == TK_DOUBLE &&
      t->next->next &&
      (t->next->next->kind == TK_COMPLEX || t->next->next->kind == TK_IMAGINARY)) {
    int sz = 8 * 2; // _Complex long double = 16B (lowering)
    t = t->next->next->next;
    return finish_parenthesized_type_size(t, sz, alignof_mode);
  }

  // long double: 2トークン型名
  if (t->kind == TK_LONG && t->next && t->next->kind == TK_DOUBLE) {
    t = t->next->next;
    int sz = 8; // macOS/AArch64: long double == double (64-bit)
    return finish_parenthesized_type_size(t, sz, alignof_mode);
  }
  /* 複数語の整数型 (`long long`, `unsigned long`, `unsigned int`,
   * `short int`, `signed char` 等)。単語ごとの psx_ctx_get_type_info では
   * 先頭 1 語しか消費できず `sizeof(long long)` が 2 語目の `long` で
   * E2006 になっていた。整数型指定子列をまとめて解釈してサイズを得る
   * (scalar はサイズ==アラインメントなので _Alignof でも同値)。 */
  {
    token_kind_t iks = TK_EOF;
    int iksz = 4;
    int iku = 0;
    token_t *inext = NULL;
    if (parse_integer_cast_spec_sequence(t, &iks, &iksz, &iku, &inext, NULL, NULL)) {
      return finish_parenthesized_type_size(inext, iksz, alignof_mode);
    }
  }
  bool is_type = false;
  int scalar_size = 8;
  token_kind_t type_kind = t->kind;
  psx_ctx_get_type_info(type_kind, &is_type, &scalar_size);
  if (is_type) {
    t = t->next;
    // Extension: treat sizeof(void) as 1 (GNU-compatible behavior).
    int sz = (type_kind == TK_VOID) ? 1 : scalar_size;
    return finish_parenthesized_type_size(t, sz, alignof_mode);
  }
  if (t->kind == TK_ENUM) {
    /* sizeof/_Alignof(enum E): enum は int 相当で 4 バイト。タグ名は任意。 */
    set_curtok(t->next);
    (void)tk_consume_ident();
    t = curtok();
    return finish_parenthesized_type_size(t, 4, alignof_mode);
  }
  if (psx_ctx_is_tag_aggregate_kind(t->kind)) {
    token_kind_t tag_kind = t->kind;
    set_curtok(t->next);
    token_ident_t *tag = tk_consume_ident();
    if (!tag) return -1;
    int sz = psx_ctx_get_tag_size(tag_kind, tag->str, tag->len);
    if (sz <= 0) {
      psx_diag_undefined_with_name((token_t *)tag, diag_text_for(DIAG_TEXT_TAG_TYPE), tag->str, tag->len);
    }
    /* _Alignof(struct/union T): サイズではなくアラインメントを返す。 */
    if (alignof_mode) {
      int al = psx_ctx_get_tag_align(tag_kind, tag->str, tag->len);
      if (al > 0) sz = al;
    }
    t = curtok();
    return finish_parenthesized_type_size(t, sz, alignof_mode);
  }
  if (psx_ctx_is_typedef_name_token(t)) {
    token_ident_t *id = (token_ident_t *)t;
    int td_elem = 8;
    int td_ptr = 0;
    int td_is_array = 0;
    int td_sizeof = 0;
    psx_typedef_info_t _ti;
    if (psx_ctx_find_typedef_name(id->str, id->len, &_ti)) {
      td_elem = _ti.elem_size;
      td_ptr = _ti.is_pointer;
      td_is_array = _ti.is_array;
    }
    t = t->next;
    int sz = td_ptr ? 8 : td_elem;
    /* pointer-element 配列 typedef (`typedef BinOp OpArr3[3]`) は is_pointer=1 だが
     * is_array=1 でもある。この場合 sizeof_size (= 8*N) を優先する。pure pointer typedef
     * (is_array=0) は引き続き 8 (= pointer サイズ) を返す。 */
    if ((!td_ptr || td_is_array) &&
        psx_ctx_find_typedef_sizeof(id->str, id->len, &td_sizeof)) {
      sz = td_sizeof;
    }
    if (sz <= 0 && !td_ptr && _ti.tag_kind != TK_EOF && _ti.tag_name) {
      int tag_sz = psx_ctx_get_tag_size(_ti.tag_kind, _ti.tag_name, _ti.tag_len);
      if (tag_sz > 0) sz = tag_sz;
    }
    return finish_parenthesized_type_size(t, sz, alignof_mode);
  }
  return -1;
}
static node_t *parse_call_postfix(node_t *callee, expr_parse_ctx_t *ctx);

void psx_expr_reset_translation_unit_state(void) {
  string_label_count = 0;
  float_label_count = 0;
  compound_lit_seq = 0;
}

// expr = assign ("," assign)*
node_t *psx_expr_expr(void) {
  expr_parse_ctx_t ctx = expr_parse_ctx_default();
  return expr_internal_ctx(&ctx);
}

// assign = conditional (("=" | "+=" | "-=" | "*=" | "/=" | "%=" | "<<=" | ">>=" | "&=" | "^=" | "|=") assign)?
node_t *psx_expr_assign(void) {
  expr_parse_ctx_t ctx = expr_parse_ctx_default();
  return assign_ctx(&ctx);
}

static node_t *expr_internal_ctx(expr_parse_ctx_t *ctx) {
  enter_expr_nest_or_die(ctx);
  node_t *node = assign_ctx(ctx);
  while (curtok()->kind == TK_COMMA) {
    set_curtok(curtok()->next);
    node_t *rhs = assign_ctx(ctx);
    node_t *comma = psx_node_new_binary(ND_COMMA, node, rhs);
    ps_node_materialize_type(comma);
    node = comma;
  }
  leave_expr_nest(ctx);
  return node;
}

static node_t *assign_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = conditional_ctx(ctx);
  node_t *lhs_prefix = NULL;
  node_t *assign_target = node;
  if (node && node->kind == ND_COMMA && node->rhs &&
      (node->rhs->kind == ND_LVAR || node->rhs->kind == ND_DEREF || node->rhs->kind == ND_GVAR)) {
    lhs_prefix = node->lhs;
    assign_target = node->rhs;
  }
  /* C11 6.5.16p2: 代入演算子の LHS は modifiable lvalue でなければならない。
   * 関数識別子 (ND_FUNCREF) はそうではない (`f = 5;` 等は非合法)。後段の IR builder で
   * "ir build/emit failed" になっていたのを、ここで分かりやすい診断にする。
   * 代入系トークン (`=`/`+=`/`-=`/...) が来ているときだけ check し、それ以外
   * (関数呼び出し `f(...)` や関数アドレス取得 `&f` 等) は素通し。 */
  switch (curtok()->kind) {
    case TK_ASSIGN: {
      token_t *assign_tok = curtok();
      set_curtok(curtok()->next);
      node_t *rhs = assign_ctx(ctx);
      node_t *assign_node = psx_node_new_assign(assign_target, rhs);
      assign_node->is_source_assignment = 1;
      assign_node->tok = assign_tok;
      node = (node_t *)assign_node;
      if (lhs_prefix) node = psx_node_new_binary(ND_COMMA, lhs_prefix, node);
      break;
    }
    case TK_PLUSEQ:
    case TK_MINUSEQ:
    case TK_MULEQ:
    case TK_DIVEQ:
    case TK_MODEQ:
    case TK_SHLEQ:
    case TK_SHREQ:
    case TK_ANDEQ:
    case TK_XOREQ:
    case TK_OREQ: {
      token_t *op_tok = curtok();
      set_curtok(curtok()->next);
      node_t *compound = psx_node_new_assign(assign_target, assign_ctx(ctx));
      compound->is_source_compound_assignment = 1;
      compound->source_op = op_tok->kind;
      compound->tok = op_tok;
      node = lhs_prefix
                 ? psx_node_new_binary(ND_COMMA, lhs_prefix, compound)
                 : compound;
      break;
    }
    default: break;
  }
  return node;
}

static node_t *conditional_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = logical_or_ctx(ctx);
  if (curtok()->kind == TK_QUESTION) {
    set_curtok(curtok()->next);
    node_ctrl_t *ternary = arena_alloc(sizeof(node_ctrl_t));
    ternary->base.kind = ND_TERNARY;
    ternary->base.lhs = node;
    ternary->base.rhs = expr_internal_ctx(ctx);
    tk_expect(':');
    ternary->els = conditional_ctx(ctx);
    ps_node_materialize_type((node_t *)ternary);
    return (node_t *)ternary;
  }
  return node;
}

static node_t *logical_or_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = logical_and_ctx(ctx);
  while (curtok()->kind == TK_OROR) {
    set_curtok(curtok()->next);
    node_t *rhs = logical_and_ctx(ctx);
    node = new_binary_with_source_op(ND_LOGOR, node, rhs, TK_OROR);
  }
  return node;
}

static node_t *logical_and_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = bit_or_ctx(ctx);
  while (curtok()->kind == TK_ANDAND) {
    set_curtok(curtok()->next);
    node_t *rhs = bit_or_ctx(ctx);
    node = new_binary_with_source_op(ND_LOGAND, node, rhs, TK_ANDAND);
  }
  return node;
}

static node_t *bit_or_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = bit_xor_ctx(ctx);
  while (curtok()->kind == TK_PIPE) {
    set_curtok(curtok()->next);
    node = psx_node_new_binary(ND_BITOR, node, bit_xor_ctx(ctx));
  }
  return node;
}

static node_t *bit_xor_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = bit_and_ctx(ctx);
  while (curtok()->kind == TK_CARET) {
    set_curtok(curtok()->next);
    node = psx_node_new_binary(ND_BITXOR, node, bit_and_ctx(ctx));
  }
  return node;
}

static node_t *bit_and_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = equality_ctx(ctx);
  while (curtok()->kind == TK_AMP) {
    set_curtok(curtok()->next);
    node = psx_node_new_binary(ND_BITAND, node, equality_ctx(ctx));
  }
  return node;
}

static node_t *equality_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = relational_ctx(ctx);
  for (;;) {
    if (curtok()->kind == TK_EQEQ) {
      set_curtok(curtok()->next);
      node_t *rhs = relational_ctx(ctx);
      node = new_binary_with_source_op(ND_EQ, node, rhs, TK_EQEQ);
    } else if (curtok()->kind == TK_NEQ) {
      set_curtok(curtok()->next);
      node_t *rhs = relational_ctx(ctx);
      node = new_binary_with_source_op(ND_NE, node, rhs, TK_NEQ);
    }
    else return node;
  }
}

static node_t *relational_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = shift_ctx(ctx);
  for (;;) {
    if (curtok()->kind == TK_LT) {
      set_curtok(curtok()->next);
      node_t *rhs = shift_ctx(ctx);
      node = new_binary_with_source_op(ND_LT, node, rhs, TK_LT);
    } else if (curtok()->kind == TK_LE) {
      set_curtok(curtok()->next);
      node_t *rhs = shift_ctx(ctx);
      node = new_binary_with_source_op(ND_LE, node, rhs, TK_LE);
    } else if (curtok()->kind == TK_GT) {
      set_curtok(curtok()->next);
      node_t *rhs = shift_ctx(ctx);
      node = new_binary_with_source_op(ND_LT, rhs, node, TK_GT);
    } else if (curtok()->kind == TK_GE) {
      set_curtok(curtok()->next);
      node_t *rhs = shift_ctx(ctx);
      node = new_binary_with_source_op(ND_LE, rhs, node, TK_GE);
    }
    else return node;
  }
}

static node_t *shift_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = add_ctx(ctx);
  for (;;) {
    if (curtok()->kind == TK_SHL) {
      set_curtok(curtok()->next);
      node_t *rhs = add_ctx(ctx);
      node = new_binary_with_source_op(ND_SHL, node, rhs, TK_SHL);
    } else if (curtok()->kind == TK_SHR) {
      set_curtok(curtok()->next);
      node_t *rhs = add_ctx(ctx);
      node = new_binary_with_source_op(ND_SHR, node, rhs, TK_SHR);
    }
    else return node;
  }
}

static node_t *add_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = mul_ctx(ctx);
  for (;;) {
    if (curtok()->kind == TK_PLUS) {
      set_curtok(curtok()->next);
      node_t *rhs = mul_ctx(ctx);
      node = new_binary_with_source_op(ND_ADD, node, rhs, TK_PLUS);
    } else if (curtok()->kind == TK_MINUS) {
      set_curtok(curtok()->next);
      node_t *rhs = mul_ctx(ctx);
      node = new_binary_with_source_op(ND_SUB, node, rhs, TK_MINUS);
    }
    else return node;
  }
}

static node_t *mul_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = cast_ctx(ctx);
  for (;;) {
    if (curtok()->kind == TK_MUL) {
      set_curtok(curtok()->next);
      node_t *rhs = cast_ctx(ctx);
      node = new_binary_with_source_op(ND_MUL, node, rhs, TK_MUL);
    } else if (curtok()->kind == TK_DIV) {
      set_curtok(curtok()->next);
      node_t *rhs = cast_ctx(ctx);
      node = new_binary_with_source_op(ND_DIV, node, rhs, TK_DIV);
    } else if (curtok()->kind == TK_MOD) {
      set_curtok(curtok()->next);
      node_t *rhs = cast_ctx(ctx);
      node = new_binary_with_source_op(ND_MOD, node, rhs, TK_MOD);
    }
    else return node;
  }
}

static node_t *cast_ctx(expr_parse_ctx_t *ctx) {
  return cast_with_compound_addr_context(0, ctx);
}

static node_t *cast_with_compound_addr_context(int compound_addr_context, expr_parse_ctx_t *ctx) {
  token_t *cast_tok = curtok();
  parsed_parenthesized_type_name_t parsed_type;
  if (parse_parenthesized_type_name(curtok(), &parsed_type)) {
    psx_type_name_t *cast = &parsed_type.name;
    if (parsed_type.after_rparen &&
        parsed_type.after_rparen->kind == TK_LBRACE) {
      // compound literal は primary/postfix 側で処理する
      return unary_with_compound_addr_context(compound_addr_context, ctx);
    }
    set_curtok(parsed_type.after_rparen);
    node_t *operand = cast_with_compound_addr_context(compound_addr_context, ctx);
    node_t *source_cast =
        psx_node_new_source_cast(operand, psx_type_name_build(cast));
    source_cast->tok = cast_tok;
    return apply_postfix(source_cast, ctx);
  }
  return unary_ctx(ctx);
}

static node_t *sizeof_vla_runtime_size_node(int slot_off) {
  node_t *lvar = psx_node_new_unsigned_lvar_typed(slot_off, 8);
  return psx_node_new_integer_cast_result(lvar, NULL, 8, 1, 0);
}

static node_t *append_comma_expr(node_t *prefix, node_t *expr) {
  if (!expr) return prefix;
  return prefix ? psx_node_new_binary(ND_COMMA, prefix, expr) : expr;
}

static node_t *parse_sizeof_vla_subscript_prefix(int sub_depth, expr_parse_ctx_t *ctx) {
  node_t *prefix = NULL;
  for (int i = 0; i < sub_depth; i++) {
    tk_expect('[');
    node_t *idx = expr_internal_ctx(ctx);
    tk_expect(']');
    prefix = append_comma_expr(prefix, idx);
  }
  tk_expect(')');
  return prefix;
}

// sizeof (expr) / sizeof (type) を処理する。`sizeof` トークンは呼び出し前に消費済み。
// VLA: sizeof(vla_var) は実行時バイトサイズ ([x29+16+offset+8]) をロード。
static node_t *parse_sizeof_operand(expr_parse_ctx_t *ctx) {
  if (curtok()->kind == TK_LPAREN) {
    set_curtok(curtok()->next);
    parsed_parenthesized_type_name_t parsed_type;
    if (curtok()->kind == TK_LPAREN &&
        parse_parenthesized_type_name(curtok(), &parsed_type) &&
        parsed_type.after_rparen &&
        parsed_type.after_rparen->kind == TK_LBRACE) {
      psx_type_name_t *type = &parsed_type.name;
      expr_parse_ctx_t child_ctx = expr_parse_ctx_unevaluated_child(ctx);
      node_t *node = parse_compound_literal_from_type(
          psx_type_name_build(type), parsed_type.after_rparen, 0,
          &child_ctx);
      tk_expect(')');
      return psx_node_new_num(sizeof_expr_node(node));
    }
    int type_sz = parse_parenthesized_type_size(0);
    if (type_sz >= 0) return psx_node_new_num(type_sz);
    if (curtok()->kind == TK_IDENT) {
      token_ident_t *id = (token_ident_t *)curtok();
      lvar_t *arr_var = psx_decl_find_lvar(id->str, id->len);
      /* `sizeof(vla)` は VLA 全体のランタイムサイズ (offset+8 のスロット) を返す。
       * ただし `sizeof(vla[0])` 等、ident の後に postfix (`[`/`.`/`->` など) が続く形は
       * 式として評価しなければならない。ident 直後が `)` のときだけ全体サイズ扱いにする
       * (非 VLA 配列分岐と同じく peek で確認。これがないと `sizeof(a[0])` が `a` を消費して
       * `)` を期待し E2006 になっていた)。 */
      if (arr_var && arr_var->is_vla && ps_lvar_pointer_qual_levels(arr_var) == 0 &&
          curtok()->next && curtok()->next->kind == TK_RPAREN) {
        set_curtok(curtok()->next);
        tk_expect(')');
        /* VLA メタ slot (offset+8 = total size) を 8B scalar として返す。find_owning_lvar
         * が arr_var (size=16 の VLA メタ) を所属判定すると variadic 引数経路で
         * cg_size_needs_indirect_struct(16) が真となり「struct 16B」扱いで 2 slot 渡しに
         * 化けて garbage が混じる。ND_CAST でラップして scalar 8B unsigned long として
         * 明示し、所属判定を回避する。 */
        return annotate_lvar_sizeof_usage_node(
            sizeof_vla_runtime_size_node(arr_var->offset + 8), arr_var);
      }
      /* `sizeof(vlaN[i][j]...)` は N-D VLA の中間サブ配列のランタイムサイズ (例 3D の k*elem、
       * 4D の k*l*elem など)。連続する `[...]` を D 段 peek して、`(D-1)*8 + 16` 番目のスロット
       * (vla_row_stride_frame_off + (D-1)*8) の値を返す。直後が `)` の形でなければ通常経路。
       * D=1 段は下の vla_row_stride 経路 (depth==1 では「= vla_row 自体」と等価)、最深まで
       * subscript したケース (要素 = elem 定数) は arr_var->vla_strides_remaining < D-1 で
       * 弾いてこの分岐に乗らないようにする。 */
      if (arr_var && arr_var->is_vla && arr_var->vla_strides_remaining > 0 &&
          curtok()->next && curtok()->next->kind == TK_LBRACKET) {
        int sub_depth = 0;
        token_t *t = curtok()->next;
        while (t && t->kind == TK_LBRACKET) {
          token_t *inner = t->next;
          int depth = 1;
          while (inner && depth > 0) {
            if (inner->kind == TK_LBRACKET) depth++;
            else if (inner->kind == TK_RBRACKET) { depth--; if (depth == 0) break; }
            inner = inner->next;
          }
          if (!inner || inner->kind != TK_RBRACKET) break;
          sub_depth++;
          t = inner->next;
        }
        /* sub_depth >= 2 かつ最終段以前 (要素レベルを越えない) なら vla_row+(D-1)*8 を返す。
         * D-1 <= vla_strides_remaining が成り立つときに該当 (D=1 は vla_row 経路、最終要素は
         * scalar elem)。t が `)` を指していれば「式の終わり」。 */
        if (sub_depth >= 2 && t && t->kind == TK_RPAREN &&
            (sub_depth - 1) <= arr_var->vla_strides_remaining) {
          set_curtok(curtok()->next);  /* consume ident */
          node_t *prefix = parse_sizeof_vla_subscript_prefix(sub_depth, ctx);
          int slot_off = arr_var->vla_row_stride_frame_off + 8 * (sub_depth - 1);
          node_t *size_node = annotate_lvar_sizeof_usage_node(
              sizeof_vla_runtime_size_node(slot_off), arr_var);
          return prefix ? psx_node_new_binary(ND_COMMA, prefix, size_node) : size_node;
        }
      }
      /* `sizeof(vla2d[i])` は行のランタイムサイズ (内側次元 * elem)。行ストライドは
       * 添字に依存しないので vla_row_stride_frame_off スロットの値が答え。`a[...]` の
       * 直後が `)` の形 (= 1 段添字) のときだけ。2 段 `a[i][j]` は要素なので除外。 */
      if (arr_var && arr_var->is_vla && arr_var->vla_row_stride_frame_off != 0 &&
          curtok()->next && curtok()->next->kind == TK_LBRACKET) {
        token_t *t = curtok()->next->next;  /* `[` の次 */
        int depth = 1;
        while (t && depth > 0) {
          if (t->kind == TK_LBRACKET) depth++;
          else if (t->kind == TK_RBRACKET) { depth--; if (depth == 0) break; }
          t = t->next;
        }
        if (t && t->kind == TK_RBRACKET && t->next && t->next->kind == TK_RPAREN) {
          set_curtok(curtok()->next);  /* consume ident */
          node_t *prefix = parse_sizeof_vla_subscript_prefix(1, ctx);
          /* 2D VLA の行サイズも同様に ND_CAST でラップして所属判定を回避し、scalar 8B
           * unsigned long として variadic 経路に乗せる。 */
          node_t *size_node = annotate_lvar_sizeof_usage_node(
              sizeof_vla_runtime_size_node(arr_var->vla_row_stride_frame_off), arr_var);
          return prefix ? psx_node_new_binary(ND_COMMA, prefix, size_node) : size_node;
        }
      }
      /* sizeof(arr) where arr is a non-VLA array: C 仕様で array → pointer
       * の decay は起きないので、配列の合計サイズ (var->size) を返す。
       * 通常の式解析では `arr` が ND_ADDR(int) (= 4 バイト) になってしまうので
       * ここで先回りする。 */
      if (arr_var && ps_lvar_is_array(arr_var)) {
        token_t *peek = curtok()->next;
        if (peek && peek->kind == TK_RPAREN) {
          int array_size = ps_lvar_decl_sizeof(arr_var, 0);
          if (array_size > 0) {
            set_curtok(peek->next);
            return annotate_lvar_sizeof_usage_node(psx_node_new_num(array_size), arr_var);
          }
        }
      }
      /* static local 配列はグローバルへ lowering され alias lvar は is_array=0 /
       * size=0 なので上の分岐に乗らない。実サイズは lowering 先グローバルの
       * type_size にある (`static int a[10]` → 40)。 */
      if (arr_var && lvar_is_static_local_array(arr_var)) {
        token_t *peek = curtok()->next;
        if (peek && peek->kind == TK_RPAREN) {
          global_var_t *sgv = ps_find_global_var(arr_var->static_global_name,
                                                  arr_var->static_global_name_len);
          int static_array_size = ps_gvar_decl_sizeof(sgv, 0);
          if (static_array_size > 0) {
            set_curtok(peek->next);
            return annotate_lvar_sizeof_usage_node(psx_node_new_num(static_array_size), arr_var);
          }
        }
      }
      /* ローカル lvar が見つからなければ global 配列を探す。`int g[] = {...}`
       * のような要素数推定後の type_size (apply_toplevel_object_initializer で
       * 確定済み) を全体サイズとして返す。 */
      if (!arr_var) {
        for (global_var_t *gv = ps_find_global_var(id->str, id->len); gv; gv = NULL) {
          if (gv->name_len != id->len ||
              memcmp(gv->name, id->str, (size_t)id->len) != 0) continue;
          int global_array_size = ps_gvar_decl_sizeof(gv, 0);
          if (ps_gvar_is_array(gv) && global_array_size > 0) {
            token_t *peek = curtok()->next;
            if (peek && peek->kind == TK_RPAREN) {
              set_curtok(peek->next);
              return psx_node_new_num(global_array_size);
            }
          }
          break;
        }
      }
      if (0) {  /* keep brace structure (revisit if VLA path is restructured) */
        return psx_node_new_lvar_typed(arr_var->offset + 8, 8);
      }
    }
	    expr_parse_ctx_t child_ctx = expr_parse_ctx_unevaluated_child(ctx);
	    node_t *node = expr_internal_ctx(&child_ctx);
	    tk_expect(')');
	    return psx_node_new_num(sizeof_expr_node(node));
	  }
	  expr_parse_ctx_t child_ctx = expr_parse_ctx_unevaluated_child(ctx);
	  node_t *node = unary_ctx(&child_ctx);
	  return psx_node_new_num(sizeof_expr_node(node));
	}

static node_t *build_pre_inc_dec_node(node_kind_t kind, const char *op, expr_parse_ctx_t *ctx) {
  node_t *target = unary_ctx(ctx);
  psx_node_expect_incdec_target(target, op);
  node_t *node = arena_alloc(sizeof(node_t));
  node->kind = kind;
  node->lhs = target;
  return node;
}

// `*operand` を表す ND_DEREF ノードを構築する。tag/pointer-qual の伝播も行う。
static node_t *build_unary_deref_node(node_t *operand) {
  /* C11 6.5.3.2p2: 単項 `*` のオペランドはポインタ型でなければならない。
   * 診断は parser 側に残し、ND_DEREF の型 metadata 初期化は node_utils に集約する。 */
  if (operand && (operand->kind == ND_LVAR || operand->kind == ND_GVAR ||
                  operand->kind == ND_NUM)) {
    int looks_ptr = ps_node_is_pointer(operand) ||
                    ps_node_pointer_qual_levels(operand) > 0;
    int ts = ps_node_type_size(operand);
    if (!looks_ptr && ts > 0 && ts < 8) {
      psx_diag_ctx(curtok(), "deref",
                   "deref のオペランドはポインタ型でなければなりません (C11 6.5.3.2p2)");
    }
    if (psx_node_pointee_is_void(operand)) {
      psx_diag_ctx(curtok(), "deref",
                   "void* の deref はできません — キャストが必要です (C11 6.5.3.2)");
    }
  } else if (psx_node_pointee_is_void(operand)) {
    psx_diag_ctx(curtok(), "deref",
                 "void* の deref はできません — キャストが必要です (C11 6.5.3.2)");
  }
  return psx_node_new_unary_deref_for(operand);
}

// `&operand`。コンマ式 (a, b) に対する `&(a, b)` は a を評価した上で &b を返す形に組み立てる。
static node_t *build_unary_addr_node(node_t *operand) {
  /* C11 6.5.3.2p1: bitfield のアドレスは取得できない。
   * `s.f` (bitfield) は ND_DEREF にラップされて bit_width が立っているので
   * ここで弾く。 */
  if (operand && operand->kind == ND_DEREF) {
    if (psx_node_bitfield_width(operand) > 0) {
      psx_diag_ctx(curtok(), "addr",
                   "ビットフィールドのアドレスは取得できません (C11 6.5.3.2p1)");
    }
  }
  if (operand && operand->kind == ND_COMMA && operand->rhs) {
    /* `&(compound-literal)` 等、値が COMMA(init, ref) の形。rhs に同じ単項 & の
     * ロジックを再帰適用する (直接 ND_ADDR で包むと配列複合リテラルの rhs が
     * 既に ND_ADDR (decay 済み) のとき二重に ND_ADDR でラップされ ir_build が
     * 失敗する)。下の ND_ADDR/ND_FUNCREF 簡約をここでも効かせる。 */
    return psx_node_new_binary(ND_COMMA, operand->lhs, build_unary_addr_node(operand->rhs));
  }
  // C 仕様: 配列名 `arr` は (sizeof/&/レジスタ変数を除く) 文脈ではポインタ崩壊する。
  // `&arr` ではアドレス値はそのまま (型だけ `int(*)[N]` に変わる)。
  // ag_c では配列ローカル変数の参照は build_array_lvar_addr_node により
  // ND_ADDR(ND_LVAR) として表現されているため、`&arr` でさらに ND_ADDR でラップすると
  // codegen の `gen_lval` が ND_ADDR を受理せず E4002 になる。
  // 既に ND_ADDR で表現されているアドレス値はそのまま返す。
  if (operand && operand->kind == ND_ADDR) {
    /* `&arr` : 配列は既に decay 済みの ND_ADDR で表現されアドレス値は同じ。ただし
     * 結果型は `int(*)[N]` (ポインタ, 8B) なので、type_size=8 のコピーを返して
     * sizeof(&arr) が要素サイズでなく 8 を返すようにする (共有ノードは変更しない)。 */
    return psx_node_new_explicit_addr_value_for(operand);
  }
  /* `&f` (f は関数): 関数のアドレスは関数ポインタそのもの (= `f`)。ND_FUNCREF を
   * そのまま返す (ND_ADDR でラップすると IR builder が扱えず失敗する)。 */
  if (operand && operand->kind == ND_FUNCREF) {
    return operand;
  }
  return psx_node_new_unary_addr_for(operand);
}

static node_t *unary_ctx(expr_parse_ctx_t *ctx) {
  return unary_with_compound_addr_context(0, ctx);
}

static node_t *unary_with_compound_addr_context(int compound_addr_context, expr_parse_ctx_t *ctx) {
  token_kind_t k = curtok()->kind;
  if (k == TK_SIZEOF)  { set_curtok(curtok()->next); return parse_sizeof_operand(ctx); }
  /* GNU 拡張 __real__ / __imag__: 複素数の実部/虚部を取り出す単項演算子
   * (実数オペランドでは __real__ x = x, __imag__ x = 0)。キーワードではなく
   * 特殊識別子として扱う (__func__ と同様)。creal/cimag を rvalue にも効かせる。 */
  if (k == TK_IDENT) {
    token_ident_t *kid = (token_ident_t *)curtok();
    if (kid->len == 8 && (memcmp(kid->str, "__real__", 8) == 0 ||
                          memcmp(kid->str, "__imag__", 8) == 0)) {
      int is_real = (kid->str[2] == 'r');
      set_curtok(curtok()->next);
      node_t *operand = cast_ctx(ctx);
      node_t *n = arena_alloc(sizeof(node_t));
      n->kind = is_real ? ND_CREAL : ND_CIMAG;
      n->lhs = operand;
      tk_float_kind_t operand_fp = expr_node_fp_kind(operand);
      n->fp_kind = operand_fp != TK_FLOAT_KIND_NONE ? operand_fp : TK_FLOAT_KIND_DOUBLE;
      n->type = psx_type_new_float(
          (tk_float_kind_t)n->fp_kind,
          n->fp_kind == TK_FLOAT_KIND_FLOAT ? 4 : 8);
      return n;
    }
  }
  if (k == TK_ALIGNOF) {
    set_curtok(curtok()->next);
    tk_expect('(');
    int type_sz = parse_parenthesized_type_size(1);
    if (type_sz < 0) {
      psx_diag_ctx(curtok(), "alignof", "%s",
                   diag_message_for(DIAG_ERR_PARSER_ALIGNOF_TYPE_NAME_REQUIRED));
    }
    return psx_node_new_num(type_sz);
  }
  if (k == TK_INC) { set_curtok(curtok()->next); return build_pre_inc_dec_node(ND_PRE_INC, "++", ctx); }
  if (k == TK_DEC) { set_curtok(curtok()->next); return build_pre_inc_dec_node(ND_PRE_DEC, "--", ctx); }
  if (k == TK_PLUS)  { set_curtok(curtok()->next); return cast_ctx(ctx); }
  if (k == TK_MINUS) {
    set_curtok(curtok()->next);
    node_t *operand = cast_ctx(ctx);
    /* 浮動小数の単項マイナスは符号ビット反転 (ND_FNEG → IR_FNEG)。`0.0 - x` だと
     * x が +0.0 のとき結果が +0.0 になり (IEEE)、本来の -0.0 (符号反転) と異なる。
     * `-0.0` 定数 / `1.0/-0.0` = -inf / signbit 等のため正しく -0.0 を生成する。
     * 整数は従来どおり `0 - x`。 */
    tk_float_kind_t operand_fp = expr_node_fp_kind(operand);
    if (operand_fp != TK_FLOAT_KIND_NONE) {
      node_t *neg = arena_alloc(sizeof(node_t));
      neg->kind = ND_FNEG;
      neg->lhs = operand;
      neg->fp_kind = operand_fp;
      neg->type = psx_type_new_float(
          operand_fp, operand_fp == TK_FLOAT_KIND_FLOAT ? 4 : 8);
      return neg;
    }
    return psx_node_new_binary(ND_SUB, psx_node_new_num(0), operand);
  }
  if (k == TK_BANG)  {
    set_curtok(curtok()->next);
    node_t *eq = psx_node_new_binary(ND_EQ, cast_ctx(ctx), psx_node_new_num(0));
    eq->from_logical_not = 1;  /* `!p == 0` の precedence-trap 警告に使う */
    return eq;
  }
  if (k == TK_TILDE) {
    set_curtok(curtok()->next);
    node_t *neg = psx_node_new_binary(ND_SUB, psx_node_new_num(0), cast_ctx(ctx));
    return psx_node_new_binary(ND_SUB, neg, psx_node_new_num(1));
  }
  if (k == TK_MUL) { set_curtok(curtok()->next); return build_unary_deref_node(cast_ctx(ctx)); }
  if (k == TK_AMP) {
    set_curtok(curtok()->next);
    /* `&(int){5}`: ファイルスコープのスカラ複合リテラルを静的 gvar として
     * 実体化させ、アドレスを取れるようにする。 */
    node_t *operand = cast_with_compound_addr_context(1, ctx);
    return build_unary_addr_node(operand);
  }
  return apply_postfix(primary_with_compound_addr_context(compound_addr_context, ctx), ctx);
}

// 配列添字 `[idx]` 用のスケール倍率と次段の要素サイズ (inner_ds) を計算する。
// 多次元 VLA では実行時ストライドをフレームから読む経路がある。
static node_t *make_subscript_scaled_offset(node_t *node, node_t *idx,
                                            int *out_es, int *out_inner_ds,
                                            int *out_next_ds,
                                            int *out_extras, int *out_extras_count) {
  int ds = ps_node_deref_size(node);
  int ts = ps_node_type_size(node);
  int es = ds ? ds : (ts ? ts : 8);
  if (node->kind == ND_DEREF &&
      ps_node_pointer_qual_levels(node) == 1 &&
      psx_node_base_deref_size(node) > 0) {
    token_kind_t tag_kind = TK_EOF;
    ps_node_get_tag_type(node, &tag_kind, NULL, NULL, NULL);
    if (ds == 0 &&
        !(ps_node_is_pointer(node) && !psx_node_scalar_ptr_member_lvalue(node) &&
          node->lhs && node->lhs->kind == ND_ADD &&
          psx_ctx_is_tag_aggregate_kind(tag_kind))) {
      es = psx_node_base_deref_size(node);
    }
  }
  int vla_rsf = 0;  // 実行時行ストライドのフレームオフセット (0=なし)
  int inner_ds = 0; // 次の次元の要素サイズ (0=スカラ)
  int next_ds = 0;  // さらに次の次元の要素サイズ (3D 用、0=なし)
  int extras[5] = {0};
  int extras_count = 0;
  vla_rsf = ps_node_vla_row_stride_frame_off(node);
  psx_node_pointer_stride_metadata(node, &inner_ds, &next_ds, extras, &extras_count);
  psx_type_t *node_type = ps_node_get_type(node);
  int stride_from_canonical_type = 0;
  if (node_type && node_type->kind == PSX_TYPE_POINTER &&
      node_type->base && node_type->base->kind == PSX_TYPE_POINTER &&
      node_type->base->base && node_type->base->base->kind == PSX_TYPE_ARRAY) {
    es = 8;
    inner_ds = 0;
    next_ds = 0;
    extras_count = 0;
    stride_from_canonical_type = 1;
  }
  if (node_type && node_type->kind == PSX_TYPE_POINTER &&
      node_type->base && node_type->base->kind == PSX_TYPE_ARRAY) {
    int array_size = ps_type_sizeof(node_type->base);
    int elem_stride = ps_type_deref_size(node_type->base);
    if (array_size > 0) es = array_size;
    inner_ds = elem_stride > 0 ? elem_stride : 0;
    next_ds = node_type->base->outer_stride;
    extras_count = node_type->base->extra_strides_count;
    for (int i = 0; i < extras_count && i < 5; i++) {
      extras[i] = node_type->base->extra_strides[i];
    }
    stride_from_canonical_type = 1;
  }
  if (node_type && node_type->kind == PSX_TYPE_ARRAY &&
      node_type->base && node_type->base->kind == PSX_TYPE_POINTER) {
    int pointer_elem_size = ps_type_sizeof(node_type->base);
    if (pointer_elem_size > 0) es = pointer_elem_size;
    inner_ds = 0;
    next_ds = 0;
    extras_count = 0;
    stride_from_canonical_type = 1;
  }
  int ptr_array_bytes = psx_node_ptr_array_pointee_bytes(node);
  if (!stride_from_canonical_type && ptr_array_bytes > 0 && node->kind == ND_DEREF) {
    if (node_type && node_type->kind == PSX_TYPE_ARRAY && node_type->base &&
        psx_type_is_pointer(node_type->base)) {
      int pointer_elem_size = ps_type_sizeof(node_type->base);
      if (pointer_elem_size > 0) es = pointer_elem_size;
    }
  }
  if (!stride_from_canonical_type && ptr_array_bytes > 0 && node->kind != ND_DEREF) {
    int bds = psx_node_base_deref_size(node);
    int has_outer_row_stride = (ds > 8 && inner_ds > 0);
    if (!has_outer_row_stride) {
      es = (ds == 8 && ptr_array_bytes > ds) ? ds : ptr_array_bytes;
      if (bds > 0 && inner_ds == ptr_array_bytes) inner_ds = bds;
    }
  }
  if (!stride_from_canonical_type && node->kind == ND_DEREF &&
      ps_node_pointer_qual_levels(node) == 0 &&
      inner_ds <= 0 && vla_rsf == 0) {
    int bds = psx_node_base_deref_size(node);
    if (bds == 0 && node->lhs) bds = psx_node_base_deref_size(node->lhs);
    int pointer_array_element_row =
        ptr_array_bytes > 0 && ds > bds;
    if (bds > 0 && ts > bds && !pointer_array_element_row) es = bds;
  }
  node_t *scaled;
  if (vla_rsf) {
    // 実行時ストライド: フレームスロットから行ストライドをロード
    node_t *stride_node = psx_node_new_lvar_typed(vla_rsf, 8);
    scaled = psx_node_new_binary(ND_MUL, idx, stride_node);
    es = inner_ds ? inner_ds : 1; // type_size設定用 (実際は次段でderef_size経由で使用)
  } else {
    scaled = psx_node_new_binary(ND_MUL, idx, psx_node_new_num(es));
  }
  *out_es = es;
  *out_inner_ds = inner_ds;
  if (out_next_ds) *out_next_ds = next_ds;
  if (out_extras && out_extras_count) {
    *out_extras_count = extras_count;
    for (int i = 0; i < extras_count && i < 5; i++) out_extras[i] = extras[i];
  }
  return scaled;
}

// 添字対象が ND_DEREF のとき、配列・メンバ配列・多次元 VLA では
// 「base + offset」 (lhs) を使う方が効率的かつ正しい。
static node_t *subscript_base_address_of(node_t *node) {
  if (node->kind != ND_DEREF) return node;
  if (psx_node_subscript_deref_uses_base_address(node)) return node->lhs;
  /* 3D VLA `int t[n][m][k]` の最初の subscript結果 t[i] は deref_size=0 (次 stride は
   * runtime, vla_row_stride_frame_off=mid_slot 経由) だが、配列の中間「2D サブ配列」を
   * 表すため subscript chain では address (lhs) を返す。これがないと t[i][j] が ND_DEREF
   * を 1 バイト整数として load してしまい SIGSEGV。 */
  /* スカラポインタメンバ (`struct S { char *name; }; s.name[0]`) を subscript
   * する場合、base は「ポインタ値の load」(= ND_DEREF をそのまま使う) でなければ
   * いけない。配列メンバとは違って ND_ADD (= メンバスロットのアドレス) を base に
   * 使うと、ポインタ値ではなくスロット自身のアドレスから byte を読んでしまう。
   * 配列メンバの decay 表現とは is_scalar_ptr_member で区別する。 */
  if (psx_node_scalar_ptr_member_lvalue(node)) return node;
  if (node->lhs && node->lhs->kind == ND_ADD &&
      node->lhs->rhs && node->lhs->rhs->kind == ND_NUM) {
    // Member lvalue (`s.m`) is represented as `*(base + off)`.
    return node->lhs;
  }
  return node;
}

// `base[idx]` を表す ND_DEREF ノードを構築する。tag / 多段ポインタ伝播を行う。
static node_t *build_subscript_deref(node_t *node, node_t *idx) {
  /* C11 6.5.2.1p1: `a[b]` は `*(a + b)` の構文糖。
   * a または b のどちらかがポインタ (または配列) でなければならない。
   * ND_DEREF (メンバアクセスや多次元 subscript の中間結果) は flex 配列等
   * メタ情報が落ちる場合があるので、kind 単独で許容する (誤検出を避ける)。
   * 明確に「スカラ int を subscript している」ケース (両辺とも非ポインタの
   * 単純 LVAR/GVAR/NUM) のみ弾く。 */
  int node_ok = ps_node_is_pointer(node) || node->kind == ND_DEREF ||
                node->kind == ND_ADDR;
  int idx_ok = ps_node_is_pointer(idx) || idx->kind == ND_DEREF ||
               idx->kind == ND_ADDR;
  if (!node_ok && !idx_ok) {
    psx_diag_ctx(curtok(), "subscript",
                 "サブスクリプトの両辺ともポインタ/配列ではありません (C11 6.5.2.1p1)");
  }
  /* C11 6.5.2.1p1: `a[b]` ≡ `b[a]`。左がポインタ/配列でないが右がそうなら入れ替えて
   * 「左 = base, 右 = index」前提の以降のロジックに乗せる。
   * 例: `3[arr]` → `arr[3]` 相当に正規化。 */
  if (!node_ok && idx_ok) {
    node_t *tmp = node; node = idx; idx = tmp;
    int tmp_ok = node_ok; node_ok = idx_ok; idx_ok = tmp_ok;
  }
  int es = 0, inner_ds = 0, next_ds = 0;
  int extras[5] = {0};
  int extras_count = 0;
  node_t *scaled = make_subscript_scaled_offset(node, idx, &es, &inner_ds, &next_ds,
                                                extras, &extras_count);
  node_t *base_addr = subscript_base_address_of(node);
  return psx_node_new_subscript_deref_for(node, base_addr, scaled, es, inner_ds, next_ds,
                                           extras, extras_count);
}

static node_t *build_post_inc_dec_node(node_kind_t kind, node_t *operand, const char *op) {
  psx_node_expect_incdec_target(operand, op);
  node_t *n = arena_alloc(sizeof(node_t));
  n->kind = kind;
  n->lhs = operand;
  return n;
}

static bool is_postfix_op_token(token_kind_t k) {
  return k == TK_LBRACKET || k == TK_LPAREN || k == TK_DOT ||
         k == TK_ARROW || k == TK_INC || k == TK_DEC;
}

static node_t *apply_postfix(node_t *node, expr_parse_ctx_t *ctx) {
  // 後置演算がコンマ式の rhs 側に適用される: `(a, b)++` ⇒ `(a, b++)`。
  if (node && node->kind == ND_COMMA && is_postfix_op_token(curtok()->kind)) {
    node->rhs = apply_postfix(node->rhs, ctx);
    return node;
  }
  for (;;) {
    token_kind_t k = curtok()->kind;
    if (k == TK_LBRACKET) {
      set_curtok(curtok()->next);
      node_t *idx = expr_internal_ctx(ctx);
      tk_expect(']');
      node = build_subscript_deref(node, idx);
      continue;
    }
    if (k == TK_LPAREN) {
      node = parse_call_postfix(node, ctx);
      continue;
    }
    if (k == TK_DOT || k == TK_ARROW) {
      token_t *op_tok = curtok();
      set_curtok(curtok()->next);
      node = build_member_access(node, k == TK_ARROW ? 1 : 0, op_tok);
      continue;
    }
    if (k == TK_INC) {
      set_curtok(curtok()->next);
      node = build_post_inc_dec_node(ND_POST_INC, node, "++");
      continue;
    }
    if (k == TK_DEC) {
      set_curtok(curtok()->next);
      node = build_post_inc_dec_node(ND_POST_DEC, node, "--");
      continue;
    }
    return node;
  }
}

static int expr_funcall_returns_decayable_funcptr(node_t *fcall) {
  if (!fcall || fcall->kind != ND_FUNCALL) return 0;
  psx_decl_funcptr_sig_t sig = ps_node_funcptr_sig(fcall);
  int callable_sig = ps_decl_funcptr_sig_has_payload(sig);
  return callable_sig && ps_node_pointer_qual_levels(fcall) <= 1;
}

static int expr_node_is_decayable_funcptr_value(node_t *node) {
  if (!node) return 0;
  if (node->kind != ND_DEREF && node->kind != ND_FUNCALL && node->kind != ND_CAST) return 0;
  if (ps_node_pointer_qual_levels(node) > 1) return 0;
  return ps_node_has_funcptr_signature(node);
}

static node_t *parse_call_postfix(node_t *callee, expr_parse_ctx_t *ctx) {
  tk_expect('(');
  node_func_t *node = arena_alloc(sizeof(node_func_t));
  node->base.kind = ND_FUNCALL;
  /* `(*fp)(args)` / `(**fp)(args)` / `(*(T)fp)(args)`:
   * function pointer value に対する単項 `*` は関数へ戻って即座に function pointer
   * へ減衰するだけなので、呼び出し callee から剥がす。
   * 一方 `int (**getpp(void))(int); (*getpp())(args)` の `*` は function pointer
   * object をロードする実体 deref なので、operand がまだ 2 段以上なら残す。 */
  while (callee && callee->kind == ND_DEREF) {
    node_t *lhs = callee->lhs;
    if (expr_node_is_decayable_funcptr_value(lhs)) {
      callee = lhs;
      continue;
    } else if (lhs && lhs->kind == ND_FUNCALL && expr_funcall_returns_decayable_funcptr(lhs)) {
      callee = lhs;
      continue;
    } else {
      node_t *base = callee;
      while (base && base->kind == ND_DEREF) base = base->lhs;
      if (base && (base->kind == ND_LVAR || base->kind == ND_GVAR) &&
          ps_node_pointer_qual_levels(base) <= 1) {
        callee = base;
      }
      break;
    }
  }
  /* callee が bare 関数参照 (ND_FUNCREF) のとき — 典型的には `_Generic(...)(args)` が
   * 関数を選んだ場合や `(funcname)(args)` — は直接呼び出しに変換する。funcname 経由なら
   * プロトタイプから戻り型/引数の fp ABI を引けるので、tgmath の `sqrt(2.0)` 等が double を
   * 正しく d0 で渡し受けできる (bare funcref の間接呼び出しはシグネチャを持たず整数扱いで
   * 値が化けていた)。 */
  if (callee && callee->kind == ND_FUNCREF) {
    node_funcref_t *fr = (node_funcref_t *)callee;
    node->funcname = fr->funcname;
    node->funcname_len = fr->funcname_len;
    node->callee = NULL;
    callee = NULL;
  } else {
    node->callee = callee;
  }
  /* 間接呼び出しで callee の function-pointer signature (= 関数戻り型の fp_kind) を
   * funcall ノードに載せる。これがないと ir_builder が戻り値型を整数
   * (I32) と判定し戻り値を x0 で読んでいた (FP 戻り値は d0 に返るため化けていた)。
   * 以前は `pointee_fp_kind` を流用していたため、データポインタ pointee と function
   * pointer return FP の正本が混ざっていた。 */
  if (callee) {
    tk_float_kind_t ret_fp = ps_node_funcptr_ret_fp_kind(callee);
    if (ret_fp == TK_FLOAT_KIND_NONE) {
      ret_fp = psx_node_pointee_fp_kind(callee);
    }
    if (ret_fp != TK_FLOAT_KIND_NONE &&
        !ps_node_funcptr_returns_pointee_array(callee)) {
      node->base.fp_kind = ret_fp;
    }
    if (ps_node_funcptr_returns_complex(callee)) {
      node->base.is_complex = 1;
    }
  }
  int nargs = 0;
  int arg_cap = 16;
  node->args = calloc(arg_cap, sizeof(node_t *));
  if (curtok()->kind == TK_RPAREN) {
    set_curtok(curtok()->next);
  } else {
    node->args[nargs++] = assign_ctx(ctx);
    while (curtok()->kind == TK_COMMA) {
      set_curtok(curtok()->next);
      if (nargs >= arg_cap) {
        arg_cap = pda_next_cap(arg_cap, nargs + 1);
        node->args = pda_xreallocarray(node->args, (size_t)arg_cap, sizeof(node_t *));
      }
      node->args[nargs++] = assign_ctx(ctx);
    }
    tk_expect(')');
  }
  node->nargs = nargs;
  ps_node_materialize_type((node_t *)node);
  return (node_t *)node;
}

// TK_LPAREN を見たときの compound literal `(T){...}` 試行。
// パースできたら結果ノードを返し、できなければ NULL（呼び出し側は通常の式へ）。
static node_t *try_parse_compound_literal(int compound_addr_context, expr_parse_ctx_t *ctx) {
  parsed_parenthesized_type_name_t parsed_type;
  if (curtok()->kind == TK_LPAREN &&
      parse_parenthesized_type_name(curtok(), &parsed_type) &&
      parsed_type.after_rparen &&
      parsed_type.after_rparen->kind == TK_LBRACE) {
    psx_type_name_t *type = &parsed_type.name;
    return parse_compound_literal_from_type(
        psx_type_name_build(type), parsed_type.after_rparen,
        compound_addr_context, ctx);
  }
  return NULL;
}

// _Generic( ctrl, T1: e1, T2: e2, ..., default: ed ) を評価して選択された式ノードを返す。
static node_t *parse_generic_selection(expr_parse_ctx_t *ctx) {
  set_curtok(curtok()->next); // skip TK_GENERIC
  tk_expect('(');
  /* Complex derived cast type-names still need their serialized declarator
   * shape until cast and association parsing share one canonical type-name
   * parser. Ordinary controls use the canonical node type directly. */
  psx_type_t *control_type = NULL;
  int got_cast_ty = 0;
  if (curtok()->kind == TK_LPAREN) {
    token_t *save = curtok();
    /* トークンストリーム経路: 巻き戻し先 (save) より古いトークンを解放させない。
     * これがパーサ内で唯一のバックトラックで、式内に収まる。非ストリーム経路では no-op。 */
    tk_allocator_recyc_pin(save);
    set_curtok(curtok()->next); // skip '('
    psx_type_t *cast_control_type = NULL;
    if (parse_generic_assoc_type(&cast_control_type) && curtok()->kind == TK_RPAREN &&
        curtok()->next && curtok()->next->kind != TK_LBRACE &&
        cast_control_type && cast_control_type->type_sig != NULL) {
      set_curtok(curtok()->next); // skip ')'
      cast_ctx(ctx);                     // 制御式は未評価 (C11 6.5.1.1) なので operand の値は捨てる
      if (curtok()->kind == TK_COMMA) {
        control_type = cast_control_type;
        got_cast_ty = 1;
      }
    }
    if (!got_cast_ty && save->next && save->next->kind == TK_LPAREN) {
      set_curtok(save->next->next); // skip outer '(' and inner '('
      cast_control_type = NULL;
      if (parse_generic_assoc_type(&cast_control_type) && curtok()->kind == TK_RPAREN &&
          curtok()->next && curtok()->next->kind != TK_LBRACE &&
          cast_control_type && cast_control_type->type_sig != NULL) {
        set_curtok(curtok()->next); // skip inner ')'
        cast_ctx(ctx);
        if (curtok()->kind == TK_RPAREN && curtok()->next &&
            curtok()->next->kind == TK_COMMA) {
          set_curtok(curtok()->next); // skip outer ')'
          control_type = cast_control_type;
          got_cast_ty = 1;
        }
      }
    }
    if (!got_cast_ty) set_curtok(save); // 純粋なキャストでなければ巻き戻して通常解析
    tk_allocator_recyc_unpin();
  }
  if (!got_cast_ty) {
    node_t *control = assign_ctx(ctx);
    control_type = generic_control_type(control);
  }
  tk_expect(',');

  node_t *selected = NULL;
  node_t *default_expr = NULL;
  int matched = 0;
  for (;;) {
    if (curtok()->kind == TK_DEFAULT) {
      set_curtok(curtok()->next);
      tk_expect(':');
      node_t *expr_node = assign_ctx(ctx);
      if (!default_expr) default_expr = expr_node;
    } else {
      psx_type_t *assoc_type = NULL;
      if (!parse_generic_assoc_type(&assoc_type)) {
        psx_diag_ctx(curtok(), "generic", "%s",
                     diag_message_for(DIAG_ERR_PARSER_GENERIC_ASSOC_TYPE_INVALID));
      }
      tk_expect(':');
      node_t *expr_node = assign_ctx(ctx);
      if (!matched && psx_type_generic_matches(control_type, assoc_type)) {
        selected = expr_node;
        matched = 1;
      }
    }
    if (!tk_consume(',')) break;
  }
  tk_expect(')');
  if (!selected) selected = default_expr;
  if (!selected) {
    psx_diag_ctx(curtok(), "generic", "%s",
                 diag_message_for(DIAG_ERR_PARSER_GENERIC_NO_MATCH));
  }
  return selected;
}

// TK_NUM を node_num_t に変換。浮動小数点ならリテラルテーブルにも登録。
static node_t *parse_num_literal(void) {
  token_t *tok = curtok();
  token_num_t *num = (token_num_t *)tok;
  node_num_t *node = arena_alloc(sizeof(node_num_t));
  node->base.kind = ND_NUM;
  if (num->num_kind == TK_NUM_KIND_INT) {
    node->base.fp_kind = TK_FLOAT_KIND_NONE;
    node->float_suffix_kind = TK_FLOAT_SUFFIX_NONE;
    node->val = tk_as_num_int(tok)->val;
    /* long / long long サフィックス付き整数リテラルは値が 32bit に収まっても i64 と
     * して扱う (`2L * u` 等が 32bit 演算で wrap しないように)。unsigned サフィックスも
     * 比較/除算の符号判定のため node に伝播する。 */
    node->int_is_long = (tk_as_num_int(tok)->int_size != TK_INT_SIZE_INT) ? 1 : 0;
    node->int_is_long_long = (tk_as_num_int(tok)->int_size == TK_INT_SIZE_LONG_LONG) ? 1 : 0;
    node->base.is_unsigned = tk_as_num_int(tok)->is_unsigned ? 1 : 0;
    int int_size = node->int_is_long ? 8 : 4;
    node->base.type = psx_type_new_integer(
        node->base.is_unsigned ? TK_UNSIGNED : TK_INT,
        int_size, node->base.is_unsigned);
    node->base.type->is_long_long = node->int_is_long_long ? 1 : 0;
  } else {
    node->base.fp_kind = tk_as_num_float(tok)->fp_kind;
    node->float_suffix_kind = tk_as_num_float(tok)->float_suffix_kind;
    node->fval = tk_as_num_float(tok)->fval;
    node->base.type = psx_type_new_float(
        (tk_float_kind_t)node->base.fp_kind,
        node->base.fp_kind == TK_FLOAT_KIND_FLOAT ? 4 : 8);
  }
  if (node->base.fp_kind) {
    float_lit_t *lit = calloc(1, sizeof(float_lit_t));
    lit->id = float_label_count++;
    lit->fval = node->fval;
    lit->fp_kind = node->base.fp_kind;
    lit->float_suffix_kind = node->float_suffix_kind;
    psx_register_float_lit(lit);
    node->fval_id = lit->id;
  }
  set_curtok(curtok()->next);
  return (node_t *)node;
}

// 内容文字列・幅・プレフィックスから ND_STRING ノードと .LC ラベルを生成する。
// str はコピーせず lit->str に直接渡されるので、呼び出し側で alloc 済みであること。
static node_string_t *make_string_lit_node(char *str, int len,
                                           tk_char_width_t char_width,
                                           tk_string_prefix_kind_t prefix_kind) {
  node_string_t *snode = arena_alloc(sizeof(node_string_t));
  snode->base.kind = ND_STRING;
  int id = string_label_count++;
  int label_len = snprintf(NULL, 0, ".LC%d", id);
  snode->string_label = calloc((size_t)label_len + 1, 1);
  snprintf(snode->string_label, (size_t)label_len + 1, ".LC%d", id);
  string_lit_t *lit = calloc(1, sizeof(string_lit_t));
  lit->label = snode->string_label;
  lit->str = str;
  lit->len = len;
  lit->char_width = char_width ? char_width : TK_CHAR_WIDTH_CHAR;
  lit->str_prefix_kind = prefix_kind;
  psx_register_string_lit(lit);
  /* 文字列リテラルは char (または wchar) 配列で、式中ではポインタに decay する。
   * `"abc"[1]` の subscript チェックや (ptr + n) のスケーリングに使う。 */
  snode->char_width = char_width ? char_width : TK_CHAR_WIDTH_CHAR;
  snode->str_prefix_kind = prefix_kind;
  int elem_width = snode->char_width;
  int elem_is_unsigned = prefix_kind == TK_STR_PREFIX_u ||
                         prefix_kind == TK_STR_PREFIX_U;
  token_kind_t elem_kind = elem_width == TK_CHAR_WIDTH_CHAR
                               ? TK_CHAR
                               : (elem_is_unsigned ? TK_UNSIGNED : TK_INT);
  psx_type_t *elem_type =
      psx_type_new_integer(elem_kind, elem_width, elem_is_unsigned);
  snode->base.type = psx_type_new_pointer(elem_type, elem_width);
  snode->base.type->base_deref_size = elem_width;
  /* byte_len は「デコード後」の内容長 (要素数)。str はソースのまま (`\t` 等の
   * エスケープシーケンスを含む raw) なので、エスケープを 1 要素に畳んで数える。
   * これがないと sizeof("\t") が raw の 2(+1) を返していた (正しくは 1+1)。 */
  snode->byte_len = tk_count_string_code_units(str, len,
                                               char_width ? (int)char_width
                                                          : TK_CHAR_WIDTH_CHAR);
  return snode;
}

// C11 6.4.2.2 __func__: 各関数本体に暗黙定義される const char[] の関数名。
static node_t *make_func_name_string_node(void) {
  char *current_funcname = NULL;
  int current_funcname_len = 0;
  psx_decl_get_current_funcname(&current_funcname, &current_funcname_len);
  const char *fname = current_funcname ? current_funcname : "";
  int flen = current_funcname ? current_funcname_len : 0;
  char *fstr = calloc((size_t)flen + 1, 1);
  memcpy(fstr, fname, (size_t)flen);
  return (node_t *)make_string_lit_node(fstr, flen, TK_CHAR_WIDTH_CHAR, TK_STR_PREFIX_NONE);
}

// 連続する TK_STRING リテラルを結合して 1 つの ND_STRING ノードを返す。
static node_t *parse_string_literal_sequence(void) {
  tk_char_width_t merged_width = TK_CHAR_WIDTH_CHAR;
  tk_string_prefix_kind_t merged_prefix_kind = TK_STR_PREFIX_NONE;
  size_t total_len = 0;
  token_t *t = curtok();
  while (t && t->kind == TK_STRING) {
    token_string_t *st = (token_string_t *)t;
    /* char_width 0 は接頭辞なし (通常の char 文字列) として扱う。stringize `#x` の
     * 結果トークンは char_width を 0 のままにするため、`"a" S(b)` のように 2 番目以降に
     * 来ると CHAR(1) と不一致になり E3002 で誤って弾かれていた (先頭に来る `S(a) "b"`
     * は下の正規化で通っていた)。比較側も 0→CHAR に正規化する。 */
    tk_char_width_t tw = st->char_width ? st->char_width : TK_CHAR_WIDTH_CHAR;
    if (total_len == 0) {
      merged_width = tw;
      merged_prefix_kind = st->str_prefix_kind;
    } else if (merged_width != tw) {
      diag_emit_tokf(DIAG_ERR_PARSER_UNEXPECTED_TOKEN, t, "%s",
                     diag_message_for(DIAG_ERR_PARSER_STRING_PREFIX_MISMATCH));
    }
    if (st->len < 0 || (size_t)st->len > SIZE_MAX - total_len - 1) {
      diag_emit_tokf(DIAG_ERR_PARSER_STRING_LITERAL_TOO_LARGE, t, "%s",
                     diag_message_for(DIAG_ERR_PARSER_STRING_LITERAL_TOO_LARGE));
    }
    total_len += (size_t)st->len;
    t = t->next;
  }
  if (total_len > (size_t)INT_MAX) {
    diag_emit_tokf(DIAG_ERR_PARSER_STRING_LITERAL_TOO_LARGE, curtok(), "%s",
                   diag_message_for(DIAG_ERR_PARSER_STRING_LITERAL_TOO_LARGE));
  }
  char *merged = calloc(total_len + 1, 1);
  if (!merged) {
    diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
  }
  size_t off = 0;
  while (curtok() && curtok()->kind == TK_STRING) {
    token_string_t *st = (token_string_t *)curtok();
    if (st->len < 0 || (size_t)st->len > total_len - off) {
      diag_emit_tokf(DIAG_ERR_PARSER_STRING_CONCAT_SIZE_INVALID, curtok(), "%s",
                     diag_message_for(DIAG_ERR_PARSER_STRING_CONCAT_SIZE_INVALID));
    }
    memcpy(merged + off, st->str, (size_t)st->len);
    off += (size_t)st->len;
    set_curtok(curtok()->next);
  }
  return (node_t *)make_string_lit_node(merged, (int)total_len, merged_width, merged_prefix_kind);
}

// GCC __builtin_expect(exp, c): 第1引数 exp をそのまま返す (分岐ヒントは無視)。
static node_t *try_parse_builtin_expect_call(token_ident_t *tok, expr_parse_ctx_t *ctx) {
  if (tok->len != 16 || memcmp(tok->str, "__builtin_expect", 16) != 0) return NULL;
  if (curtok()->kind != TK_LPAREN) return NULL;
  set_curtok(curtok()->next); // skip '('
  node_t *exp = assign_ctx(ctx);
  tk_expect(',');
  (void)assign_ctx(ctx); // discard hint
  tk_expect(')');
  return exp;
}

// 名前が宣言済みでない (var==NULL) 識別子の直後に '(' が来ている場合の通常関数呼び出し。
// 戻り値型はcanonical function typeから引く。
static node_t *build_unqualified_call(token_ident_t *tok, expr_parse_ctx_t *ctx) {
  set_curtok(curtok()->next); // skip '('
  node_func_t *node = arena_alloc(sizeof(node_func_t));
  node->base.kind = ND_FUNCALL;
  node->base.tok = (token_t *)tok;
  node->callee = NULL;
  node->funcname = tok->str;
  node->funcname_len = tok->len;
  int nargs = 0;
  int arg_cap = 16;
  node->args = calloc(arg_cap, sizeof(node_t *));
  if (curtok()->kind == TK_RPAREN) {
    set_curtok(curtok()->next);
  } else {
    node->args[nargs++] = assign_ctx(ctx);
    while (curtok()->kind == TK_COMMA) {
      set_curtok(curtok()->next);
      if (nargs >= arg_cap) {
        arg_cap = pda_next_cap(arg_cap, nargs + 1);
        node->args = pda_xreallocarray(node->args, (size_t)arg_cap, sizeof(node_t *));
      }
      node->args[nargs++] = assign_ctx(ctx);
    }
    tk_expect(')');
  }
  node->nargs = nargs;
  /* C99/C11 では implicit function declaration は禁止 (C89 では int 戻りで暗黙宣言可)。
   * `undecl_func()` のように未宣言関数を呼ぶ場合に診断する。clang は default で warning、
   * `-Werror=implicit-function-declaration` で error。ag_c も warning として扱う。
   * tok が関数として登録されておらず、グローバル変数 (関数ポインタ) でもないなら未宣言。 */
  if (!ps_ctx_has_function_name(tok->str, tok->len) &&
      !ps_find_global_var(tok->str, tok->len)) {
    node->base.is_implicit_func_decl = 1;
  }
  /* C11 6.5.2.2p2: 呼び出しの実引数数は仮引数数と一致 (non-variadic)、
   * または >= 固定引数数 (variadic) でなければならない。
   * 既に登録されている関数のみチェック (未宣言識別子は別エラーで弾かれる)。 */
  if (ps_ctx_has_function_name(tok->str, tok->len)) {
    int expected = 0;
    int is_variadic = ps_ctx_get_function_is_variadic(tok->str, tok->len, &expected) ? 1 : 0;
    int mismatch = is_variadic ? (nargs < expected) : (nargs != expected);
    if (mismatch) {
      psx_diag_ctx(curtok(), "funcall",
                   "関数呼び出しの引数数が一致しません: '%.*s' 期待 %s%d、実際 %d",
                   tok->len, tok->str,
                   is_variadic ? "≥" : "", expected, nargs);
    }
  }
  ps_node_materialize_type((node_t *)node);
  return (node_t *)node;
}

// 関数名識別子（呼び出しじゃなく値として使われる場合）の ND_FUNCREF ノード。
static node_t *build_funcref_node(token_ident_t *tok) {
  node_funcref_t *fr = arena_alloc(sizeof(node_funcref_t));
  fr->base.kind = ND_FUNCREF;
  fr->funcname = tok->str;
  fr->funcname_len = tok->len;
  return (node_t *)fr;
}

// グローバル変数表から名前を引く。見つからなければ NULL。
// 配列のときは ND_ADDR でラップして返す。
static node_t *try_build_global_var_node(token_ident_t *tok) {
  for (global_var_t *gv = ps_find_global_var(tok->str, tok->len); gv; gv = NULL) {
    if (gv->name_len != tok->len || memcmp(gv->name, tok->str, (size_t)tok->len) != 0) continue;
    if (ps_gvar_is_array(gv)) {
      return psx_node_new_gvar_array_addr_for(gv);
    }
    return psx_node_new_gvar_for(gv);
  }
  return NULL;
}

/* static local 配列のベースアドレスを ND_ADDR(ND_GVAR) として返す。
 * 配列は decl.c の try_lower_static_local_array でグローバルにリダイレクトされ、
 * alias lvar (is_static_local=1, static_global_name=mangled) を持つ。
 * alias は size=0 で frame 割当を抑制しているため、サイズ情報はグローバル変数表
 * から名前検索で引く。多次元配列は alias lvar に保存した stride 情報を
 * ND_ADDR(ND_GVAR) へ伝播し、通常のローカル/グローバル配列と同じ subscript 経路に乗せる。 */
static node_t *build_static_local_array_addr_node(lvar_t *var) {
  /* static-local array aliases are lowering metadata; do not materialize
   * var->decl_type while recognizing them, because the alias intentionally has
   * size=0/is_array=0 and carries its array shape in stride fields. */
  short gv_type_size = (short)var->elem_size;
  for (global_var_t *gv = ps_find_global_var(var->static_global_name, var->static_global_name_len); gv; gv = NULL) {
    if (gv->name_len == var->static_global_name_len &&
        memcmp(gv->name, var->static_global_name, (size_t)gv->name_len) == 0) {
      int storage_size = ps_gvar_storage_size(gv, 0);
      if (storage_size > 0) gv_type_size = (short)storage_size;
      break;
    }
  }
  return psx_node_new_static_local_array_addr_for(var, gv_type_size);
}

/* alias lvar が「static local 配列」を表すかを判別。
 * try_lower_static_local_array が is_static_local=1 + static_global_name +
 * elem_size>0 + size=0 + is_array=0 の組合せで登録する。スカラ static_local
 * (try_lower_static_local_scalar) は size>0 / fp_kind / pointer 等で別経路。 */
static int lvar_is_static_local_array(lvar_t *var) {
  return var && var->is_static_local && var->static_global_name &&
         var->elem_size > 0 && var->size == 0 && !var->is_vla &&
         !var->is_param;
}

// 配列ローカル変数（非 VLA）: ベースアドレスを ND_ADDR(ND_LVAR) として返す。
static node_t *build_array_lvar_addr_node(lvar_t *var) {
  return psx_node_new_lvar_array_addr_for(var, ps_lvar_tag_kind(var) != TK_EOF);
}

// byref 仮引数 (>16バイト構造体の値渡し): IR entry で受け取った pointer から
// フレーム上の通常 lvar slot へ memcpy 済みなので、式としては通常の struct lvar。
static node_t *build_byref_param_node(lvar_t *var) {
  return psx_node_new_lvar_identifier_ref_for(var);
}

// 識別子トークン tok を解決して node を返す:
//   1. __func__ → 暗黙文字列リテラル
//   2. 未定義 + enum const → 定数
//   3. 未定義 + '(' → 関数呼び出し
//   4. 未定義 + 既登録関数名 → 関数参照
//   5. 未定義 + グローバル変数 → ND_GVAR
//   6. それ以外 → ローカル変数 (必要なら新規登録)
static node_t *resolve_identifier(token_ident_t *tok, expr_parse_ctx_t *ctx) {
  if (tok->len == 8 && memcmp(tok->str, "__func__", 8) == 0) {
    return make_func_name_string_node();
  }
  // stdarg.h の va_start マクロが参照する ag_c 固有 builtin。
  // codegen で `add x?, x29, #STACK_SIZE` を出して variadic 引数領域の
  // 先頭アドレスを返す。
  if (tok->len == 13 && memcmp(tok->str, "__va_arg_area", 13) == 0) {
    node_t *n = arena_alloc(sizeof(node_t));
    n->kind = ND_VA_ARG_AREA;
    n->fp_kind = TK_FLOAT_KIND_NONE;
    return n;
  }
  lvar_t *var = psx_decl_find_lvar(tok->str, tok->len);
  if (!var) {
    long long enum_val = 0;
    if (psx_ctx_find_enum_const(tok->str, tok->len, &enum_val)) {
      return psx_node_new_num(enum_val);
    }
  }
  if (curtok()->kind == TK_LPAREN && !var) {
    node_t *be = try_parse_builtin_expect_call(tok, ctx);
    if (be) return be;
    /* `gp(...)` でグローバル関数ポインタを呼び出す場合は、まずグローバル変数として
     * 解決して間接呼び出しに回す。global var として見つからなければ通常の
     * unqualified function call として処理する。 */
    node_t *gv = try_build_global_var_node(tok);
    if (gv) return gv;
    return build_unqualified_call(tok, ctx);
  }
  if (!var && ps_ctx_has_function_name(tok->str, tok->len)) {
    return build_funcref_node(tok);
  }
  if (!var) {
    node_t *gv = try_build_global_var_node(tok);
    if (gv) return gv;
  }
  if (!var) {
    /* C89/C99/C11: 変数は必ず宣言が必要。未宣言識別子はエラー。
     * (旧 ag_c は暗黙のローカル変数として自動登録していたが、これは
     *  非標準動作なので削除した。tok を渡して位置情報を診断に含める。) */
    psx_diag_undefined_with_name((token_t *)tok, "variable", tok->str, tok->len);
    /* diag_emit_tokf は exit するためここには到達しないが、
     * 解析を続けたい場合のフォールバックとして lvar 登録しておく。 */
    var = psx_decl_register_lvar(tok->str, tok->len);
  }
  /* static local 配列はグローバルに lowering 済み (decl.c:try_lower_static_local_array)。
   * alias lvar の offset=0 は意味を持たないので、build_array_lvar_addr_node が
   * フレーム上の偽アドレスを base にしないよう専用経路で ND_ADDR(ND_GVAR) を返す。 */
  if (lvar_is_static_local_array(var)) {
    return annotate_lvar_usage_node(build_static_local_array_addr_node(var), var, ctx);
  }
  if (ps_lvar_is_array(var) && !ps_lvar_is_vla(var)) {
    return annotate_lvar_usage_node(build_array_lvar_addr_node(var), var, ctx);
  }
  if (ps_lvar_is_vla(var)) {
    return annotate_lvar_usage_node(psx_node_new_vla_decay_ref_for(var), var, ctx);
  }
  if (var->is_byref_param) {
    return annotate_lvar_usage_node(build_byref_param_node(var), var, ctx);
  }
  return annotate_lvar_usage_node(psx_node_new_lvar_identifier_ref_for(var), var, ctx);
}

static node_t *primary_with_compound_addr_context(int compound_addr_context, expr_parse_ctx_t *ctx) {
  node_t *cl = try_parse_compound_literal(compound_addr_context, ctx);
  if (cl) return cl;

  if (curtok()->kind == TK_GENERIC) return parse_generic_selection(ctx);

  if (curtok()->kind == TK_NUM) return parse_num_literal();

  if (curtok()->kind == TK_LPAREN && curtok()->next &&
      curtok()->next->kind == TK_LBRACE) {
    return psx_parse_statement_expression();
  }

  if (curtok()->kind == TK_LPAREN) {
    enter_paren_nest_or_die(ctx);
    set_curtok(curtok()->next);
    node_t *node = expr_internal_ctx(ctx);
    tk_expect(')');
    leave_paren_nest(ctx);
    return node;
  }

  token_ident_t *tok = tk_consume_ident();
  if (tok) return resolve_identifier(tok, ctx);

  if (curtok()->kind == TK_STRING) {
    return parse_string_literal_sequence();
  }

  psx_diag_ctx(curtok(), "primary", "%s",
               diag_message_for(DIAG_ERR_PARSER_PRIMARY_NUMBER_EXPECTED));
  return NULL;
}
