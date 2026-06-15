#include "internal/expr.h"
#include "internal/arena.h"
#include "internal/core.h"
#include "internal/decl.h"
#include "internal/diag.h"
#include "internal/dynarray.h"
#include "internal/node_utils.h"
#include "internal/semantic_ctx.h"
#include "config_runtime.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static token_kind_t g_current_ret_token_kind = TK_INT;
static tk_float_kind_t g_current_ret_fp_kind = TK_FLOAT_KIND_NONE;
static int g_current_ret_struct_size = 0;
static int g_current_ret_is_pointer = 0;
static char *g_current_funcname = NULL;
static int g_current_funcname_len = 0;
static int string_label_count = 0;
static int float_label_count = 0;
static int compound_lit_seq = 0;
static int g_expr_nest_depth = 0;
static int g_paren_nest_depth = 0;

#define PS_MAX_EXPR_NEST_DEPTH 1024
#define PS_MAX_PAREN_NEST_DEPTH 1024

static inline token_t *curtok(void) { return tk_get_current_token(); }
static inline void set_curtok(token_t *tok) { tk_set_current_token(tok); }

static node_lvar_t *as_lvar(node_t *node) { return (node_lvar_t *)node; }

typedef struct {
  token_kind_t kind;
  int scalar_size;
  int is_unsigned;
  int is_pointer;
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
} generic_type_t;

static void consume_local_type_quals(token_t **cur);
static long long eval_const_expr_type_size(node_t *n, int *ok);
static void apply_array_abstract_suffix_size(int *sz);
static int is_type_name_start_token(token_t *t);
static char *new_compound_lit_name(void);
static node_t *new_typed_lvar_ref(lvar_t *var, int is_pointer);
static node_t *apply_postfix(node_t *node);
static node_t *parse_compound_literal_from_type(token_kind_t cast_kind, int cast_is_ptr,
                                                token_t *after_rparen,
                                                token_kind_t cast_tag_kind, char *cast_tag_name, int cast_tag_len,
                                                int cast_elem_size, tk_float_kind_t cast_fp_kind,
                                                int cast_array_count);

static void enter_expr_nest_or_die(void) {
  g_expr_nest_depth++;
  if (g_expr_nest_depth > PS_MAX_EXPR_NEST_DEPTH) {
    psx_diag_ctx(curtok(), "expr", "式ネストが深すぎます（上限 %d）", PS_MAX_EXPR_NEST_DEPTH);
  }
}

static void leave_expr_nest(void) {
  if (g_expr_nest_depth > 0) g_expr_nest_depth--;
}

static void enter_paren_nest_or_die(void) {
  g_paren_nest_depth++;
  if (g_paren_nest_depth > PS_MAX_PAREN_NEST_DEPTH) {
    psx_diag_ctx(curtok(), "paren", "括弧ネストが深すぎます（上限 %d）", PS_MAX_PAREN_NEST_DEPTH);
  }
}

static void leave_paren_nest(void) {
  if (g_paren_nest_depth > 0) g_paren_nest_depth--;
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
  int sz = psx_node_type_size(node);
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

static int parse_array_of_ptr_to_array_abstract_decl(token_t **ptok, int *out_array_mul) {
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
  while (*cur && ((*cur)->kind == TK_CONST || (*cur)->kind == TK_VOLATILE || (*cur)->kind == TK_RESTRICT)) {
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
                                            int *out_is_unsigned, token_t **out_next) {
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
  return 1;
}

static generic_type_t infer_generic_control_type(node_t *control) {
  generic_type_t gt = {TK_INT, 4, 0, 0, TK_EOF, NULL, 0, 0, 0, 0, 0, 0, TK_FLOAT_KIND_NONE, 0, 0, 0};
  if (!control) return gt;
  int is_tag_ptr = 0;
  psx_node_get_tag_type(control, &gt.tag_kind, &gt.tag_name, &gt.tag_len, &is_tag_ptr);
  if (!is_tag_ptr && (gt.tag_kind == TK_STRUCT || gt.tag_kind == TK_UNION)) {
    gt.kind = gt.tag_kind;
    return gt;
  }
  if (control->kind == ND_STRING) {
    gt.kind = TK_CHAR;
    gt.scalar_size = 1;
    gt.is_pointer = 1;
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
    return gt;
  }
  int ts = psx_node_type_size(control);
  int ds = psx_node_deref_size(control);
  int is_ptr = 0;
  if (control->kind == ND_LVAR) is_ptr = ((node_lvar_t *)control)->mem.is_pointer;
  else if (control->kind == ND_GVAR || control->kind == ND_DEREF || control->kind == ND_ASSIGN ||
           control->kind == ND_ADDR || control->kind == ND_STRING) {
    is_ptr = ((node_mem_t *)control)->is_pointer;
  }
  if (is_ptr) {
    gt.is_pointer = 1;
    gt.kind = TK_INT;
    gt.ptr_levels = psx_node_pointer_qual_levels(control);
    gt.ptr_deref_size = ds;
    gt.ptr_base_deref_size = psx_node_base_deref_size(control);
    gt.ptr_pointee_fp_kind = psx_node_pointee_fp_kind(control);
    if (control->kind == ND_LVAR) {
      gt.ptr_const_mask = ((node_lvar_t *)control)->mem.pointer_const_qual_mask;
      gt.ptr_volatile_mask = ((node_lvar_t *)control)->mem.pointer_volatile_qual_mask;
      gt.ptr_pointee_unsigned = ((node_lvar_t *)control)->mem.is_unsigned;
      gt.ptr_pointee_const = ((node_lvar_t *)control)->mem.is_const_qualified;
      gt.ptr_pointee_volatile = ((node_lvar_t *)control)->mem.is_volatile_qualified;
    } else if (control->kind == ND_GVAR || control->kind == ND_DEREF || control->kind == ND_ASSIGN ||
               control->kind == ND_ADDR || control->kind == ND_STRING) {
      gt.ptr_const_mask = ((node_mem_t *)control)->pointer_const_qual_mask;
      gt.ptr_volatile_mask = ((node_mem_t *)control)->pointer_volatile_qual_mask;
      gt.ptr_pointee_unsigned = ((node_mem_t *)control)->is_unsigned;
      gt.ptr_pointee_const = ((node_mem_t *)control)->is_const_qualified;
      gt.ptr_pointee_volatile = ((node_mem_t *)control)->is_volatile_qualified;
    }
    return gt;
  }
  if (control->kind == ND_LVAR) gt.is_unsigned = ((node_lvar_t *)control)->mem.is_unsigned;
  else if (control->kind == ND_GVAR || control->kind == ND_DEREF || control->kind == ND_ASSIGN ||
           control->kind == ND_ADDR || control->kind == ND_STRING) {
    gt.is_unsigned = ((node_mem_t *)control)->is_unsigned;
  } else {
    gt.is_unsigned = control->is_unsigned;
  }
  gt.scalar_size = ts ? ts : 4;
  gt.kind = gt.is_unsigned ? TK_UNSIGNED : TK_INT;
  return gt;
}

static int generic_type_matches(generic_type_t control, generic_type_t assoc) {
  if (control.is_pointer != assoc.is_pointer) return 0;
  if (control.is_pointer) {
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
  if (control.kind == TK_STRUCT || control.kind == TK_UNION) {
    return control.kind == assoc.kind &&
           control.tag_len == assoc.tag_len &&
           strncmp(control.tag_name ? control.tag_name : "",
                   assoc.tag_name ? assoc.tag_name : "",
                   (size_t)control.tag_len) == 0;
  }
  if (control.kind == TK_FLOAT || control.kind == TK_DOUBLE ||
      assoc.kind == TK_FLOAT || assoc.kind == TK_DOUBLE) {
    return control.kind == assoc.kind;
  }
  return control.scalar_size == assoc.scalar_size && control.is_unsigned == assoc.is_unsigned;
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
    psx_ctx_find_typedef_name(id->str, id->len, &base_kind, &elem_size, &fp_kind,
                              &tag_kind, &tag_name, &tag_len,
                              &is_ptr, base_const, base_volatile, &td_is_unsigned);
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
  if (parse_integer_cast_spec_sequence(curtok(), &tk, base_elem_size, base_unsigned, &after)) {
    out->kind = tk;
    set_curtok(after);
    return 1;
  }
  tk = psx_consume_type_kind();
  if (tk == TK_EOF) return 0;
  out->kind = tk;
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
  (void)parse_array_of_ptr_to_array_abstract_decl(pt, NULL);
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
  set_curtok(t);
  return 1;
}

/* rvalue struct (`f().x`): 一時 lvar に代入してメンバアドレス取得可能にする。
 * 戻り値は `(tmp = base, tmp)` 形の ND_COMMA。 */
static node_t *materialize_struct_rvalue_funcall(node_t *base,
                                                  token_kind_t base_tag_kind,
                                                  char *base_tag_name, int base_tag_len) {
  int obj_size = psx_ctx_get_tag_size(base_tag_kind, base_tag_name, base_tag_len);
  if (obj_size <= 0) obj_size = psx_node_type_size(base);
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
    }
  } else if (mem_is_ptr && mem_size > 0) {
    /* スカラポインタメンバ (`char *name`): subscript や pointer 算術で
     * is_pointer 判定が要るため立てておく。is_scalar_ptr_member を立てて
     * 配列メンバの decay 表現と区別する。 */
    deref->is_pointer = 1;
    deref->is_scalar_ptr_member = 1;
  }
  deref->tag_kind = mem_info->tag_kind;
  deref->tag_name = mem_info->tag_name;
  deref->tag_len = mem_info->tag_len;
  deref->is_tag_pointer = mem_is_ptr;
  deref->bit_width = mem_info->bit_width;
  deref->bit_offset = mem_info->bit_offset;
  deref->bit_is_signed = mem_info->bit_is_signed;
  /* float/double メンバなら fp_kind を deref に伝播。 */
  if (mem_info->fp_kind != TK_FLOAT_KIND_NONE) {
    deref->base.fp_kind = mem_info->fp_kind;
  }
  /* _Bool メンバ: 配列メンバなら pointee_is_bool、それ以外は is_bool。 */
  if (mem_info->is_bool) {
    if (mem_array_len > 0 && mem_size > 0) deref->pointee_is_bool = 1;
    else                                    deref->is_bool = 1;
  }
  /* unsigned メンバ: load を zero-extend させるため is_unsigned を伝播
   * (配列メンバは要素 subscript 後に判定するのでスカラメンバのみ)。 */
  if (mem_info->is_unsigned && !(mem_array_len > 0 && mem_size > 0)) {
    deref->is_unsigned = 1;
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
  if (!psx_ctx_find_tag_member_info(base_tag_kind, base_tag_name, base_tag_len,
                                     member->str, member->len, &mem_info)) {
    psx_diag_ctx(op_tok, "member", diag_message_for(DIAG_ERR_PARSER_MEMBER_NOT_FOUND),
                 member->len, member->str);
  }
  return build_member_deref_node(base, from_ptr, &mem_info);
}

static node_t *parse_compound_literal_from_type(token_kind_t cast_kind, int cast_is_ptr,
                                                token_t *after_rparen,
                                                token_kind_t cast_tag_kind, char *cast_tag_name, int cast_tag_len,
                                                int cast_elem_size, tk_float_kind_t cast_fp_kind,
                                                int cast_array_count) {
  set_curtok(after_rparen);
  int base_elem = cast_elem_size > 0 ? cast_elem_size : 8;
  // `(T[]){...}` の空サイズは初期化子から要素数を推定する。
  if (!cast_is_ptr && cast_array_count < 0) {
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
    }
  }
  /* `(int (*[N])(args)){f1, f2, ...}` の関数ポインタ配列 compound literal を
   * 「配列実体」(size=N*8, elem=8 byte ポインタ要素) として登録する。
   * cast_is_ptr=1 + cast_array_count>0 で識別。修正前は cast_is_ptr=1 だけ
   * 見て「スカラポインタ」扱いし、初期化子経路がスカラ brace と誤認していた
   * (p304: 2 要素 brace で E3025)。 */
  int is_funcptr_array = (cast_is_ptr && cast_array_count > 0) ? 1 : 0;
  int is_arr = ((!cast_is_ptr && cast_array_count > 0) || is_funcptr_array) ? 1 : 0;
  if (is_funcptr_array) base_elem = 8;
  int var_size = is_funcptr_array ? (8 * cast_array_count)
                : (cast_is_ptr ? 8 : (is_arr ? base_elem * cast_array_count : base_elem));
  char *tmp_name = new_compound_lit_name();
  if (g_current_funcname == NULL) {
    tk_expect('{');
    node_t *init_expr = psx_expr_assign();
    tk_expect('}');
    if (!is_arr && init_expr && init_expr->kind == ND_NUM) {
      return apply_postfix(init_expr);
    }
    global_var_t *gv = calloc(1, sizeof(global_var_t));
    gv->name = tmp_name;
    gv->name_len = (int)strlen(tmp_name);
    gv->type_size = var_size;
    gv->deref_size = base_elem;
    gv->is_array = is_arr;
    if (init_expr && init_expr->kind == ND_NUM) {
      gv->has_init = 1;
      gv->init_val = ((node_num_t *)init_expr)->val;
    }
    gv->next = global_vars;
    global_vars = gv;
    node_gvar_t *gvar_node = arena_alloc(sizeof(node_gvar_t));
    gvar_node->mem.base.kind = ND_GVAR;
    gvar_node->mem.type_size = gv->type_size;
    gvar_node->mem.deref_size = gv->deref_size;
    gvar_node->name = gv->name;
    gvar_node->name_len = gv->name_len;
    gvar_node->is_thread_local = gv->is_thread_local;
    return apply_postfix((node_t *)gvar_node);
  }
  lvar_t *var = psx_decl_register_lvar_sized(tmp_name, (int)strlen(tmp_name), var_size, base_elem, is_arr);
  var->tag_kind = cast_tag_kind;
  var->tag_name = cast_tag_name;
  var->tag_len = cast_tag_len;
  /* 関数ポインタ配列は tag (struct/union) ではないので is_tag_pointer=0。
   * base_deref_size=8 はローカル `int (*ops[N])(args)` 登録 (decl.c:2259) と
   * 同じ。subscript の `ops[i]` を関数ポインタ値として load するために要る。 */
  if (is_funcptr_array) {
    var->is_tag_pointer = 0;
    var->base_deref_size = 8;
  } else {
    var->is_tag_pointer = cast_is_ptr ? 1 : 0;
  }
  var->fp_kind = cast_fp_kind;
  node_t *init = psx_decl_parse_initializer_for_var(var, cast_is_ptr);
  node_t *ref;
  if (is_arr) {
    node_mem_t *addr_node = arena_alloc(sizeof(node_mem_t));
    addr_node->base.kind = ND_ADDR;
    addr_node->base.lhs = psx_node_new_lvar(var->offset);
    addr_node->type_size = var->elem_size;
    addr_node->deref_size = var->elem_size;
    /* `(int[N]){...}` 複合リテラルは配列名と同じくポインタへ崩壊する。
     * 後続の `[i]` サブスクリプトを通すために is_pointer を立てる。 */
    addr_node->is_pointer = 1;
    ref = (node_t *)addr_node;
  } else {
    ref = new_typed_lvar_ref(var, cast_is_ptr);
  }
  (void)cast_kind;
  return psx_node_new_binary(ND_COMMA, init, apply_postfix(ref));
}

static int parse_cast_type(token_t *tok, token_kind_t *type_kind, int *is_pointer, token_t **after_rparen,
                           token_kind_t *out_tag_kind, char **out_tag_name, int *out_tag_len,
                           int *out_elem_size, tk_float_kind_t *out_fp_kind, int *out_array_count) {
  if (!tok || tok->kind != TK_LPAREN) return 0;
  token_t *t = tok->next;
  if (!t) return 0;
  *type_kind = TK_EOF;
  if (out_tag_kind) *out_tag_kind = TK_EOF;
  if (out_tag_name) *out_tag_name = NULL;
  if (out_tag_len) *out_tag_len = 0;
  if (out_elem_size) *out_elem_size = 8;
  if (out_fp_kind) *out_fp_kind = TK_FLOAT_KIND_NONE;
  if (out_array_count) *out_array_count = 0;

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
      } else if (parse_integer_cast_spec_sequence(q, &inner_kind, &inner_elem, NULL, &q)) {
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
      psx_ctx_find_typedef_name(id->str, id->len, &td_base, &td_elem, &td_fp, &td_tag, &td_tag_name, &td_tag_len,
                                &td_ptr, NULL, NULL, NULL);
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
      if (parse_integer_cast_spec_sequence(t, type_kind, out_elem_size, NULL, &t)) {
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
    psx_ctx_find_typedef_name(id->str, id->len, &td_base, &td_elem, &td_fp, &td_tag, &td_tag_name, &td_tag_len,
                              &td_ptr, NULL, NULL, NULL);
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
  (void)parse_array_of_ptr_to_array_abstract_decl(&t, NULL);
  (void)parse_array_of_ptr_to_array_of_ptr_abstract_decl(&t, NULL);
  (void)parse_ptr_to_func_returning_ptr_to_array_abstract_decl(&t);
  (void)parse_array_of_ptr_to_func_returning_ptr_to_array_abstract_decl(&t, NULL);
  (void)parse_ptr_to_func_returning_ptr_to_func_abstract_decl(&t);
  (void)parse_ptr_to_func_returning_ptr_to_func_returning_ptr_to_array_abstract_decl(&t);
  // 配列宣言子 [N] を受理する（非ポインタ型のみ）
  // 空 `[]` は -1 を返し、呼び出し側で初期化子から要素数を推定させる。
  if (!*is_pointer && t && t->kind == TK_LBRACKET) {
    t = t->next;
    int n = 0;
    if (t && t->kind == TK_RBRACKET) {
      n = -1; // size unspecified, infer from initializer
    } else if (t && t->kind == TK_NUM && tk_as_num(t)->num_kind == TK_NUM_KIND_INT) {
      n = (int)tk_as_num_int(t)->uval;
      t = t->next;
    }
    if (!t || t->kind != TK_RBRACKET) return 0;
    t = t->next;
    if (out_array_count) *out_array_count = n;
  }
  if (!t || t->kind != TK_RPAREN) return 0;
  *after_rparen = t->next;
  return 1;
}

static node_t *expr_internal(void);
static node_t *assign(void);
static node_t *conditional(void);
static node_t *logical_or(void);
static node_t *logical_and(void);
static node_t *bit_or(void);
static node_t *bit_xor(void);
static node_t *bit_and(void);
static node_t *equality(void);
static node_t *relational(void);
static node_t *shift(void);
static node_t *add(void);
static node_t *mul(void);
static node_t *cast(void);
static node_t *unary(void);
static node_t *primary(void);
static node_t *apply_postfix(node_t *node);

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
  int op_sz = psx_node_type_size(v);
  return op_sz > 0 && cast_elem_size > 0 && op_sz == cast_elem_size;
}

static char *new_compound_lit_name(void) {
  int n = compound_lit_seq++;
  int len = snprintf(NULL, 0, "__compound_lit_%d", n);
  char *name = calloc((size_t)len + 1, 1);
  snprintf(name, (size_t)len + 1, "__compound_lit_%d", n);
  return name;
}

static node_t *new_typed_lvar_ref(lvar_t *var, int is_pointer) {
  node_t *ref = psx_node_new_lvar_typed(var->offset, is_pointer ? 8 : var->elem_size);
  ref->fp_kind = var->fp_kind;
  as_lvar(ref)->mem.deref_size = var->elem_size;
  as_lvar(ref)->mem.is_pointer = is_pointer;
  as_lvar(ref)->mem.tag_kind = var->tag_kind;
  as_lvar(ref)->mem.tag_name = var->tag_name;
  as_lvar(ref)->mem.tag_len = var->tag_len;
  as_lvar(ref)->mem.is_tag_pointer = var->is_tag_pointer;
  as_lvar(ref)->mem.is_const_qualified = var->is_const_qualified;
  as_lvar(ref)->mem.is_volatile_qualified = var->is_volatile_qualified;
  as_lvar(ref)->mem.is_pointer_const_qualified = var->is_pointer_const_qualified;
  as_lvar(ref)->mem.is_pointer_volatile_qualified = var->is_pointer_volatile_qualified;
  as_lvar(ref)->mem.pointer_const_qual_mask = var->pointer_const_qual_mask;
  as_lvar(ref)->mem.pointer_volatile_qual_mask = var->pointer_volatile_qual_mask;
  as_lvar(ref)->mem.pointer_qual_levels = var->pointer_qual_levels;
  as_lvar(ref)->mem.base_deref_size = var->base_deref_size;
  as_lvar(ref)->mem.is_unsigned = var->is_unsigned;
  return ref;
}

static node_t *new_member_lvar_ref(lvar_t *owner, int member_offset, int member_type_size,
                                   token_kind_t member_tag_kind, char *member_tag_name,
                                   int member_tag_len, int member_is_tag_pointer) {
  node_t *lvar = psx_node_new_lvar_typed(owner->offset + member_offset, member_type_size);
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
static int finish_parenthesized_type_size(token_t *t, int sz) {
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
  apply_array_abstract_suffix_size(&sz);
  tk_expect(')');
  return sz;
}

static int parse_parenthesized_type_size(void) {
  token_t *t = curtok();
  if (t->kind == TK_LPAREN && is_type_name_start_token(t->next)) {
    set_curtok(t->next);
    int sz = parse_parenthesized_type_size();
    if (sz < 0) return -1;
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
    return finish_parenthesized_type_size(t, sz);
  }
  if ((t->kind == TK_FLOAT || t->kind == TK_DOUBLE) &&
      t->next && (t->next->kind == TK_COMPLEX || t->next->kind == TK_IMAGINARY)) {
    int base_sz = (t->kind == TK_FLOAT) ? 4 : 8;
    int sz = base_sz * 2; // _Complex: 基底型の2倍
    t = t->next->next;
    return finish_parenthesized_type_size(t, sz);
  }
  if (t->kind == TK_LONG && t->next && t->next->kind == TK_DOUBLE &&
      t->next->next &&
      (t->next->next->kind == TK_COMPLEX || t->next->next->kind == TK_IMAGINARY)) {
    int sz = 8 * 2; // _Complex long double = 16B (lowering)
    t = t->next->next->next;
    return finish_parenthesized_type_size(t, sz);
  }

  // long double: 2トークン型名
  if (t->kind == TK_LONG && t->next && t->next->kind == TK_DOUBLE) {
    t = t->next->next;
    int sz = 8; // macOS/AArch64: long double == double (64-bit)
    return finish_parenthesized_type_size(t, sz);
  }
  bool is_type = false;
  int scalar_size = 8;
  token_kind_t type_kind = t->kind;
  psx_ctx_get_type_info(type_kind, &is_type, &scalar_size);
  if (is_type) {
    t = t->next;
    // Extension: treat sizeof(void) as 1 (GNU-compatible behavior).
    int sz = (type_kind == TK_VOID) ? 1 : scalar_size;
    return finish_parenthesized_type_size(t, sz);
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
    t = curtok();
    return finish_parenthesized_type_size(t, sz);
  }
  if (psx_ctx_is_typedef_name_token(t)) {
    token_ident_t *id = (token_ident_t *)t;
    token_kind_t td_base = TK_EOF;
    int td_elem = 8;
    tk_float_kind_t td_fp = TK_FLOAT_KIND_NONE;
    token_kind_t td_tag = TK_EOF;
    char *td_tag_name = NULL;
    int td_tag_len = 0;
    int td_ptr = 0;
    int td_sizeof = 0;
    psx_ctx_find_typedef_name(id->str, id->len, &td_base, &td_elem, &td_fp, &td_tag, &td_tag_name, &td_tag_len,
                              &td_ptr, NULL, NULL, NULL);
    t = t->next;
    int sz = td_ptr ? 8 : td_elem;
    if (!td_ptr && psx_ctx_find_typedef_sizeof(id->str, id->len, &td_sizeof)) {
      sz = td_sizeof;
    }
    return finish_parenthesized_type_size(t, sz);
  }
  return -1;
}
static node_t *parse_call_postfix(node_t *callee);

void psx_expr_set_current_func_ret_type(token_kind_t ret_kind, tk_float_kind_t fp_kind) {
  g_current_ret_token_kind = ret_kind;
  g_current_ret_fp_kind = fp_kind;
}

void psx_expr_set_current_func_ret_struct_size(int size) {
  g_current_ret_struct_size = size;
}

int psx_expr_current_func_ret_struct_size(void) {
  return g_current_ret_struct_size;
}

token_kind_t psx_expr_current_func_ret_token_kind(void) {
  return g_current_ret_token_kind;
}

tk_float_kind_t psx_expr_current_func_ret_fp_kind(void) {
  return g_current_ret_fp_kind;
}

void psx_expr_set_current_func_ret_is_pointer(int is_pointer) {
  g_current_ret_is_pointer = is_pointer ? 1 : 0;
}

int psx_expr_current_func_ret_is_pointer(void) {
  return g_current_ret_is_pointer;
}

void psx_expr_set_current_funcname(char *name, int len) {
  g_current_funcname = name;
  g_current_funcname_len = len;
}

void psx_expr_get_current_funcname(char **out_name, int *out_len) {
  if (out_name) *out_name = g_current_funcname;
  if (out_len) *out_len = g_current_funcname_len;
}

// expr = assign ("," assign)*
node_t *psx_expr_expr(void) {
  return expr_internal();
}

// assign = conditional (("=" | "+=" | "-=" | "*=" | "/=" | "%=" | "<<=" | ">>=" | "&=" | "^=" | "|=") assign)?
node_t *psx_expr_assign(void) {
  return assign();
}

static node_t *expr_internal(void) {
  enter_expr_nest_or_die();
  node_t *node = assign();
  while (curtok()->kind == TK_COMMA) {
    set_curtok(curtok()->next);
    node_t *rhs = assign();
    node_t *comma = psx_node_new_binary(ND_COMMA, node, rhs);
    comma->fp_kind = rhs ? rhs->fp_kind : TK_FLOAT_KIND_NONE;
    node = comma;
  }
  leave_expr_nest();
  return node;
}

// 浮動小数式を整数へ変換するため ND_FP_TO_INT でラップ。fp_kind==NONE なら no-op。
// `(int)d`/`(char)d`/`(long)d` 等で codegen に fcvtzs を出させるために使う。
static node_t *wrap_fp_to_int_if_needed(node_t *operand) {
  if (!operand || operand->fp_kind == TK_FLOAT_KIND_NONE) return operand;
  node_t *cvt = arena_alloc(sizeof(node_t));
  cvt->kind = ND_FP_TO_INT;
  cvt->lhs = operand;
  cvt->fp_kind = TK_FLOAT_KIND_NONE;
  return cvt;
}

/* `(float)x` / `(double)x` キャスト。operand が目的のFP型と異なる (整数、または
 * float↔double の別幅) 場合に ND_INT_TO_FP でラップし、codegen が I2F/F2F 変換を
 * 発行できるようにする。同じFP型なら no-op で素通りさせる。 */
static node_t *wrap_to_fp(node_t *operand, tk_float_kind_t target) {
  if (!operand) return operand;
  if (operand->fp_kind == target) return operand;
  // float(4) と double/long double(8) は同幅グループで判定する。
  bool op_is_double = operand->fp_kind >= TK_FLOAT_KIND_DOUBLE;
  bool tgt_is_double = target >= TK_FLOAT_KIND_DOUBLE;
  if (operand->fp_kind != TK_FLOAT_KIND_NONE && op_is_double == tgt_is_double) {
    operand->fp_kind = target;
    return operand;
  }
  node_t *cvt = arena_alloc(sizeof(node_t));
  cvt->kind = ND_INT_TO_FP;
  cvt->lhs = operand;
  cvt->fp_kind = target;
  return cvt;
}

static node_t *apply_cast(token_kind_t type_kind, int is_pointer, node_t *operand,
                          token_kind_t cast_tag_kind, char *cast_tag_name, int cast_tag_len) {
  if (is_pointer || type_kind == TK_LONG) {
    operand = wrap_fp_to_int_if_needed(operand);
    operand->fp_kind = TK_FLOAT_KIND_NONE;
    /* `(struct V *)x` / `(union U *)x`: tag 情報を後段の `->` 等が読めるよう
     * ND_PTR_CAST でラップする (operand 自体は他から共有される可能性があるので
     * 直接書き換えない)。これで `((struct V*)0)->b` のような offsetof 風や
     * `((struct V*)void_ptr)->m` が動く。 */
    if (is_pointer && (cast_tag_kind == TK_STRUCT || cast_tag_kind == TK_UNION)) {
      node_mem_t *wrap = arena_alloc(sizeof(node_mem_t));
      wrap->base.kind = ND_PTR_CAST;
      wrap->base.lhs = operand;
      wrap->tag_kind = cast_tag_kind;
      wrap->tag_name = cast_tag_name;
      wrap->tag_len = cast_tag_len;
      wrap->is_tag_pointer = 1;
      wrap->is_pointer = 1;
      wrap->type_size = 8;
      wrap->pointer_qual_levels = 1;
      return (node_t *)wrap;
    }
    // `(float*)X` / `(double*)X` の場合、後段の `*` deref が FP load を出せる
    // よう pointee_fp_kind を保持する ND_PTR_CAST でラップする。
    if (is_pointer && (type_kind == TK_FLOAT || type_kind == TK_DOUBLE)) {
      node_mem_t *wrap = arena_alloc(sizeof(node_mem_t));
      wrap->base.kind = ND_PTR_CAST;
      wrap->base.lhs = operand;
      wrap->pointee_fp_kind = (type_kind == TK_FLOAT) ? TK_FLOAT_KIND_FLOAT
                                                     : TK_FLOAT_KIND_DOUBLE;
      wrap->deref_size = (type_kind == TK_FLOAT) ? 4 : 8;
      wrap->type_size = 8; // pointer 値そのもの
      wrap->is_pointer = 1;
      wrap->pointer_qual_levels = 1;
      wrap->base_deref_size = (type_kind == TK_FLOAT) ? 4 : 8;
      return (node_t *)wrap;
    }
    /* `(int *)void_p` などポインタ型キャスト: 元の operand に pointee_is_void
     * が立っている場合、後続 deref エラーを誤発生させないよう ND_PTR_CAST で
     * ラップして pointee_is_void をクリアする。 */
    if (is_pointer && operand->kind == ND_LVAR &&
        ((node_lvar_t *)operand)->mem.pointee_is_void) {
      node_mem_t *wrap = arena_alloc(sizeof(node_mem_t));
      wrap->base.kind = ND_PTR_CAST;
      wrap->base.lhs = operand;
      wrap->is_pointer = 1;
      wrap->pointer_qual_levels = 1;
      wrap->type_size = 8;
      /* pointee_is_void は明示的にデフォルト (0) のままにする */
      return (node_t *)wrap;
    }
    /* (long)ptr のようにポインタ→long の明示キャスト: 結果は整数なので
     * node_mem_t の is_pointer をクリアし、後段の代入/初期化制約検査が
     * 誤発火しないようにする。 */
    if (!is_pointer && type_kind == TK_LONG) {
      if (operand->kind == ND_LVAR || operand->kind == ND_GVAR ||
          operand->kind == ND_DEREF || operand->kind == ND_ADDR ||
          operand->kind == ND_STRING || operand->kind == ND_PTR_CAST ||
          operand->kind == ND_ASSIGN) {
        ((node_mem_t *)operand)->is_pointer = 0;
      }
    }
    return operand;
  }
  if (type_kind == TK_STRUCT || type_kind == TK_UNION) {
    const char *kind = (type_kind == TK_STRUCT) ? "struct" : "union";
    psx_diag_ctx(curtok(), "cast", diag_message_for(DIAG_ERR_PARSER_CAST_NONSCALAR_UNSUPPORTED),
                 kind);
  }
  if (type_kind == TK_FLOAT) {
    return wrap_to_fp(operand, TK_FLOAT_KIND_FLOAT);
  }
  if (type_kind == TK_DOUBLE) {
    return wrap_to_fp(operand, TK_FLOAT_KIND_DOUBLE);
  }
  if (type_kind == TK_INT || type_kind == TK_ENUM) {
    operand = wrap_fp_to_int_if_needed(operand);
    operand->fp_kind = TK_FLOAT_KIND_NONE;
    /* ポインタ→整数の明示キャストでは初期化/代入時の制約違反検査を回避するため、
     * node_mem_t を持つノードの is_pointer をクリアする。 */
    if (operand->kind == ND_LVAR || operand->kind == ND_GVAR ||
        operand->kind == ND_DEREF || operand->kind == ND_ADDR ||
        operand->kind == ND_STRING || operand->kind == ND_PTR_CAST ||
        operand->kind == ND_ASSIGN) {
      ((node_mem_t *)operand)->is_pointer = 0;
      /* `(int)u` は符号付き int。終端値ノードでは is_unsigned をクリアして後段の
       * 比較/除算を signed にする (`i < (int)n` が unsigned 比較になっていた)。
       * binop ノード (シフト等) は is_unsigned が LSR/ASR など自身の演算も兼ねる
       * ため触れない (`(int)(u>>60)` の LSR を ASR に変えてしまう)。
       * ただし char/short など int 未満幅の operand では is_unsigned が load の
       * 符号拡張 (ldrsh/ldrh) も兼ねるため、ここで書き換えると値そのものが化ける
       * (`(int)(unsigned short)0xFFFF` が -1 に、`(unsigned)(short)-1` が 65535 に)。
       * sub-int operand は元の load 符号性を保ち、暗黙変換と同じ正しい昇格に任せる。 */
      if (psx_node_type_size(operand) >= 4) psx_node_set_unsigned(operand, 0);
    }
    return operand;
  }
  if (type_kind == TK_SIGNED || type_kind == TK_UNSIGNED) {
    operand = wrap_fp_to_int_if_needed(operand);
    operand->fp_kind = TK_FLOAT_KIND_NONE;
    int target_unsigned = (type_kind == TK_UNSIGNED) ? 1 : 0;
    /* sub-int (char/short) を (unsigned) へ: operand 自身の load 符号性 (ldrsh/ldrh)
     * を保ったまま 32bit unsigned 値へ昇格する必要がある。is_unsigned を直接立てると
     * load 拡張が変わり値が化ける。代わりに & 0xffffffff で 64bit reg 上の load 済み
     * 値を低 32bit へ折り返し、結果ノードに unsigned を付ける。これで
     * `(unsigned)(short)-1` が 0xffffffff になり、符号混在のインライン比較/除算も
     * 正しく unsigned 扱いになる (operand 幅<4 は UAC で signed 昇格扱いだった)。 */
    /* op_sz が 1/2 = 真の char/short load のみ対象。NUM 等は type_size 0 を返すので
     * 除外する (`(unsigned)13` を ND_BITAND で包んで誤って AST 形を変えないため)。 */
    int op_sz = psx_node_type_size(operand);
    if (target_unsigned && op_sz >= 1 && op_sz < 4 &&
        operand->fp_kind == TK_FLOAT_KIND_NONE && !psx_node_is_pointer(operand)) {
      node_t *masked = psx_node_new_binary(ND_BITAND, operand, psx_node_new_num(0xffffffffLL));
      psx_node_set_unsigned(masked, 1);
      return masked;
    }
    if (operand->kind == ND_LVAR || operand->kind == ND_GVAR ||
        operand->kind == ND_DEREF || operand->kind == ND_ADDR ||
        operand->kind == ND_STRING || operand->kind == ND_PTR_CAST ||
        operand->kind == ND_ASSIGN) {
      ((node_mem_t *)operand)->is_pointer = 0;
      /* sub-int operand では is_unsigned 書き換えが load 拡張を壊すため触れない
       * (上の TK_INT と同じ理由)。int 幅以上のみ符号ラベルを更新する。 */
      if (psx_node_type_size(operand) >= 4)
        psx_node_set_unsigned(operand, target_unsigned);
    }
    return operand;
  }
  if (type_kind == TK_BOOL) {
    return psx_node_new_binary(ND_NE, operand, psx_node_new_num(0));
  }
  if (type_kind == TK_VOID) {
    // 現状ASTでは専用ノードを持たず、既存ノードのまま評価値を捨てる文脈で利用する。
    operand->fp_kind = TK_FLOAT_KIND_NONE;
    return operand;
  }
  if (type_kind == TK_SHORT) {
    operand = wrap_fp_to_int_if_needed(operand);
    return psx_node_new_binary(ND_BITAND, operand, psx_node_new_num(0xffff));
  }
  if (type_kind == TK_CHAR) {
    operand = wrap_fp_to_int_if_needed(operand);
    return psx_node_new_binary(ND_BITAND, operand, psx_node_new_num(0xff));
  }
  // Guard rail for unexpected parser state: known cast kinds should be handled above.
  psx_diag_ctx(curtok(), "cast", "%s",
               diag_message_for(DIAG_ERR_PARSER_CAST_TYPE_RESOLVE_FAILED));
  return operand;
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

static node_t *assign(void) {
  node_t *node = conditional();
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
  switch (curtok()->kind) {
    case TK_ASSIGN: {
      psx_node_reject_const_assign(assign_target, "=");
      set_curtok(curtok()->next);
      node_t *rhs = assign();
      psx_node_reject_const_qual_discard(assign_target, rhs);
      if (assign_target->kind == ND_LVAR) {
        lvar_t *lv = psx_decl_find_lvar_by_offset(((node_lvar_t *)assign_target)->offset);
        if (lv) lv->is_initialized = 1;
      }
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
      assign_node->type_size = psx_node_type_size(assign_node->base.lhs);
      assign_node->base.fp_kind = assign_node->base.lhs ? assign_node->base.lhs->fp_kind : 0;
      node = (node_t *)assign_node;
      if (lhs_prefix) node = psx_node_new_binary(ND_COMMA, lhs_prefix, node);
      break;
    }
    case TK_PLUSEQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(assign_target, ND_ADD, assign(), "+="); if (lhs_prefix) node = psx_node_new_binary(ND_COMMA, lhs_prefix, node); break;
    case TK_MINUSEQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(assign_target, ND_SUB, assign(), "-="); if (lhs_prefix) node = psx_node_new_binary(ND_COMMA, lhs_prefix, node); break;
    case TK_MULEQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(assign_target, ND_MUL, assign(), "*="); if (lhs_prefix) node = psx_node_new_binary(ND_COMMA, lhs_prefix, node); break;
    case TK_DIVEQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(assign_target, ND_DIV, assign(), "/="); if (lhs_prefix) node = psx_node_new_binary(ND_COMMA, lhs_prefix, node); break;
    case TK_MODEQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(assign_target, ND_MOD, assign(), "%="); if (lhs_prefix) node = psx_node_new_binary(ND_COMMA, lhs_prefix, node); break;
    case TK_SHLEQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(assign_target, ND_SHL, assign(), "<<="); if (lhs_prefix) node = psx_node_new_binary(ND_COMMA, lhs_prefix, node); break;
    case TK_SHREQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(assign_target, ND_SHR, assign(), ">>="); if (lhs_prefix) node = psx_node_new_binary(ND_COMMA, lhs_prefix, node); break;
    case TK_ANDEQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(assign_target, ND_BITAND, assign(), "&="); if (lhs_prefix) node = psx_node_new_binary(ND_COMMA, lhs_prefix, node); break;
    case TK_XOREQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(assign_target, ND_BITXOR, assign(), "^="); if (lhs_prefix) node = psx_node_new_binary(ND_COMMA, lhs_prefix, node); break;
    case TK_OREQ: set_curtok(curtok()->next); node = psx_node_new_compound_assign(assign_target, ND_BITOR, assign(), "|="); if (lhs_prefix) node = psx_node_new_binary(ND_COMMA, lhs_prefix, node); break;
    default: break;
  }
  return node;
}

static node_t *conditional(void) {
  node_t *node = logical_or();
  if (curtok()->kind == TK_QUESTION) {
    set_curtok(curtok()->next);
    node_ctrl_t *ternary = arena_alloc(sizeof(node_ctrl_t));
    ternary->base.kind = ND_TERNARY;
    ternary->base.lhs = node;
    ternary->base.rhs = expr_internal();
    tk_expect(':');
    ternary->els = conditional();
    ternary->base.fp_kind = ternary->base.rhs->fp_kind;
    if (ternary->els && ternary->els->fp_kind > ternary->base.fp_kind) {
      ternary->base.fp_kind = ternary->els->fp_kind;
    }
    return (node_t *)ternary;
  }
  return node;
}

static node_t *logical_or(void) {
  node_t *node = logical_and();
  while (curtok()->kind == TK_OROR) {
    set_curtok(curtok()->next);
    node = psx_node_new_binary(ND_LOGOR, node, logical_and());
  }
  return node;
}

static node_t *logical_and(void) {
  node_t *node = bit_or();
  while (curtok()->kind == TK_ANDAND) {
    set_curtok(curtok()->next);
    node = psx_node_new_binary(ND_LOGAND, node, bit_or());
  }
  return node;
}

static node_t *bit_or(void) {
  node_t *node = bit_xor();
  while (curtok()->kind == TK_PIPE) {
    set_curtok(curtok()->next);
    node = psx_node_new_binary(ND_BITOR, node, bit_xor());
  }
  return node;
}

static node_t *bit_xor(void) {
  node_t *node = bit_and();
  while (curtok()->kind == TK_CARET) {
    set_curtok(curtok()->next);
    node = psx_node_new_binary(ND_BITXOR, node, bit_and());
  }
  return node;
}

static node_t *bit_and(void) {
  node_t *node = equality();
  while (curtok()->kind == TK_AMP) {
    set_curtok(curtok()->next);
    node = psx_node_new_binary(ND_BITAND, node, equality());
  }
  return node;
}

static node_t *equality(void) {
  node_t *node = relational();
  for (;;) {
    if (curtok()->kind == TK_EQEQ) {
      set_curtok(curtok()->next);
      node = psx_node_new_binary(ND_EQ, node, relational());
    } else if (curtok()->kind == TK_NEQ) {
      set_curtok(curtok()->next);
      node = psx_node_new_binary(ND_NE, node, relational());
    }
    else return node;
  }
}

static node_t *relational(void) {
  node_t *node = shift();
  for (;;) {
    if (curtok()->kind == TK_LT) {
      set_curtok(curtok()->next);
      node = psx_node_new_binary(ND_LT, node, shift());
    } else if (curtok()->kind == TK_LE) {
      set_curtok(curtok()->next);
      node = psx_node_new_binary(ND_LE, node, shift());
    } else if (curtok()->kind == TK_GT) {
      set_curtok(curtok()->next);
      node = psx_node_new_binary(ND_LT, shift(), node);
    } else if (curtok()->kind == TK_GE) {
      set_curtok(curtok()->next);
      node = psx_node_new_binary(ND_LE, shift(), node);
    }
    else return node;
  }
}

static node_t *shift(void) {
  node_t *node = add();
  for (;;) {
    if (curtok()->kind == TK_SHL) {
      set_curtok(curtok()->next);
      node = psx_node_new_binary(ND_SHL, node, add());
    } else if (curtok()->kind == TK_SHR) {
      set_curtok(curtok()->next);
      node = psx_node_new_binary(ND_SHR, node, add());
    }
    else return node;
  }
}

/* C11 6.5.6 のポインタ算術判定。struct/union タグポインタは is_pointer ではなく
 * is_tag_pointer で表現されるため、`&s[i] - &s[j]` や `sp + n` のスケーリング/差分が
 * 効かず byte 単位になっていた。タグポインタもポインタとして扱う。 */
static int node_is_ptr_for_arith(node_t *n) {
  if (psx_node_is_pointer(n)) return 1;
  token_kind_t tk = TK_EOF; char *tn = NULL; int tl = 0, is_tp = 0;
  psx_node_get_tag_type(n, &tk, &tn, &tl, &is_tp);
  return is_tp;
}

static node_t *add(void) {
  node_t *node = mul();
  for (;;) {
    if (curtok()->kind == TK_PLUS) {
      set_curtok(curtok()->next);
      node_t *rhs = mul();
      /* C11 6.5.6p2: 加算では「ポインタ + 整数」と「整数 + ポインタ」が
       * 共に許される。左が非ポインタで右がポインタなら可換性で swap し、
       * 既存の「左 = ポインタ, 右 = 整数 (scaling 対象)」経路に乗せる。
       * 修正前は `2 + a` で左辺 (整数) を見るだけで scaling されず、
       * `2 + a` が整数加算 → 不正アドレス deref で garbage 値を返していた。 */
      if (!node_is_ptr_for_arith(node) && node_is_ptr_for_arith(rhs)) {
        node_t *tmp = node; node = rhs; rhs = tmp;
      }
      if (node_is_ptr_for_arith(node)) {
        int ds = psx_node_deref_size(node);
        if (ds > 1) {
          // ポインタ + 整数: 整数を要素サイズ倍にスケーリング
          rhs = psx_node_new_binary(ND_MUL, rhs, psx_node_new_num(ds));
        }
      }
      node = psx_node_new_binary(ND_ADD, node, rhs);
    } else if (curtok()->kind == TK_MINUS) {
      set_curtok(curtok()->next);
      node_t *rhs = mul();
      int both_ptr = node_is_ptr_for_arith(node) && node_is_ptr_for_arith(rhs);
      if (both_ptr) {
        // ポインタ - ポインタ (C11 6.5.6p9): 結果は要素数 (= ptrdiff_t)。
        // (p - q) / sizeof(*p) を生成する。両辺が同じ型を指す前提。
        int ds = psx_node_deref_size(node);
        node_t *diff = psx_node_new_binary(ND_SUB, node, rhs);
        node = (ds > 1)
                 ? psx_node_new_binary(ND_DIV, diff, psx_node_new_num(ds))
                 : diff;
      } else {
        if (node_is_ptr_for_arith(node)) {
          int ds = psx_node_deref_size(node);
          if (ds > 1) {
            // ポインタ - 整数: 整数を要素サイズ倍にスケーリング
            rhs = psx_node_new_binary(ND_MUL, rhs, psx_node_new_num(ds));
          }
        }
        node = psx_node_new_binary(ND_SUB, node, rhs);
      }
    }
    else return node;
  }
}

static node_t *mul(void) {
  node_t *node = cast();
  for (;;) {
    if (curtok()->kind == TK_MUL) {
      set_curtok(curtok()->next);
      node = psx_node_new_binary(ND_MUL, node, cast());
    } else if (curtok()->kind == TK_DIV) {
      set_curtok(curtok()->next);
      node = psx_node_new_binary(ND_DIV, node, cast());
    } else if (curtok()->kind == TK_MOD) {
      set_curtok(curtok()->next);
      node = psx_node_new_binary(ND_MOD, node, cast());
    }
    else return node;
  }
}

static node_t *cast(void) {
  token_kind_t cast_kind = TK_EOF;
  int cast_is_ptr = 0;
  token_t *after_rparen = NULL;
  token_kind_t cast_tag_kind = TK_EOF;
  char *cast_tag_name = NULL;
  int cast_tag_len = 0;
  int cast_elem_size = 8;
  tk_float_kind_t cast_fp_kind = TK_FLOAT_KIND_NONE;
  int cast_array_count = 0;
  if (parse_cast_type(curtok(), &cast_kind, &cast_is_ptr, &after_rparen,
                      &cast_tag_kind, &cast_tag_name, &cast_tag_len,
                      &cast_elem_size, &cast_fp_kind, &cast_array_count)) {
    if (after_rparen && after_rparen->kind == TK_LBRACE) {
      // compound literal は primary/postfix 側で処理する
      return unary();
    }
    set_curtok(after_rparen);
    node_t *operand = cast();
    if (!cast_is_ptr && (cast_kind == TK_STRUCT || cast_kind == TK_UNION)) {
      if (is_same_tag_nonscalar_expr(operand, cast_kind, cast_tag_name, cast_tag_len)) {
        // same-tag non-scalar cast: treat as no-op for now
        return apply_postfix(operand);
      }
      if (ps_get_enable_size_compatible_nonscalar_cast() &&
          is_size_compatible_nonscalar_expr(operand, cast_kind, cast_elem_size)) {
        // minimal extension: same-kind and same-size non-scalar cast as no-op
        return apply_postfix(operand);
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
                                                     cast_elem_size, cast_fp_kind));
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
                                                    cast_elem_size, cast_fp_kind));
      }
      const char *kind = (cast_kind == TK_STRUCT) ? "struct" : "union";
      psx_diag_ctx(curtok(), "cast", diag_message_for(DIAG_ERR_PARSER_CAST_NONSCALAR_UNSUPPORTED),
                   kind);
    }
    return apply_postfix(apply_cast(cast_kind, cast_is_ptr, operand,
                                     cast_tag_kind, cast_tag_name, cast_tag_len));
  }
  return unary();
}

// sizeof (expr) / sizeof (type) を処理する。`sizeof` トークンは呼び出し前に消費済み。
// VLA: sizeof(vla_var) は実行時バイトサイズ ([x29+16+offset+8]) をロード。
static node_t *parse_sizeof_operand(void) {
  if (curtok()->kind == TK_LPAREN) {
    set_curtok(curtok()->next);
    int type_sz = parse_parenthesized_type_size();
    if (type_sz >= 0) return psx_node_new_num(type_sz);
    if (curtok()->kind == TK_IDENT) {
      token_ident_t *id = (token_ident_t *)curtok();
      lvar_t *arr_var = psx_decl_find_lvar(id->str, id->len);
      if (arr_var && arr_var->is_vla) {
        set_curtok(curtok()->next);
        tk_expect(')');
        return psx_node_new_lvar_typed(arr_var->offset + 8, 8);
      }
      /* sizeof(arr) where arr is a non-VLA array: C 仕様で array → pointer
       * の decay は起きないので、配列の合計サイズ (var->size) を返す。
       * 通常の式解析では `arr` が ND_ADDR(int) (= 4 バイト) になってしまうので
       * ここで先回りする。 */
      if (arr_var && arr_var->is_array) {
        token_t *peek = curtok()->next;
        if (peek && peek->kind == TK_RPAREN) {
          set_curtok(peek->next);
          return psx_node_new_num(arr_var->size);
        }
      }
      /* ローカル lvar が見つからなければ global 配列を探す。`int g[] = {...}`
       * のような要素数推定後の type_size (apply_toplevel_object_initializer で
       * 確定済み) を全体サイズとして返す。 */
      if (!arr_var) {
        for (global_var_t *gv = global_vars; gv; gv = gv->next) {
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
    node_t *node = expr_internal();
    tk_expect(')');
    return psx_node_new_num(sizeof_expr_node(node));
  }
  return psx_node_new_num(sizeof_expr_node(unary()));
}

static node_t *build_pre_inc_dec_node(node_kind_t kind, const char *op) {
  node_t *target = unary();
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
    int looks_ptr = psx_node_is_pointer(operand) ||
                    psx_node_pointer_qual_levels(operand) > 0;
    int ts = psx_node_type_size(operand);
    /* type_size が 1/2/4 (char/short/int) で pointer 指示がなければ明確に
     * スカラ整数 → deref はエラー。 */
    if (!looks_ptr && ts > 0 && ts < 8) {
      psx_diag_ctx(curtok(), "deref",
                   "deref のオペランドはポインタ型でなければなりません (C11 6.5.3.2p2)");
    }
    /* void* の deref は不正 (pointee の型が不完全)。 */
    int pointee_void = 0;
    if (operand->kind == ND_LVAR) pointee_void = ((node_lvar_t *)operand)->mem.pointee_is_void;
    else if (operand->kind == ND_GVAR) pointee_void = ((node_mem_t *)operand)->pointee_is_void;
    if (pointee_void) {
      psx_diag_ctx(curtok(), "deref",
                   "void* の deref はできません — キャストが必要です (C11 6.5.3.2)");
    }
  }
  node_mem_t *node = arena_alloc(sizeof(node_mem_t));
  node->base.kind = ND_DEREF;
  node->base.lhs = operand;
  node->base.fp_kind = TK_FLOAT_KIND_NONE;
  int ds = psx_node_deref_size(operand);
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
    int pointee_uns = 0;
    if (operand->kind == ND_LVAR) pointee_uns = as_lvar(operand)->mem.pointee_is_unsigned;
    else if (operand->kind == ND_GVAR || operand->kind == ND_DEREF ||
             operand->kind == ND_ADDR || operand->kind == ND_PTR_CAST)
      pointee_uns = ((node_mem_t *)operand)->pointee_is_unsigned;
    if (pointee_uns) node->is_unsigned = 1;
  }
  if (pql >= 2) {
    node->is_pointer = 1;
    int new_pql = pql - 1;
    node->pointer_qual_levels = new_pql;
    int bds = psx_node_base_deref_size(operand);
    node->base_deref_size = (short)bds;
    node->deref_size = (new_pql >= 2) ? 8 : (short)bds;
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
      lvar_t *src = psx_decl_find_lvar_by_offset(((node_lvar_t *)probe)->offset);
      if (src && src->outer_stride > 0 && src->mid_stride == 0 && !src->is_array) {
        node->deref_size = (short)src->elem_size;
      }
    }
  }
  if (operand && operand->kind == ND_LVAR) {
    lvar_t *src = psx_decl_find_lvar_by_offset(((node_lvar_t *)operand)->offset);
    if (src && src->outer_stride > 0 && src->mid_stride > 0) {
      // 2D 以上 (`*p` の結果が配列)
      node->deref_size = (short)src->mid_stride;
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
   * のままで build_node_deref の配列崩壊判定 (deref_size>0 && type_size>8) に乗らず、
   * 行を値ロードして garbage を返す (`int *q=m[0]` 相当の `*m` / `*(m+k)`)。 */
  if (node->deref_size == 0 && node->type_size > 8) {
    node_t *probe = operand;
    while (probe && (probe->kind == ND_ADD || probe->kind == ND_SUB)) probe = probe->lhs;
    node_mem_t *pm = NULL;
    if (probe && probe->kind == ND_LVAR) pm = &as_lvar(probe)->mem;
    else if (probe && (probe->kind == ND_ADDR || probe->kind == ND_GVAR ||
                       probe->kind == ND_DEREF || probe->kind == ND_PTR_CAST ||
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
      node->is_pointer = 1;
    }
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
    node->deref_size = psx_node_type_size(operand);
    node->type_size = 8;
    return (node_t *)node;
  }
  /* タグなしオペランド: operand の型サイズが pointee サイズ */
  int ts = psx_node_type_size(operand);
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
    return psx_node_new_binary(ND_COMMA, operand->lhs, wrap_as_addr(operand->rhs));
  }
  // C 仕様: 配列名 `arr` は (sizeof/&/レジスタ変数を除く) 文脈ではポインタ崩壊する。
  // `&arr` ではアドレス値はそのまま (型だけ `int(*)[N]` に変わる)。
  // ag_c では配列ローカル変数の参照は build_array_lvar_addr_node により
  // ND_ADDR(ND_LVAR) として表現されているため、`&arr` でさらに ND_ADDR でラップすると
  // codegen の `gen_lval` が ND_ADDR を受理せず E4002 になる。
  // 既に ND_ADDR で表現されているアドレス値はそのまま返す。
  if (operand && operand->kind == ND_ADDR) {
    return operand;
  }
  /* `&f` (f は関数): 関数のアドレスは関数ポインタそのもの (= `f`)。ND_FUNCREF を
   * そのまま返す (ND_ADDR でラップすると IR builder が扱えず失敗する)。 */
  if (operand && operand->kind == ND_FUNCREF) {
    return operand;
  }
  return wrap_as_addr(operand);
}

static node_t *unary(void) {
  token_kind_t k = curtok()->kind;
  if (k == TK_SIZEOF)  { set_curtok(curtok()->next); return parse_sizeof_operand(); }
  if (k == TK_ALIGNOF) {
    set_curtok(curtok()->next);
    tk_expect('(');
    int type_sz = parse_parenthesized_type_size();
    if (type_sz < 0) {
      psx_diag_ctx(curtok(), "alignof", "%s",
                   diag_message_for(DIAG_ERR_PARSER_ALIGNOF_TYPE_NAME_REQUIRED));
    }
    return psx_node_new_num(type_sz);
  }
  if (k == TK_INC) { set_curtok(curtok()->next); return build_pre_inc_dec_node(ND_PRE_INC, "++"); }
  if (k == TK_DEC) { set_curtok(curtok()->next); return build_pre_inc_dec_node(ND_PRE_DEC, "--"); }
  if (k == TK_PLUS)  { set_curtok(curtok()->next); return cast(); }
  if (k == TK_MINUS) { set_curtok(curtok()->next); return psx_node_new_binary(ND_SUB, psx_node_new_num(0), cast()); }
  if (k == TK_BANG)  { set_curtok(curtok()->next); return psx_node_new_binary(ND_EQ, cast(), psx_node_new_num(0)); }
  if (k == TK_TILDE) {
    set_curtok(curtok()->next);
    node_t *neg = psx_node_new_binary(ND_SUB, psx_node_new_num(0), cast());
    return psx_node_new_binary(ND_SUB, neg, psx_node_new_num(1));
  }
  if (k == TK_MUL) { set_curtok(curtok()->next); return build_unary_deref_node(cast()); }
  if (k == TK_AMP) { set_curtok(curtok()->next); return build_unary_addr_node(cast()); }
  return apply_postfix(primary());
}

// 配列添字 `[idx]` 用のスケール倍率と次段の要素サイズ (inner_ds) を計算する。
// 多次元 VLA では実行時ストライドをフレームから読む経路がある。
static node_t *make_subscript_scaled_offset(node_t *node, node_t *idx,
                                            int *out_es, int *out_inner_ds,
                                            int *out_next_ds,
                                            int *out_extras, int *out_extras_count) {
  int ds = psx_node_deref_size(node);
  int ts = psx_node_type_size(node);
  int es = ds ? ds : (ts ? ts : 8);
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
  } else if (node->kind == ND_DEREF || node->kind == ND_ADDR) {
    node_mem_t *m = (node_mem_t *)node;
    inner_ds = m->inner_deref_size;
    next_ds = m->next_deref_size;
    extras_count = m->extra_strides_count;
    for (int i = 0; i < extras_count && i < 5; i++) extras[i] = m->extra_strides[i];
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
  int node_ok = psx_node_is_pointer(node) || node->kind == ND_DEREF ||
                node->kind == ND_ADDR;
  int idx_ok = psx_node_is_pointer(idx) || idx->kind == ND_DEREF ||
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
  if (pql >= 1 && bds > 0) {
    deref->is_pointer = 1;
    /* genuine ポインタ変数 (`int **pp`, ND_LVAR/ND_GVAR) の subscript は 1 段の
     * ポインタを消費するので結果の pql を 1 減らす (`pp[i]` は int*、pql=1)。
     * 配列 (`int *arr[N]`, ND_ADDR decay) は配列次元を消費し要素の pql を保つ
     * (`arr[i]` は int*、pql=1)。pql を減らさないと `*pp[0]` がポインタ扱いの
     * ままになり、スカラ初期化 `int u=*pp[0];` が誤って弾かれ、算術も pointer 化
     * していた。 */
    int result_pql = pql;
    if ((node->kind == ND_LVAR || node->kind == ND_GVAR) && pql >= 2) {
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
  }
  /* 配列 (pql=0 でも pointee_fp_kind を持つ ND_ADDR) の subscript 結果も
   * FP load にするため、pointee_fp_kind を見て fp_kind を引き継ぐ。 */
  {
    tk_float_kind_t arr_pointee_fp = psx_node_pointee_fp_kind(node);
    if (arr_pointee_fp != TK_FLOAT_KIND_NONE && pql == 0) {
      deref->base.fp_kind = arr_pointee_fp;
    }
  }
  if (pql == 1) {
    tk_float_kind_t pointee_fp = psx_node_pointee_fp_kind(node);
    if (pointee_fp != TK_FLOAT_KIND_NONE) {
      deref->base.fp_kind = pointee_fp;
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
    if (base_mem && base_mem->pointee_is_bool) {
      if (pql == 0 && inner_ds == 0) {
        /* 最終要素まで到達: 代入正規化用に is_bool を立てる。 */
        deref->is_bool = 1;
      } else {
        /* 中間配列 (まだ要素ではない): 次段 subscript へ pointee_is_bool を伝播。 */
        deref->pointee_is_bool = 1;
      }
    }
    /* unsigned 配列/ポインタの subscript 結果: 最終要素なら is_unsigned (zero-extend
     * load)、中間配列なら次段へ pointee_is_unsigned を伝播。 */
    if (base_mem && base_mem->pointee_is_unsigned) {
      if (pql == 0 && inner_ds == 0) deref->is_unsigned = 1;
      else                           deref->pointee_is_unsigned = 1;
    }
    /* `char *names[N]` 等: グローバルポインタ配列の要素 subscript 結果は
     * 「スカラポインタ値の load」(= struct メンバ char* と同じ semantics)。
     * is_scalar_ptr_member を立てて subscript_base_address_of が ND_DEREF を
     * そのまま返し、次段の subscript でポインタ値を base として使うようにする。 */
    if (base_mem && base_mem->pointee_is_scalar_ptr && pql == 0) {
      deref->is_scalar_ptr_member = 1;
      deref->is_pointer = 1;
      /* deref_size を pointee の素のサイズに更新 (char* なら 1)。
       * gv->pointee_elem_size から伝播するため、ND_ADDR/ND_GVAR を辿って取得する。 */
      if (node->kind == ND_ADDR && node->lhs && node->lhs->kind == ND_GVAR) {
        node_gvar_t *gv_node = (node_gvar_t *)node->lhs;
        for (global_var_t *gv = global_vars; gv; gv = gv->next) {
          if (gv->name_len == gv_node->name_len &&
              memcmp(gv->name, gv_node->name, (size_t)gv->name_len) == 0) {
            if (gv->pointee_elem_size > 0) {
              deref->deref_size = gv->pointee_elem_size;
            }
            break;
          }
        }
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

static node_t *apply_postfix(node_t *node) {
  // 後置演算がコンマ式の rhs 側に適用される: `(a, b)++` ⇒ `(a, b++)`。
  if (node && node->kind == ND_COMMA && is_postfix_op_token(curtok()->kind)) {
    node->rhs = apply_postfix(node->rhs);
    return node;
  }
  for (;;) {
    token_kind_t k = curtok()->kind;
    if (k == TK_LBRACKET) {
      set_curtok(curtok()->next);
      node_t *idx = expr_internal();
      tk_expect(']');
      node = build_subscript_deref(node, idx);
      continue;
    }
    if (k == TK_LPAREN) {
      node = parse_call_postfix(node);
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

static node_t *parse_call_postfix(node_t *callee) {
  tk_expect('(');
  node_func_t *node = arena_alloc(sizeof(node_func_t));
  node->base.kind = ND_FUNCALL;
  /* `(*fp)(args)` / `(**fp)(args)`: 関数ポインタの「単項 deref」は関数へ戻り即座に
   * 関数ポインタへ減衰するので `fp(args)` と等価。単項 deref を辿って最下層が関数
   * ポインタ lvar (pointer_qual_levels<=1) なら全段剥がす。
   * subscript の結果 (`ops[i]`, lhs=ND_ADD で最下層が lvar にならない) や、
   * ポインタ→関数ポインタ (`int(**pp)(); (*pp)()`, pql>=2) は実体 deref なので除外。 */
  if (callee && callee->kind == ND_DEREF) {
    node_t *base = callee;
    while (base && base->kind == ND_DEREF) base = base->lhs;
    if (base && (base->kind == ND_LVAR || base->kind == ND_GVAR) &&
        psx_node_pointer_qual_levels(base) <= 1) {
      callee = base;
    }
  }
  node->callee = callee;
  int nargs = 0;
  int arg_cap = 16;
  node->args = calloc(arg_cap, sizeof(node_t *));
  if (curtok()->kind == TK_RPAREN) {
    set_curtok(curtok()->next);
  } else {
    node->args[nargs++] = assign();
    while (curtok()->kind == TK_COMMA) {
      set_curtok(curtok()->next);
      if (nargs >= arg_cap) {
        arg_cap = pda_next_cap(arg_cap, nargs + 1);
        node->args = pda_xreallocarray(node->args, (size_t)arg_cap, sizeof(node_t *));
      }
      node->args[nargs++] = assign();
    }
    tk_expect(')');
  }
  node->nargs = nargs;
  return (node_t *)node;
}

// TK_LPAREN を見たときの compound literal `(T){...}` 試行。
// パースできたら結果ノードを返し、できなければ NULL（呼び出し側は通常の式へ）。
static node_t *try_parse_compound_literal(void) {
  token_kind_t cast_kind = TK_EOF;
  int cast_is_ptr = 0;
  token_t *after_rparen = NULL;
  token_kind_t cast_tag_kind = TK_EOF;
  char *cast_tag_name = NULL;
  int cast_tag_len = 0;
  int cast_elem_size = 8;
  tk_float_kind_t cast_fp_kind = TK_FLOAT_KIND_NONE;
  int cast_array_count = 0;
  if (curtok()->kind == TK_LPAREN &&
      parse_cast_type(curtok(), &cast_kind, &cast_is_ptr, &after_rparen,
                      &cast_tag_kind, &cast_tag_name, &cast_tag_len,
                      &cast_elem_size, &cast_fp_kind, &cast_array_count) &&
      after_rparen && after_rparen->kind == TK_LBRACE) {
    return parse_compound_literal_from_type(cast_kind, cast_is_ptr, after_rparen,
                                            cast_tag_kind, cast_tag_name, cast_tag_len,
                                            cast_elem_size, cast_fp_kind, cast_array_count);
  }
  return NULL;
}

// _Generic( ctrl, T1: e1, T2: e2, ..., default: ed ) を評価して選択された式ノードを返す。
static node_t *parse_generic_selection(void) {
  set_curtok(curtok()->next); // skip TK_GENERIC
  tk_expect('(');
  node_t *control = assign();
  generic_type_t control_ty = infer_generic_control_type(control);
  tk_expect(',');

  node_t *selected = NULL;
  node_t *default_expr = NULL;
  int matched = 0;
  for (;;) {
    if (curtok()->kind == TK_DEFAULT) {
      set_curtok(curtok()->next);
      tk_expect(':');
      node_t *expr_node = assign();
      if (!default_expr) default_expr = expr_node;
    } else {
      generic_type_t assoc_ty = {0};
      assoc_ty.kind = TK_EOF;
      if (!parse_generic_assoc_type(&assoc_ty)) {
        psx_diag_ctx(curtok(), "generic", "%s",
                     diag_message_for(DIAG_ERR_PARSER_GENERIC_ASSOC_TYPE_INVALID));
      }
      tk_expect(':');
      node_t *expr_node = assign();
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
  snode->byte_len = len;
  return snode;
}

// C11 6.4.2.2 __func__: 各関数本体に暗黙定義される const char[] の関数名。
static node_t *make_func_name_string_node(void) {
  const char *fname = g_current_funcname ? g_current_funcname : "";
  int flen = g_current_funcname ? g_current_funcname_len : 0;
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
    if (total_len == 0) {
      merged_width = st->char_width ? st->char_width : TK_CHAR_WIDTH_CHAR;
      merged_prefix_kind = st->str_prefix_kind;
    } else if (merged_width != st->char_width) {
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

// 名前が宣言済みでない (var==NULL) 識別子の直後に '(' が来ている場合の通常関数呼び出し。
// 戻り値型 (ret_struct_size 等) は psx_ctx から引く。
static node_t *build_unqualified_call(token_ident_t *tok) {
  set_curtok(curtok()->next); // skip '('
  node_func_t *node = arena_alloc(sizeof(node_func_t));
  node->base.kind = ND_FUNCALL;
  node->callee = NULL;
  node->funcname = tok->str;
  node->funcname_len = tok->len;
  int nargs = 0;
  int arg_cap = 16;
  node->args = calloc(arg_cap, sizeof(node_t *));
  if (curtok()->kind == TK_RPAREN) {
    set_curtok(curtok()->next);
  } else {
    node->args[nargs++] = assign();
    while (curtok()->kind == TK_COMMA) {
      set_curtok(curtok()->next);
      if (nargs >= arg_cap) {
        arg_cap = pda_next_cap(arg_cap, nargs + 1);
        node->args = pda_xreallocarray(node->args, (size_t)arg_cap, sizeof(node_t *));
      }
      node->args[nargs++] = assign();
    }
    tk_expect(')');
  }
  node->nargs = nargs;
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
  node->base.ret_struct_size = psx_ctx_get_function_ret_struct_size(tok->str, tok->len);
  // 関数戻り値が float/double のときは call ノードに fp_kind を設定し、
  // `(int)call()` キャストで apply_cast が ND_FP_TO_INT を挿入できるようにする。
  node->base.fp_kind = psx_ctx_get_function_ret_fp_kind(tok->str, tok->len);
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
  for (global_var_t *gv = global_vars; gv; gv = gv->next) {
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
      base->name = gv->name;
      base->name_len = gv->name_len;
      base->is_thread_local = gv->is_thread_local;
      node_mem_t *addr = arena_alloc(sizeof(node_mem_t));
      addr->base.kind = ND_ADDR;
      addr->base.lhs = (node_t *)base;
      addr->tag_kind = gv->tag_kind;
      addr->tag_name = gv->tag_name;
      addr->tag_len = gv->tag_len;
      if (gv->tag_kind != TK_EOF) addr->is_tag_pointer = 1;
      // 多次元配列: outer_stride を 1 次サブスクリプトのステップとして使う。
      // ローカル配列の build_array_lvar_addr_node と同じレイアウト。
      int stride = (gv->outer_stride > 0) ? gv->outer_stride : gv->deref_size;
      addr->type_size = stride;
      addr->deref_size = stride;
      addr->is_pointer = 1;
      /* `double a[5]` 等: 要素型 fp_kind を pointee_fp_kind に伝播し、
       * build_subscript_deref が FP load を組み立てられるようにする。 */
      addr->pointee_fp_kind = gv->fp_kind;
      /* unsigned グローバル配列: 要素 subscript 結果を zero-extend load させる。 */
      addr->pointee_is_unsigned = gv->is_unsigned ? 1 : 0;
      /* `char *names[N]` 等のグローバルポインタ配列: 各要素 (= スカラポインタ) の
       * pointee サイズ情報を伝播。subscript の結果 ND_DEREF に is_scalar_ptr_member
       * を立てて、struct メンバ char* (commit 6a663ed) と同じく ND_DEREF をそのまま
       * subscript base にしてポインタ値の load を引き起こす。
       * 関数ポインタ配列 (`int (*ops[N])(int)`) は ops[i](val) で deref→call され、
       * 2 段 subscript はしない (= pointee_elem_size を見ない) ので影響なし。 */
      if (gv->pointee_elem_size > 0 && gv->tag_kind == TK_EOF) {
        addr->pointee_is_scalar_ptr = 1;
      }
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
      return (node_t *)addr;
    }
    node_gvar_t *gvar_node = arena_alloc(sizeof(node_gvar_t));
    gvar_node->mem.base.kind = ND_GVAR;
    gvar_node->mem.type_size = gv->type_size;
    gvar_node->mem.deref_size = gv->deref_size;
    /* タグ情報 (struct / union): build_member_access が `.x` を解決するときに
     * psx_node_get_tag_type 経由でここを読む。 */
    gvar_node->mem.tag_kind = gv->tag_kind;
    gvar_node->mem.tag_name = gv->tag_name;
    gvar_node->mem.tag_len = gv->tag_len;
    gvar_node->mem.is_tag_pointer = gv->is_tag_pointer;
    if (gv->is_tag_pointer) gvar_node->mem.is_pointer = 1;
    /* 浮動小数スカラのグローバル: fp_kind を node に伝播。IR builder が
     * これを見て IR_TY_F32/F64 として load を発行する。 */
    gvar_node->mem.base.fp_kind = gv->fp_kind;
    /* _Bool スカラ: 代入/複合代入の正規化 (C11 6.3.1.2) のため is_bool を伝播。 */
    gvar_node->mem.is_bool = gv->is_bool;
    /* unsigned スカラ: load を zero-extend するため is_unsigned を伝播。 */
    gvar_node->mem.is_unsigned = gv->is_unsigned;
    gvar_node->name = gv->name;
    gvar_node->name_len = gv->name_len;
    gvar_node->is_thread_local = gv->is_thread_local;
    return (node_t *)gvar_node;
  }
  return NULL;
}

/* static local 配列のベースアドレスを ND_ADDR(ND_GVAR) として返す。
 * 配列は decl.c の try_lower_static_local_array でグローバルにリダイレクトされ、
 * alias lvar (is_static_local=1, static_global_name=mangled) を持つ。
 * alias は size=0 で frame 割当を抑制しているため、サイズ情報は global_vars
 * から名前で引く。1D 整数配列のみ scope に入る (outer/mid stride は 0 のまま)。 */
static node_t *build_static_local_array_addr_node(lvar_t *var) {
  /* global_vars リストから名前で引いて type_size を取る。 */
  short gv_type_size = (short)var->elem_size;
  for (global_var_t *gv = global_vars; gv; gv = gv->next) {
    if (gv->name_len == var->static_global_name_len &&
        memcmp(gv->name, var->static_global_name, (size_t)gv->name_len) == 0) {
      gv_type_size = gv->type_size;
      break;
    }
  }
  node_gvar_t *base = arena_alloc(sizeof(node_gvar_t));
  base->mem.base.kind = ND_GVAR;
  base->mem.type_size = gv_type_size;
  base->mem.deref_size = (short)var->elem_size;
  base->mem.is_unsigned = var->is_unsigned;
  base->name = var->static_global_name;
  base->name_len = var->static_global_name_len;
  node_mem_t *addr = arena_alloc(sizeof(node_mem_t));
  addr->base.kind = ND_ADDR;
  addr->base.lhs = (node_t *)base;
  int stride = var->elem_size; /* 1D limited */
  addr->type_size = stride;
  addr->deref_size = stride;
  addr->is_pointer = 1;
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
  node->base.lhs = psx_node_new_lvar(var->offset);
  int stride = (var->outer_stride > 0) ? var->outer_stride : var->elem_size;
  node->type_size = stride;
  node->deref_size = stride;
  /* `double a[3]` のような配列は要素型が float/double のとき、
   * pointee_fp_kind を要素型として伝播する (subscript 結果が FP load になるよう
   * build_subscript_deref が見る)。配列パラメータでは var->pointee_fp_kind が
   * 使われるので両方を fall-through する。 */
  node->pointee_fp_kind = var->pointee_fp_kind != TK_FLOAT_KIND_NONE
                             ? var->pointee_fp_kind
                             : var->fp_kind;
  /* `_Bool a[5]` の要素アクセスを正規化するために、配列ベースの ND_ADDR にも
   * pointee_is_bool を伝播する (build_subscript_deref が deref に引き継ぐ)。 */
  node->pointee_is_bool = var->is_bool ? 1 : 0;
  /* `unsigned a[5]` / `unsigned *p`: 要素/pointee が unsigned なら subscript/deref
   * 結果を zero-extend load させるため pointee_is_unsigned を伝播する。 */
  node->pointee_is_unsigned = var->is_unsigned ? 1 : 0;
  if (var->outer_stride > 0) {
    // 2D: inner_deref_size = elem_size （1段サブスクリプト後の要素）
    // 3D: inner_deref_size = mid_stride （1段サブスクリプト後はまだ配列なので、その内側ストライド）
    //     next_deref_size = elem_size （2段サブスクリプト後の要素）
    // 4D 以上: 上記に加えて extra_strides を node 側にコピー。
    //         3 段目で使うストライド = extra_strides[0]、最後が elem_size。
    if (var->mid_stride > 0) {
      node->inner_deref_size = (short)var->mid_stride;
      if (var->extra_strides_count > 0) {
        // 4D+: 3 段目ストライド = extra_strides[0]、残りは node->extra_strides に
        node->next_deref_size = (short)var->extra_strides[0];
        for (int i = 1; i < var->extra_strides_count && (i - 1) < 5; i++) {
          node->extra_strides[i - 1] = var->extra_strides[i];
        }
        node->extra_strides[var->extra_strides_count - 1] = var->elem_size;
        node->extra_strides_count = var->extra_strides_count;
      } else {
        node->next_deref_size = (short)var->elem_size;
      }
    } else {
      node->inner_deref_size = (short)var->elem_size;
    }
  }
  node->tag_kind = var->tag_kind;
  node->tag_name = var->tag_name;
  node->tag_len = var->tag_len;
  node->is_tag_pointer = (var->tag_kind != TK_EOF) ? 1 : 0;
  node->is_pointer = 1;
  node->pointer_qual_levels = var->pointer_qual_levels;
  node->base_deref_size = var->base_deref_size;
  return (node_t *)node;
}

// byref 仮引数 (>16バイト構造体の値渡し): フレームスロットからポインタを読み込み、
// ND_DEREF でラップして「struct値」として見せる。
//   p.member → build_member_access(ND_DEREF(ptr_lvar), from_ptr=0)
//     → ND_ADDR(ND_DEREF(ptr_lvar)) = struct base ptr → offset → deref → member ✓
static node_t *build_byref_param_node(lvar_t *var) {
  node_t *ptr_lvar = psx_node_new_lvar_typed(var->offset, 8); // loads ptr from frame
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
    node_gvar_t *gv = arena_alloc(sizeof(node_gvar_t));
    gv->mem.base.kind = ND_GVAR;
    int sz = var->size > 0 ? var->size : var->elem_size;
    gv->mem.type_size = (short)sz;
    gv->mem.deref_size = (short)(var->elem_size > 0 ? var->elem_size : sz);
    gv->mem.is_unsigned = var->is_unsigned;
    gv->mem.base.fp_kind = var->fp_kind;
    gv->name = var->static_global_name;
    gv->name_len = var->static_global_name_len;
    var->is_used = 1;
    return (node_t *)gv;
  }
  int lvar_is_pointer = var->is_array || var->is_vla || var->pointer_qual_levels > 0 ||
                        (var->size > var->elem_size) ||
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
  node_t *n = psx_node_new_lvar_typed(var->offset,
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
  } else {
    as_lvar(n)->mem.inner_deref_size = vla_is_multidim ? var->elem_size : 0;
  }
  as_lvar(n)->mem.vla_row_stride_frame_off = var->vla_row_stride_frame_off;
  as_lvar(n)->mem.tag_kind = var->tag_kind;
  as_lvar(n)->mem.tag_name = var->tag_name;
  as_lvar(n)->mem.tag_len = var->tag_len;
  as_lvar(n)->mem.is_tag_pointer = var->is_tag_pointer;
  as_lvar(n)->mem.is_pointer = lvar_is_pointer;
  as_lvar(n)->mem.is_const_qualified = var->is_const_qualified;
  as_lvar(n)->mem.is_volatile_qualified = var->is_volatile_qualified;
  as_lvar(n)->mem.is_pointer_const_qualified = var->is_pointer_const_qualified;
  as_lvar(n)->mem.is_pointer_volatile_qualified = var->is_pointer_volatile_qualified;
  as_lvar(n)->mem.pointer_const_qual_mask = var->pointer_const_qual_mask;
  as_lvar(n)->mem.pointer_volatile_qual_mask = var->pointer_volatile_qual_mask;
  as_lvar(n)->mem.pointer_qual_levels = var->pointer_qual_levels;
  as_lvar(n)->mem.base_deref_size = var->base_deref_size;
  as_lvar(n)->mem.pointee_fp_kind = var->pointee_fp_kind;
  as_lvar(n)->mem.is_unsigned = var->is_unsigned;
  /* `unsigned *p` の `*p` を zero-extend load させるため pointee_is_unsigned を
   * 伝播する (var->is_unsigned は基底型 unsigned を表すのでポインタにも乗る)。 */
  as_lvar(n)->mem.pointee_is_unsigned = var->is_unsigned;
  as_lvar(n)->mem.is_complex = var->is_complex;
  as_lvar(n)->mem.is_atomic = var->is_atomic;
  as_lvar(n)->mem.pointee_is_void = var->pointee_is_void;
  as_lvar(n)->mem.is_bool = var->is_bool;
  n->is_complex = var->is_complex;
  n->is_atomic = var->is_atomic;
  n->fp_kind = var->fp_kind;
  return n;
}

// 識別子トークン tok を解決して node を返す:
//   1. __func__ → 暗黙文字列リテラル
//   2. 未定義 + enum const → 定数
//   3. 未定義 + '(' → 関数呼び出し
//   4. 未定義 + 既登録関数名 → 関数参照
//   5. 未定義 + グローバル変数 → ND_GVAR
//   6. それ以外 → ローカル変数 (必要なら新規登録)
static node_t *resolve_identifier(token_ident_t *tok) {
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
    /* `gp(...)` でグローバル関数ポインタを呼び出す場合は、まずグローバル変数として
     * 解決して間接呼び出しに回す。global var として見つからなければ通常の
     * unqualified function call として処理する。 */
    node_t *gv = try_build_global_var_node(tok);
    if (gv) return gv;
    return build_unqualified_call(tok);
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
  var->is_used = 1;
  /* static local 配列はグローバルに lowering 済み (decl.c:try_lower_static_local_array)。
   * alias lvar の offset=0 は意味を持たないので、build_array_lvar_addr_node が
   * フレーム上の偽アドレスを base にしないよう専用経路で ND_ADDR(ND_GVAR) を返す。 */
  if (lvar_is_static_local_array(var)) {
    return build_static_local_array_addr_node(var);
  }
  if (var->is_array && !var->is_vla) return build_array_lvar_addr_node(var);
  if (var->is_byref_param) return build_byref_param_node(var);
  return build_lvar_or_vla_node(var);
}

static node_t *primary(void) {
  node_t *cl = try_parse_compound_literal();
  if (cl) return cl;

  if (curtok()->kind == TK_GENERIC) return parse_generic_selection();

  if (curtok()->kind == TK_NUM) return parse_num_literal();

  if (curtok()->kind == TK_LPAREN) {
    enter_paren_nest_or_die();
    set_curtok(curtok()->next);
    node_t *node = expr_internal();
    tk_expect(')');
    leave_paren_nest();
    return node;
  }

  token_ident_t *tok = tk_consume_ident();
  if (tok) return resolve_identifier(tok);

  if (curtok()->kind == TK_STRING) {
    return parse_string_literal_sequence();
  }

  psx_diag_ctx(curtok(), "primary", "%s",
               diag_message_for(DIAG_ERR_PARSER_PRIMARY_NUMBER_EXPECTED));
  return NULL;
}
