#include "expr.h"
#include "arena.h"
#include "core.h"
#include "decl.h"
#include "diag.h"
#include "dynarray.h"
#include "node_utils.h"
#include "ret_pointee_array.h"
#include "semantic_ctx.h"
#include "stmt.h"
#include "config_runtime.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include "../tokenizer/allocator.h"
#include "../tokenizer/escape.h"
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

static node_lvar_t *as_lvar(node_t *node) { return (node_lvar_t *)node; }

typedef struct {
  token_kind_t kind;
  int scalar_size;
  int is_unsigned;
  int is_long_long;  /* long long (C11 6.2.5: long と long long は同サイズでも別型) */
  int is_long_double;/* long double (C11 6.2.5: double と long double は同表現でも別型) */
  int is_plain_char; /* plain char (C11 6.2.5/6.7.2.1: char と signed/unsigned char は別型) */
  int is_pointer;
  int is_array;
  int is_funcptr;
  int is_func_designator;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int ptr_levels;
  unsigned int ptr_const_mask;
  unsigned int ptr_volatile_mask;
  int ptr_deref_size;
  int ptr_base_deref_size;
  tk_float_kind_t ptr_pointee_fp_kind;
  int ptr_pointee_unsigned;
  int ptr_pointee_const;
  int ptr_pointee_volatile;
  /* 複雑な派生型 (関数ポインタ / ネスト宣言子) の正規化トークン文字列。非 NULL のとき
   * 構造的フィールドより優先し、control と assoc 双方が非 NULL なら strcmp で照合する。 */
  char *type_sig;
} generic_type_t;

typedef struct {
  int unevaluated_operand_depth;
  int expr_nest_depth;
  int paren_nest_depth;
} expr_parse_ctx_t;

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

static int expr_node_is_long_long_type(node_t *n) {
  return psx_node_is_long_long_type(n);
}
static void apply_array_abstract_suffix_size(int *sz);
static int is_type_name_start_token(token_t *t);
static char *new_compound_lit_name(void);
static void set_lvar_array_strides_from_dims(lvar_t *var, const int *dims, int dim_count, int elem_size);
static void set_gvar_array_strides_from_dims(global_var_t *gv, const int *dims, int dim_count, int elem_size);
static void set_addr_array_strides_from_lvar(node_mem_t *addr, const lvar_t *var);
static void set_addr_array_strides_from_gvar(node_mem_t *addr, const global_var_t *gv);
static int lvar_is_static_local_array(lvar_t *var);
static node_t *new_typed_lvar_ref(lvar_t *var, int is_pointer);
static node_t *apply_postfix(node_t *node, expr_parse_ctx_t *ctx);
static node_t *parse_compound_literal_from_type(token_kind_t cast_kind, int cast_is_ptr,
                                                token_t *after_rparen,
                                                token_kind_t cast_tag_kind, char *cast_tag_name, int cast_tag_len,
                                                int cast_elem_size, tk_float_kind_t cast_fp_kind,
                                                int cast_array_count,
                                                const int *cast_array_dims, int cast_array_dim_count,
                                                int cast_is_complex,
                                                int cast_ptr_array_pointee_bytes,
                                                int compound_addr_context,
                                                expr_parse_ctx_t *ctx);

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

static int sizeof_expr_node(node_t *node) {
  /* C11 6.5.3.4p2: sizeof は対象式が「配列型」のときは配列全体のサイズを返す。
   * `"hello"` の sizeof は `char[6]` の 6 であって、decay 後のポインタサイズ
   * (= 8) ではない。 */
  if (node && node->kind == ND_STRING) {
    node_string_t *s = (node_string_t *)node;
    int elem = s->char_width ? (int)s->char_width : 1;
    return (s->byte_len + 1) * elem;
  }
  if (node && (node->kind == ND_ADDR || node->kind == ND_COMMA)) {
    node_t *target = node->kind == ND_COMMA ? node->rhs : node;
    if (target && target->kind == ND_ADDR) {
      int cl_array_size = ((node_mem_t *)target)->compound_literal_array_size;
      if (cl_array_size > 0) return cl_array_size;
    }
  }
  int sz = ps_node_type_size(node);
  if (sz) return sz;
  if (node && node->fp_kind == TK_FLOAT_KIND_FLOAT) return 4;
  if (node && node->fp_kind >= TK_FLOAT_KIND_DOUBLE) return 8;
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
static bool consume_const_dim_brackets(token_t **pt, int *out_mul) {
  token_t *t = *pt;
  if (!t || t->kind != TK_LBRACKET) return false;
  int array_mul = 1;
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
    array_mul *= dim;
    t = after;
  }
  *pt = t;
  if (out_mul) *out_mul = array_mul;
  return true;
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

static int parse_funcptr_abstract_decl(token_t **ptok, int *is_pointer) {
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
  token_t *after_params = skip_balanced_paren_token(t);
  if (!after_params) return 0;
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

static int parse_ptr_to_array_abstract_decl(token_t **ptok, int *is_pointer) {
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
  if (!skip_bracket_sequence(&t)) return 0;
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

static int parse_array_of_ptr_to_array_abstract_decl_skip(token_t **ptok, int *out_array_mul) {
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
  if (!skip_bracket_sequence(&t)) return 0;
  *ptok = t;
  if (out_array_mul) *out_array_mul = array_mul;
  return 1;
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
static int parse_ptr_to_func_returning_ptr_to_array_abstract_decl(token_t **ptok) {
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
  token_t *after_params = skip_balanced_paren_token(t);
  if (!after_params) return 0;
  t = after_params;
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!skip_bracket_sequence(&t)) return 0;
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
static int parse_ptr_to_func_returning_ptr_to_func_abstract_decl(token_t **ptok) {
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
  token_t *after_params = skip_balanced_paren_token(t);
  if (!after_params) return 0;
  t = after_params;
  if (!t || t->kind != TK_RPAREN) return 0;
  t = t->next;
  if (!t || t->kind != TK_LPAREN) return 0;
  after_params = skip_balanced_paren_token(t);
  if (!after_params) return 0;
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
    *is_pointer = 1;
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

static generic_type_t infer_generic_control_type(node_t *control) {
  generic_type_t gt = {0};
  gt.kind = TK_INT;
  gt.scalar_size = 4;
  gt.tag_kind = TK_EOF;
  gt.ptr_pointee_fp_kind = TK_FLOAT_KIND_NONE;
  if (!control) return gt;
  int is_tag_ptr = 0;
  psx_node_get_tag_type(control, &gt.tag_kind, &gt.tag_name, &gt.tag_len, &is_tag_ptr);
  if (!is_tag_ptr && (gt.tag_kind == TK_STRUCT || gt.tag_kind == TK_UNION)) {
    gt.kind = gt.tag_kind;
    return gt;
  }
  if (control->kind == ND_STRING) {
    /* 文字列リテラルは _Generic では char[] が char* へ decay した型として扱われる
     * (clang/gcc 準拠)。pointee サイズ (= 文字幅) を ptr_deref_size に入れないと
     * `char*` association と一致せず default に落ちていた。 */
    int char_w = ps_node_deref_size(control);
    gt.kind = TK_CHAR;
    gt.scalar_size = 1;
    gt.is_pointer = 1;
    gt.ptr_deref_size = char_w > 0 ? char_w : 1;
    return gt;
  }
  if (control->kind == ND_FUNCREF) {
    node_funcref_t *fr = (node_funcref_t *)control;
    psx_function_ret_info_t ret = psx_ctx_get_function_ret_info(fr->funcname, fr->funcname_len);
    gt.is_pointer = 1;
    gt.is_funcptr = 1;
    gt.is_func_designator = 1;
    gt.kind = ret.token_kind;
    if (gt.kind == TK_EOF) gt.kind = TK_INT;
    gt.ptr_levels = 1;
    gt.ptr_pointee_fp_kind = ret.fp_kind;
    gt.ptr_pointee_unsigned = ret.is_unsigned;
    bool ret_is_type = false;
    int ret_size = 4;
    psx_ctx_get_type_info(gt.kind, &ret_is_type, &ret_size);
    (void)ret_is_type;
    gt.scalar_size = ret_size;
    gt.ptr_deref_size = ret_size;
    gt.ptr_base_deref_size = ret_size;
    gt.tag_kind = ret.tag_kind;
    gt.tag_name = ret.tag_name;
    gt.tag_len = ret.tag_len;
    return gt;
  }
  if (control->fp_kind == TK_FLOAT_KIND_FLOAT) {
    gt.kind = TK_FLOAT;
    gt.scalar_size = 4;
    return gt;
  }
  if (control->fp_kind >= TK_FLOAT_KIND_DOUBLE) {
    gt.kind = TK_DOUBLE;
    gt.scalar_size = 8;
    /* long double は double に lowering され fp_kind=DOUBLE になるが _Generic では別型。
     * 宣言時に立てた is_long_double ビット (ノードへ伝播済み) を読んで区別し、
     * `long double:` 関連型と一致させる。 */
    gt.is_long_double = psx_node_is_long_double_type(control);
    return gt;
  }
  int ts = ps_node_type_size(control);
  int ds = ps_node_deref_size(control);
  int is_ptr = 0;
  is_ptr = ps_node_is_pointer(control);
  if (is_ptr) {
    gt.is_pointer = 1;
    gt.kind = TK_INT;
    gt.ptr_levels = psx_node_pointer_qual_levels(control);
    gt.ptr_deref_size = ds;
    gt.ptr_base_deref_size = psx_node_base_deref_size(control);
    gt.ptr_pointee_fp_kind = psx_node_pointee_fp_kind(control);
    gt.ptr_const_mask = psx_node_pointer_const_qual_mask(control);
    gt.ptr_volatile_mask = psx_node_pointer_volatile_qual_mask(control);
    gt.ptr_pointee_unsigned = psx_node_pointee_is_unsigned(control);
    gt.ptr_pointee_const = psx_node_pointee_is_const_qualified(control);
    gt.ptr_pointee_volatile = psx_node_pointee_is_volatile_qualified(control);
    return gt;
  }
  gt.is_unsigned = psx_node_is_unsigned_type(control);
  gt.scalar_size = ts ? ts : 4;
  /* 変数等の long long / plain char の型識別を制御式型へ反映 (_Generic 用)。 */
  if (expr_node_is_long_long_type(control)) gt.is_long_long = 1;
  if (psx_node_is_plain_char_type(control)) gt.is_plain_char = 1;
  /* long/long long サフィックス付き整数リテラル (`42L`) は long (8B) として扱い、
   * _Generic の `long:` association と一致させる (int_is_long は parse_num_literal が立てる)。 */
  if (control->kind == ND_NUM && ((node_num_t *)control)->int_is_long) {
    gt.scalar_size = 8;
    /* `0LL` は long long。long (8B) の association より long long を優先させるため
     * is_long_long を立てる (generic_type_matches が同サイズ整数で区別する)。 */
    if (((node_num_t *)control)->int_is_long_long) gt.is_long_long = 1;
  }
  gt.kind = gt.is_unsigned ? TK_UNSIGNED : TK_INT;
  return gt;
}

static int generic_type_matches(generic_type_t control, generic_type_t assoc) {
  /* 複雑な派生型 (関数ポインタ / ネスト宣言子): 双方が型シグネチャ文字列を持つときは
   * それで照合する。構造的フィールドでは引数の個数/型やネスト構造を区別できないため。
   * 片方のみシグネチャを持つ場合は従来の構造的照合に委ねる (回帰回避; 加算的変更)。 */
  if (control.type_sig && assoc.type_sig) {
    return strcmp(control.type_sig, assoc.type_sig) == 0;
  }
  if (control.is_pointer != assoc.is_pointer) return 0;
  if (control.is_pointer) {
    if (control.is_funcptr || assoc.is_funcptr) {
      if (control.is_func_designator && assoc.type_sig && !control.type_sig) return 0;
      return control.is_funcptr == assoc.is_funcptr &&
             control.kind == assoc.kind &&
             control.ptr_pointee_fp_kind == assoc.ptr_pointee_fp_kind &&
             control.ptr_pointee_unsigned == assoc.ptr_pointee_unsigned;
    }
    if (control.ptr_levels && assoc.ptr_levels && control.ptr_levels != assoc.ptr_levels) return 0;
    if (control.ptr_const_mask != assoc.ptr_const_mask ||
        control.ptr_volatile_mask != assoc.ptr_volatile_mask) {
      return 0;
    }
    // struct/union ポインタはタグ一致で比較
    if (control.tag_kind != TK_EOF || assoc.tag_kind != TK_EOF) {
      return control.tag_kind == assoc.tag_kind &&
             control.tag_len == assoc.tag_len &&
             strncmp(control.tag_name ? control.tag_name : "",
                     assoc.tag_name ? assoc.tag_name : "",
                     (size_t)control.tag_len) == 0;
    }
    // 浮動小数点ポインタは pointee FP 種別で比較
    if (control.ptr_pointee_fp_kind != TK_FLOAT_KIND_NONE ||
        assoc.ptr_pointee_fp_kind != TK_FLOAT_KIND_NONE) {
      return control.ptr_pointee_fp_kind == assoc.ptr_pointee_fp_kind &&
             control.ptr_pointee_unsigned == assoc.ptr_pointee_unsigned &&
             control.ptr_pointee_const == assoc.ptr_pointee_const &&
             control.ptr_pointee_volatile == assoc.ptr_pointee_volatile;
    }
    // それ以外は pointee サイズで比較（int*/char*/short*/long* など）
    if (control.ptr_levels >= 2 || assoc.ptr_levels >= 2) {
      return control.ptr_base_deref_size == assoc.ptr_base_deref_size &&
             control.ptr_pointee_unsigned == assoc.ptr_pointee_unsigned &&
             control.ptr_pointee_const == assoc.ptr_pointee_const &&
             control.ptr_pointee_volatile == assoc.ptr_pointee_volatile;
    }
    return control.ptr_deref_size == assoc.ptr_deref_size &&
           control.ptr_pointee_unsigned == assoc.ptr_pointee_unsigned &&
           control.ptr_pointee_const == assoc.ptr_pointee_const &&
           control.ptr_pointee_volatile == assoc.ptr_pointee_volatile;
  }
  if (control.is_array || assoc.is_array) return control.is_array == assoc.is_array;
  /* struct/union/enum タグ型: どちらか一方でもタグを持てば、両者のタグが一致する
   * ときのみマッチ。control 側だけを見ると、control が int・assoc が単一 int メンバの
   * struct のとき下のスカラ経路でサイズ一致して誤マッチしていた (`_Generic((int),
   * Anon:.., int:..)` が Anon を選ぶ)。 */
  if (control.tag_kind != TK_EOF || assoc.tag_kind != TK_EOF) {
    return control.tag_kind == assoc.tag_kind &&
           control.tag_len == assoc.tag_len &&
           strncmp(control.tag_name ? control.tag_name : "",
                   assoc.tag_name ? assoc.tag_name : "",
                   (size_t)control.tag_len) == 0;
  }
  if (control.kind == TK_FLOAT || control.kind == TK_DOUBLE ||
      assoc.kind == TK_FLOAT || assoc.kind == TK_DOUBLE) {
    /* double と long double は同じ kind (TK_DOUBLE) に lowering されるが C11 では別型。
     * is_long_double で区別する (制御式が素の double のとき long double: に当てない)。 */
    return control.kind == assoc.kind &&
           control.is_long_double == assoc.is_long_double;
  }
  return control.scalar_size == assoc.scalar_size && control.is_unsigned == assoc.is_unsigned &&
         control.is_long_long == assoc.is_long_long &&
         control.is_plain_char == assoc.is_plain_char;
}

// _Generic の関連型に出てくる CV 修飾子を読み飛ばしながら、
// const/volatile が現れたかどうかを out_const / out_volatile に反映する。
// include_restrict が true の場合は restrict も受理する（後置 cv 用）。
static void consume_assoc_cv_quals(int *out_const, int *out_volatile, bool include_restrict) {
  for (;;) {
    token_kind_t k = curtok()->kind;
    if (k == TK_CONST)        { *out_const = 1;    set_curtok(curtok()->next); continue; }
    if (k == TK_VOLATILE)     { *out_volatile = 1; set_curtok(curtok()->next); continue; }
    if (include_restrict && k == TK_RESTRICT) { set_curtok(curtok()->next); continue; }
    return;
  }
}

// _Generic 関連型のベース型 1 つを読む。typedef 名 / struct-or-union-or-enum タグ /
// スカラ型の 3 経路。out / 各 base_* に結果を埋め、未認識なら 0 を返す。
static int parse_assoc_base_type(generic_type_t *out,
                                 int *base_elem_size, tk_float_kind_t *base_fp_kind,
                                 int *base_unsigned, int *base_const, int *base_volatile) {
  if (psx_ctx_is_typedef_name_token(curtok())) {
    token_ident_t *id = (token_ident_t *)curtok();
    token_kind_t base_kind = TK_EOF;
    int elem_size = 8;
    tk_float_kind_t fp_kind = TK_FLOAT_KIND_NONE;
    token_kind_t tag_kind = TK_EOF;
    char *tag_name = NULL;
    int tag_len = 0;
    int is_ptr = 0;
    int td_is_unsigned = 0;
    psx_typedef_info_t _ti = {0};
    if (psx_ctx_find_typedef_name(id->str, id->len, &_ti)) {
      base_kind = _ti.base_kind; elem_size = _ti.elem_size; fp_kind = _ti.fp_kind;
      tag_kind = _ti.tag_kind; tag_name = _ti.tag_name; tag_len = _ti.tag_len;
      is_ptr = _ti.is_pointer; td_is_unsigned = _ti.is_unsigned;
      out->is_long_double = _ti.is_long_double ? 1 : 0;
      out->is_array = _ti.is_array;
      out->is_funcptr = _ti.is_funcptr;
      if (base_const) *base_const = _ti.pointee_const_qualified;
      if (base_volatile) *base_volatile = _ti.pointee_volatile_qualified;
    }
    set_curtok(curtok()->next);
    out->kind = (tag_kind != TK_EOF) ? tag_kind : base_kind;
    out->scalar_size = elem_size;
    out->is_unsigned = td_is_unsigned;
    out->is_pointer = is_ptr;
    out->tag_kind = tag_kind;
    out->tag_name = tag_name;
    out->tag_len = tag_len;
    *base_elem_size = elem_size;
    *base_fp_kind = fp_kind;
    *base_unsigned = td_is_unsigned;
    return 1;
  }
  if (psx_ctx_is_tag_keyword(curtok()->kind)) {
    token_kind_t tag_kind = curtok()->kind;
    set_curtok(curtok()->next);
    token_ident_t *tag = tk_consume_ident();
    if (!tag) return 0;
    if (!psx_ctx_has_tag_type(tag_kind, tag->str, tag->len)) {
      psx_diag_undefined_with_name((token_t *)tag, diag_text_for(DIAG_TEXT_TAG_TYPE_SUFFIX), tag->str, tag->len);
    }
    out->kind = tag_kind;
    out->tag_kind = tag_kind;
    out->tag_name = tag->str;
    out->tag_len = tag->len;
    *base_elem_size = psx_ctx_get_tag_size(tag_kind, tag->str, tag->len);
    return 1;
  }
  // スカラ型
  token_kind_t tk = TK_EOF;
  token_t *after = NULL;
  int is_ll = 0;
  int is_pc = 0;
  if (parse_integer_cast_spec_sequence(curtok(), &tk, base_elem_size, base_unsigned, &after, &is_ll, &is_pc)) {
    out->kind = tk;
    out->is_long_long = is_ll;
    out->is_plain_char = is_pc;
    set_curtok(after);
    return 1;
  }
  psx_type_spec_result_t type_spec;
  tk = psx_consume_type_kind_ex(&type_spec);
  if (tk == TK_EOF) return 0;
  out->kind = tk;
  /* `long double` は double に lowering され kind=TK_DOUBLE になるが、_Generic では
   * double と別型。side-channel フラグで区別する (double 制御式が long double: に
   * 誤マッチして tgmath の sqrt(2.0) が sqrtl を呼ぶのを防ぐ)。 */
  out->is_long_double = type_spec.is_long_double;
  psx_ctx_get_type_info(tk, NULL, base_elem_size);
  if (tk == TK_FLOAT) *base_fp_kind = TK_FLOAT_KIND_FLOAT;
  else if (tk == TK_DOUBLE) *base_fp_kind = TK_FLOAT_KIND_DOUBLE;
  *base_unsigned = (tk == TK_UNSIGNED);
  return 1;
}

// `*` 列を読み、各レベルに const/volatile/restrict/_Atomic 修飾を反映する。
// _Atomic(T) 形式（次が '(') はポインタ修飾子ではないのでスキップ対象外。
static void parse_pointer_levels_with_quals(generic_type_t *out, token_t **pt) {
  token_t *t = *pt;
  while (t && t->kind == TK_MUL) {
    out->is_pointer = 1;
    out->ptr_levels++;
    int level = out->ptr_levels;
    t = t->next;
    while (t && (t->kind == TK_CONST || t->kind == TK_VOLATILE || t->kind == TK_RESTRICT ||
                 (t->kind == TK_ATOMIC && !(t->next && t->next->kind == TK_LPAREN)))) {
      if (t->kind == TK_CONST) out->ptr_const_mask |= (1u << (level - 1));
      if (t->kind == TK_VOLATILE) out->ptr_volatile_mask |= (1u << (level - 1));
      t = t->next;
    }
  }
  *pt = t;
}

// type-name の abstract-declarator のうち、_Generic 関連型で受理する形を順に試す。
// 例: int (*)(int) / int (*)[3] / int (*[3])(int) など。
static void try_parse_assoc_abstract_declarators(generic_type_t *out, token_t **pt) {
  (void)parse_funcptr_abstract_decl(pt, &out->is_pointer);
  (void)parse_ptr_to_array_abstract_decl(pt, &out->is_pointer);
  (void)parse_array_of_funcptr_abstract_decl(pt, NULL);
  (void)parse_array_of_ptr_to_array_abstract_decl_skip(pt, NULL);
  (void)parse_array_of_ptr_to_array_of_ptr_abstract_decl(pt, NULL);
  (void)parse_ptr_to_func_returning_ptr_to_array_abstract_decl(pt);
  (void)parse_array_of_ptr_to_func_returning_ptr_to_array_abstract_decl(pt, NULL);
  (void)parse_ptr_to_func_returning_ptr_to_func_abstract_decl(pt);
  (void)parse_ptr_to_func_returning_ptr_to_func_returning_ptr_to_array_abstract_decl(pt);
}

static int parse_generic_assoc_type(generic_type_t *out) {
  *out = (generic_type_t){0};
  out->kind = TK_EOF;
  out->tag_kind = TK_EOF;
  out->ptr_pointee_fp_kind = TK_FLOAT_KIND_NONE;
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
  consume_assoc_cv_quals(&base_const, &base_volatile, false);
  if (!parse_assoc_base_type(out, &base_elem_size, &base_fp_kind,
                             &base_unsigned, &base_const, &base_volatile)) return 0;
  out->scalar_size = base_elem_size;
  out->is_unsigned = base_unsigned;
  consume_assoc_cv_quals(&base_const, &base_volatile, true);
  token_t *t = curtok();
  parse_pointer_levels_with_quals(out, &t);
  try_parse_assoc_abstract_declarators(out, &t);
  // 配列サフィックス: ポインタとして扱わない場合のみ '[' 列を読み飛ばす。
  if (!out->is_pointer) {
    while (t && t->kind == TK_LBRACKET) {
      token_t *after = skip_balanced_bracket_token(t);
      if (!after) break;
      out->is_array = 1;
      t = after;
    }
  }
  if (out->is_pointer) {
    if (out->ptr_levels == 0) out->ptr_levels = 1;
    out->ptr_deref_size = base_elem_size;
    out->ptr_base_deref_size = base_elem_size;
    out->ptr_pointee_fp_kind = base_fp_kind;
    out->ptr_pointee_unsigned = base_unsigned;
    out->ptr_pointee_const = base_const;
    out->ptr_pointee_volatile = base_volatile;
  }
  /* 関数ポインタ / ネスト宣言子など '(' を含む複雑型のみ型シグネチャを作る。 */
  out->type_sig = psx_serialize_decl_type_tokens(sig_start, t, NULL);
  set_curtok(t);
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
  psx_decl_set_var_tag(var, base_tag_kind, base_tag_name, base_tag_len, 0);
  node_t *lhs_obj = new_typed_lvar_ref(var, 0);
  node_mem_t *assign_node = psx_node_new_assign(lhs_obj, base);
  assign_node->type_size = obj_size;
  return psx_node_new_binary(ND_COMMA, (node_t *)assign_node, new_typed_lvar_ref(var, 0));
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
  psx_decl_set_var_tag(var, base_tag_kind, base_tag_name, base_tag_len, 0);
  node_t *lhs_then = new_typed_lvar_ref(var, 0);
  node_mem_t *assign_then = psx_node_new_assign(lhs_then, tern->base.rhs);
  assign_then->type_size = obj_size;
  node_t *lhs_else = new_typed_lvar_ref(var, 0);
  node_mem_t *assign_else = psx_node_new_assign(lhs_else, tern->els);
  assign_else->type_size = obj_size;
  node_ctrl_t *select = arena_alloc(sizeof(node_ctrl_t));
  select->base.kind = ND_TERNARY;
  select->base.lhs = tern->base.lhs;
  select->base.rhs = (node_t *)assign_then;
  select->els = (node_t *)assign_else;
  return psx_node_new_binary(ND_COMMA, (node_t *)select, new_typed_lvar_ref(var, 0));
}

static int member_ptr_array_pointee_elem_size(const tag_member_info_t *mem_info) {
  if (!mem_info || mem_info->ptr_array_pointee_bytes <= 0 || mem_info->arr_ndim <= 0) return 0;
  int count = 1;
  for (int i = 0; i < mem_info->arr_ndim && i < 8; i++) {
    if (mem_info->arr_dims[i] <= 0) return 0;
    count *= mem_info->arr_dims[i];
  }
  if (count <= 0 || (mem_info->ptr_array_pointee_bytes % count) != 0) return 0;
  return mem_info->ptr_array_pointee_bytes / count;
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
      node_mem_t *addr_rhs = arena_alloc(sizeof(node_mem_t));
      addr_rhs->base.kind = ND_ADDR;
      addr_rhs->base.lhs = base->rhs;
      addr_rhs->type_size = 8;
      addr_base = psx_node_new_binary(ND_COMMA, base->lhs, (node_t *)addr_rhs);
    } else {
      node_mem_t *addr = arena_alloc(sizeof(node_mem_t));
      addr->base.kind = ND_ADDR;
      addr->base.lhs = base;
      addr->type_size = 8;
      addr_base = (node_t *)addr;
    }
  }
  node_t *addr = psx_node_new_binary(ND_ADD, addr_base, psx_node_new_num(mem_info->offset));
  node_mem_t *deref = arena_alloc(sizeof(node_mem_t));
  deref->base.kind = ND_DEREF;
  deref->base.lhs = addr;
  int mem_size = mem_info->type_size;
  int mem_array_len = mem_info->array_len;
  int mem_is_ptr = mem_info->is_tag_pointer;
  deref->type_size = mem_size ? mem_size : 8;
  deref->deref_size = mem_info->deref_size;
  if (mem_array_len > 0 && mem_size > 0) {
    /* メンバが配列 (ポインタ配列も含む): 式中では配列名がポインタへ崩壊する。
     * 後続 subscript / pointer arith のため、type_size を配列全体、deref_size を
     * 1 要素サイズに合わせ、is_pointer=1 を立てる。
     * `int *arr[N]` のような配列メンバではこの経路で `arr[i]` が 8 byte step に
     * なる (mem_size = 8 = sizeof(int*) なので)。 */
    deref->type_size = mem_size * mem_array_len;
    deref->deref_size = mem_size;
    deref->is_pointer = 1;
    /* 多次元配列メンバ (`int a[2][2]`): 第1サブスクリプトは行ストライド
     * (outer_stride) でステップし、その結果が要素サイズ (mem_size) で添字できる
     * よう inner_deref_size に要素サイズを置く。ローカル多次元配列の decay と同じ
     * 表現 (deref_size=行ストライド, inner_deref_size=要素)。 */
    if (mem_info->outer_stride > 0) {
      deref->deref_size = mem_info->outer_stride;
      deref->inner_deref_size = (short)mem_size;
      /* 3 次元以上の配列メンバ (`int t[2][2][2]` / `char c[2][2][3]`): 1 段目 subscript の
       * 後に内側 2 段以上が残るため、ローカル/グローバルの 3D 配列と同じく mid_stride /
       * elem_size の 2 段を渡す。ローカル build_array_lvar_addr_node では
       * inner_deref_size=mid_stride, next_deref_size=elem_size として 3 段の subscript を
       * 連鎖させる。member 経由でも同じ表現に乗せる。これがないと 3 段目 subscript が
       * elem_size 直接 step にならず誤アドレスを load して SIGSEGV になっていた。 */
      if (mem_info->mid_stride > 0) {
        deref->inner_deref_size = (short)mem_info->mid_stride;
        deref->next_deref_size = (short)mem_size;
      }
    }
    /* ポインタ配列の各要素は単一ポインタ。subscript 後の 1 段 deref では qual_levels を引き継ぐ。 */
    if (mem_is_ptr) {
      deref->is_tag_pointer = 0;
      /* ポインタ配列メンバ (`T *arr[N]`) の各要素は単段ポインタ。ローカルの
       * `T *arr[N]` と同じく pql=1 / base_deref_size=要素 pointee サイズ を立て、
       * build_subscript_deref の「要素がポインタ」分岐に乗せる。これにより
       * `struct N *arr[N]` の `arr[i]` 結果に is_tag_pointer が立ち、`arr[i]->m`
       * が解決できる (それまで struct 値扱いで E3005)。 */
      deref->pointer_qual_levels = 1;
      deref->base_deref_size = (short)mem_info->deref_size;
      /* array-of-pointer-to-array メンバ (`int (*p[M])[N]`): 要素ポインタが指す配列の
       * 全バイト数を deref ノードに carry。build_subscript_deref が `s.p[i]` の結果
       * deref に pointer-to-array 情報を伝播し、`(*s.p[i])[j]` の単項 `*` が要素ストライドに
       * 再設定する経路に乗せる。 */
      if (mem_info->ptr_array_pointee_bytes > 0) {
        deref->ptr_array_pointee_bytes = mem_info->ptr_array_pointee_bytes;
        int ptr_arr_elem = member_ptr_array_pointee_elem_size(mem_info);
        if (ptr_arr_elem > 0) deref->base_deref_size = (short)ptr_arr_elem;
      }
    }
  } else if (mem_is_ptr && mem_size > 0 && mem_info->outer_stride > 0) {
    /* pointer-to-array メンバ (`struct S { int (*p)[N]; }` / `int (*p)[M][N]`):
     * mem_info->outer_stride に pointee の全バイト数を保存。多次元 pointee の場合は
     * mem_info->mid_stride に 1 段目 subscript stride (= N*elem) も保存されている。
     * deref->deref_size に outer_stride、続いて inner_deref_size / next_deref_size に
     * 段ストライドを並べ、build_unary_deref_node が `*s.p` 構築時に 1 段スライドして
     * carry できるようにする (ローカル `int (*p)[M][N]` の lvar 表現と整合)。 */
    deref->is_pointer = 1;
    deref->is_scalar_ptr_member = 1;
    deref->deref_size = (short)mem_info->outer_stride;
    if (mem_info->mid_stride > 0) {
      /* 2D pointee: 1 段目 subscript stride = mid_stride、最終要素 = elem */
      deref->inner_deref_size = (short)mem_info->mid_stride;
      deref->next_deref_size = (short)mem_info->deref_size;
    } else {
      /* 1D pointee: 要素サイズだけ */
      deref->inner_deref_size = (short)mem_info->deref_size;
    }
  } else if (mem_is_ptr && mem_size > 0) {
    /* スカラポインタメンバ (`char *name`): subscript や pointer 算術で
     * is_pointer 判定が要るため立てておく。is_scalar_ptr_member を立てて
     * 配列メンバの decay 表現と区別する。 */
    deref->is_pointer = 1;
    deref->is_scalar_ptr_member = 1;
    if ((mem_info->tag_kind == TK_STRUCT || mem_info->tag_kind == TK_UNION) &&
        mem_info->tag_name) {
      int pointee_size = psx_ctx_get_tag_size(mem_info->tag_kind, mem_info->tag_name,
                                              mem_info->tag_len);
      deref->pointer_qual_levels = mem_info->pointer_qual_levels > 0
                                      ? mem_info->pointer_qual_levels
                                      : 1;
      deref->base_deref_size = (short)(pointee_size > 0 ? pointee_size : 8);
    }
    if (mem_info->ptr_array_pointee_bytes > 0) {
      deref->ptr_array_pointee_bytes = mem_info->ptr_array_pointee_bytes;
      int ptr_arr_elem = member_ptr_array_pointee_elem_size(mem_info);
      deref->base_deref_size = (short)(ptr_arr_elem > 0 ? ptr_arr_elem : mem_info->deref_size);
      deref->deref_size = 8;
    }
  }
  deref->tag_kind = mem_info->tag_kind;
  deref->tag_name = mem_info->tag_name;
  deref->tag_len = mem_info->tag_len;
  deref->is_tag_pointer = mem_is_ptr;
  if (psx_node_pointee_is_const_qualified(base)) deref->is_const_qualified = 1;
  if (psx_node_pointee_is_volatile_qualified(base)) deref->is_volatile_qualified = 1;
  deref->bit_width = mem_info->bit_width;
  deref->bit_offset = mem_info->bit_offset;
  deref->bit_is_signed = mem_info->bit_is_signed;
  psx_node_copy_funcptr_metadata_from_tag_member(deref, mem_info);
  /* float/double メンバなら fp_kind を deref に伝播。配列メンバ (`float v[4]`) は
   * 式中でポインタへ decay するので pointee_fp_kind に入れて subscript 結果を fp load
   * にする (スカラメンバはそのまま base.fp_kind)。is_bool と同じ分岐。これがないと
   * `s.v[i]` が整数 load になり float 値が化けていた。 */
  if (mem_info->fp_kind != TK_FLOAT_KIND_NONE) {
    if (mem_array_len > 0 && mem_size > 0)      deref->pointee_fp_kind = mem_info->fp_kind;
    /* ポインタメンバ (関数ポインタ `double (*f)(double)`): fp_kind は「指す先 /
     * 戻り型」の種別なので pointee_fp_kind に載せる (base.fp_kind に載せると 8B の
     * ポインタ値を double としてロードしてしまう)。呼び出し側 parse_call_postfix が
     * pointee_fp_kind を funcall に伝播し、戻り値を d0 で読む。 */
    else if (mem_is_ptr && mem_size > 0)        deref->pointee_fp_kind = mem_info->fp_kind;
    else                                         deref->base.fp_kind = mem_info->fp_kind;
  }
  /* _Bool メンバ: 配列メンバなら pointee_is_bool、それ以外は is_bool。 */
  if (mem_info->is_bool) {
    if (mem_array_len > 0 && mem_size > 0) deref->pointee_is_bool = 1;
    else                                    deref->is_bool = 1;
  }
  /* unsigned メンバ: load を zero-extend させるため伝播。配列メンバなら
   * pointee_is_unsigned (build_subscript_deref が要素 load を zero-extend にする)、
   * スカラメンバなら is_unsigned。is_bool と同じ分岐。これがないと
   * `struct S { unsigned char x[1]; }` の s.x[0]=200 が ldrsb で -56 に化ける。 */
  if (mem_info->is_unsigned) {
    if (mem_array_len > 0 && mem_size > 0) deref->pointee_is_unsigned = 1;
    else                                    deref->is_unsigned = 1;
  }
  return (node_t *)deref;
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
  psx_node_get_tag_type(base, &base_tag_kind, &base_tag_name, &base_tag_len, &base_is_ptr);
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
      ? psx_ctx_find_tag_member_info_at_scope(base_tag_kind, base_tag_name, base_tag_len,
                                              base_scope, member->str, member->len, &mem_info)
      : psx_ctx_find_tag_member_info(base_tag_kind, base_tag_name, base_tag_len,
                                     member->str, member->len, &mem_info);
  if (!found) {
    psx_diag_ctx(op_tok, "member", diag_message_for(DIAG_ERR_PARSER_MEMBER_NOT_FOUND),
                 member->len, member->str);
  }
  return build_member_deref_node(base, from_ptr, &mem_info);
}

static node_t *parse_compound_literal_from_type(token_kind_t cast_kind, int cast_is_ptr,
                                                token_t *after_rparen,
                                                token_kind_t cast_tag_kind, char *cast_tag_name, int cast_tag_len,
                                                int cast_elem_size, tk_float_kind_t cast_fp_kind,
                                                int cast_array_count,
                                                const int *cast_array_dims, int cast_array_dim_count,
                                                int cast_is_complex,
                                                int cast_ptr_array_pointee_bytes,
                                                int compound_addr_context,
                                                expr_parse_ctx_t *ctx) {
  set_curtok(after_rparen);
  int cl_is_complex = cast_is_complex;
  int cl_ptr_array_pointee_bytes = cast_ptr_array_pointee_bytes;
  int base_elem = cast_elem_size > 0 ? cast_elem_size : 8;
  int local_array_dims[8] = {0};
  int local_array_dim_count = 0;
  if (cast_array_dims && cast_array_dim_count > 0) {
    local_array_dim_count = cast_array_dim_count > 8 ? 8 : cast_array_dim_count;
    for (int i = 0; i < local_array_dim_count; i++) local_array_dims[i] = cast_array_dims[i];
  }
  // `(T[]){...}` / `(T *[]){...}` の空サイズは初期化子から要素数を推定する。
  if (cast_array_count < 0) {
    long long inferred = 0;
    // 特例: `(char[]){"abc" "def" ...}` のように波括弧で文字列リテラル列を
    // 包んでいる場合は、文字列内容長 + 1 を要素数として採用する。
    if (base_elem == 1 && after_rparen && after_rparen->kind == TK_LBRACE) {
      token_t *t = after_rparen->next;
      if (t && t->kind == TK_STRING) {
        long long total = 0;
        token_t *cur = t;
        while (cur && cur->kind == TK_STRING) {
          total += ((token_string_t *)cur)->len;
          cur = cur->next;
        }
        if (cur && cur->kind == TK_RBRACE) {
          inferred = total + 1; // 終端 NUL
        }
      }
    }
    if (inferred <= 0) {
      inferred = psx_decl_count_brace_init_elements(after_rparen);
    }
    if (inferred <= 0) {
      psx_diag_ctx(curtok(), "expr",
                   "複合リテラル `(T[]){...}` の要素数を初期化子から推定できません");
      cast_array_count = 1;
    } else {
      cast_array_count = (int)inferred;
      local_array_dims[0] = cast_array_count;
      local_array_dim_count = 1;
    }
  }
  /* `(int (*[N])(args)){f1, f2, ...}` や `(int *[N]){&x, &y}` のように
   * 要素がポインタの配列 compound literal は、配列実体 (N * 8 byte) として登録する。
   * cast_is_ptr=1 + cast_array_count>0 で識別。 */
  int is_pointer_elem_array = (cast_is_ptr && cast_array_count > 0) ? 1 : 0;
  int is_arr = ((!cast_is_ptr && cast_array_count > 0) || is_pointer_elem_array) ? 1 : 0;
  if (is_pointer_elem_array) base_elem = 8;
  int var_size = is_pointer_elem_array ? (8 * cast_array_count)
                : (cast_is_ptr ? 8 : (is_arr ? base_elem * cast_array_count : base_elem));
  /* `(double _Complex){re, im}` 等の複素数 compound literal: {実部, 虚部} で
   * base_elem*2 バイト。is_complex を立てて psx_decl_parse_initializer_for_var の
   * 複素数 brace 経路に乗せる。 */
  int cl_complex_scalar = (cl_is_complex && !is_arr && !cast_is_ptr) ? 1 : 0;
  if (cl_complex_scalar) var_size = base_elem * 2;
  char *tmp_name = new_compound_lit_name();
  char *current_funcname = NULL;
  int current_funcname_len = 0;
  psx_decl_get_current_funcname(&current_funcname, &current_funcname_len);
  (void)current_funcname_len;
  if (current_funcname == NULL) {
    int want_addr = compound_addr_context;
    /* struct/union/配列のファイルスコープ複合リテラル `&(struct S){3,4}` /
     * `&(int[3]){1,2,3}[0]`: 単一スカラしか扱えない下の経路では brace の `,` で E2006 に
     * なる。グローバル struct/配列と同じ psx_parse_global_brace_init_flat で gvar 実体へ
     * 展開し、アドレス可能なノードを返す。 */
    int cl_is_aggregate = is_arr || cast_tag_kind == TK_STRUCT || cast_tag_kind == TK_UNION;
    if (cl_is_aggregate) {
      global_var_t *gv = calloc(1, sizeof(global_var_t));
      gv->name = tmp_name;
      gv->name_len = (int)strlen(tmp_name);
      gv->type_size = var_size;
      gv->deref_size = base_elem;
      gv->is_array = is_arr;
      if (is_pointer_elem_array) {
        gv->pointee_elem_size = cast_elem_size > 0 ? (short)cast_elem_size : 8;
        gv->is_tag_pointer = (cast_tag_kind != TK_EOF) ? 1 : 0;
      }
      gv->fp_kind = cast_fp_kind;
      gv->tag_kind = cast_tag_kind;
      gv->tag_name = cast_tag_name;
      gv->tag_len = cast_tag_len;
      if (is_arr) set_gvar_array_strides_from_dims(gv, local_array_dims, local_array_dim_count, base_elem);
      /* 匿名複合リテラルは内部リンケージ (.global を出さない)。`___compound_lit_N` は
       * namespace 対象外 (__ 始まり) なので、.global だと別 fixture とリンク衝突する。 */
      gv->is_static = 1;
      gv->has_init = 1;
      int cap = 16;
      gv->init_values = calloc((size_t)cap, sizeof(long long));
      gv->init_fvalues = calloc((size_t)cap, sizeof(double));  /* fp 要素 (`(double[]){...}`) 用 */
      gv->init_value_symbols = calloc((size_t)cap, sizeof(char *));
      gv->init_value_symbol_lens = calloc((size_t)cap, sizeof(int));
      gv->init_count = 0;
      psx_parse_global_brace_init_flat(gv, &cap, -1);
      psx_register_global_var(gv);
      node_gvar_t *gvar_node = (node_gvar_t *)psx_node_new_gvar_for(gv);
      if (is_arr) {
        /* 配列複合リテラルはポインタへ decay。ND_ADDR で包み subscript / `&` を通す。 */
        node_mem_t *addr = arena_alloc(sizeof(node_mem_t));
        addr->base.kind = ND_ADDR;
        addr->base.lhs = (node_t *)gvar_node;
        addr->tag_kind = gv->tag_kind;
        addr->tag_name = gv->tag_name;
        addr->tag_len = gv->tag_len;
        set_addr_array_strides_from_gvar(addr, gv);
        if (cl_ptr_array_pointee_bytes > 0) {
          addr->ptr_array_pointee_bytes = cl_ptr_array_pointee_bytes;
          addr->base_deref_size = (short)(cast_elem_size > 0 ? cast_elem_size : 8);
        }
        addr->is_pointer = 1;
        if (gv->tag_kind != TK_EOF) addr->is_tag_pointer = 1;
        addr->compound_literal_array_size = var_size;
        return apply_postfix((node_t *)addr, ctx);
      }
      return apply_postfix((node_t *)gvar_node, ctx);
    }
    tk_expect('{');
    node_t *init_expr = psx_expr_assign();
    tk_expect('}');
    /* `&(int){5}` のように `&` のオペランドなら、ND_NUM への短絡 (アドレス取得不能)
     * を避けて下の gvar 実体化経路へ進む。 */
    if (!is_arr && !want_addr && init_expr && init_expr->kind == ND_NUM) {
      return apply_postfix(init_expr, ctx);
    }
    global_var_t *gv = calloc(1, sizeof(global_var_t));
    gv->name = tmp_name;
    gv->name_len = (int)strlen(tmp_name);
    gv->type_size = var_size;
    gv->deref_size = base_elem;
    gv->is_array = is_arr;
    gv->fp_kind = cast_fp_kind;
    gv->is_static = 1;  /* 匿名複合リテラルは内部リンケージ (.global を出さない) */
    if (is_arr) set_gvar_array_strides_from_dims(gv, local_array_dims, local_array_dim_count, base_elem);
    if (init_expr && init_expr->kind == ND_NUM) {
      node_num_t *n = (node_num_t *)init_expr;
      gv->has_init = 1;
      if (cast_fp_kind != TK_FLOAT_KIND_NONE) {
        /* `&(double){2.5}` 等の浮動小数複合リテラル: emit は fp スカラを fval から
         * IEEE-754 で出力する。整数リテラルで書かれていても (`(double){3}`) 宣言型を優先。 */
        gv->fval = (n->base.fp_kind != TK_FLOAT_KIND_NONE) ? n->fval : (double)n->val;
      } else {
        gv->init_val = n->val;
      }
    }
    psx_register_global_var(gv);
    node_gvar_t *gvar_node = (node_gvar_t *)psx_node_new_gvar_for(gv);
    return apply_postfix((node_t *)gvar_node, ctx);
  }
  lvar_t *var = psx_decl_register_lvar_sized(tmp_name, (int)strlen(tmp_name),
                                             var_size, cl_complex_scalar ? var_size : base_elem, is_arr);
  var->tag_kind = cast_tag_kind;
  var->tag_name = cast_tag_name;
  var->tag_len = cast_tag_len;
  if (is_arr) set_lvar_array_strides_from_dims(var, local_array_dims, local_array_dim_count, base_elem);
  if (cl_complex_scalar) var->is_complex = 1;  /* elem_size = var_size (=base_elem*2)、brace-init で half= elem/2 */
  /* 要素がポインタの配列は、ローカル `T *arr[N]` と同じく subscript 後の値を
   * 単段ポインタとして扱えるよう pointer metadata を持たせる。 */
  if (is_pointer_elem_array) {
    var->is_tag_pointer = 0;
    var->pointer_qual_levels = 1;
    var->base_deref_size = (short)(cast_elem_size > 0 ? cast_elem_size : 8);
    var->ptr_array_pointee_bytes = cl_ptr_array_pointee_bytes;
    /* 配列要素はタグ実体ではなくポインタ。初期化 lvar に tag を残すと
     * parse_array_braced_init が `{&a,&b}` を brace 省略 struct 値として扱い、
     * struct 内容を 32bit store してしまう。 */
    var->tag_kind = TK_EOF;
    var->tag_name = NULL;
    var->tag_len = 0;
  } else {
    var->is_tag_pointer = cast_is_ptr ? 1 : 0;
  }
  var->fp_kind = cast_fp_kind;
  node_t *init = psx_decl_parse_initializer_for_var(var, cast_is_ptr);
  node_t *ref;
  if (is_arr) {
    node_mem_t *addr_node = arena_alloc(sizeof(node_mem_t));
    addr_node->base.kind = ND_ADDR;
    addr_node->base.lhs = psx_node_new_lvar_for(var);
    addr_node->tag_kind = is_pointer_elem_array ? cast_tag_kind : var->tag_kind;
    addr_node->tag_name = is_pointer_elem_array ? cast_tag_name : var->tag_name;
    addr_node->tag_len = is_pointer_elem_array ? cast_tag_len : var->tag_len;
    set_addr_array_strides_from_lvar(addr_node, var);
    /* `(int[N]){...}` 複合リテラルは配列名と同じくポインタへ崩壊する。
     * 後続の `[i]` サブスクリプトを通すために is_pointer を立てる。 */
    addr_node->is_pointer = 1;
    if (addr_node->tag_kind != TK_EOF) addr_node->is_tag_pointer = 1;
    addr_node->compound_literal_array_size = var_size;
    ref = (node_t *)addr_node;
  } else {
    ref = new_typed_lvar_ref(var, cast_is_ptr);
  }
  (void)cast_kind;
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

static int parse_cast_type(token_t *tok, token_kind_t *type_kind, int *is_pointer, token_t **after_rparen,
                           token_kind_t *out_tag_kind, char **out_tag_name, int *out_tag_len,
                           int *out_elem_size, tk_float_kind_t *out_fp_kind, int *out_array_count,
                           int *out_array_dims, int *out_array_dim_count, int *out_is_unsigned,
                           int *out_is_long_long, int *out_is_plain_char,
                           int *out_is_complex, int *out_ptr_array_pointee_bytes) {
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
  if (out_is_complex) *out_is_complex = 0;
  if (out_ptr_array_pointee_bytes) *out_ptr_array_pointee_bytes = 0;

  consume_local_type_quals(&t);
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
    token_t *q = t->next->next;
    token_kind_t inner_kind = TK_EOF;
    token_kind_t inner_tag_kind = TK_EOF;
    char *inner_tag_name = NULL;
    int inner_tag_len = 0;
    int inner_ptr = 0;
    int inner_elem = 8;
    tk_float_kind_t inner_fp = TK_FLOAT_KIND_NONE;
    int nested_atomic_wrappers = 0;
    while (q && q->kind == TK_ATOMIC && q->next && q->next->kind == TK_LPAREN) {
      nested_atomic_wrappers++;
      q = q->next->next;
      consume_local_type_quals(&q);
    }
    if (q && q->kind == TK_ATOMIC && !(q->next && q->next->kind == TK_LPAREN)) q = q->next;
    consume_local_type_quals(&q);

    bool inner_is_type = false;
    bool q_is_type = false;
    if (q) psx_ctx_get_type_info(q->kind, &q_is_type, NULL);
    if (q_is_type) {
      inner_is_type = true;
      if (q->kind == TK_LONG && q->next && q->next->kind == TK_DOUBLE) {
        inner_kind = TK_DOUBLE;
        inner_elem = 8;
        inner_fp = TK_FLOAT_KIND_DOUBLE;
        q = q->next->next;
      } else if (parse_integer_cast_spec_sequence(q, &inner_kind, &inner_elem, NULL, &q,
                                                  out_is_long_long, out_is_plain_char)) {
        inner_fp = TK_FLOAT_KIND_NONE;
      } else {
        inner_kind = q->kind;
        psx_ctx_get_type_info(inner_kind, NULL, &inner_elem);
        if (inner_kind == TK_FLOAT) inner_fp = TK_FLOAT_KIND_FLOAT;
        else if (inner_kind == TK_DOUBLE) inner_fp = TK_FLOAT_KIND_DOUBLE;
        q = q->next;
      }
    } else if (q && psx_ctx_is_tag_keyword(q->kind)) {
      token_kind_t tag_kind = q->kind;
      q = q->next;
      token_ident_t *tag = (token_ident_t *)q;
      if (!q || q->kind != TK_IDENT) return 0;
      if (!psx_ctx_has_tag_type(tag_kind, tag->str, tag->len)) {
        psx_diag_undefined_with_name(q, diag_text_for(DIAG_TEXT_TAG_TYPE_SUFFIX), tag->str, tag->len);
      }
      inner_kind = tag_kind;
      inner_tag_kind = tag_kind;
      inner_tag_name = tag->str;
      inner_tag_len = tag->len;
      inner_elem = psx_ctx_get_tag_size(tag_kind, tag->str, tag->len);
      inner_is_type = true;
      q = q->next;
    } else if (q && psx_ctx_is_typedef_name_token(q)) {
      token_ident_t *id = (token_ident_t *)q;
      token_kind_t td_base = TK_EOF;
      int td_elem = 8;
      tk_float_kind_t td_fp = TK_FLOAT_KIND_NONE;
      token_kind_t td_tag = TK_EOF;
      char *td_tag_name = NULL;
      int td_tag_len = 0;
      int td_ptr = 0;
      psx_typedef_info_t _ti;
      if (psx_ctx_find_typedef_name(id->str, id->len, &_ti)) {
        td_base = _ti.base_kind; td_elem = _ti.elem_size; td_fp = _ti.fp_kind;
        td_tag = _ti.tag_kind; td_tag_name = _ti.tag_name; td_tag_len = _ti.tag_len;
        td_ptr = _ti.is_pointer;
      }
      inner_kind = (td_tag != TK_EOF) ? td_tag : td_base;
      inner_tag_kind = td_tag;
      inner_tag_name = td_tag_name;
      inner_tag_len = td_tag_len;
      inner_elem = td_elem;
      inner_fp = td_fp;
      inner_ptr = td_ptr;
      inner_is_type = true;
      q = q->next;
    }
    if (!inner_is_type) return 0;
    consume_cast_pointer_suffix(&q, &inner_ptr);
    while (nested_atomic_wrappers-- > 0) {
      if (!q || q->kind != TK_RPAREN) return 0;
      q = q->next;
    }
    if (!q || q->kind != TK_RPAREN) return 0;
    *type_kind = inner_kind;
    *is_pointer = inner_ptr;
    if (out_tag_kind) *out_tag_kind = inner_tag_kind;
    if (out_tag_name) *out_tag_name = inner_tag_name;
    if (out_tag_len) *out_tag_len = inner_tag_len;
    if (out_elem_size) *out_elem_size = inner_elem;
    if (out_fp_kind) *out_fp_kind = inner_fp;
    t = q->next;
    goto cast_parse_postfix;
  }

  bool is_type = false;
  psx_ctx_get_type_info(t->kind, &is_type, NULL);
  // Minimal support for C11 complex/imaginary cast spellings:
  //   (_Complex float), (_Imaginary double), (double _Complex), ...
  if (t->kind == TK_COMPLEX || t->kind == TK_IMAGINARY) {
    if (out_is_complex) *out_is_complex = 1;
    token_t *q = t->next;
    if (q && q->kind == TK_LONG && q->next && q->next->kind == TK_DOUBLE) {
      *type_kind = TK_DOUBLE;
      if (out_elem_size) *out_elem_size = 8;
      if (out_fp_kind) *out_fp_kind = TK_FLOAT_KIND_DOUBLE;
      t = q->next->next;
      is_type = true;
    } else if (q && (q->kind == TK_FLOAT || q->kind == TK_DOUBLE)) {
      *type_kind = q->kind;
      if (out_elem_size) psx_ctx_get_type_info(*type_kind, NULL, out_elem_size);
      if (out_fp_kind) {
        if (*type_kind == TK_FLOAT) *out_fp_kind = TK_FLOAT_KIND_FLOAT;
        else *out_fp_kind = TK_FLOAT_KIND_DOUBLE;
      }
      t = q->next;
      is_type = true;
    } else {
      diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, t,
                     "%s",
                     diag_message_for(DIAG_ERR_PARSER_COMPLEX_IMAGINARY_CAST_REQUIRES_FLOAT));
    }
  } else if ((t->kind == TK_FLOAT || t->kind == TK_DOUBLE || t->kind == TK_LONG) &&
             t->next && (t->next->kind == TK_COMPLEX || t->next->kind == TK_IMAGINARY)) {
    if (out_is_complex) *out_is_complex = 1;
    if (t->kind == TK_LONG) {
      if (!t->next || t->next->kind != TK_DOUBLE || !t->next->next ||
          (t->next->next->kind != TK_COMPLEX && t->next->next->kind != TK_IMAGINARY)) {
        diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, t,
                       "%s",
                       diag_message_for(DIAG_ERR_PARSER_COMPLEX_IMAGINARY_CAST_REQUIRES_FLOAT));
      }
      *type_kind = TK_DOUBLE;
      if (out_elem_size) *out_elem_size = 8;
      if (out_fp_kind) *out_fp_kind = TK_FLOAT_KIND_DOUBLE;
      t = t->next->next->next;
    } else {
      *type_kind = t->kind;
      if (out_elem_size) psx_ctx_get_type_info(*type_kind, NULL, out_elem_size);
      if (out_fp_kind) {
        if (*type_kind == TK_FLOAT) *out_fp_kind = TK_FLOAT_KIND_FLOAT;
        else *out_fp_kind = TK_FLOAT_KIND_DOUBLE;
      }
      t = t->next->next;
    }
    is_type = true;
  }
  if (t->kind == TK_LONG && t->next && t->next->kind == TK_DOUBLE) {
    *type_kind = TK_DOUBLE;
    if (out_elem_size) *out_elem_size = 8;
    if (out_fp_kind) *out_fp_kind = TK_FLOAT_KIND_DOUBLE;
    t = t->next->next;
    is_type = true;
  }
  if (is_type) {
    if (*type_kind == TK_EOF) {
      if (parse_integer_cast_spec_sequence(t, type_kind, out_elem_size, out_is_unsigned, &t,
                                           out_is_long_long, out_is_plain_char)) {
        if (out_fp_kind) *out_fp_kind = TK_FLOAT_KIND_NONE;
      } else {
        *type_kind = t->kind;
        if (out_elem_size) psx_ctx_get_type_info(*type_kind, NULL, out_elem_size);
        if (out_fp_kind) {
          if (*type_kind == TK_FLOAT) *out_fp_kind = TK_FLOAT_KIND_FLOAT;
          else if (*type_kind == TK_DOUBLE) *out_fp_kind = TK_FLOAT_KIND_DOUBLE;
        }
        t = t->next;
      }
    }
  } else if (psx_ctx_is_tag_keyword(t->kind)) {
    token_kind_t tag_kind = t->kind;
    t = t->next;
    token_ident_t *tag = (token_ident_t *)t;
    if (!t || t->kind != TK_IDENT) return 0;
    if (!psx_ctx_has_tag_type(tag_kind, tag->str, tag->len)) {
      psx_diag_undefined_with_name(t, diag_text_for(DIAG_TEXT_TAG_TYPE_SUFFIX), tag->str, tag->len);
    }
    *type_kind = tag_kind;
    if (out_tag_kind) *out_tag_kind = tag_kind;
    if (out_tag_name) *out_tag_name = tag->str;
    if (out_tag_len) *out_tag_len = tag->len;
    if (out_elem_size) *out_elem_size = psx_ctx_get_tag_size(tag_kind, tag->str, tag->len);
    t = t->next;
  } else if (psx_ctx_is_typedef_name_token(t)) {
    token_ident_t *id = (token_ident_t *)t;
    token_kind_t td_base = TK_EOF;
    int td_elem = 8;
    tk_float_kind_t td_fp = TK_FLOAT_KIND_NONE;
    token_kind_t td_tag = TK_EOF;
    char *td_tag_name = NULL;
    int td_tag_len = 0;
    int td_ptr = 0;
    psx_typedef_info_t _ti = {0};
    if (psx_ctx_find_typedef_name(id->str, id->len, &_ti)) {
      td_base = _ti.base_kind; td_elem = _ti.elem_size; td_fp = _ti.fp_kind;
      td_tag = _ti.tag_kind; td_tag_name = _ti.tag_name; td_tag_len = _ti.tag_len;
      td_ptr = _ti.is_pointer;
      if (out_is_unsigned) *out_is_unsigned = _ti.is_unsigned;
      if (_ti.is_array && _ti.array_dim_count > 0) {
        int total = 1;
        int dim_count = _ti.array_dim_count > 8 ? 8 : _ti.array_dim_count;
        for (int i = 0; i < dim_count; i++) {
          if (out_array_dims) out_array_dims[i] = _ti.array_dims[i];
          if (_ti.array_dims[i] > 0) total *= _ti.array_dims[i];
        }
        if (out_array_count) *out_array_count = total;
        if (out_array_dim_count) *out_array_dim_count = dim_count;
      }
    }
    *type_kind = (td_tag != TK_EOF) ? td_tag : td_base;
    if (out_tag_kind) *out_tag_kind = td_tag;
    if (out_tag_name) *out_tag_name = td_tag_name;
    if (out_tag_len) *out_tag_len = td_tag_len;
    if (out_elem_size) *out_elem_size = td_elem;
    if (out_fp_kind) *out_fp_kind = td_fp;
    *is_pointer = td_ptr;
    t = t->next;
  } else {
    return 0;
  }

cast_parse_postfix:
  if (*is_pointer != 1) *is_pointer = 0;
  consume_cast_pointer_suffix(&t, is_pointer);
  parse_funcptr_abstract_decl(&t, is_pointer);
  (void)parse_ptr_to_array_abstract_decl(&t, is_pointer);
  /* `(int (*[N])(args)){...}` のような関数ポインタ配列 compound literal の
   * 配列サイズ N を out_array_count に保存。修正前は NULL で破棄され、
   * compound literal 経路がスカラ初期化子と誤認していた (p304)。 */
  {
    int fp_array_mul = 0;
    if (parse_array_of_funcptr_abstract_decl(&t, &fp_array_mul)) {
      if (fp_array_mul > 0 && out_array_count) {
        *out_array_count = fp_array_mul;
      }
    }
  }
  {
    int ptr_array_mul = 0;
    int ptr_array_pointee_mul = 0;
    if (parse_array_of_ptr_to_array_abstract_decl_ex(&t, &ptr_array_mul, &ptr_array_pointee_mul)) {
      if (ptr_array_mul > 0 && out_array_count) *out_array_count = ptr_array_mul;
      if (ptr_array_mul > 0 && out_array_dims) out_array_dims[0] = ptr_array_mul;
      if (ptr_array_mul > 0 && out_array_dim_count) *out_array_dim_count = 1;
      if (ptr_array_pointee_mul > 0 && out_elem_size) {
        if (out_ptr_array_pointee_bytes) {
          *out_ptr_array_pointee_bytes = ptr_array_pointee_mul * (*out_elem_size);
        }
      }
      *is_pointer = 1;
    }
  }
  (void)parse_array_of_ptr_to_array_of_ptr_abstract_decl(&t, NULL);
  (void)parse_ptr_to_func_returning_ptr_to_array_abstract_decl(&t);
  (void)parse_array_of_ptr_to_func_returning_ptr_to_array_abstract_decl(&t, NULL);
  (void)parse_ptr_to_func_returning_ptr_to_func_abstract_decl(&t);
  (void)parse_ptr_to_func_returning_ptr_to_func_returning_ptr_to_array_abstract_decl(&t);
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
  }
  if (!t || t->kind != TK_RPAREN || !t->next) return 0;
  *after_rparen = t->next;
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

static int is_same_tag_nonscalar_expr(node_t *expr, token_kind_t cast_kind, char *cast_tag_name, int cast_tag_len) {
  if (!expr) return 0;
  node_t *v = expr;
  while (v && v->kind == ND_COMMA) v = v->rhs;
  if (!v) return 0;
  if (v->kind == ND_TERNARY) {
    node_ctrl_t *t = (node_ctrl_t *)v;
    return is_same_tag_nonscalar_expr(t->base.rhs, cast_kind, cast_tag_name, cast_tag_len) &&
           is_same_tag_nonscalar_expr(t->els, cast_kind, cast_tag_name, cast_tag_len);
  }
  token_kind_t op_tag_kind = TK_EOF;
  char *op_tag_name = NULL;
  int op_tag_len = 0;
  int op_is_tag_ptr = 0;
  psx_node_get_tag_type(v, &op_tag_kind, &op_tag_name, &op_tag_len, &op_is_tag_ptr);
  return !op_is_tag_ptr && op_tag_kind == cast_kind && op_tag_len == cast_tag_len &&
         strncmp(op_tag_name ? op_tag_name : "", cast_tag_name ? cast_tag_name : "", (size_t)cast_tag_len) == 0;
}

static int is_size_compatible_nonscalar_expr(node_t *expr, token_kind_t cast_kind, int cast_elem_size) {
  if (!expr) return 0;
  node_t *v = expr;
  while (v && v->kind == ND_COMMA) v = v->rhs;
  if (!v) return 0;
  if (v->kind == ND_TERNARY) {
    node_ctrl_t *t = (node_ctrl_t *)v;
    return is_size_compatible_nonscalar_expr(t->base.rhs, cast_kind, cast_elem_size) &&
           is_size_compatible_nonscalar_expr(t->els, cast_kind, cast_elem_size);
  }
  token_kind_t op_tag_kind = TK_EOF;
  char *op_tag_name = NULL;
  int op_tag_len = 0;
  int op_is_tag_ptr = 0;
  psx_node_get_tag_type(v, &op_tag_kind, &op_tag_name, &op_tag_len, &op_is_tag_ptr);
  if (op_is_tag_ptr || op_tag_kind != cast_kind) return 0;
  int op_sz = ps_node_type_size(v);
  return op_sz > 0 && cast_elem_size > 0 && op_sz == cast_elem_size;
}

static char *new_compound_lit_name(void) {
  int n = compound_lit_seq++;
  int len = snprintf(NULL, 0, "__compound_lit_%d", n);
  char *name = calloc((size_t)len + 1, 1);
  snprintf(name, (size_t)len + 1, "__compound_lit_%d", n);
  return name;
}

static void set_lvar_array_strides_from_dims(lvar_t *var, const int *dims, int dim_count, int elem_size) {
  if (!var || !dims || dim_count < 2 || elem_size <= 0) return;
  int outer_mul = 1;
  for (int i = 1; i < dim_count; i++) {
    if (dims[i] > 0) outer_mul *= dims[i];
  }
  var->outer_stride = outer_mul * elem_size;
  if (dim_count >= 3) {
    int mid_mul = 1;
    for (int i = 2; i < dim_count; i++) {
      if (dims[i] > 0) mid_mul *= dims[i];
    }
    var->mid_stride = mid_mul * elem_size;
  }
  if (dim_count >= 4) {
    var->extra_strides = calloc(5, sizeof(int));
    int idx = 0;
    for (int start = 3; start < dim_count && idx < 5; start++) {
      int rest_mul = 1;
      for (int j = start; j < dim_count; j++) {
        if (dims[j] > 0) rest_mul *= dims[j];
      }
      var->extra_strides[idx++] = rest_mul * elem_size;
    }
    var->extra_strides_count = (unsigned char)idx;
  }
}

static void set_gvar_array_strides_from_dims(global_var_t *gv, const int *dims, int dim_count, int elem_size) {
  if (!gv || !dims || dim_count < 2 || elem_size <= 0) return;
  int outer_mul = 1;
  for (int i = 1; i < dim_count; i++) {
    if (dims[i] > 0) outer_mul *= dims[i];
  }
  gv->outer_stride = outer_mul * elem_size;
  if (dim_count >= 3) {
    int mid_mul = 1;
    for (int i = 2; i < dim_count; i++) {
      if (dims[i] > 0) mid_mul *= dims[i];
    }
    gv->mid_stride = mid_mul * elem_size;
  }
  if (dim_count >= 4) {
    int idx = 0;
    for (int start = 3; start < dim_count && idx < 5; start++) {
      int rest_mul = 1;
      for (int j = start; j < dim_count; j++) {
        if (dims[j] > 0) rest_mul *= dims[j];
      }
      gv->extra_strides[idx++] = rest_mul * elem_size;
    }
    gv->extra_strides_count = (unsigned char)idx;
  }
}

static void set_addr_array_strides_from_lvar(node_mem_t *addr, const lvar_t *var) {
  if (!addr || !var) return;
  int stride = (var->outer_stride > 0) ? var->outer_stride : var->elem_size;
  addr->type_size = stride;
  addr->deref_size = stride;
  addr->ptr_array_pointee_bytes = var->ptr_array_pointee_bytes;
  addr->pointer_qual_levels = var->pointer_qual_levels;
  addr->base_deref_size = var->base_deref_size;
  if (var->outer_stride > 0) {
    if (var->mid_stride > 0) {
      addr->inner_deref_size = (short)var->mid_stride;
      if (var->extra_strides_count > 0) {
        addr->next_deref_size = (short)var->extra_strides[0];
        for (int i = 1; i < var->extra_strides_count && (i - 1) < 5; i++) {
          addr->extra_strides[i - 1] = var->extra_strides[i];
        }
        addr->extra_strides[var->extra_strides_count - 1] = var->elem_size;
        addr->extra_strides_count = var->extra_strides_count;
      } else {
        addr->next_deref_size = (short)var->elem_size;
      }
    } else {
      addr->inner_deref_size = (short)var->elem_size;
    }
  }
}

static void set_addr_array_strides_from_gvar(node_mem_t *addr, const global_var_t *gv) {
  if (!addr || !gv) return;
  int stride = (gv->outer_stride > 0) ? gv->outer_stride : gv->deref_size;
  addr->type_size = stride;
  addr->deref_size = stride;
  if (gv->outer_stride > 0) {
    if (gv->mid_stride > 0) {
      addr->inner_deref_size = (short)gv->mid_stride;
      if (gv->extra_strides_count > 0) {
        addr->next_deref_size = (short)gv->extra_strides[0];
        for (int i = 1; i < gv->extra_strides_count && (i - 1) < 5; i++) {
          addr->extra_strides[i - 1] = gv->extra_strides[i];
        }
        addr->extra_strides[gv->extra_strides_count - 1] = gv->deref_size;
        addr->extra_strides_count = gv->extra_strides_count;
      } else {
        addr->next_deref_size = (short)gv->deref_size;
      }
    } else {
      addr->inner_deref_size = (short)gv->deref_size;
    }
  }
}

static node_t *new_typed_lvar_ref(lvar_t *var, int is_pointer) {
  node_t *ref = psx_node_new_lvar_typed_for(var, is_pointer ? 8 : var->elem_size);
  as_lvar(ref)->mem.deref_size = var->elem_size;
  as_lvar(ref)->mem.is_pointer = is_pointer;
  return ref;
}

static node_t *new_member_lvar_ref(lvar_t *owner, int member_offset, int member_type_size,
                                   token_kind_t member_tag_kind, char *member_tag_name,
                                   int member_tag_len, int member_is_tag_pointer) {
  node_t *lvar = psx_node_new_lvar_typed(owner->offset + member_offset, member_type_size);
  ((node_lvar_t *)lvar)->var = owner;
  as_lvar(lvar)->mem.tag_kind = member_tag_kind;
  as_lvar(lvar)->mem.tag_name = member_tag_name;
  as_lvar(lvar)->mem.tag_len = member_tag_len;
  as_lvar(lvar)->mem.is_tag_pointer = member_is_tag_pointer;
  return lvar;
}

static node_t *lower_union_value_cast(node_t *operand,
                                      token_kind_t cast_tag_kind, char *cast_tag_name, int cast_tag_len,
                                      int cast_elem_size, tk_float_kind_t cast_fp_kind) {
  int base_elem = cast_elem_size > 0 ? cast_elem_size : 8;
  char *tmp_name = new_compound_lit_name();
  lvar_t *var = psx_decl_register_lvar_sized(tmp_name, (int)strlen(tmp_name), base_elem, base_elem, 0);
  var->tag_kind = cast_tag_kind;
  var->tag_name = cast_tag_name;
  var->tag_len = cast_tag_len;
  var->is_tag_pointer = 0;
  var->fp_kind = cast_fp_kind;

  tag_member_info_t info = {0};
  int member_count = psx_ctx_get_tag_member_count(cast_tag_kind, cast_tag_name, cast_tag_len);
  bool found = false;
  for (int ordinal = 0; ordinal < member_count; ordinal++) {
    found = psx_ctx_get_tag_member_info(cast_tag_kind, cast_tag_name, cast_tag_len, ordinal, &info);
    if (!found) break;
    if (info.len > 0) break;
  }
  if (!found || info.len <= 0) {
    psx_diag_ctx(curtok(), "cast", "%s",
                 diag_message_for(DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
  }

  node_t *lhs = new_member_lvar_ref(var, info.offset, info.type_size,
                                    info.tag_kind, info.tag_name, info.tag_len, info.is_tag_pointer);
  node_mem_t *assign_node = psx_node_new_assign(lhs, operand);
  assign_node->type_size = info.type_size;

  node_t *ref = new_typed_lvar_ref(var, 0);
  return psx_node_new_binary(ND_COMMA, (node_t *)assign_node, ref);
}

static node_t *lower_struct_value_cast(node_t *operand,
                                       token_kind_t cast_tag_kind, char *cast_tag_name, int cast_tag_len,
                                       int cast_elem_size, tk_float_kind_t cast_fp_kind) {
  int base_elem = cast_elem_size > 0 ? cast_elem_size : 8;
  char *tmp_name = new_compound_lit_name();
  lvar_t *var = psx_decl_register_lvar_sized(tmp_name, (int)strlen(tmp_name), base_elem, base_elem, 0);
  var->tag_kind = cast_tag_kind;
  var->tag_name = cast_tag_name;
  var->tag_len = cast_tag_len;
  var->is_tag_pointer = 0;
  var->fp_kind = cast_fp_kind;

  tag_member_info_t info = {0};
  int member_count = psx_ctx_get_tag_member_count(cast_tag_kind, cast_tag_name, cast_tag_len);
  bool found = false;
  for (int ordinal = 0; ordinal < member_count; ordinal++) {
    found = psx_ctx_get_tag_member_info(cast_tag_kind, cast_tag_name, cast_tag_len, ordinal, &info);
    if (!found) break;
    if (info.len > 0) break;
  }
  if (!found || info.len <= 0) {
    psx_diag_ctx(curtok(), "cast", "%s",
                 diag_message_for(DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
  }

  node_t *lhs = new_member_lvar_ref(var, info.offset, info.type_size,
                                    info.tag_kind, info.tag_name, info.tag_len, info.is_tag_pointer);
  node_mem_t *assign_node = psx_node_new_assign(lhs, operand);
  assign_node->type_size = info.type_size;

  node_t *ref = new_typed_lvar_ref(var, 0);
  return psx_node_new_binary(ND_COMMA, (node_t *)assign_node, ref);
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
  if (parse_ptr_to_func_returning_ptr_to_array_abstract_decl(&t)) {
    sz = 8;
  }
  if (parse_array_of_ptr_to_func_returning_ptr_to_array_abstract_decl(&t, &fp_array_mul)) {
    sz = 8 * fp_array_mul;
  }
  if (parse_ptr_to_func_returning_ptr_to_func_abstract_decl(&t)) {
    sz = 8;
  }
  if (parse_ptr_to_func_returning_ptr_to_func_returning_ptr_to_array_abstract_decl(&t)) {
    sz = 8;
  }
  if (parse_funcptr_abstract_decl(&t, &fp_ptr)) {
    sz = 8;
  }
  if (parse_ptr_to_array_abstract_decl(&t, &fp_ptr)) {
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
  if (t->kind == TK_STRUCT || t->kind == TK_UNION) {
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
    comma->fp_kind = rhs ? rhs->fp_kind : TK_FLOAT_KIND_NONE;
    node = comma;
  }
  leave_expr_nest(ctx);
  return node;
}

static psx_type_t *expr_type_new_void(void) {
  psx_type_t *type = psx_type_new(PSX_TYPE_VOID);
  type->scalar_kind = TK_VOID;
  return type;
}

static int expr_integer_type_size(token_kind_t kind, int fallback_size) {
  switch (kind) {
    case TK_BOOL:
    case TK_CHAR:
      return 1;
    case TK_SHORT:
      return 2;
    case TK_LONG:
      return 8;
    case TK_INT:
    case TK_SIGNED:
    case TK_UNSIGNED:
    case TK_ENUM:
      return 4;
    default:
      return fallback_size > 0 ? fallback_size : 4;
  }
}

static psx_type_t *expr_cast_target_type(token_kind_t type_kind, int is_pointer,
                                         token_kind_t cast_tag_kind, char *cast_tag_name,
                                         int cast_tag_len, int cast_elem_size,
                                         tk_float_kind_t cast_fp_kind,
                                         int cast_is_unsigned, int cast_is_long_long,
                                         int cast_is_plain_char, int cast_is_complex) {
  psx_type_t *base = NULL;
  if (cast_tag_kind == TK_STRUCT || cast_tag_kind == TK_UNION) {
    base = psx_type_new_tag(cast_tag_kind, cast_tag_name, cast_tag_len, 0, cast_elem_size);
  } else if (type_kind == TK_VOID) {
    base = expr_type_new_void();
  } else if (cast_is_complex) {
    base = psx_type_new(PSX_TYPE_COMPLEX);
    base->fp_kind = cast_fp_kind;
    int elem_size = cast_elem_size > 0 ? cast_elem_size
                  : cast_fp_kind == TK_FLOAT_KIND_FLOAT ? 4 : 8;
    base->size = elem_size * 2;
    base->align = base->size >= 8 ? 8 : 4;
  } else if (cast_fp_kind != TK_FLOAT_KIND_NONE || type_kind == TK_FLOAT || type_kind == TK_DOUBLE) {
    tk_float_kind_t fp = cast_fp_kind != TK_FLOAT_KIND_NONE ? cast_fp_kind
                       : type_kind == TK_FLOAT ? TK_FLOAT_KIND_FLOAT
                       : TK_FLOAT_KIND_DOUBLE;
    base = psx_type_new_float(fp, fp == TK_FLOAT_KIND_FLOAT ? 4 : 8);
  } else {
    int sz = expr_integer_type_size(type_kind, cast_elem_size);
    token_kind_t scalar = type_kind == TK_SIGNED ? TK_INT : type_kind;
    base = psx_type_new_integer(scalar, sz, cast_is_unsigned);
    base->is_long_long = cast_is_long_long ? 1 : 0;
    base->is_plain_char = cast_is_plain_char ? 1 : 0;
  }

  if (!is_pointer) return base;

  int deref_size = cast_elem_size > 0 ? cast_elem_size : psx_type_sizeof(base);
  psx_type_t *ptr = psx_type_new_pointer(base, deref_size);
  ptr->pointer_qual_levels = 1;
  ptr->base_deref_size = psx_type_sizeof(base);
  return ptr;
}

static node_t *annotate_cast_type(node_t *node, psx_type_t *type) {
  if (node && type) node->type = type;
  return node;
}

// 浮動小数式を整数へ変換するため ND_FP_TO_INT でラップ。fp_kind==NONE なら no-op。
// `(int)d`/`(char)d`/`(long)d` 等で codegen に fcvtzs を出させるために使う。
static node_t *wrap_fp_to_int_if_needed(node_t *operand) {
  if (!operand || operand->fp_kind == TK_FLOAT_KIND_NONE) return operand;
  node_mem_t *mem = arena_alloc(sizeof(node_mem_t));
  node_t *cvt = &mem->base;
  cvt->kind = ND_FP_TO_INT;
  cvt->lhs = operand;
  cvt->fp_kind = TK_FLOAT_KIND_NONE;
  mem->type_size = 4;
  return annotate_cast_type(cvt, psx_type_new_integer(TK_INT, 4, 0));
}

static node_t *wrap_fp_to_int_width(node_t *operand, int width) {
  if (!operand || operand->fp_kind == TK_FLOAT_KIND_NONE) return operand;
  node_mem_t *mem = arena_alloc(sizeof(node_mem_t));
  node_t *cvt = &mem->base;
  cvt->kind = ND_FP_TO_INT;
  cvt->lhs = operand;
  cvt->fp_kind = TK_FLOAT_KIND_NONE;
  mem->type_size = (width == 8) ? 8 : 4;
  return annotate_cast_type(cvt, psx_type_new_integer(TK_INT, mem->type_size, 0));
}

/* `(float)x` / `(double)x` キャスト。operand が目的のFP型と異なる (整数、または
 * float↔double の別幅) 場合に ND_INT_TO_FP でラップし、codegen が I2F/F2F 変換を
 * 発行できるようにする。同じFP型なら no-op で素通りさせる。 */
static node_t *wrap_to_fp(node_t *operand, tk_float_kind_t target) {
  if (!operand) return operand;
  psx_type_t *target_type = psx_type_new_float(target, target == TK_FLOAT_KIND_FLOAT ? 4 : 8);
  if (operand->fp_kind == target) return annotate_cast_type(operand, target_type);
  // float(4) と double/long double(8) は同幅グループで判定する。
  bool op_is_double = operand->fp_kind >= TK_FLOAT_KIND_DOUBLE;
  bool tgt_is_double = target >= TK_FLOAT_KIND_DOUBLE;
  if (operand->fp_kind != TK_FLOAT_KIND_NONE && op_is_double == tgt_is_double) {
    operand->fp_kind = target;
    return annotate_cast_type(operand, target_type);
  }
  node_t *cvt = arena_alloc(sizeof(node_t));
  cvt->kind = ND_INT_TO_FP;
  cvt->lhs = operand;
  cvt->fp_kind = target;
  return annotate_cast_type(cvt, target_type);
}

static node_t *wrap_i64_to_i32_trunc_cast(node_t *operand, psx_type_t *cast_type,
                                          int target_unsigned) {
  node_t *trunc = psx_node_new_shift_trunc_extend(operand, 32, target_unsigned);

  node_mem_t *wrap = arena_alloc(sizeof(node_mem_t));
  wrap->base.kind = ND_CAST;
  wrap->base.lhs = trunc;
  wrap->type_size = 4;
  wrap->is_unsigned = target_unsigned ? 1 : 0;
  return annotate_cast_type((node_t *)wrap, cast_type);
}

static node_t *wrap_integer_cast_result(node_t *operand, psx_type_t *cast_type,
                                        int type_size, int target_unsigned,
                                        int target_long_long) {
  node_mem_t *wrap = arena_alloc(sizeof(node_mem_t));
  wrap->base.kind = ND_CAST;
  wrap->base.lhs = operand;
  wrap->type_size = (short)type_size;
  wrap->is_unsigned = target_unsigned ? 1 : 0;
  wrap->is_long_long = target_long_long ? 1 : 0;
  return annotate_cast_type((node_t *)wrap, cast_type);
}

static node_t *wrap_pointer_cast_result(node_t *operand, psx_type_t *cast_type,
                                        token_kind_t type_kind,
                                        token_kind_t cast_tag_kind,
                                        char *cast_tag_name, int cast_tag_len,
                                        int cast_elem_size,
                                        int cast_is_unsigned) {
  node_mem_t *wrap = arena_alloc(sizeof(node_mem_t));
  wrap->base.kind = ND_CAST;
  wrap->base.lhs = operand;
  wrap->is_pointer = 1;
  wrap->pointer_qual_levels = 1;
  wrap->type_size = 8;
  if (cast_elem_size > 0) wrap->deref_size = (short)cast_elem_size;
  if (type_kind == TK_VOID) {
    wrap->pointee_is_void = 1;
  } else if (cast_tag_kind == TK_STRUCT || cast_tag_kind == TK_UNION) {
    wrap->tag_kind = cast_tag_kind;
    wrap->tag_name = cast_tag_name;
    wrap->tag_len = cast_tag_len;
    wrap->is_tag_pointer = 1;
  } else if (type_kind == TK_FLOAT) {
    wrap->pointee_fp_kind = TK_FLOAT_KIND_FLOAT;
    if (wrap->deref_size <= 0) wrap->deref_size = 4;
  } else if (type_kind == TK_DOUBLE) {
    wrap->pointee_fp_kind = TK_FLOAT_KIND_DOUBLE;
    if (wrap->deref_size <= 0) wrap->deref_size = 8;
  } else if (cast_is_unsigned || type_kind == TK_UNSIGNED) {
    wrap->pointee_is_unsigned = 1;
  } else if (type_kind == TK_BOOL) {
    wrap->pointee_is_bool = 1;
  }
  return annotate_cast_type((node_t *)wrap, cast_type);
}

static node_t *wrap_void_cast_result(node_t *operand, psx_type_t *cast_type) {
  node_mem_t *wrap = arena_alloc(sizeof(node_mem_t));
  wrap->base.kind = ND_CAST;
  wrap->base.lhs = operand;
  wrap->type_size = 0;
  return annotate_cast_type((node_t *)wrap, cast_type);
}

static node_t *apply_cast(token_kind_t type_kind, int is_pointer, node_t *operand,
                          token_kind_t cast_tag_kind, char *cast_tag_name, int cast_tag_len,
                          int cast_elem_size, tk_float_kind_t cast_fp_kind,
                          int cast_is_unsigned, int cast_is_long_long, int cast_is_plain_char,
                          int cast_is_complex) {
  /* 浮動小数点定数 ND_NUM をスカラ整数型へキャストするのは定数畳み込みできる。
   * wrap_fp_to_int_if_needed 経由で ND_FP_TO_INT に変換してしまうと「定数」ではなく
   * なり、グローバル初期化 (`int g = (int)3.7;`) の has_init が立たず `.comm` に
   * 落ちて 0 に化けていた。型ごとの切り詰めは下流の per-kind 分岐に任せ、ここでは
   * fp_kind=NONE の整数 ND_NUM に正規化するだけにする。 */
  if (operand && operand->kind == ND_NUM &&
      operand->fp_kind != TK_FLOAT_KIND_NONE && !is_pointer &&
      (type_kind == TK_INT || type_kind == TK_LONG || type_kind == TK_SHORT ||
       type_kind == TK_CHAR || type_kind == TK_ENUM ||
       type_kind == TK_SIGNED || type_kind == TK_UNSIGNED || type_kind == TK_BOOL)) {
    double f = ((node_num_t *)operand)->fval;
    operand = psx_node_new_num((long long)f);
  }
  psx_type_t *cast_type = expr_cast_target_type(type_kind, is_pointer,
                                                cast_tag_kind, cast_tag_name, cast_tag_len,
                                                cast_elem_size, cast_fp_kind,
                                                cast_is_unsigned, cast_is_long_long,
                                                cast_is_plain_char, cast_is_complex);
  if (is_pointer || type_kind == TK_LONG) {
    operand = wrap_fp_to_int_if_needed(operand);
    operand->fp_kind = TK_FLOAT_KIND_NONE;
    if (!is_pointer && type_kind == TK_LONG) {
      if (operand->kind == ND_NUM) {
        ((node_num_t *)operand)->int_is_long = 1;
        ((node_num_t *)operand)->int_is_long_long = cast_is_long_long ? 1 : 0;
        psx_node_set_unsigned(operand, cast_is_unsigned);
        return annotate_cast_type(operand, cast_type);
      }
      /* `(long)unsigned_int` (int 未満幅の unsigned も含む): I64 へ zero-extend する。
       * `(long)` は通常 no-op だが、その場合 `(long)u + (long)u` の二項演算が I32 のまま
       * 計算され、符号なし 32bit ラップマスクで 2^32 を超える和が切り詰められていた。
       * ND_CAST(widen_zext_i64) でラップし IR_ZEXT を明示挿入する (coerce は常に SEXT
       * で unsigned widen に乗れない)。signed の `(long)` は coerce の SEXT で正しく動くため
       * widen_zext_i64 は立てない。 */
      if (!ps_node_is_pointer(operand)) {
        node_mem_t *wrap = arena_alloc(sizeof(node_mem_t));
        wrap->base.kind = ND_CAST;
        wrap->base.lhs = operand;
        wrap->type_size = 8;
        wrap->is_unsigned = cast_is_unsigned ? 1 : 0;
        wrap->is_long_long = cast_is_long_long ? 1 : 0;
        if (psx_node_integer_value_is_unsigned(operand) &&
            ps_node_type_size(operand) >= 1 && ps_node_type_size(operand) < 8)
          wrap->widen_zext_i64 = 1;
        return annotate_cast_type((node_t *)wrap, cast_type);
      }
    }
    /* `(struct V *)x` / `(union U *)x`: tag 情報を後段の `->` 等が読めるよう
     * ND_CAST でラップする (operand 自体は他から共有される可能性があるので
     * 直接書き換えない)。これで `((struct V*)0)->b` のような offsetof 風や
     * `((struct V*)void_ptr)->m` が動く。 */
    if (is_pointer && (cast_tag_kind == TK_STRUCT || cast_tag_kind == TK_UNION)) {
      return wrap_pointer_cast_result(operand, cast_type, type_kind,
                                      cast_tag_kind, cast_tag_name, cast_tag_len,
                                      cast_elem_size, cast_is_unsigned);
    }
    // `(float*)X` / `(double*)X` の場合、後段の `*` deref が FP load を出せる
    // よう pointee_fp_kind を保持する ND_CAST でラップする。
    if (is_pointer && (type_kind == TK_FLOAT || type_kind == TK_DOUBLE)) {
      /* base_deref_size は立てない: `(double*)X` の指す要素はスカラ double であって
       * 「ポインタ要素」ではない。立てると `((double*)X)[i]` の添字結果が誤って
       * ポインタ扱いされ E3064 になる (`*(double*)X` の deref は deref_size/pointee_fp_kind
       * のみ見るので影響なし)。 */
      return wrap_pointer_cast_result(operand, cast_type, type_kind,
                                      cast_tag_kind, cast_tag_name, cast_tag_len,
                                      cast_elem_size, cast_is_unsigned);
    }
    /* `(int *)void_p` などポインタ型キャスト: 元の operand に pointee_is_void
     * が立っている場合、後続 deref エラーを誤発生させないよう ND_CAST で
     * ラップして pointee_is_void をクリアする。 */
    if (is_pointer && operand->kind == ND_LVAR &&
        ((node_lvar_t *)operand)->mem.pointee_is_void) {
      /* キャスト先のポインタ要素サイズを反映する。これがないと `((int*)void_p)[i]` が
       * 既定の 8 バイトストライドで添字され誤った要素を読む。base_deref_size は立てない
       * (立てると「要素自体がポインタ」扱いになり subscript 結果が誤ってポインタ化する)。 */
      /* pointee_is_void は明示的にデフォルト (0) のままにする */
      return wrap_pointer_cast_result(operand, cast_type, type_kind,
                                      cast_tag_kind, cast_tag_name, cast_tag_len,
                                      cast_elem_size, cast_is_unsigned);
    }
    /* `(int *)x` / `(char *)x` など、スカラ整数型への (単段) ポインタキャスト:
     * 後段の deref / ポインタ算術が新しい要素サイズを使うよう ND_CAST で
     * deref_size を更新する。これがないとインライン `*(int*)(cp+4)` が元 operand の
     * char サイズ (1) で 1 バイトしかロードしていなかった (変数に代入した場合は
     * 変数の型で正しく動いていた)。多段ポインタ (`int**`) は operand 側の表現を
     * 優先するためここでは触れない (cast_elem_size は基底型サイズで段数を持たない)。 */
    /* オペランドがポインタ (通常 is_pointer=1) または tag ポインタ (struct/union の `&s`、
     * wrap_as_addr が is_tag_pointer=1 / is_pointer=0 で生成) のとき、新しいポインタ型として
     * ラップする。後者を含めないと `(char*)&s - (char*)&s.c` のような struct ポインタの cast が
     * 元の ND_ADDR をそのまま返し is_pointer=0 のまま残るため、ND_SUB の「ポインタ - ポインタ
     * = ptrdiff_t」分岐が成立せず、long 初期化が「ポインタを scalar に init」と reject される。 */
    int operand_is_ptr_or_tag = ps_node_is_pointer(operand) ||
                                ((operand->kind == ND_ADDR || operand->kind == ND_DEREF ||
                                  operand->kind == ND_LVAR || operand->kind == ND_GVAR ||
                                  operand->kind == ND_CAST) &&
                                 ((node_mem_t *)operand)->is_tag_pointer);
    if (is_pointer && cast_elem_size > 0 &&
        operand_is_ptr_or_tag &&
        psx_node_pointer_qual_levels(operand) <= 1) {
      /* float/double ポインタへのキャストは pointee_fp_kind を立てる。これがないと
       * deref_size=8 が「8 バイトポインタ要素」と誤解され `((double*)&d)[i]` の結果が
       * ポインタ扱いされたり整数ロードになる (`*(double*)p` / creal のメモリ経由に必要)。 */
      return wrap_pointer_cast_result(operand, cast_type, type_kind,
                                      cast_tag_kind, cast_tag_name, cast_tag_len,
                                      cast_elem_size, cast_is_unsigned);
    }
    /* (long)ptr のような pointer→integer cast は、operand の pointer 情報を
     * 壊さずに scalar 結果 wrapper として表す。 */
    if (!is_pointer && type_kind == TK_LONG && ps_node_is_pointer(operand)) {
      return wrap_integer_cast_result(operand, cast_type, 8,
                                      cast_is_unsigned, cast_is_long_long);
    }
    if (is_pointer) {
      return wrap_pointer_cast_result(operand, cast_type, type_kind,
                                      cast_tag_kind, cast_tag_name, cast_tag_len,
                                      cast_elem_size, cast_is_unsigned);
    }
    return annotate_cast_type(operand, cast_type);
  }
  if (type_kind == TK_STRUCT || type_kind == TK_UNION) {
    const char *kind = (type_kind == TK_STRUCT) ? "struct" : "union";
    psx_diag_ctx(curtok(), "cast", diag_message_for(DIAG_ERR_PARSER_CAST_NONSCALAR_UNSUPPORTED),
                 kind);
  }
  if (type_kind == TK_FLOAT) {
    return annotate_cast_type(wrap_to_fp(operand, TK_FLOAT_KIND_FLOAT), cast_type);
  }
  if (type_kind == TK_DOUBLE) {
    return annotate_cast_type(wrap_to_fp(operand, TK_FLOAT_KIND_DOUBLE), cast_type);
  }
  if (type_kind == TK_INT || type_kind == TK_ENUM) {
    operand = wrap_fp_to_int_if_needed(operand);
    operand->fp_kind = TK_FLOAT_KIND_NONE;
    /* 定数の (int) キャスト: 32bit 符号付きへ切り詰める。これがないと
     * `(int)0x100000000L == 0` が定数畳み込みで 0x100000000==0 と評価され偽になっていた
     * (戻り値や代入では store 幅で切り詰められ偶然合っていた)。 */
    if (operand->kind == ND_NUM) {
      return annotate_cast_type(psx_node_new_num((long long)(int)((node_num_t *)operand)->val),
                                cast_type);
    }
    /* int 幅超 (long, 8B) の非ポインタ値の (int) キャスト: 32bit 符号付きへ切り詰める。
     * `(x << 32) >> 32` (算術右シフト) で低 32bit を 64bit へ符号拡張するため、後段の
     * 比較/演算が 64bit 幅でも正しい値になる。代入では store 幅で偶然合っていたが
     * `(int)long_var == 0` 等のインライン比較が 64bit 比較で誤っていた。
     * ポインタ→int は稀かつ別経路 (is_pointer クリア) のためここでは触れない。
     * long 戻り関数は ps_node_type_size(ND_FUNCALL) が 8 を返すためここに入る。 */
    if (ps_node_type_size(operand) > 4 &&
        !ps_node_is_pointer(operand)) {
      return wrap_i64_to_i32_trunc_cast(operand, cast_type, 0);
    }
    if (ps_node_type_size(operand) >= 4 && !ps_node_is_pointer(operand)) {
      return wrap_integer_cast_result(operand, cast_type, 4, 0, 0);
    }
    if (ps_node_is_pointer(operand)) {
      return wrap_integer_cast_result(operand, cast_type, 4, 0, 0);
    }
    return annotate_cast_type(operand, cast_type);
  }
  if (type_kind == TK_SIGNED || type_kind == TK_UNSIGNED) {
    operand = wrap_fp_to_int_if_needed(operand);
    operand->fp_kind = TK_FLOAT_KIND_NONE;
    int target_unsigned = (type_kind == TK_UNSIGNED) ? 1 : 0;
    /* 定数の (signed/unsigned) キャスト: 32bit へ切り詰める (TK_INT と同じ理由)。 */
    if (operand->kind == ND_NUM) {
      long long v = ((node_num_t *)operand)->val;
      node_t *n = psx_node_new_num(target_unsigned ? (long long)(unsigned)v
                                                    : (long long)(int)v);
      if (target_unsigned) psx_node_set_unsigned(n, 1);
      return annotate_cast_type(n, cast_type);
    }
    /* int 幅超 (long, 8B) の非ポインタ値の (signed/unsigned) キャスト: 32bit へ
     * 切り詰める。`(x<<32)>>32` で低 32bit を 64bit へ拡張 (unsigned は論理シフトで
     * ゼロ拡張、signed は算術シフトで符号拡張) する。long 戻り関数は
     * ps_node_type_size(ND_FUNCALL) が 8 を返すため対象になる。 */
    if (ps_node_type_size(operand) > 4 &&
        !ps_node_is_pointer(operand)) {
      return wrap_i64_to_i32_trunc_cast(operand, cast_type, target_unsigned);
    }
    /* sub-int (char/short) を (unsigned) へ: operand 自身の load 符号性 (ldrsh/ldrh)
     * を保ったまま 32bit unsigned 値へ昇格する必要がある。is_unsigned を直接立てると
     * load 拡張が変わり値が化ける。代わりに & 0xffffffff で 64bit reg 上の load 済み
     * 値を低 32bit へ折り返し、結果ノードに unsigned を付ける。これで
     * `(unsigned)(short)-1` が 0xffffffff になり、符号混在のインライン比較/除算も
     * 正しく unsigned 扱いになる (operand 幅<4 は UAC で signed 昇格扱いだった)。 */
    /* op_sz が 1/2 = 真の char/short load のみ対象。NUM 等は type_size 0 を返すので
     * 除外する (`(unsigned)13` を ND_BITAND で包んで誤って AST 形を変えないため)。 */
    int op_sz = ps_node_type_size(operand);
    if (target_unsigned && op_sz >= 1 && op_sz < 4 &&
        operand->fp_kind == TK_FLOAT_KIND_NONE && !ps_node_is_pointer(operand)) {
      node_t *masked = psx_node_new_binary(ND_BITAND, operand, psx_node_new_num(0xffffffffLL));
      psx_node_set_unsigned(masked, 1);
      return annotate_cast_type(masked, cast_type);
    }
    if (op_sz >= 4 && !ps_node_is_pointer(operand)) {
      return wrap_integer_cast_result(operand, cast_type, 4, target_unsigned, 0);
    }
    if (ps_node_is_pointer(operand)) {
      return wrap_integer_cast_result(operand, cast_type, 4, target_unsigned, 0);
    }
    return annotate_cast_type(operand, cast_type);
  }
  if (type_kind == TK_BOOL) {
    return annotate_cast_type(psx_node_new_binary(ND_NE, operand, psx_node_new_num(0)),
                              cast_type);
  }
  if (type_kind == TK_VOID) {
    return wrap_void_cast_result(operand, cast_type);
  }
  if (type_kind == TK_SHORT || type_kind == TK_CHAR) {
    operand = wrap_fp_to_int_if_needed(operand);
    operand->fp_kind = TK_FLOAT_KIND_NONE;
    int width = (type_kind == TK_SHORT) ? 16 : 8;
    long long mask = (type_kind == TK_SHORT) ? 0xffffLL : 0xffLL;
    /* 定数: ホスト側で目的幅へ切り詰める。符号付きは符号拡張、unsigned はゼロ拡張。
     * これがないと `(short)40000` が `40000 & 0xffff` = 40000 のまま定数畳み込みされ、
     * `(short)40000 == -25536` 等のインライン比較が偽になっていた。 */
    if (operand->kind == ND_NUM) {
      long long v = ((node_num_t *)operand)->val;
      long long tv;
      if (type_kind == TK_SHORT)
        tv = cast_is_unsigned ? (long long)(unsigned short)v : (long long)(short)v;
      else
        tv = cast_is_unsigned ? (long long)(unsigned char)v : (long long)(signed char)v;
      node_t *n = psx_node_new_num(tv);
      ((node_num_t *)n)->int_width = (unsigned char)(type_kind == TK_SHORT ? 2 : 1);
      if (cast_is_plain_char) ((node_num_t *)n)->int_is_plain_char = 1;
      if (cast_is_unsigned) psx_node_set_unsigned(n, 1);
      return annotate_cast_type(n, cast_type);
    }
    /* unsigned char/short: & マスクでゼロ拡張し unsigned ラベルを付ける。 */
    if (cast_is_unsigned) {
      node_t *masked = psx_node_new_binary(ND_BITAND, operand, psx_node_new_num(mask));
      psx_node_set_unsigned(masked, 1);
      node_mem_t *wrap = arena_alloc(sizeof(node_mem_t));
      wrap->base.kind = ND_CAST;
      wrap->base.lhs = masked;
      wrap->type_size = (short)(width / 8);
      wrap->is_unsigned = 1;
      return annotate_cast_type((node_t *)wrap, cast_type);
    }
    /* signed char/short: `(x << (src_width-width)) >> (src_width-width)` の算術シフトで符号拡張する。
     * 従来の `& マスク` だけだとビット (width-1) が立った runtime 値が符号拡張されず、
     * インライン比較/演算で誤った正値になっていた (char/short 変数への代入では store 幅 +
     * ldrsb/ldrsh の reload で偶然符号拡張され合っていた)。`(int)long` の切り詰めと同形。 */
    int src_width = ps_node_type_size(operand) >= 8 ? 64 : 32;
    int sh = src_width - width;
    node_t *trunc = psx_node_new_shift_trunc_extend(operand, sh, 0);
    node_mem_t *wrap = arena_alloc(sizeof(node_mem_t));
    wrap->base.kind = ND_CAST;
    wrap->base.lhs = trunc;
    wrap->type_size = (short)(width / 8);
    if (cast_is_plain_char) wrap->is_plain_char = 1;
    return annotate_cast_type((node_t *)wrap, cast_type);
  }
  // Guard rail for unexpected parser state: known cast kinds should be handled above.
  psx_diag_ctx(curtok(), "cast", "%s",
               diag_message_for(DIAG_ERR_PARSER_CAST_TYPE_RESOLVE_FAILED));
  return annotate_cast_type(operand, cast_type);
}

static bool is_compound_assign_token(token_kind_t k) {
  return k == TK_PLUSEQ || k == TK_MINUSEQ || k == TK_MULEQ || k == TK_DIVEQ ||
         k == TK_MODEQ || k == TK_SHLEQ || k == TK_SHREQ || k == TK_ANDEQ ||
         k == TK_XOREQ || k == TK_OREQ;
}

/* 複合代入 (`a[i++] += v` 等) の左辺は C11 6.5.16.2p3 で「一度だけ評価」する
 * 規定。左辺が ND_DEREF (subscript / ポインタ deref) の場合、そのアドレス式には
 * `i++` や `p++`、関数呼び出しといった副作用が埋もれ得る。複合代入は内部で左辺を
 * 読み出し用と書き込み用に 2 回展開するため、アドレスをそのまま共有すると副作用が
 * 二重評価される。ここでアドレスを temp ポインタへ一度だけ退避し、戻り値はその temp
 * 経由の DEREF (副作用なし) にする。退避代入は *prefix_io に連結する。 */
static node_t *hoist_compound_assign_lvalue(node_t *target, node_t **prefix_io) {
  if (!target || target->kind != ND_DEREF || !target->lhs) return target;
  node_t *addr = target->lhs; /* 副作用を含み得るアドレス式 */
  char *tmp_name = new_compound_lit_name();
  lvar_t *t = psx_decl_register_lvar_sized(tmp_name, (int)strlen(tmp_name), 8, 8, 0);
  /* t = &target (アドレスを一度だけ評価) */
  node_mem_t *t_assign = psx_node_new_assign(new_typed_lvar_ref(t, 1), addr);
  t_assign->type_size = 8;
  /* target のメタ情報を複製し、アドレス部だけ副作用のない temp 参照へ差し替える。 */
  node_mem_t *via = arena_alloc(sizeof(node_mem_t));
  *via = *(node_mem_t *)target;
  via->base.lhs = new_typed_lvar_ref(t, 1);
  if (*prefix_io)
    *prefix_io = psx_node_new_binary(ND_COMMA, *prefix_io, (node_t *)t_assign);
  else
    *prefix_io = (node_t *)t_assign;
  return (node_t *)via;
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
  if (is_compound_assign_token(curtok()->kind)) {
    assign_target = hoist_compound_assign_lvalue(assign_target, &lhs_prefix);
  }
  /* C11 6.5.16p2: 代入演算子の LHS は modifiable lvalue でなければならない。
   * 関数識別子 (ND_FUNCREF) はそうではない (`f = 5;` 等は非合法)。後段の IR builder で
   * "ir build/emit failed" になっていたのを、ここで分かりやすい診断にする。
   * 代入系トークン (`=`/`+=`/`-=`/...) が来ているときだけ check し、それ以外
   * (関数呼び出し `f(...)` や関数アドレス取得 `&f` 等) は素通し。 */
  if (assign_target && assign_target->kind == ND_FUNCREF &&
      (curtok()->kind == TK_ASSIGN || is_compound_assign_token(curtok()->kind))) {
    psx_diag_ctx(curtok(), "assign",
                 "関数識別子に代入することはできません (C11 6.5.16p2)");
  }
  switch (curtok()->kind) {
    case TK_ASSIGN: {
      psx_node_reject_const_assign(assign_target, "=");
      set_curtok(curtok()->next);
      node_t *rhs = assign_ctx(ctx);
      psx_node_reject_const_qual_discard(assign_target, rhs);
      /* C11 6.3.1.2: _Bool への代入は (rhs != 0) を 0/1 で表す。
       * struct メンバ `s.b` (ND_DEREF で mem.is_bool が立っている) や、
       * _Bool ローカル変数 (ND_LVAR で lvar の is_bool) の場合に正規化する。 */
      int lhs_is_bool = 0;
      if (assign_target) {
        if (assign_target->kind == ND_DEREF || assign_target->kind == ND_GVAR ||
            assign_target->kind == ND_LVAR) {
          lhs_is_bool = ((node_mem_t *)assign_target)->is_bool;
        }
      }
      if (lhs_is_bool && rhs) {
        rhs = psx_node_new_binary(ND_NE, rhs, psx_node_new_num(0));
      }
      node_mem_t *assign_node = psx_node_new_assign(assign_target, rhs);
      assign_node->type_size = ps_node_type_size(assign_node->base.lhs);
      assign_node->base.fp_kind = assign_node->base.lhs ? assign_node->base.lhs->fp_kind : 0;
      assign_node->base.is_source_assignment = 1;
      node = (node_t *)assign_node;
      if (lhs_prefix) node = psx_node_new_binary(ND_COMMA, lhs_prefix, node);
      break;
    }
    case TK_PLUSEQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(assign_target, ND_ADD, assign_ctx(ctx), "+="); if (lhs_prefix) node = psx_node_new_binary(ND_COMMA, lhs_prefix, node); break;
    case TK_MINUSEQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(assign_target, ND_SUB, assign_ctx(ctx), "-="); if (lhs_prefix) node = psx_node_new_binary(ND_COMMA, lhs_prefix, node); break;
    case TK_MULEQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(assign_target, ND_MUL, assign_ctx(ctx), "*="); if (lhs_prefix) node = psx_node_new_binary(ND_COMMA, lhs_prefix, node); break;
    case TK_DIVEQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(assign_target, ND_DIV, assign_ctx(ctx), "/="); if (lhs_prefix) node = psx_node_new_binary(ND_COMMA, lhs_prefix, node); break;
    case TK_MODEQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(assign_target, ND_MOD, assign_ctx(ctx), "%="); if (lhs_prefix) node = psx_node_new_binary(ND_COMMA, lhs_prefix, node); break;
    case TK_SHLEQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(assign_target, ND_SHL, assign_ctx(ctx), "<<="); if (lhs_prefix) node = psx_node_new_binary(ND_COMMA, lhs_prefix, node); break;
    case TK_SHREQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(assign_target, ND_SHR, assign_ctx(ctx), ">>="); if (lhs_prefix) node = psx_node_new_binary(ND_COMMA, lhs_prefix, node); break;
    case TK_ANDEQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(assign_target, ND_BITAND, assign_ctx(ctx), "&="); if (lhs_prefix) node = psx_node_new_binary(ND_COMMA, lhs_prefix, node); break;
    case TK_XOREQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(assign_target, ND_BITXOR, assign_ctx(ctx), "^="); if (lhs_prefix) node = psx_node_new_binary(ND_COMMA, lhs_prefix, node); break;
    case TK_OREQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(assign_target, ND_BITOR, assign_ctx(ctx), "|="); if (lhs_prefix) node = psx_node_new_binary(ND_COMMA, lhs_prefix, node); break;
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
    ternary->base.fp_kind = ternary->base.rhs->fp_kind;
    if (ternary->els && ternary->els->fp_kind > ternary->base.fp_kind) {
      ternary->base.fp_kind = ternary->els->fp_kind;
    }
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

/* C11 6.5.6 のポインタ算術判定。struct/union タグポインタは is_pointer ではなく
 * is_tag_pointer で表現されるため、`&s[i] - &s[j]` や `sp + n` のスケーリング/差分が
 * 効かず byte 単位になっていた。タグポインタもポインタとして扱う。 */
static int node_is_ptr_for_arith(node_t *n) {
  if (ps_node_is_pointer(n)) return 1;
  /* 多次元配列の「行」(`int m[3][4]` の `m[i]`、`int(*p)[4]` の `*(p+k)` / `p[i]`、
   * 3D 以上の中間行 `int t[2][2][2]` の `t[i]` / `*(t[i]+k)`) は ND_DEREF/ND_ADDR で
   * 表現され、値文脈ではポインタ (= 行先頭アドレス) へ decay する。is_pointer を立てると
   * subscript_base_address_of が行をポインタ値として load してしまい多次元 subscript が
   * 壊れるため、算術スケール用の判定だけここで拾う。行は type_size (= 行全体のバイト数) >
   * deref_size (= 次段ストライド) で見分ける (スカラ要素 `int *p` の `p[i]` は
   * type_size == deref_size == 要素サイズ)。スケールは add_ctx(ctx) 側で ps_node_deref_size(n)
   * (= 次段ストライド) を使う。これがないと `m[i] + k` / `*(t[i]+k) + j` が byte 加算に
   * なり不正アドレスを deref していた。 */
  if ((n->kind == ND_DEREF || n->kind == ND_ADDR) && !ps_node_is_pointer(n)) {
    int ds = ps_node_deref_size(n);
    if (ds > 0 && ps_node_type_size(n) > ds)
      return 1;
  }
  token_kind_t tk = TK_EOF; char *tn = NULL; int tl = 0, is_tp = 0;
  psx_node_get_tag_type(n, &tk, &tn, &tl, &is_tp);
  return is_tp;
}

static node_t *add_ctx(expr_parse_ctx_t *ctx) {
  node_t *node = mul_ctx(ctx);
  for (;;) {
    if (curtok()->kind == TK_PLUS) {
      set_curtok(curtok()->next);
      node_t *rhs = mul_ctx(ctx);
      /* C11 6.5.6p2: 加算では「ポインタ + 整数」と「整数 + ポインタ」が
       * 共に許される。左が非ポインタで右がポインタなら可換性で swap し、
       * 既存の「左 = ポインタ, 右 = 整数 (scaling 対象)」経路に乗せる。
       * 修正前は `2 + a` で左辺 (整数) を見るだけで scaling されず、
       * `2 + a` が整数加算 → 不正アドレス deref で garbage 値を返していた。 */
      if (!node_is_ptr_for_arith(node) && node_is_ptr_for_arith(rhs)) {
        node_t *tmp = node; node = rhs; rhs = tmp;
      }
      if (node_is_ptr_for_arith(node)) {
        int vla_rsf = psx_node_vla_row_stride_frame_off(node);
        if (vla_rsf != 0) {
          /* pointer-to-VLA (`int (*p)[m]`): 1 要素 = 1 行 = 実行時ストライド (m*elem)。
           * スロットからロードして掛ける (定数 deref_size は 0 なので使えない)。 */
          rhs = psx_node_new_binary(ND_MUL, rhs, psx_node_new_lvar_typed(vla_rsf, 8));
        } else {
          int ds = ps_node_deref_size(node);
          if (ds > 1) {
            // ポインタ + 整数: 整数を要素サイズ倍にスケーリング
            rhs = psx_node_new_binary(ND_MUL, rhs, psx_node_new_num(ds));
          }
        }
      }
      node = node_is_ptr_for_arith(node)
               ? psx_node_new_binary(ND_ADD, node, rhs)
               : new_binary_with_source_op(ND_ADD, node, rhs, TK_PLUS);
    } else if (curtok()->kind == TK_MINUS) {
      set_curtok(curtok()->next);
      node_t *rhs = mul_ctx(ctx);
      int both_ptr = node_is_ptr_for_arith(node) && node_is_ptr_for_arith(rhs);
      if (both_ptr) {
        // ポインタ - ポインタ (C11 6.5.6p9): 結果は要素数 (= ptrdiff_t)。
        // (p - q) / sizeof(*p) を生成する。両辺が同じ型を指す前提。
        int ds = ps_node_deref_size(node);
        node_t *diff = psx_node_new_binary(ND_SUB, node, rhs);
        node = (ds > 1)
                 ? psx_node_new_binary(ND_DIV, diff, psx_node_new_num(ds))
                 : diff;
      } else {
        if (node_is_ptr_for_arith(node)) {
          int vla_rsf = psx_node_vla_row_stride_frame_off(node);
          if (vla_rsf != 0) {
            rhs = psx_node_new_binary(ND_MUL, rhs, psx_node_new_lvar_typed(vla_rsf, 8));
          } else {
            int ds = ps_node_deref_size(node);
            if (ds > 1) {
              // ポインタ - 整数: 整数を要素サイズ倍にスケーリング
              rhs = psx_node_new_binary(ND_MUL, rhs, psx_node_new_num(ds));
            }
          }
        }
        node = node_is_ptr_for_arith(node)
                 ? psx_node_new_binary(ND_SUB, node, rhs)
                 : new_binary_with_source_op(ND_SUB, node, rhs, TK_MINUS);
      }
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
  token_kind_t cast_kind = TK_EOF;
  int cast_is_ptr = 0;
  token_t *after_rparen = NULL;
  token_kind_t cast_tag_kind = TK_EOF;
  char *cast_tag_name = NULL;
  int cast_tag_len = 0;
  int cast_elem_size = 8;
  tk_float_kind_t cast_fp_kind = TK_FLOAT_KIND_NONE;
  int cast_array_count = 0;
  int cast_is_unsigned = 0;
  int cast_is_long_long = 0;
  int cast_is_plain_char = 0;
  int cast_is_complex = 0;
  if (parse_cast_type(curtok(), &cast_kind, &cast_is_ptr, &after_rparen,
                      &cast_tag_kind, &cast_tag_name, &cast_tag_len,
                      &cast_elem_size, &cast_fp_kind, &cast_array_count, NULL, NULL,
                      &cast_is_unsigned, &cast_is_long_long, &cast_is_plain_char,
                      &cast_is_complex, NULL)) {
    if (after_rparen && after_rparen->kind == TK_LBRACE) {
      // compound literal は primary/postfix 側で処理する
      return unary_with_compound_addr_context(compound_addr_context, ctx);
    }
    set_curtok(after_rparen);
    node_t *operand = cast_with_compound_addr_context(compound_addr_context, ctx);
    if (!cast_is_ptr && (cast_kind == TK_STRUCT || cast_kind == TK_UNION)) {
      if (is_same_tag_nonscalar_expr(operand, cast_kind, cast_tag_name, cast_tag_len)) {
        // same-tag non-scalar cast: treat as no-op for now
        return apply_postfix(operand, ctx);
      }
      if (ps_get_enable_size_compatible_nonscalar_cast() &&
          is_size_compatible_nonscalar_expr(operand, cast_kind, cast_elem_size)) {
        // minimal extension: same-kind and same-size non-scalar cast as no-op
        return apply_postfix(operand, ctx);
      }
      if (cast_kind == TK_STRUCT) {
        token_kind_t op_tag_kind = TK_EOF;
        char *op_tag_name = NULL;
        int op_tag_len = 0;
        int op_is_tag_ptr = 0;
        psx_node_get_tag_type(operand, &op_tag_kind, &op_tag_name, &op_tag_len, &op_is_tag_ptr);
        if (!op_is_tag_ptr && (op_tag_kind == TK_STRUCT || op_tag_kind == TK_UNION)) {
          psx_diag_ctx(curtok(), "cast", diag_message_for(DIAG_ERR_PARSER_CAST_NONSCALAR_TYPE_MISMATCH),
                       "struct");
        }
        if (!ps_get_enable_struct_scalar_pointer_cast()) {
          psx_diag_ctx(curtok(), "cast", "%s",
                       diag_message_for(DIAG_ERR_PARSER_CAST_STRUCT_SCALAR_POINTER_DISABLED));
        }
        return apply_postfix(lower_struct_value_cast(operand, cast_tag_kind, cast_tag_name, cast_tag_len,
                                                     cast_elem_size, cast_fp_kind), ctx);
      }
      if (cast_kind == TK_UNION) {
        token_kind_t op_tag_kind = TK_EOF;
        char *op_tag_name = NULL;
        int op_tag_len = 0;
        int op_is_tag_ptr = 0;
        psx_node_get_tag_type(operand, &op_tag_kind, &op_tag_name, &op_tag_len, &op_is_tag_ptr);
        if (!op_is_tag_ptr && (op_tag_kind == TK_STRUCT || op_tag_kind == TK_UNION)) {
          psx_diag_ctx(curtok(), "cast", diag_message_for(DIAG_ERR_PARSER_CAST_NONSCALAR_TYPE_MISMATCH),
                       "union");
        }
        if (!ps_get_enable_union_scalar_pointer_cast()) {
          psx_diag_ctx(curtok(), "cast", "%s",
                       diag_message_for(DIAG_ERR_PARSER_CAST_UNION_SCALAR_POINTER_DISABLED));
        }
        // staged extension: allow scalar/pointer -> union value cast by
        // initializing the first union member, then yielding the union object.
        return apply_postfix(lower_union_value_cast(operand, cast_tag_kind, cast_tag_name, cast_tag_len,
                                                    cast_elem_size, cast_fp_kind), ctx);
      }
      const char *kind = (cast_kind == TK_STRUCT) ? "struct" : "union";
      psx_diag_ctx(curtok(), "cast", diag_message_for(DIAG_ERR_PARSER_CAST_NONSCALAR_UNSUPPORTED),
                   kind);
    }
    return apply_postfix(apply_cast(cast_kind, cast_is_ptr, operand,
                                     cast_tag_kind, cast_tag_name, cast_tag_len,
                                     cast_elem_size, cast_fp_kind,
                                     cast_is_unsigned, cast_is_long_long,
                                     cast_is_plain_char, cast_is_complex), ctx);
  }
  return unary_ctx(ctx);
}

static node_t *sizeof_vla_runtime_size_node(int slot_off) {
  node_t *lvar = psx_node_new_lvar_typed(slot_off, 8);
  as_lvar(lvar)->mem.is_unsigned = 1;
  node_mem_t *cast = arena_alloc(sizeof(node_mem_t));
  cast->base.kind = ND_CAST;
  cast->base.lhs = lvar;
  cast->type_size = 8;
  cast->is_unsigned = 1;
  return (node_t *)cast;
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
    token_kind_t cast_kind = TK_EOF;
    int cast_is_ptr = 0;
    token_t *after_rparen = NULL;
    token_kind_t cast_tag_kind = TK_EOF;
    char *cast_tag_name = NULL;
    int cast_tag_len = 0;
    int cast_elem_size = 8;
    tk_float_kind_t cast_fp_kind = TK_FLOAT_KIND_NONE;
    int cast_array_count = 0;
    int cast_array_dims[8] = {0};
    int cast_array_dim_count = 0;
    int cast_is_complex = 0;
    int cast_ptr_array_pointee_bytes = 0;
    if (curtok()->kind == TK_LPAREN &&
        parse_cast_type(curtok(), &cast_kind, &cast_is_ptr, &after_rparen,
                        &cast_tag_kind, &cast_tag_name, &cast_tag_len,
                        &cast_elem_size, &cast_fp_kind, &cast_array_count,
                        cast_array_dims, &cast_array_dim_count, NULL, NULL, NULL,
                        &cast_is_complex, &cast_ptr_array_pointee_bytes) &&
        after_rparen && after_rparen->kind == TK_LBRACE) {
      expr_parse_ctx_t child_ctx = expr_parse_ctx_unevaluated_child(ctx);
      node_t *node = parse_compound_literal_from_type(cast_kind, cast_is_ptr, after_rparen,
                                                      cast_tag_kind, cast_tag_name, cast_tag_len,
                                                      cast_elem_size, cast_fp_kind, cast_array_count,
                                                      cast_array_dims, cast_array_dim_count,
                                                      cast_is_complex,
                                                      cast_ptr_array_pointee_bytes,
                                                      0,
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
      if (arr_var && arr_var->is_vla &&
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
      if (arr_var && arr_var->is_array) {
        token_t *peek = curtok()->next;
        if (peek && peek->kind == TK_RPAREN) {
          set_curtok(peek->next);
          return annotate_lvar_sizeof_usage_node(psx_node_new_num(arr_var->size), arr_var);
        }
      }
      /* static local 配列はグローバルへ lowering され alias lvar は is_array=0 /
       * size=0 なので上の分岐に乗らない。実サイズは lowering 先グローバルの
       * type_size にある (`static int a[10]` → 40)。 */
      if (arr_var && lvar_is_static_local_array(arr_var)) {
        token_t *peek = curtok()->next;
        if (peek && peek->kind == TK_RPAREN) {
          global_var_t *sgv = psx_find_global_var(arr_var->static_global_name,
                                                  arr_var->static_global_name_len);
          if (sgv && sgv->type_size > 0) {
            set_curtok(peek->next);
            return annotate_lvar_sizeof_usage_node(psx_node_new_num(sgv->type_size), arr_var);
          }
        }
      }
      /* ローカル lvar が見つからなければ global 配列を探す。`int g[] = {...}`
       * のような要素数推定後の type_size (apply_toplevel_object_initializer で
       * 確定済み) を全体サイズとして返す。 */
      if (!arr_var) {
        for (global_var_t *gv = psx_find_global_var(id->str, id->len); gv; gv = NULL) {
          if (gv->name_len != id->len ||
              memcmp(gv->name, id->str, (size_t)id->len) != 0) continue;
          if (gv->is_array && gv->type_size > 0) {
            token_t *peek = curtok()->next;
            if (peek && peek->kind == TK_RPAREN) {
              set_curtok(peek->next);
              return psx_node_new_num(gv->type_size);
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
   * 明確に「小さな整数スカラ」(ND_LVAR/ND_GVAR で type_size < 8 かつ
   * 非ポインタ非配列) を deref するときだけエラーにする。8B 値は関数ポインタ
   * や long も含まれるので保守的に許容する。
   * また pointee_is_void が立っているとき (`void *p`) は deref 不可。 */
  if (operand && (operand->kind == ND_LVAR || operand->kind == ND_GVAR ||
                  operand->kind == ND_NUM)) {
    int looks_ptr = ps_node_is_pointer(operand) ||
                    psx_node_pointer_qual_levels(operand) > 0;
    int ts = ps_node_type_size(operand);
    /* type_size が 1/2/4 (char/short/int) で pointer 指示がなければ明確に
     * スカラ整数 → deref はエラー。 */
    if (!looks_ptr && ts > 0 && ts < 8) {
      psx_diag_ctx(curtok(), "deref",
                   "deref のオペランドはポインタ型でなければなりません (C11 6.5.3.2p2)");
    }
    /* void* の deref は不正 (pointee の型が不完全)。 */
    if (psx_node_pointee_is_void(operand)) {
      psx_diag_ctx(curtok(), "deref",
                   "void* の deref はできません — キャストが必要です (C11 6.5.3.2)");
    }
  } else if (psx_node_pointee_is_void(operand)) {
    psx_diag_ctx(curtok(), "deref",
                 "void* の deref はできません — キャストが必要です (C11 6.5.3.2)");
  }
  node_mem_t *node = arena_alloc(sizeof(node_mem_t));
  node->base.kind = ND_DEREF;
  node->base.lhs = operand;
  node->base.fp_kind = TK_FLOAT_KIND_NONE;
  int ds = ps_node_deref_size(operand);
  node->type_size = ds ? ds : 8;
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int is_tag_ptr = 0;
  psx_node_get_tag_type(operand, &tag_kind, &tag_name, &tag_len, &is_tag_ptr);
  if (tag_kind != TK_EOF && is_tag_ptr) {
    node->tag_kind = tag_kind;
    node->tag_name = tag_name;
    node->tag_len = tag_len;
    /* `*p` (p=struct N*) は struct 実体だが、`*pp` (pp=struct N**) の結果は
     * まだ struct ポインタ。多段ポインタ (pql>=2) なら is_tag_pointer を維持する。 */
    node->is_tag_pointer = (psx_node_pointer_qual_levels(operand) >= 2) ? 1 : 0;
    node->deref_size = 0;
  }
  // 多段ポインタ: *pp (int**) → int* なので is_pointer と deref_size を伝播
  int pql = psx_node_pointer_qual_levels(operand);
  tk_float_kind_t pointee_fp = psx_node_pointee_fp_kind(operand);
  /* `double *a` 仮引数のように pql を持たず pointee_fp_kind だけで fp ポインタを
   * 表す場合も含め、単段 (pql<=1) の deref 結果に fp 種別を引き継ぐ。 */
  if (pql <= 1 && pointee_fp != TK_FLOAT_KIND_NONE) {
    node->base.fp_kind = pointee_fp;
  }
  /* `unsigned *p` の `*p`: pointee が unsigned なら deref 結果も unsigned
   * (zero-extend load)。pointee_is_unsigned は build_lvar_or_vla_node が立てる。 */
  if (pql <= 1) {
    if (psx_node_pointee_is_unsigned(operand)) node->is_unsigned = 1;
  }
  int operand_ptr_array_pointee_bytes = psx_node_ptr_array_pointee_bytes(operand);
  if (operand_ptr_array_pointee_bytes > 0) {
    node->ptr_array_pointee_bytes = operand_ptr_array_pointee_bytes;
    int operand_base_deref_size = psx_node_base_deref_size(operand);
    if (node->base_deref_size == 0 && operand_base_deref_size > 0) {
      node->base_deref_size = (short)operand_base_deref_size;
    }
  }
  if (psx_node_pointee_is_const_qualified(operand)) node->is_const_qualified = 1;
  if (psx_node_pointee_is_volatile_qualified(operand)) node->is_volatile_qualified = 1;
  if (pql >= 2) {
    node->is_pointer = 1;
    int new_pql = pql - 1;
    node->pointer_qual_levels = new_pql;
    int bds = psx_node_base_deref_size(operand);
    node->base_deref_size = (short)bds;
    node->deref_size = (new_pql >= 2) ? 8 : (short)bds;
    /* 最内 pointee の fp 種別を 1 段下の deref 結果へ引き継ぐ。これがないと
     * `double **pp` の `*pp` が fp 情報を失い、最終 `**pp` が fp load/store に
     * ならず float がゴミ・double の書き込みが落ちていた。 */
    node->pointee_fp_kind = pointee_fp;
  }
  // 仮引数 typedef 配列ポインタ (`typedef int M[D0][D1]...; void f(M *p)`):
  // p (ND_LVAR) は M のサイズと strides を保持しているが、
  //   outer_stride = sizeof(M) (= p[i] のステップ、つまり M 全体)
  //   mid_stride   = M の 1 段目のステップ
  //   extra_strides = それ以降の段
  // ということに注意。`*p` の結果は M 自身であり、subscript するときの
  // ステップは「M の 1 段目」(= p->mid_stride) になる。よって 1 段スライドして
  // 継承する: deref_size ← mid_stride、inner_deref_size ← extra_strides[0]、…
  /* `int (*p)[N]` (1D 配列へのポインタ) の `*p` および `*(p+k)` を解決する。
   * operand が ND_LVAR 直接、または ND_ADD(p, ...) の場合に、p の outer_stride>0
   * かつ非配列なら、ND_DEREF の deref_size を elem_size にセットして
   * subscript_base_address_of が load を skip できるようにする。 */
  {
    node_t *probe = operand;
    while (probe && probe->kind == ND_ADD) probe = probe->lhs;
    if (probe && probe->kind == ND_LVAR) {
      lvar_t *src = psx_node_lvar_symbol(probe);
      if (src && src->outer_stride > 0 && src->mid_stride == 0 && !src->is_array) {
        node->deref_size = (short)src->elem_size;
        /* タグ情報の carry: `struct S (*ap)[N]` の `*ap` は struct S[N] (配列) を表す。
         * 変数 ap 自体は is_tag_pointer=0 (ポインタ-to-配列であって tag ポインタでない)
         * のため上流の `is_tag_ptr` ガードで tag が落ちている。subscript 結果の
         * `(*ap)[i].m` 解決のため、deref ノードに tag を carry する (is_tag_pointer=0:
         * 結果は配列で、その要素が struct)。 */
        if (src->tag_kind != TK_EOF && !src->is_tag_pointer && node->tag_kind == TK_EOF) {
          node->tag_kind = src->tag_kind;
          node->tag_name = src->tag_name;
          node->tag_len = src->tag_len;
          node->is_tag_pointer = 0;
        }
        /* 要素がポインタの配列へのポインタ (`IP (*pia)[3]` / `BinOp (*pa)[3]`): src 側で
         * pointer_qual_levels=1 と base_deref_size=要素 pointee サイズが立つ。これを ND_DEREF
         * に carry し、build_subscript_deref の「要素はポインタ」分岐に乗せて `(*pia)[0]` の
         * 結果を scalar pointer として扱えるようにする。これがないと `*(*pia)[0]` の最終
         * deref が 8B のままで int (4B) 比較が型ずれする。 */
        if (src->pointer_qual_levels >= 1 && src->base_deref_size > 0) {
          node->pointer_qual_levels = src->pointer_qual_levels;
          node->base_deref_size = src->base_deref_size;
        }
        if (src->ptr_array_pointee_bytes > 0) {
          node->ptr_array_pointee_bytes = src->ptr_array_pointee_bytes;
          if (node->base_deref_size == 0) node->base_deref_size = (short)src->elem_size;
        }
      }
    } else if (probe && probe->kind == ND_FUNCALL) {
      /* `int (*f())[N]` の `*f()` / `*(f()+k)`: 結果は行 (int[N])。subscript_base_address_of が
       * load を skip し `(*f())[i]` が要素ストライドで添字できるよう、deref_size を要素サイズに
       * する (ローカル `int (*p)[N]` の `*p` と同じ)。 ds=N*elem を first_dim で割って elem を得る。 */
      psx_type_t *func_type = psx_node_get_type(probe);
      if (func_type && func_type->funcptr_ret_pointee_array_first_dim > 0) {
        int inner = func_type->outer_stride;
        int next = func_type->mid_stride;
        if (inner <= 0) {
          psx_ret_pointee_array_t dims = psx_ret_pointee_array_make(
              func_type->funcptr_ret_pointee_array_first_dim,
              func_type->funcptr_ret_pointee_array_second_dim,
              0);
          int rowstride = ps_node_deref_size(probe);
          psx_ret_pointee_array_strides_from_row(dims, rowstride, &inner, &next);
        }
        if (inner > 0) {
          node->deref_size = (short)inner;
          if (next > 0) node->inner_deref_size = (short)next;
        }
        if (func_type->base &&
            (func_type->base->kind == PSX_TYPE_STRUCT ||
             func_type->base->kind == PSX_TYPE_UNION)) {
          node->tag_kind = func_type->base->tag_kind;
          node->tag_name = func_type->base->tag_name;
          node->tag_len = func_type->base->tag_len;
          node->tag_scope_depth_p1 = func_type->base->tag_scope_depth_p1;
          node->is_tag_pointer = 0;
        }
      }
    } else if (probe && probe->kind == ND_DEREF) {
      /* struct メンバ `int (*p)[N]` (および 2D pointee `int (*p)[M][N]`) の `*s.p`:
       * probe (= s.p のメンバ deref) は build_member_deref_node で is_tag_pointer=1、
       * deref_size=pointee 全バイト数、inner_deref_size=1 段目 stride、next_deref_size=elem
       * (2D 時) として組まれている。1 段スライドして carry:
       *   新 deref_size      ← probe.inner_deref_size  (1 段目 subscript の要素 stride)
       *   新 inner_deref_size ← probe.next_deref_size  (2 段目 subscript の要素 stride)
       * ローカル `int (*p)[M][N]` の `*p` と同じ表現に揃え、subscript_base_address_of が
       * lhs (= s.p) を返す経路に乗せる (is_pointer は立てない)。 */
      node_mem_t *pm = (node_mem_t *)probe;
      if (pm->is_tag_pointer && pm->inner_deref_size > 0
          && pm->deref_size > pm->inner_deref_size) {
        node->deref_size = pm->inner_deref_size;
        if (pm->next_deref_size > 0) {
          node->inner_deref_size = pm->next_deref_size;
        }
      }
    }
  }
  if (operand && operand->kind == ND_LVAR) {
    lvar_t *src = psx_node_lvar_symbol(operand);
    if (src && src->outer_stride > 0 && src->mid_stride > 0) {
      // 2D 以上 (`*p` の結果が配列)
      node->deref_size = (short)src->mid_stride;
      /* タグ情報の carry: `struct S (*ap)[N][M]` の `*ap` は struct S[N][M] (2D 配列)。
       * 1D 版と同様に、subscript 連鎖 `(*ap)[i][j].m` の解決のため tag を deref ノードへ
       * carry する (is_tag_pointer=0: 結果は配列で、最終要素が struct)。 */
      if (src->tag_kind != TK_EOF && !src->is_tag_pointer && node->tag_kind == TK_EOF) {
        node->tag_kind = src->tag_kind;
        node->tag_name = src->tag_name;
        node->tag_len = src->tag_len;
        node->is_tag_pointer = 0;
      }
      if (src->extra_strides_count > 0) {
        node->inner_deref_size = (short)src->extra_strides[0];
        if (src->extra_strides_count > 1) {
          node->next_deref_size = (short)src->extra_strides[1];
          for (int i = 2; i < src->extra_strides_count && (i - 2) < 5; i++) {
            node->extra_strides[i - 2] = src->extra_strides[i];
          }
          // 末尾要素 stride = elem_size
          if (src->extra_strides_count - 2 < 5) {
            node->extra_strides[src->extra_strides_count - 2] = src->elem_size;
            node->extra_strides_count = (unsigned char)(src->extra_strides_count - 1);
          } else {
            node->extra_strides_count = (unsigned char)(src->extra_strides_count - 2);
          }
        } else {
          node->next_deref_size = (short)src->elem_size;
        }
      } else {
        node->inner_deref_size = (short)src->elem_size;
      }
    }
  }
  /* 通常の多次元配列 `int m[3][4]` は build_array_lvar_addr_node により
   * ND_ADDR(deref_size=行ストライド, inner_deref_size=要素) として表現される
   * (上の ND_LVAR 専用ブロックには該当しない)。`*m` / `*(m+k)` の operand を
   * ND_ADD を辿って基底まで下り、inner_deref_size>0 (= まだ多次元) なら結果の
   * 「行」に内側ストライドを 1 段シフトして引き継ぐ。これがないと結果 deref_size=0
   * のままで build_node_deref の配列崩壊判定に乗らず、行を値ロードして garbage を返す
   * (`int *q=m[0]` 相当の `*m` / `*(m+k)`)。
   * 旧実装は `node->type_size > 8` を門にしていたが、それだと行全体が 8 バイト以下に
   * なる 3 次元以上の中間行 (`int t[2][2][2]` の `*(t[i]+k)` = int[2] = 8B) で伝播が
   * 漏れ SIGSEGV していた。pm->inner_deref_size>0 自体が「結果がまだ配列」の指標なので
   * type_size 門は不要。is_pointer は立てない: subscript 経路の行と統一し !is_pointer の
   * まま扱う。崩壊は build_node_deref の小行節、算術スケールは node_is_ptr_for_arith に
   * 委ねる (is_pointer を立てると loaded ポインタ値と区別できず崩壊判定が壊れる)。 */
  if (node->deref_size == 0) {
    node_t *probe = operand;
    while (probe && (probe->kind == ND_ADD || probe->kind == ND_SUB)) probe = probe->lhs;
    node_mem_t *pm = NULL;
    if (probe && probe->kind == ND_LVAR) pm = &as_lvar(probe)->mem;
    else if (probe && (probe->kind == ND_ADDR || probe->kind == ND_GVAR ||
                       probe->kind == ND_DEREF || probe->kind == ND_CAST ||
                       probe->kind == ND_STRING)) pm = (node_mem_t *)probe;
    if (pm && pm->inner_deref_size > 0) {
      node->deref_size = pm->inner_deref_size;
      node->inner_deref_size = pm->next_deref_size;
      if (pm->extra_strides_count > 0) {
        node->next_deref_size = (short)pm->extra_strides[0];
        for (int i = 1; i < pm->extra_strides_count && (i - 1) < 5; i++)
          node->extra_strides[i - 1] = pm->extra_strides[i];
        node->extra_strides_count = (unsigned char)(pm->extra_strides_count - 1);
      }
    }
  }
  /* deref 結果がまだ「行」(配列) の場合 (`(*dp)[j]` の *dp、`*m`/`*(m+k)` 等)、要素 fp 種別を
   * pointee_fp_kind にも伝播し、後続 subscript `(*dp)[j]` が要素を fp load できるようにする。
   * 上の `pql<=1` 分岐 (2549) は `double *p` のスカラ deref を想定して base.fp_kind を立てるが、
   * 配列へのポインタや多次元配列の行 deref では結果はスカラでなく行なので、subscript 経路は
   * pointee_fp_kind を見る。base.fp_kind は残す: スカラ deref `*p` を `_Generic(*p, double:..)`
   * が control->fp_kind で判定するため (クリアすると double* deref が default に落ちる)。
   * deref_size>0 が「結果がまだ subscript 可能な配列/行」の指標。これがないと double の
   * `(*dp)[j]` が整数 load になり値が化けていた (local/global 共通)。 */
  if (node->deref_size > 0 && node->base.fp_kind != TK_FLOAT_KIND_NONE &&
      node->pointee_fp_kind == TK_FLOAT_KIND_NONE) {
    node->pointee_fp_kind = node->base.fp_kind;
  }
  return (node_t *)node;
}

// `&operand` を ND_ADDR でラップする。
// オペランドが struct タグを持っていれば is_tag_pointer/deref_size/type_size を設定。
// 非タグ型 (int / char / 関数ポインタ等) でも is_pointer=1 と deref_size を立てる
// — ポインタ - ポインタ や ポインタ + 整数 の判定に使われる。
static node_t *wrap_as_addr(node_t *operand) {
  node_mem_t *node = arena_alloc(sizeof(node_mem_t));
  node->base.kind = ND_ADDR;
  node->base.lhs = operand;
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int is_tag_ptr = 0;
  psx_node_get_tag_type(operand, &tag_kind, &tag_name, &tag_len, &is_tag_ptr);
  if (tag_kind != TK_EOF && !is_tag_ptr) {
    node->tag_kind = tag_kind;
    node->tag_name = tag_name;
    node->tag_len = tag_len;
    node->is_tag_pointer = 1;
    node->deref_size = ps_node_type_size(operand);
    node->type_size = 8;
    return (node_t *)node;
  }
  /* タグなしオペランド: operand の型サイズが pointee サイズ */
  int ts = ps_node_type_size(operand);
  if (ts > 0) {
    node->deref_size = ts;
    node->is_pointer = 1;
    node->type_size = 8;
  }
  return (node_t *)node;
}

// `&operand`。コンマ式 (a, b) に対する `&(a, b)` は a を評価した上で &b を返す形に組み立てる。
static node_t *build_unary_addr_node(node_t *operand) {
  /* C11 6.5.3.2p1: bitfield のアドレスは取得できない。
   * `s.f` (bitfield) は ND_DEREF にラップされて bit_width が立っているので
   * ここで弾く。 */
  if (operand && operand->kind == ND_DEREF) {
    node_mem_t *mm = (node_mem_t *)operand;
    if (mm->bit_width > 0) {
      psx_diag_ctx(curtok(), "addr",
                   "ビットフィールドのアドレスは取得できません (C11 6.5.3.2p1)");
    }
  }
  if (operand && operand->kind == ND_COMMA && operand->rhs) {
    /* `&(compound-literal)` 等、値が COMMA(init, ref) の形。rhs に同じ単項 & の
     * ロジックを再帰適用する (wrap_as_addr 直呼びだと配列複合リテラルの rhs が
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
    node_mem_t *opm = (node_mem_t *)operand;
    if (opm->type_size != 8 || opm->compound_literal_array_size > 0) {
      node_mem_t *cp = arena_alloc(sizeof(node_mem_t));
      *cp = *opm;
      cp->type_size = 8;
      if (opm->compound_literal_array_size > 0) {
        int old_inner = opm->inner_deref_size;
        int old_next = opm->next_deref_size;
        int old_extras[5] = {0};
        int old_extra_count = opm->extra_strides_count;
        for (int i = 0; i < old_extra_count && i < 5; i++) old_extras[i] = opm->extra_strides[i];
        cp->inner_deref_size = opm->deref_size;
        cp->deref_size = opm->compound_literal_array_size;
        cp->next_deref_size = old_inner;
        cp->extra_strides_count = 0;
        for (int i = 0; i < 5; i++) cp->extra_strides[i] = 0;
        if (old_next > 0) {
          cp->extra_strides[0] = old_next;
          int n = 1;
          for (int i = 0; i < old_extra_count && n < 5; i++, n++) cp->extra_strides[n] = old_extras[i];
          cp->extra_strides_count = (unsigned char)n;
        }
      }
      cp->compound_literal_array_size = 0;
      cp->base.is_explicit_addr_expr = 1;
      return (node_t *)cp;
    }
    operand->is_explicit_addr_expr = 1;
    return operand;
  }
  /* `&f` (f は関数): 関数のアドレスは関数ポインタそのもの (= `f`)。ND_FUNCREF を
   * そのまま返す (ND_ADDR でラップすると IR builder が扱えず失敗する)。 */
  if (operand && operand->kind == ND_FUNCREF) {
    return operand;
  }
  node_t *addr = wrap_as_addr(operand);
  if (addr) addr->is_explicit_addr_expr = 1;
  return addr;
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
      n->fp_kind = (operand && operand->fp_kind != TK_FLOAT_KIND_NONE)
                       ? operand->fp_kind : TK_FLOAT_KIND_DOUBLE;
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
    if (operand && operand->fp_kind != TK_FLOAT_KIND_NONE) {
      node_t *neg = arena_alloc(sizeof(node_t));
      neg->kind = ND_FNEG;
      neg->lhs = operand;
      neg->fp_kind = operand->fp_kind;
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
      psx_node_pointer_qual_levels(node) == 1 &&
      psx_node_base_deref_size(node) > 0) {
    node_mem_t *m = (node_mem_t *)node;
    if (m->deref_size == 0 &&
        !(m->is_pointer && !m->is_scalar_ptr_member &&
          node->lhs && node->lhs->kind == ND_ADD &&
          (m->tag_kind == TK_STRUCT || m->tag_kind == TK_UNION))) {
      es = psx_node_base_deref_size(node);
    }
  }
  int vla_rsf = 0;  // 実行時行ストライドのフレームオフセット (0=なし)
  int inner_ds = 0; // 次の次元の要素サイズ (0=スカラ)
  int next_ds = 0;  // さらに次の次元の要素サイズ (3D 用、0=なし)
  int extras[5] = {0};
  int extras_count = 0;
  if (node->kind == ND_LVAR) {
    vla_rsf = as_lvar(node)->mem.vla_row_stride_frame_off;
    inner_ds = as_lvar(node)->mem.inner_deref_size;
    next_ds = as_lvar(node)->mem.next_deref_size;
    extras_count = as_lvar(node)->mem.extra_strides_count;
    for (int i = 0; i < extras_count && i < 5; i++) extras[i] = as_lvar(node)->mem.extra_strides[i];
  } else if (node->kind == ND_DEREF || node->kind == ND_ADDR || node->kind == ND_GVAR) {
    /* ND_GVAR も node_mem_t を先頭メンバに持つので同じキャストで読める。配列へのポインタ
     * グローバル `T (*pa)[N]` は inner_deref_size=elem を持ち、第1subscript `pa[i]` の
     * 結果 (行) が第2subscript `pa[i][j]` 用の要素ストライドを引き継げるようにする
     * (これがないと inner_ds=0 で第2subscript が行ストライドのまま誤ロードしていた)。 */
    node_mem_t *m = (node_mem_t *)node;
    inner_ds = m->inner_deref_size;
    next_ds = m->next_deref_size;
    extras_count = m->extra_strides_count;
    for (int i = 0; i < extras_count && i < 5; i++) extras[i] = m->extra_strides[i];
    /* 3D VLA chain: t[i] の結果 deref が vla_row_stride_frame_off = mid slot を持っているとき、
     * 続く `t[i][j]` の subscript は runtime mid stride で行う。これがないと vla_rsf=0 のまま
     * inner_ds (= elem_size) でスケールしてしまい mid stride (k*elem) が反映されない。 */
    if (node->kind == ND_DEREF) vla_rsf = m->vla_row_stride_frame_off;
  } else if (node->kind == ND_ADD || node->kind == ND_SUB) {
    /* ポインタ算術 `t+1` の結果 (ND_ADD) を subscript するとき、ポインタ被演算子
     * (配列へのポインタ等) の多段ストライド (inner_deref_size 等) を引き継ぐ。
     * これがないと `(t+1)[0]` の結果 deref_size が 0 になり配列へ decay せず誤ロードし、
     * 外側 `*` が値をアドレスとして deref して SIGBUS になる (`t[1]` は base が lvar で
     * 上の分岐が拾えていた)。スカラポインタは inner_deref_size=0 なので無影響。 */
    node_t *p = node;
    while (p && (p->kind == ND_ADD || p->kind == ND_SUB)) p = p->lhs;
    if (p && (p->kind == ND_LVAR || p->kind == ND_DEREF ||
              p->kind == ND_ADDR || p->kind == ND_GVAR)) {
      node_mem_t *m = (node_mem_t *)p;
      inner_ds = m->inner_deref_size;
      next_ds = m->next_deref_size;
      extras_count = m->extra_strides_count;
      for (int i = 0; i < extras_count && i < 5; i++) extras[i] = m->extra_strides[i];
    }
  } else if (node->kind == ND_FUNCALL) {
    /* 配列へのポインタ戻り `int (*f())[N]`: 第1 subscript `f()[i]` の結果 (行) が第2
     * subscript `f()[i][j]` 用の要素ストライド (base elem) を引き継げるよう inner_ds を立てる
     * (ローカル `int (*p)[N]` の inner_deref_size=elem と同じ。これがないと f()[i][j] が
     * 行ストライドのまま誤ロード→SIGSEGV)。 */
    psx_type_t *func_type = psx_node_get_type(node);
    if (func_type && func_type->funcptr_ret_pointee_array_first_dim > 0) {
      inner_ds = func_type->outer_stride;
      next_ds = func_type->mid_stride;
      if (inner_ds <= 0) {
        psx_ret_pointee_array_t dims = psx_ret_pointee_array_make(
            func_type->funcptr_ret_pointee_array_first_dim,
            func_type->funcptr_ret_pointee_array_second_dim,
            0);
        psx_ret_pointee_array_strides_from_row(dims, ds, &inner_ds, &next_ds);
      }
    }
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
  node_mem_t *mem = (node_mem_t *)node;
  if (mem->deref_size > 0 && !mem->is_pointer) return node->lhs;
  /* 3D VLA `int t[n][m][k]` の最初の subscript結果 t[i] は deref_size=0 (次 stride は
   * runtime, vla_row_stride_frame_off=mid_slot 経由) だが、配列の中間「2D サブ配列」を
   * 表すため subscript chain では address (lhs) を返す。これがないと t[i][j] が ND_DEREF
   * を 1 バイト整数として load してしまい SIGSEGV。 */
  if (mem->vla_row_stride_frame_off > 0 && !mem->is_pointer) return node->lhs;
  /* スカラポインタメンバ (`struct S { char *name; }; s.name[0]`) を subscript
   * する場合、base は「ポインタ値の load」(= ND_DEREF をそのまま使う) でなければ
   * いけない。配列メンバとは違って ND_ADD (= メンバスロットのアドレス) を base に
   * 使うと、ポインタ値ではなくスロット自身のアドレスから byte を読んでしまう。
   * 配列メンバの decay 表現とは is_scalar_ptr_member で区別する。 */
  if (mem->is_scalar_ptr_member) return node;
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
  node_t *addr = psx_node_new_binary(ND_ADD, base_addr, scaled);
  node_mem_t *deref = arena_alloc(sizeof(node_mem_t));
  deref->base.kind = ND_DEREF;
  deref->base.lhs = addr;
  deref->type_size = es;
  deref->deref_size = inner_ds; // 多次元配列: 次段のストライド (0=スカラ)
  deref->inner_deref_size = (short)next_ds; // さらに次段のストライド (3D 用)
  // 4 次元以上: extra_strides[0..n-1] を 1 段シフトして new node に格納。
  // current.extra_strides[0] が新しい next_deref_size に、以降が新しい extra_strides になる。
  if (extras_count > 0) {
    deref->next_deref_size = (short)extras[0];
    for (int i = 1; i < extras_count && (i - 1) < 5; i++) {
      deref->extra_strides[i - 1] = extras[i];
    }
    deref->extra_strides_count = (unsigned char)(extras_count - 1);
  }
  deref->base.fp_kind = TK_FLOAT_KIND_NONE;
  /* N-D VLA (N>=3) の subscript chain:
   * - t (ND_LVAR or ND_DEREF) は vla_row_stride_frame_off = 次の runtime stride スロット、
   *   vla_strides_remaining = その後にまだ何個 runtime stride スロットが続くか、を持つ。
   * - 1 段 subscript で stride を消費したら、結果 deref に vla_row += 8、vla_strides_remaining -= 1
   *   を carry する。remaining が消費後 0 未満になる (= もう runtime stride がない) ときは
   *   vla_row = 0 にして「以降は elem 定数 stride」へ移行する。
   * これにより 2D/3D/4D/N-D VLA を統一的に解決できる。 */
  {
    int parent_vla_row = 0;
    int parent_remaining = 0;
    int parent_elem = 0;
    if (node->kind == ND_LVAR) {
      parent_vla_row = as_lvar(node)->mem.vla_row_stride_frame_off;
      parent_remaining = as_lvar(node)->mem.vla_strides_remaining;
      parent_elem = as_lvar(node)->mem.inner_deref_size;
    } else if (node->kind == ND_DEREF || node->kind == ND_ADDR || node->kind == ND_GVAR) {
      node_mem_t *m = (node_mem_t *)node;
      parent_vla_row = m->vla_row_stride_frame_off;
      parent_remaining = m->vla_strides_remaining;
      parent_elem = m->inner_deref_size;
    }
    if (parent_vla_row != 0) {
      if (parent_remaining > 0) {
        /* 次の段もまだ runtime stride。vla_row を 8 シフトして carry。 */
        deref->vla_row_stride_frame_off = parent_vla_row + 8;
        deref->vla_strides_remaining = parent_remaining - 1;
      }
      /* parent_remaining == 0 のときは current が最終 runtime stride。
       * 結果 deref は vla_row=0 (default)、以降の subscript は elem 定数で動く。 */
      /* elem を chain に carry する (subscript_base_address_of が次段で「中間配列」を正しく
       * 認識し、make が es=elem として要素サイズを伝播できるように)。make の next_ds 伝播は
       * 2 段までしか効かないため、4D 以降で明示的に carry が必要。 */
      if (parent_elem > 0) {
        deref->inner_deref_size = (short)parent_elem;
        deref->next_deref_size = (short)parent_elem;
      }
    }
  }
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int is_tag_ptr = 0;
  psx_node_get_tag_type(node, &tag_kind, &tag_name, &tag_len, &is_tag_ptr);
  if (tag_kind != TK_EOF) {
    /* tag を持つ配列 (struct[N]) や struct ポインタを subscript した結果は
     * tag 要素そのもの。is_tag_ptr フラグの有無を問わず tag を deref へ伝播し、
     * `arr[i].member` の解決を可能にする。is_tag_pointer は 0 (実体)。 */
    deref->tag_kind = tag_kind;
    deref->tag_name = tag_name;
    deref->tag_len = tag_len;
    deref->is_tag_pointer = 0;
  }
  // 配列要素がポインタ型の場合: サブスクリプト結果にポインタ情報を伝播
  int pql = psx_node_pointer_qual_levels(node);
  int bds = psx_node_base_deref_size(node);
  /* subscript 結果がポインタになるのは「要素自体がポインタ」のときだけ:
   *   int **pp     (pql>=2, bds>0)     → pp[i] は int*
   *   int *arr[N]  (配列, pql=1, bds>0) → arr[i] は int*
   * 単段ポインタ int *p / long *p / char *p (pql=1, bds=0) の p[i] はスカラ。
   * base_deref_size>0 が「要素がさらにポインタ」の指標 (pointee が pointee を持つ)。
   * bds==0 も含め pql>=1 だけで判定していたため、`int x = p[0];` が誤って
   * 「スカラにポインタを代入」と E3064 拒否されていた (pp8/pp1/pp5)。 */
  /* 多次元ポインタ配列 (`int *t[2][2]`) の中間行: 結果はまだ「行」(配列) でポインタ要素
   * でない。pql>=1 && bds>0 の「要素はポインタ」分岐がここで発火すると deref_size が
   * 要素 stride (inner_ds) でなく bds に上書きされ、次段 subscript が誤スケール (+4 等) になる。
   * fp/unsigned と同じ中間行判定 (inner_ds>0 && es>inner_ds: 行サイズ es が要素 stride
   * inner_ds より大きい) で中間行を見分け、pointer-element 化を最終次元まで遅延する。
   * 単段 `int *arr[N]` (inner_ds==0) や genuine 多段ポインタ `int **pp` (es==inner_ds) は
   * 中間行でないので従来どおり最終要素として扱う。 */
  int subscript_is_intermediate_row = (inner_ds > 0 && es > inner_ds);
  int deref_from_pointer_to_array = 0;
  if (node->kind == ND_DEREF && node->lhs) {
    node_t *probe = node->lhs;
    while (probe && probe->kind == ND_ADD) probe = probe->lhs;
    if (probe && probe->kind == ND_LVAR) {
      lvar_t *src = psx_node_lvar_symbol(probe);
      if (src && src->outer_stride > 0 && !src->is_array) {
        deref_from_pointer_to_array = 1;
      }
    }
  }
  if (pql >= 1 && bds > 0 && subscript_is_intermediate_row) {
    /* 中間行: pointer 化せず deref_size=inner_ds を保ち、pql / bds を次段へ carry して
     * 最終次元の subscript が「要素はポインタ」分岐に乗れるようにする。 */
    deref->pointer_qual_levels = pql;
    deref->base_deref_size = (short)bds;
  } else if (pql == 1 && bds > 0 &&
             (node->kind == ND_LVAR || node->kind == ND_GVAR ||
              node->kind == ND_FUNCALL ||
              (node->kind == ND_DEREF &&
               !deref_from_pointer_to_array &&
               ((node_mem_t *)node)->ptr_array_pointee_bytes == 0 &&
               (!(node->lhs && node->lhs->kind == ND_ADD) ||
                ((node_mem_t *)node)->is_scalar_ptr_member)))) {
    /* 単段のスカラポインタ値 (`long *p`, `unsigned long *p`) の subscript は
     * pointee スカラ。`long *` ではポインタ認識のため pql=1 / bds=8 を持つが、
     * これは「要素がポインタ」ではなく「指している要素が 8B」という情報なので
     * 結果に pointer_qual_levels を carry しない。
     * `T **pp` の `(*pp)[i].member` も `*pp` が単段の `T *` 値になるためここで
     * `T` 実体に落とす。これを下の「要素がポインタ」分岐に入れると `.member` が
     * `T *` への `.` と誤判定される。 */
    deref->deref_size = 0;
  } else if (pql >= 1 && bds > 0) {
    deref->is_pointer = 1;
    /* genuine ポインタ変数 (`int **pp`, ND_LVAR/ND_GVAR) の subscript は 1 段の
     * ポインタを消費するので結果の pql を 1 減らす (`pp[i]` は int*、pql=1)。
     * 配列 (`int *arr[N]`, ND_ADDR decay) は配列次元を消費し要素の pql を保つ
     * (`arr[i]` は int*、pql=1)。pql を減らさないと `*pp[0]` がポインタ扱いの
     * ままになり、スカラ初期化 `int u=*pp[0];` が誤って弾かれ、算術も pointer 化
     * していた。 */
    int result_pql = pql;
    if ((node->kind == ND_LVAR || node->kind == ND_GVAR ||
         node->kind == ND_FUNCALL ||
         (node->kind == ND_DEREF && ((node_mem_t *)node)->is_scalar_ptr_member)) &&
        pql >= 2) {
      /* 多段ポインタ戻り `int **g()` の `g()[i]` も genuine ポインタ値の subscript
       * (配列 decay でなく)。1 段消費して結果 pql を減らす (`g()[i]` は int*)。 */
      result_pql = pql - 1;
    }
    deref->pointer_qual_levels = result_pql;
    /* base_deref_size は「結果がさらに内側ポインタを持つか」を表す。結果が単段
     * ポインタ (result_pql==1, 例 int**→int*) なら pointee はスカラなので 0。
     * 多段のまま (result_pql>=2, 例 int***→int**) なら内側 scalar size を保つ。
     * ここを常に bds にしていたため、`int **m` の `m[i]` が bds=4 を持ち、
     * `m[i][j]` が誤ってポインタ扱い (4 倍スケール) されていた。 */
    deref->base_deref_size = (result_pql >= 2) ? (short)bds : 0;
    deref->deref_size = (result_pql >= 2) ? 8 : (short)bds;
    /* 要素が struct/union ポインタ (`struct N *arr[N]`) の場合、subscript 結果は
     * struct ポインタ値なので is_tag_pointer を立てる (`arr[i]->m` の解決に必要)。 */
    if (deref->tag_kind != TK_EOF) {
      deref->is_tag_pointer = 1;
    }
    /* 結果がまだポインタ (多段ポインタの `pp[i]` や `int *arr[i]`) のとき、最内
     * pointee の fp 種別を引き継ぐ。これがないと `double **pp; *pp[0]` の最終 deref が
     * fp と分からず整数 load になっていた。結果はポインタなので base.fp_kind ではなく
     * pointee_fp_kind に運ぶ (build_unary_deref_node の多段分岐と対称)。 */
    deref->pointee_fp_kind = psx_node_pointee_fp_kind(node);
  }
  /* 配列 (pql=0 でも pointee_fp_kind を持つ ND_ADDR) の subscript 結果も
   * FP load にするため、pointee_fp_kind を見て fp_kind を引き継ぐ。多次元配列
   * (`float m[2][3]`) の 1 段目 subscript 結果はまだ配列なので、base.fp_kind では
   * なく pointee_fp_kind に伝播して次段 subscript が fp load になるようにする
   * (is_bool と同じ分岐)。これがないと `m[i][j]` が整数 load になっていた。 */
  {
    tk_float_kind_t arr_pointee_fp = psx_node_pointee_fp_kind(node);
    /* pointer-to-VLA `double (*p)[m]` の第1 subscript: vla_rsf 経路では es==inner_ds に
     * なり下の `es > inner_ds` で「最終スカラ」と誤分類される。実行時ストライド (vla_rsf)
     * があり内側次元 (inner_ds>0) を持つなら結果は「行」(中間) なので明示的に区別する。 */
    int node_vla_rsf = (node->kind == ND_LVAR) ? as_lvar(node)->mem.vla_row_stride_frame_off : 0;
    int is_vla_row = (node_vla_rsf != 0 && inner_ds > 0);
    if (arr_pointee_fp != TK_FLOAT_KIND_NONE && pql == 0) {
      /* 結果がまだ多次元配列の「行」(要素サイズ es が次段ストライド inner_ds より
       * 大きい) なら pointee_fp_kind に伝播して次段 subscript を fp load にする。
       * 最終スカラ要素 (es <= inner_ds、または inner_ds==0) なら base.fp_kind。
       * 1D の `float *a` 仮引数は inner_ds=elem_size が立つことがあるので es>inner_ds
       * で「多次元の中間」かどうかを区別する。 */
      if ((inner_ds > 0 && es > inner_ds) || is_vla_row) {
        deref->pointee_fp_kind = arr_pointee_fp;
      } else if (inner_ds == 0 && bds > 0) {
        /* 関数ポインタ配列の要素 (`double (*ops[N])(double)` の `ops[i]`): 要素は
         * 8B の関数ポインタ値であって double 値ではない。base.fp_kind に載せると
         * subscript 結果を double としてロードしてしまうため、is_pointer を立てて
         * ポインタ値としてロードし、戻り型 fp_kind を pointee_fp_kind に運ぶ
         * (呼び出し側 parse_call_postfix がこれを見て戻り値を d0 で読む)。配列要素は
         * pql=0 で登録され (line 2777 の funcptr 配列分岐)、bds>0 が「要素がポインタ」、
         * inner_ds==0 が「要素を更に添字できない不透明ポインタ」の指標。fp ポインタ
         * 仮引数 `double *a` は a[i] が fp スカラで inner_ds==es>0 になるため除外され、
         * 純粋な fp スカラ配列は bds==0 で下の else に落ちる。 */
        deref->pointee_fp_kind = arr_pointee_fp;
        deref->is_pointer = 1;
        deref->deref_size = 8;
      } else {
        deref->base.fp_kind = arr_pointee_fp;
      }
    }
  }
  if (pql == 1) {
    tk_float_kind_t pointee_fp = psx_node_pointee_fp_kind(node);
    if (pointee_fp != TK_FLOAT_KIND_NONE) {
      deref->base.fp_kind = pointee_fp;
    }
  }
  /* array-of-pointer-to-array (`int (*p[M])[N]`) の `p[i]` / `s.p[i]`: 要素は
   * pointer-to-array。結果 deref を「単一 pointer-to-array」(`int (*sp)[N]`) と同じ表現に
   * 組み直す。2D 以上の `int (*m[A][B])[N]` では 1 段目 `m[a]` はまだ行なので、
   * ptr_array_pointee_bytes / bds を carry し、最終次元で同じ組み直しを行う。 */
  {
    int base_ptr_array_pointee_bytes = psx_node_ptr_array_pointee_bytes(node);
    if (base_ptr_array_pointee_bytes > 0 && bds > 0) {
      if (subscript_is_intermediate_row) {
        deref->ptr_array_pointee_bytes = base_ptr_array_pointee_bytes;
        deref->base_deref_size = (short)bds;
      } else {
        deref->is_tag_pointer = 1;
        deref->is_pointer = 1;
        deref->type_size = 8;
        deref->deref_size = (short)base_ptr_array_pointee_bytes;
        deref->inner_deref_size = (short)bds;
        deref->pointer_qual_levels = 0;
        deref->base_deref_size = 0;
      }
    }
  }
  /* `_Bool a[5]` の subscript 結果は _Bool スカラ。代入時に rhs を `!= 0` で
   * 正規化させるため is_bool を立てる。配列ベース node が pointee_is_bool を
   * 持っていればそれを引き継ぐ。多次元配列 (`_Bool m[2][3]`) では 1 段目の
   * subscript 結果が「内側配列」(まだ要素ではない) として返るため、
   * pointee_is_bool を引き継いで次の subscript に渡せるようにする。 */
  {
    node_mem_t *base_mem = (node_t *)node && (node->kind == ND_ADDR || node->kind == ND_LVAR ||
                                              node->kind == ND_GVAR || node->kind == ND_DEREF)
                              ? (node_mem_t *)node : NULL;
    if (psx_node_pointee_is_bool(node)) {
      if (pql == 0 && inner_ds == 0) {
        /* 最終要素まで到達: 代入正規化用に is_bool を立てる。 */
        deref->is_bool = 1;
      } else {
        /* 中間配列 (まだ要素ではない): 次段 subscript へ pointee_is_bool を伝播。 */
        deref->pointee_is_bool = 1;
      }
    }
    /* unsigned 配列/ポインタの subscript 結果: 最終スカラ要素なら is_unsigned
     * (zero-extend load)、中間 (まだ配列の行 / ポインタ) なら次段へ pointee_is_unsigned
     * を伝播。最終スカラ判定は「結果がポインタでなく、かつ多次元の中間行 (es>inner_ds)
     * でない」。旧条件 `pql==0 && inner_ds==0` は `unsigned char *p` のように単段ポインタ
     * で pql=1 / inner_ds=elem(1) になるケースを最終要素と認識できず、`p[i]` が ldrsb で
     * 符号拡張されていた (es>inner_ds 判定は fp の中間行判定と対称)。 */
    if (psx_node_pointee_is_unsigned(node)) {
      int is_final_scalar = !deref->is_pointer && !(inner_ds > 0 && es > inner_ds);
      if (is_final_scalar) deref->is_unsigned = 1;
      else                 deref->pointee_is_unsigned = 1;
    }
    psx_node_copy_funcptr_metadata(deref, node);
    if (psx_node_pointee_is_const_qualified(node)) deref->is_const_qualified = 1;
    if (psx_node_pointee_is_volatile_qualified(node)) deref->is_volatile_qualified = 1;
    /* サイズ1配列メンバ (`struct S { unsigned char x[1]; }`) は struct_layout で
     * array_len=0 のスカラに潰れ pointee_is_unsigned を持てない。base 自体が
     * unsigned スカラ (is_unsigned) を最終要素まで添字した場合も zero-extend する。
     * 真のスカラの subscript は clang が拒否するのでここに来るのは [1] 配列のみ。 */
    if (base_mem && base_mem->is_unsigned && !deref->is_unsigned &&
        !deref->is_pointer && pql == 0 && inner_ds == 0) {
      deref->is_unsigned = 1;
    }
    /* `char *names[N]` 等: グローバルポインタ配列の要素 subscript 結果は
     * 「スカラポインタ値の load」(= struct メンバ char* と同じ semantics)。
     * is_scalar_ptr_member を立てて subscript_base_address_of が ND_DEREF を
     * そのまま返し、次段の subscript でポインタ値を base として使うようにする。 */
    if (base_mem && base_mem->pointee_is_scalar_ptr && pql == 0) {
      if (inner_ds == 0) {
        /* 最終次元: 要素 (スカラポインタ値) を load する。 */
        deref->is_scalar_ptr_member = 1;
        deref->is_pointer = 1;
        /* deref_size を pointee の素のサイズに更新 (char* なら 1)。1D 配列は base が ND_ADDR
         * なので gv->pointee_elem_size を引く。2D 以上は base が中間 ND_DEREF なので
         * base_mem->base_deref_size に carry された値を使う。 */
        int pelem = 0;
        if (node->kind == ND_ADDR && node->lhs && node->lhs->kind == ND_GVAR) {
          node_gvar_t *gv_node = (node_gvar_t *)node->lhs;
          for (global_var_t *gv = psx_find_global_var(gv_node->name, gv_node->name_len); gv; gv = NULL) {
            if (gv->name_len == gv_node->name_len &&
                memcmp(gv->name, gv_node->name, (size_t)gv->name_len) == 0) {
              pelem = gv->pointee_elem_size;
              break;
            }
          }
        }
        if (pelem == 0) pelem = base_mem->base_deref_size;
        if (pelem > 0) deref->deref_size = pelem;
      } else {
        /* 中間次元 (2D 以上のポインタ配列の行): まだ要素でないので load せず、
         * pointee_is_scalar_ptr と pointee サイズ (base_deref_size) を次段へ carry する。 */
        deref->pointee_is_scalar_ptr = 1;
        deref->base_deref_size = base_mem->base_deref_size;
      }
    }
  }
  return (node_t *)deref;
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

static int expr_funcall_returns_funcptr(node_t *fcall) {
  if (!fcall || fcall->kind != ND_FUNCALL) return 0;
  node_func_t *fc = (node_func_t *)fcall;
  if (!fc->callee && fc->funcname) {
    return psx_ctx_get_function_ret_info(fc->funcname, fc->funcname_len).is_funcptr;
  }
  if (fc->callee && fc->callee->kind == ND_LVAR) {
    lvar_t *lv = psx_node_lvar_symbol(fc->callee);
    return lv && lv->funcptr_ret_is_pointer;
  }
  return 0;
}

static node_t *parse_call_postfix(node_t *callee, expr_parse_ctx_t *ctx) {
  tk_expect('(');
  node_func_t *node = arena_alloc(sizeof(node_func_t));
  node->base.kind = ND_FUNCALL;
  /* `(*fp)(args)` / `(**fp)(args)`: 関数ポインタの「単項 deref」は関数へ戻り即座に
   * 関数ポインタへ減衰するので `fp(args)` と等価。単項 deref を辿って最下層が関数
   * ポインタ lvar (pointer_qual_levels<=1) なら全段剥がす。
   * `(*call())(args)` も同様: 戻り値が関数ポインタなら `*result` は減衰のみ。
   * subscript の結果 (`ops[i]`, lhs=ND_ADD で最下層が lvar にならない) や、
   * ポインタ→関数ポインタ (`int(**pp)(); (*pp)()`, pql>=2) は実体 deref なので除外。 */
  if (callee && callee->kind == ND_DEREF) {
    node_t *lhs = callee->lhs;
    if (lhs && lhs->kind == ND_FUNCALL && expr_funcall_returns_funcptr(lhs)) {
      callee = lhs;
    } else {
      node_t *base = callee;
      while (base && base->kind == ND_DEREF) base = base->lhs;
      if (base && (base->kind == ND_LVAR || base->kind == ND_GVAR) &&
          psx_node_pointer_qual_levels(base) <= 1) {
        callee = base;
      }
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
    /* 直接呼び出しと同じく、プロトタイプから戻り型情報を引く (build_unqualified_call と
     * 同じ)。これがないと fp 戻り値を x0 で読む等で値が化ける (tgmath の sqrt 等)。 */
    psx_function_ret_info_t ret = psx_ctx_get_function_ret_info(fr->funcname, fr->funcname_len);
    node->base.fp_kind = ret.fp_kind;
    node->base.ret_struct_size = ret.struct_size;
    if (ret.is_complex)
      node->base.is_complex = 1;
    if (ret.is_void)
      node->base.is_void_call = 1;
    if (ret.is_unsigned)
      psx_node_set_unsigned((node_t *)node, 1);
  } else {
    node->callee = callee;
  }
  /* 間接呼び出しで callee の pointee fp_kind (= 関数戻り型の fp_kind。
   * `double (*d)(double)` は宣言時 `double *d` と同じく pointee_fp_kind=double が
   * 立つ) を funcall ノードに載せる。これがないと ir_builder が戻り値型を整数
   * (I32) と判定し戻り値を x0 で読んでいた (FP 戻り値は d0 に返るため化けていた)。
   * 呼び出し文脈なので callee は関数ポインタであり、データポインタ `double *p` の
   * pointee と取り違える心配はない。関数ポインタ変数 (ND_LVAR/ND_GVAR) に加え、
   * 関数ポインタ配列の要素 `ops[i]` (ND_DEREF、build_subscript_deref が pointee_fp_kind
   * を設定済み) や struct メンバ `s.f` / `p->f` (build_member_deref_node が funcptr
   * メンバの戻り fp_kind を pointee_fp_kind として設定済み) も拾う。 */
  if (callee) {
    tk_float_kind_t ret_fp = psx_node_pointee_fp_kind(callee);
    if (ret_fp != TK_FLOAT_KIND_NONE &&
        !psx_node_funcptr_returns_pointee_array(callee)) {
      node->base.fp_kind = ret_fp;
    }
    if (psx_node_funcptr_returns_void(callee)) {
      node->base.is_void_call = 1;
    }
    if (psx_node_funcptr_returns_complex(callee)) {
      node->base.is_complex = 1;
    }
    /* 間接呼び出しで戻り型が struct/union 値 (`struct R (*op)(int)`) なら ret_struct_size を
     * 設定する。直接呼び出しは ret 表 (psx_ctx_get_function_ret_struct_size) から引くが、
     * 間接は callee funcptr の戻り tag (pql=1 で値戻り) からサイズを導出する。これがないと
     * ir_builder が >8B/非 pow2 struct 戻りを scalar 扱いし struct 代入/メンバアクセスが壊れる。
     * ポインタ戻り (pql>=2) は struct ポインタ値 (8B) なので ret_struct_size は立てない。 */
    token_kind_t rtk = TK_EOF; char *rtn = NULL; int rtl = 0;
    psx_node_get_tag_type(callee, &rtk, &rtn, &rtl, NULL);
    if ((rtk == TK_STRUCT || rtk == TK_UNION) &&
        psx_node_pointer_qual_levels(callee) <= 1) {
      int ss = psx_ctx_get_tag_size(rtk, rtn, rtl);
      if (ss > 0) node->base.ret_struct_size = ss;
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
  /* 関数ポインタ経由呼び出し `fp(3)`: fp 仮引数に整数実引数を渡したら昇格する。
   * 直接呼び出しは ir_builder が coerce するが、間接は funcptr 変数が仮引数型を
   * 持つ必要があるため、宣言時に記録した funcptr_param_fp_mask を見て parser 側で
   * wrap_to_fp する (既に fp の実引数なら no-op)。 */
  unsigned short fp_param_mask = 0;
  unsigned short int_param_mask = 0;
  if (callee) {
    fp_param_mask = psx_node_funcptr_param_fp_mask(callee);
    int_param_mask = psx_node_funcptr_param_int_mask(callee);
  }
  if (fp_param_mask) {
    for (int i = 0; i < nargs && i < 8; i++) {
      tk_float_kind_t pfk = (tk_float_kind_t)((fp_param_mask >> (2 * i)) & 3u);
      if (pfk != TK_FLOAT_KIND_NONE) node->args[i] = wrap_to_fp(node->args[i], pfk);
    }
  }
  if (int_param_mask) {
    for (int i = 0; i < nargs && i < 8; i++) {
      int code = (int)((int_param_mask >> (2 * i)) & 3u);
      if (code == 3) continue;
      if (code) node->args[i] = wrap_fp_to_int_width(node->args[i], code == 2 ? 8 : 4);
    }
  }
  node->nargs = nargs;
  psx_node_materialize_type((node_t *)node);
  return (node_t *)node;
}

// TK_LPAREN を見たときの compound literal `(T){...}` 試行。
// パースできたら結果ノードを返し、できなければ NULL（呼び出し側は通常の式へ）。
static node_t *try_parse_compound_literal(int compound_addr_context, expr_parse_ctx_t *ctx) {
  token_kind_t cast_kind = TK_EOF;
  int cast_is_ptr = 0;
  token_t *after_rparen = NULL;
  token_kind_t cast_tag_kind = TK_EOF;
  char *cast_tag_name = NULL;
  int cast_tag_len = 0;
  int cast_elem_size = 8;
  tk_float_kind_t cast_fp_kind = TK_FLOAT_KIND_NONE;
  int cast_array_count = 0;
  int cast_array_dims[8] = {0};
  int cast_array_dim_count = 0;
  int cast_is_complex = 0;
  int cast_ptr_array_pointee_bytes = 0;
  if (curtok()->kind == TK_LPAREN &&
      parse_cast_type(curtok(), &cast_kind, &cast_is_ptr, &after_rparen,
                      &cast_tag_kind, &cast_tag_name, &cast_tag_len,
                      &cast_elem_size, &cast_fp_kind, &cast_array_count,
                      cast_array_dims, &cast_array_dim_count, NULL, NULL, NULL,
                      &cast_is_complex, &cast_ptr_array_pointee_bytes) &&
      after_rparen && after_rparen->kind == TK_LBRACE) {
    return parse_compound_literal_from_type(cast_kind, cast_is_ptr, after_rparen,
                                            cast_tag_kind, cast_tag_name, cast_tag_len,
                                            cast_elem_size, cast_fp_kind, cast_array_count,
                                            cast_array_dims, cast_array_dim_count,
                                            cast_is_complex,
                                            cast_ptr_array_pointee_bytes,
                                            compound_addr_context,
                                            ctx);
  }
  return NULL;
}

// _Generic( ctrl, T1: e1, T2: e2, ..., default: ed ) を評価して選択された式ノードを返す。
static node_t *parse_generic_selection(expr_parse_ctx_t *ctx) {
  set_curtok(curtok()->next); // skip TK_GENERIC
  tk_expect('(');
  /* 制御式が純粋なキャスト `(T)operand` で直後が ',' の形なら、キャスト先の型 T を静的型
   * として使う。apply_cast は sub-int/int キャストを値計算ノード (`(x<<56)>>56` 等) に
   * lower し char/short/unsigned char の型幅・符号を AST に残さないため、
   * `_Generic((char)x, char:..)` 等が int 扱いになっていた。型を assoc 型と同じ
   * parse_generic_assoc_type で解釈するので表現が一致し確実にマッチする。型トレイト idiom
   * `(T)0` をカバー。複雑な式 (`(char)x + 0` 等、結果型は昇格で int) は従来どおり下の
   * infer_generic_control_type に委ねる。複合リテラル `(T){..}` は `{` を見て除外。 */
  generic_type_t control_ty;
  int got_cast_ty = 0;
  if (curtok()->kind == TK_LPAREN) {
    token_t *save = curtok();
    /* トークンストリーム経路: 巻き戻し先 (save) より古いトークンを解放させない。
     * これがパーサ内で唯一のバックトラックで、式内に収まる。非ストリーム経路では no-op。 */
    tk_allocator_recyc_pin(save);
    set_curtok(curtok()->next); // skip '('
    generic_type_t cty = {0};
    cty.kind = TK_EOF;
    cty.tag_kind = TK_EOF;
    cty.ptr_pointee_fp_kind = TK_FLOAT_KIND_NONE;
    /* キャスト型は (a) スカラ算術型 (char/short/int/long/unsigned/float/double 等、非ポインタ
     * 非タグ)、または (b) 正規化トークン文字列 (type_sig) を持つ複雑な派生型 (関数ポインタ /
     * ネスト宣言子) のときに採用する。(b) は以前 assoc 側の構造的照合が不完全で
     * `(int(*)(int))0` が array-of-funcptr assoc へ誤マッチしたため除外していたが、type_sig の
     * strcmp 照合になり安全に区別できる。単純ポインタ (`int*`, type_sig なし) は従来どおり
     * 下の infer に委ねる。 */
    if (parse_generic_assoc_type(&cty) && curtok()->kind == TK_RPAREN &&
        curtok()->next && curtok()->next->kind != TK_LBRACE &&
        (cty.type_sig != NULL || (!cty.is_pointer && cty.tag_kind == TK_EOF))) {
      set_curtok(curtok()->next); // skip ')'
      cast_ctx(ctx);                     // 制御式は未評価 (C11 6.5.1.1) なので operand の値は捨てる
      if (curtok()->kind == TK_COMMA) {
        control_ty = cty;
        got_cast_ty = 1;
      }
    }
    if (!got_cast_ty && save->next && save->next->kind == TK_LPAREN) {
      set_curtok(save->next->next); // skip outer '(' and inner '('
      cty = (generic_type_t){0};
      cty.kind = TK_EOF;
      cty.tag_kind = TK_EOF;
      cty.ptr_pointee_fp_kind = TK_FLOAT_KIND_NONE;
      if (parse_generic_assoc_type(&cty) && curtok()->kind == TK_RPAREN &&
          curtok()->next && curtok()->next->kind != TK_LBRACE &&
          (cty.type_sig != NULL || (!cty.is_pointer && cty.tag_kind == TK_EOF))) {
        set_curtok(curtok()->next); // skip inner ')'
        cast_ctx(ctx);
        if (curtok()->kind == TK_RPAREN && curtok()->next &&
            curtok()->next->kind == TK_COMMA) {
          set_curtok(curtok()->next); // skip outer ')'
          control_ty = cty;
          got_cast_ty = 1;
        }
      }
    }
    if (!got_cast_ty) set_curtok(save); // 純粋なキャストでなければ巻き戻して通常解析
    tk_allocator_recyc_unpin();
  }
  if (!got_cast_ty) {
    /* 制御式が単一の識別子 `_Generic(var, ...)` なら、宣言時に記録した型シグネチャを
     * 名前で引いて control に付与する (関数ポインタ/ネスト宣言子の照合用)。 */
    token_ident_t *ctrl_id = NULL;
    if (curtok()->kind == TK_IDENT && curtok()->next && curtok()->next->kind == TK_COMMA) {
      ctrl_id = (token_ident_t *)curtok();
    }
    node_t *control = assign_ctx(ctx);
    control_ty = infer_generic_control_type(control);
    if (ctrl_id) control_ty.type_sig = psx_lookup_var_type_sig(ctrl_id->str, ctrl_id->len);
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
      generic_type_t assoc_ty = {0};
      assoc_ty.kind = TK_EOF;
      if (!parse_generic_assoc_type(&assoc_ty)) {
        psx_diag_ctx(curtok(), "generic", "%s",
                     diag_message_for(DIAG_ERR_PARSER_GENERIC_ASSOC_TYPE_INVALID));
      }
      tk_expect(':');
      node_t *expr_node = assign_ctx(ctx);
      if (!matched && generic_type_matches(control_ty, assoc_ty)) {
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

// TK_NUM を node_num_t に変換。浮動小数点なら float_literals テーブルにも登録。
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
  } else {
    node->base.fp_kind = tk_as_num_float(tok)->fp_kind;
    node->float_suffix_kind = tk_as_num_float(tok)->float_suffix_kind;
    node->fval = tk_as_num_float(tok)->fval;
  }
  if (node->base.fp_kind) {
    float_lit_t *lit = calloc(1, sizeof(float_lit_t));
    lit->id = float_label_count++;
    lit->fval = node->fval;
    lit->fp_kind = node->base.fp_kind;
    lit->float_suffix_kind = node->float_suffix_kind;
    lit->next = float_literals;
    float_literals = lit;
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
  snode->mem.base.kind = ND_STRING;
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
  lit->next = string_literals;
  string_literals = lit;
  snode->mem.type_size = 8;
  snode->mem.deref_size = char_width ? char_width : TK_CHAR_WIDTH_CHAR;
  /* 文字列リテラルは char (または wchar) 配列で、式中ではポインタに decay する。
   * `"abc"[1]` の subscript チェックや (ptr + n) のスケーリングに使う。 */
  snode->mem.is_pointer = 1;
  snode->mem.base.fp_kind = TK_FLOAT_KIND_NONE;
  snode->char_width = char_width ? char_width : TK_CHAR_WIDTH_CHAR;
  snode->str_prefix_kind = prefix_kind;
  /* byte_len は「デコード後」の内容長 (要素数)。str はソースのまま (`\t` 等の
   * エスケープシーケンスを含む raw) なので、エスケープを 1 要素に畳んで数える。
   * これがないと sizeof("\t") が raw の 2(+1) を返していた (正しくは 1+1)。 */
  int decoded = 0;
  int cw_count = char_width ? (int)char_width : TK_CHAR_WIDTH_CHAR;
  for (int sp = 0; sp < len; ) {
    uint32_t units[2];
    decoded += tk_next_string_code_units(str, len, &sp, cw_count, units);
  }
  snode->byte_len = decoded;
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
// 戻り値型 (ret_struct_size 等) は psx_ctx から引く。
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
  if (!psx_ctx_has_function_name(tok->str, tok->len) &&
      !psx_find_global_var(tok->str, tok->len)) {
    node->base.is_implicit_func_decl = 1;
  }
  /* C11 6.5.2.2p2: 呼び出しの実引数数は仮引数数と一致 (non-variadic)、
   * または >= 固定引数数 (variadic) でなければならない。
   * 既に登録されている関数のみチェック (未宣言識別子は別エラーで弾かれる)。 */
  if (psx_ctx_has_function_name(tok->str, tok->len)) {
    int expected = 0;
    int is_variadic = psx_ctx_get_function_is_variadic(tok->str, tok->len, &expected) ? 1 : 0;
    int mismatch = is_variadic ? (nargs < expected) : (nargs != expected);
    if (mismatch) {
      psx_diag_ctx(curtok(), "funcall",
                   "関数呼び出しの引数数が一致しません: '%.*s' 期待 %s%d、実際 %d",
                   tok->len, tok->str,
                   is_variadic ? "≥" : "", expected, nargs);
    }
  }
  psx_function_ret_info_t ret = psx_ctx_get_function_ret_info(tok->str, tok->len);
  node->base.ret_struct_size = ret.struct_size;
  // 関数戻り値が float/double のときは call ノードに fp_kind を設定し、
  // `(int)call()` キャストで apply_cast が ND_FP_TO_INT を挿入できるようにする。
  node->base.fp_kind = ret.fp_kind;
  /* 戻り値が _Complex のとき call ノードに is_complex を立てる。build_node_funcall が
   * HFA 戻り値 (d0/d1) を一時 slot に受け、複素数値として扱えるようにする。 */
  if (ret.is_complex) {
    node->base.is_complex = 1;
  }
  if (ret.is_void) {
    node->base.is_void_call = 1;
  }
  /* 戻り値型が unsigned のとき call ノードに is_unsigned を立てる。これがないと
   * `unsigned f(); f() <= 100` が符号付き比較 (LE) になり、32bit 比較で 0xFFFFFFFF を
   * 負数扱いして誤判定する (戻り値の符号性が funcall ノードへ伝播していなかった)。
   * ただし unsigned char/short は整数昇格 (C11 6.3.1.1) で signed int になるため
   * is_unsigned を立てない。立てると `unsigned char f(); f() > -1` が unsigned 比較に
   * なり -1 が UINT_MAX 扱いで誤って false になる (unsigned int/long のみ保持)。 */
  if (ret.is_unsigned && !ret.is_pointer) {
    /* ポインタ戻り (`unsigned char *g()`) の ctx unsigned は pointee 符号 (subscript 用) で
     * あって戻り値そのものではないので、ここ (戻り値の符号化) では除外する。 */
    if (ret.token_kind != TK_CHAR && ret.token_kind != TK_SHORT) {
      node->base.is_unsigned = 1;
    }
  }
  psx_node_materialize_type((node_t *)node);
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
  for (global_var_t *gv = psx_find_global_var(tok->str, tok->len); gv; gv = NULL) {
    if (gv->name_len != tok->len || memcmp(gv->name, tok->str, (size_t)tok->len) != 0) continue;
    if (gv->is_array) {
      node_gvar_t *base = arena_alloc(sizeof(node_gvar_t));
      base->mem.base.kind = ND_GVAR;
      base->mem.type_size = gv->type_size;
      base->mem.deref_size = gv->deref_size;
      /* struct 配列の要素はタグ型なので、ND_GVAR と外側 ND_ADDR の両方に
       * tag 情報を伝播させて `gpts[i].x` の member access を解決できるようにする。 */
      base->mem.tag_kind = gv->tag_kind;
      base->mem.tag_name = gv->tag_name;
      base->mem.tag_len = gv->tag_len;
      base->mem.tag_scope_depth_p1 = gv->tag_scope_depth_p1;  /* shadow 対応 */
      base->mem.is_const_qualified = gv->is_const_qualified;
      base->mem.is_volatile_qualified = gv->is_volatile_qualified;
      base->name = gv->name;
      base->name_len = gv->name_len;
      base->is_thread_local = gv->is_thread_local;
      node_mem_t *addr = arena_alloc(sizeof(node_mem_t));
      addr->base.kind = ND_ADDR;
      addr->base.lhs = (node_t *)base;
      psx_node_init_gvar_array_addr_metadata(addr, gv);
      return (node_t *)addr;
    }
    return psx_node_new_gvar_for(gv);
  }
  return NULL;
}

/* static local 配列のベースアドレスを ND_ADDR(ND_GVAR) として返す。
 * 配列は decl.c の try_lower_static_local_array でグローバルにリダイレクトされ、
 * alias lvar (is_static_local=1, static_global_name=mangled) を持つ。
 * alias は size=0 で frame 割当を抑制しているため、サイズ情報は global_vars
 * から名前で引く。多次元配列は alias lvar に保存した stride 情報を
 * ND_ADDR(ND_GVAR) へ伝播し、通常のローカル/グローバル配列と同じ subscript 経路に乗せる。 */
static node_t *build_static_local_array_addr_node(lvar_t *var) {
  /* global_vars リストから名前で引いて type_size を取る。 */
  short gv_type_size = (short)var->elem_size;
  for (global_var_t *gv = psx_find_global_var(var->static_global_name, var->static_global_name_len); gv; gv = NULL) {
    if (gv->name_len == var->static_global_name_len &&
        memcmp(gv->name, var->static_global_name, (size_t)gv->name_len) == 0) {
      gv_type_size = gv->type_size;
      break;
    }
  }
  node_t *base = psx_node_new_static_local_gvar_for(var, gv_type_size);
  node_mem_t *addr = arena_alloc(sizeof(node_mem_t));
  addr->base.kind = ND_ADDR;
  addr->base.lhs = base;
  psx_node_init_lvar_array_addr_metadata(addr, var, 0);
  return (node_t *)addr;
}

/* alias lvar が「static local 配列」を表すかを判別。
 * try_lower_static_local_array が is_static_local=1 + static_global_name +
 * elem_size>0 + size=0 + is_array=0 の組合せで登録する。スカラ static_local
 * (try_lower_static_local_scalar) は size>0 / fp_kind / pointer 等で別経路。 */
static int lvar_is_static_local_array(lvar_t *var) {
  return var->is_static_local && var->static_global_name &&
         var->elem_size > 0 && var->size == 0 && !var->is_vla &&
         !var->is_param;
}

// 配列ローカル変数（非 VLA）: ベースアドレスを ND_ADDR(ND_LVAR) として返す。
static node_t *build_array_lvar_addr_node(lvar_t *var) {
  node_mem_t *node = arena_alloc(sizeof(node_mem_t));
  node->base.kind = ND_ADDR;
  node->base.lhs = psx_node_new_lvar_for(var);
  psx_node_init_lvar_array_addr_metadata(node, var, var->tag_kind != TK_EOF);
  return (node_t *)node;
}

// byref 仮引数 (>16バイト構造体の値渡し): フレームスロットからポインタを読み込み、
// ND_DEREF でラップして「struct値」として見せる。
//   p.member → build_member_access(ND_DEREF(ptr_lvar), from_ptr=0)
//     → ND_ADDR(ND_DEREF(ptr_lvar)) = struct base ptr → offset → deref → member ✓
static node_t *build_byref_param_node(lvar_t *var) {
  node_t *ptr_lvar = psx_node_new_lvar_typed_for(var, 8); // loads ptr from frame
  node_mem_t *deref = arena_alloc(sizeof(node_mem_t));
  deref->base.kind = ND_DEREF;
  deref->base.lhs = ptr_lvar;
  deref->type_size = var->elem_size; // 実際の構造体サイズ
  deref->tag_kind = var->tag_kind;
  deref->tag_name = var->tag_name;
  deref->tag_len = var->tag_len;
  deref->is_tag_pointer = 0; // 値（構造体）であってポインタではない
  return (node_t *)deref;
}

// 通常のローカル変数 / VLA: lvar から型属性をコピーした ND_LVAR ノードを作る。
// VLA や配列は「ポインタとして扱う」分岐があり、deref_size / inner_deref_size が
// 多次元 VLA / outer_stride によって変わる。
static node_t *build_lvar_or_vla_node(lvar_t *var) {
  /* `static` ローカル: 実体はグローバルに lowering されているので、
   * ローカル参照ではなく ND_GVAR を返す。 */
  if (var->is_static_local && var->static_global_name) {
    int sz = var->size > 0 ? var->size : var->elem_size;
    return psx_node_new_static_local_gvar_for(var, sz);
  }
  int lvar_is_pointer = var->is_array || var->is_vla || var->pointer_qual_levels > 0 ||
                        (var->size > var->elem_size) ||
                        /* 配列へのポインタ `T (*p)[N]`: size==8、outer_stride>0。
                         * 要素 struct が 8B のとき `size > elem_size` (8>8) が偽になり
                         * ポインタと認識されず subscript が E3064 になっていた。 */
                        (var->outer_stride > 0 && var->size == 8 && !var->is_array && !var->is_vla) ||
                        /* `struct T *p` 仮引数: size == elem_size == 8 でも
                         * is_tag_pointer が立つのでこれをポインタとして認識する。 */
                        var->is_tag_pointer ||
                        /* `double *p` / `float *p`: size == elem_size == 8 で上の
                         * size>elem_size 判定に漏れるため、fp ポインタの印である
                         * pointee_fp_kind でポインタと認識する。 */
                        var->pointee_fp_kind != TK_FLOAT_KIND_NONE;
  /* type_size はポインタなら 8。`struct T *p` は elem_size に pointee の struct
   * サイズ (例 16) が入っているため、is_tag_pointer を見ずに elem_size を使うと
   * sizeof が誤り、代入が struct コピー扱いされ隣接スタックを破壊する。 */
  node_t *n = psx_node_new_lvar_typed_for(var,
      lvar_is_pointer ? 8 : var->elem_size);
  // 多次元VLA: outer_strideが設定されていれば外側サブスクリプトストライドとして使用
  // runtime inner (outer_stride=0): deref_sizeは0のまま (vla_row_stride_frame_offで実行時参照)
  int vla_effective_deref = 0;
  if (lvar_is_pointer) {
    vla_effective_deref = (var->outer_stride > 0) ? var->outer_stride
                            : (var->vla_row_stride_frame_off ? 0 : var->elem_size);
  }
  as_lvar(n)->mem.deref_size = vla_effective_deref;
  // 2D VLA / 多次元配列param: サブスクリプト結果の要素サイズ (次の次元のstride, 0=スカラ)
  // 3D 配列パラメータ (mid_stride > 0) では inner_deref_size=mid_stride、
  // next_deref_size=elem_size とすることで 2 段サブスクリプト後の要素アクセスに繋ぐ。
  int vla_is_multidim = (var->outer_stride != var->elem_size) ||
                        (var->vla_row_stride_frame_off != 0);
  if (var->mid_stride > 0) {
    as_lvar(n)->mem.inner_deref_size = (short)var->mid_stride;
    as_lvar(n)->mem.next_deref_size = (short)var->elem_size;
  } else if (var->vla_strides_remaining > 0) {
    /* N-D VLA (N>=3): 1 段目は vla_row (runtime)、subscript で vla_row が +=8 シフトし
     * remaining が消費される。各段の result deref に「中間配列である」と知らせるため
     * inner_deref_size=elem を立てる (2D VLA と同じ。vla_rsf が立つ間は runtime stride が
     * 優先されるので、inner_deref_size の値そのものは stride 計算に使われない。subscript の
     * 最終段で vla_row=0 に転じた後は、deref_size=elem として subscript_base_address_of が
     * 「まだ中間配列」と認識し正しく lhs (address) を返せる)。 */
    as_lvar(n)->mem.inner_deref_size = (short)var->elem_size;
    as_lvar(n)->mem.next_deref_size = (short)var->elem_size;
  } else {
    as_lvar(n)->mem.inner_deref_size = vla_is_multidim ? var->elem_size : 0;
  }
  as_lvar(n)->mem.is_pointer = lvar_is_pointer;
  return n;
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
  if (!var && psx_ctx_has_function_name(tok->str, tok->len)) {
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
  if (var->is_array && !var->is_vla) {
    return annotate_lvar_usage_node(build_array_lvar_addr_node(var), var, ctx);
  }
  if (var->is_byref_param) {
    return annotate_lvar_usage_node(build_byref_param_node(var), var, ctx);
  }
  return annotate_lvar_usage_node(build_lvar_or_vla_node(var), var, ctx);
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
