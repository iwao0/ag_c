#include "expr.h"
#include "arena.h"
#include "core.h"
#include "decl.h"
#include "diag.h"
#include "dynarray.h"
#include "node_utils.h"
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

static token_kind_t g_current_ret_token_kind = TK_INT;
static tk_float_kind_t g_current_ret_fp_kind = TK_FLOAT_KIND_NONE;
static int g_current_ret_struct_size = 0;
static int g_current_ret_is_pointer = 0;
static int g_current_ret_is_unsigned = 0;
/* 1 のとき parse_parenthesized_type_size は型の「サイズ」ではなく「アラインメント」を
 * 返す (_Alignof 用)。struct は agg_align、配列は要素アラインメント (要素数を掛けない)。 */
static int g_parse_type_alignof_mode = 0;
/* 直近の parse_cast_type が _Complex 型を解釈したら 1。複素数 compound literal
 * `(double _Complex){re, im}` を構築するため try_parse_compound_literal が読む。 */
static int g_last_cast_is_complex = 0;
/* 単項 `&` のオペランドを解析中なら 1。ファイルスコープのスカラ複合リテラル
 * `&(int){5}` で、複合リテラルを値 (ND_NUM) に短絡せず静的記憶域の gvar 実体として
 * 生成し、そのアドレスを取れるようにするためのヒント (C11 6.5.2.5: ファイルスコープの
 * 複合リテラルは静的記憶域期間を持つ)。値文脈の `int a[]={(int){1}}` を退行させない
 * よう、`&` 配下のときだけ実体化する。 */
static int g_addr_of_compound_pending = 0;
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

static void consume_local_type_quals(token_t **cur);
static long long eval_const_expr_type_size(node_t *n, int *ok);
static void apply_array_abstract_suffix_size(int *sz);
static int is_type_name_start_token(token_t *t);
static char *new_compound_lit_name(void);
static int lvar_is_static_local_array(lvar_t *var);
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
    gt.is_pointer = 1;
    gt.is_funcptr = 1;
    gt.is_func_designator = 1;
    gt.kind = psx_ctx_get_function_ret_token_kind(fr->funcname, fr->funcname_len);
    if (gt.kind == TK_EOF) gt.kind = TK_INT;
    gt.ptr_levels = 1;
    gt.ptr_pointee_fp_kind = psx_ctx_get_function_ret_fp_kind(fr->funcname, fr->funcname_len);
    gt.ptr_pointee_unsigned = psx_ctx_get_function_ret_is_unsigned(fr->funcname, fr->funcname_len);
    bool ret_is_type = false;
    int ret_size = 4;
    psx_ctx_get_type_info(gt.kind, &ret_is_type, &ret_size);
    (void)ret_is_type;
    gt.scalar_size = ret_size;
    gt.ptr_deref_size = ret_size;
    gt.ptr_base_deref_size = ret_size;
    psx_ctx_get_function_ret_tag(fr->funcname, fr->funcname_len,
                                 &gt.tag_kind, &gt.tag_name, &gt.tag_len);
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
    if (control->kind == ND_LVAR && as_lvar(control)->mem.is_long_double)
      gt.is_long_double = 1;
    else if ((control->kind == ND_GVAR || control->kind == ND_DEREF ||
              control->kind == ND_ASSIGN || control->kind == ND_ADDR) &&
             ((node_mem_t *)control)->is_long_double)
      gt.is_long_double = 1;
    return gt;
  }
  int ts = ps_node_type_size(control);
  int ds = ps_node_deref_size(control);
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
  /* 変数等の long long / plain char の型識別を制御式型へ反映 (_Generic 用)。
   * ND_LVAR は ->mem、その他の mem ノードは直接キャストで読む (is_unsigned と同じ流儀)。 */
  if (control->kind == ND_LVAR) {
    if (as_lvar(control)->mem.is_long_long) gt.is_long_long = 1;
    if (as_lvar(control)->mem.is_plain_char) gt.is_plain_char = 1;
  } else if (control->kind == ND_GVAR || control->kind == ND_DEREF ||
             control->kind == ND_ASSIGN || control->kind == ND_ADDR) {
    if (((node_mem_t *)control)->is_long_long) gt.is_long_long = 1;
    if (((node_mem_t *)control)->is_plain_char) gt.is_plain_char = 1;
  }
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
    psx_typedef_info_t _ti;
    if (psx_ctx_find_typedef_name(id->str, id->len, &_ti)) {
      base_kind = _ti.base_kind; elem_size = _ti.elem_size; fp_kind = _ti.fp_kind;
      tag_kind = _ti.tag_kind; tag_name = _ti.tag_name; tag_len = _ti.tag_len;
      is_ptr = _ti.is_pointer; td_is_unsigned = _ti.is_unsigned;
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
  tk = psx_consume_type_kind();
  if (tk == TK_EOF) return 0;
  out->kind = tk;
  /* `long double` は double に lowering され kind=TK_DOUBLE になるが、_Generic では
   * double と別型。side-channel フラグで区別する (double 制御式が long double: に
   * 誤マッチして tgmath の sqrt(2.0) が sqrtl を呼ぶのを防ぐ)。 */
  out->is_long_double = psx_last_type_is_long_double();
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

static int funcall_ret_pointee_const(node_func_t *fn) {
  if (!fn) return 0;
  if (fn->callee == NULL && fn->funcname) {
    return psx_ctx_get_function_ret_pointee_const(fn->funcname, fn->funcname_len);
  }
  if (fn->callee && (fn->callee->kind == ND_LVAR || fn->callee->kind == ND_GVAR ||
                     fn->callee->kind == ND_DEREF || fn->callee->kind == ND_ADDR ||
                     fn->callee->kind == ND_PTR_CAST)) {
    return ((node_mem_t *)fn->callee)->is_const_qualified ? 1 : 0;
  }
  return 0;
}

static int funcall_ret_pointee_volatile(node_func_t *fn) {
  if (!fn) return 0;
  if (fn->callee == NULL && fn->funcname) {
    return psx_ctx_get_function_ret_pointee_volatile(fn->funcname, fn->funcname_len);
  }
  if (fn->callee && (fn->callee->kind == ND_LVAR || fn->callee->kind == ND_GVAR ||
                     fn->callee->kind == ND_DEREF || fn->callee->kind == ND_ADDR ||
                     fn->callee->kind == ND_PTR_CAST)) {
    return ((node_mem_t *)fn->callee)->is_volatile_qualified ? 1 : 0;
  }
  return 0;
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
  }
  deref->tag_kind = mem_info->tag_kind;
  deref->tag_name = mem_info->tag_name;
  deref->tag_len = mem_info->tag_len;
  deref->is_tag_pointer = mem_is_ptr;
  if (base->kind == ND_LVAR || base->kind == ND_GVAR || base->kind == ND_DEREF) {
    node_mem_t *base_mem = (node_mem_t *)base;
    if (base_mem->is_const_qualified) deref->is_const_qualified = 1;
    if (base_mem->is_volatile_qualified) deref->is_volatile_qualified = 1;
  } else if (base->kind == ND_FUNCALL) {
    node_func_t *fn = (node_func_t *)base;
    if (funcall_ret_pointee_const(fn)) deref->is_const_qualified = 1;
    if (funcall_ret_pointee_volatile(fn)) deref->is_volatile_qualified = 1;
  }
  deref->bit_width = mem_info->bit_width;
  deref->bit_offset = mem_info->bit_offset;
  deref->bit_is_signed = mem_info->bit_is_signed;
  deref->funcptr_param_fp_mask = mem_info->funcptr_param_fp_mask;
  deref->funcptr_param_int_mask = mem_info->funcptr_param_int_mask;
  deref->funcptr_ret_pointee_array_first_dim =
      mem_info->funcptr_ret_pointee_array_first_dim;
  deref->funcptr_ret_pointee_array_second_dim =
      mem_info->funcptr_ret_pointee_array_second_dim;
  deref->funcptr_ret_pointee_array_elem_size =
      mem_info->funcptr_ret_pointee_array_elem_size;
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
                                                int cast_array_count) {
  set_curtok(after_rparen);
  /* parse_cast_type が直前に設定した「複素数キャストか」を退避する (この後の
   * 初期化子パースで parse_cast_type が再呼出され上書きされる前に保存)。 */
  int cl_is_complex = g_last_cast_is_complex;
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
  /* `(double _Complex){re, im}` 等の複素数 compound literal: {実部, 虚部} で
   * base_elem*2 バイト。is_complex を立てて psx_decl_parse_initializer_for_var の
   * 複素数 brace 経路に乗せる。 */
  int cl_complex_scalar = (cl_is_complex && !is_arr && !cast_is_ptr) ? 1 : 0;
  if (cl_complex_scalar) var_size = base_elem * 2;
  char *tmp_name = new_compound_lit_name();
  if (g_current_funcname == NULL) {
    int want_addr = g_addr_of_compound_pending;
    g_addr_of_compound_pending = 0;
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
      gv->fp_kind = cast_fp_kind;
      gv->tag_kind = cast_tag_kind;
      gv->tag_name = cast_tag_name;
      gv->tag_len = cast_tag_len;
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
      node_gvar_t *gvar_node = arena_alloc(sizeof(node_gvar_t));
      gvar_node->mem.base.kind = ND_GVAR;
      gvar_node->mem.type_size = gv->type_size;
      gvar_node->mem.deref_size = gv->deref_size;
      gvar_node->mem.tag_kind = gv->tag_kind;
      gvar_node->mem.tag_name = gv->tag_name;
      gvar_node->mem.tag_len = gv->tag_len;
      gvar_node->name = gv->name;
      gvar_node->name_len = gv->name_len;
      gvar_node->is_thread_local = gv->is_thread_local;
      if (is_arr) {
        /* 配列複合リテラルはポインタへ decay。ND_ADDR で包み subscript / `&` を通す。 */
        node_mem_t *addr = arena_alloc(sizeof(node_mem_t));
        addr->base.kind = ND_ADDR;
        addr->base.lhs = (node_t *)gvar_node;
        addr->type_size = base_elem;
        addr->deref_size = base_elem;
        addr->is_pointer = 1;
        return apply_postfix((node_t *)addr);
      }
      return apply_postfix((node_t *)gvar_node);
    }
    tk_expect('{');
    node_t *init_expr = psx_expr_assign();
    tk_expect('}');
    /* `&(int){5}` のように `&` のオペランドなら、ND_NUM への短絡 (アドレス取得不能)
     * を避けて下の gvar 実体化経路へ進む。 */
    if (!is_arr && !want_addr && init_expr && init_expr->kind == ND_NUM) {
      return apply_postfix(init_expr);
    }
    global_var_t *gv = calloc(1, sizeof(global_var_t));
    gv->name = tmp_name;
    gv->name_len = (int)strlen(tmp_name);
    gv->type_size = var_size;
    gv->deref_size = base_elem;
    gv->is_array = is_arr;
    gv->fp_kind = cast_fp_kind;
    gv->is_static = 1;  /* 匿名複合リテラルは内部リンケージ (.global を出さない) */
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
    node_gvar_t *gvar_node = arena_alloc(sizeof(node_gvar_t));
    gvar_node->mem.base.kind = ND_GVAR;
    gvar_node->mem.type_size = gv->type_size;
    gvar_node->mem.deref_size = gv->deref_size;
    gvar_node->name = gv->name;
    gvar_node->name_len = gv->name_len;
    gvar_node->is_thread_local = gv->is_thread_local;
    return apply_postfix((node_t *)gvar_node);
  }
  lvar_t *var = psx_decl_register_lvar_sized(tmp_name, (int)strlen(tmp_name),
                                             var_size, cl_complex_scalar ? var_size : base_elem, is_arr);
  var->tag_kind = cast_tag_kind;
  var->tag_name = cast_tag_name;
  var->tag_len = cast_tag_len;
  if (cl_complex_scalar) var->is_complex = 1;  /* elem_size = var_size (=base_elem*2)、brace-init で half= elem/2 */
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
                           int *out_elem_size, tk_float_kind_t *out_fp_kind, int *out_array_count,
                           int *out_is_unsigned) {
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
  if (out_is_unsigned) *out_is_unsigned = 0;
  g_last_cast_is_complex = 0;

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
      } else if (parse_integer_cast_spec_sequence(q, &inner_kind, &inner_elem, NULL, &q, NULL, NULL)) {
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
    g_last_cast_is_complex = 1;
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
    g_last_cast_is_complex = 1;
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
      if (parse_integer_cast_spec_sequence(t, type_kind, out_elem_size, out_is_unsigned, &t, NULL, NULL)) {
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
    psx_typedef_info_t _ti;
    if (psx_ctx_find_typedef_name(id->str, id->len, &_ti)) {
      td_base = _ti.base_kind; td_elem = _ti.elem_size; td_fp = _ti.fp_kind;
      td_tag = _ti.tag_kind; td_tag_name = _ti.tag_name; td_tag_len = _ti.tag_len;
      td_ptr = _ti.is_pointer;
      if (out_is_unsigned) *out_is_unsigned = _ti.is_unsigned;
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
  if (!t || t->kind != TK_RPAREN || !t->next) return 0;
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

static node_t *new_typed_lvar_ref(lvar_t *var, int is_pointer) {
  node_t *ref = psx_node_new_lvar_typed(var->offset, is_pointer ? 8 : var->elem_size);
  ref->fp_kind = var->fp_kind;
  as_lvar(ref)->mem.deref_size = var->elem_size;
  as_lvar(ref)->mem.is_pointer = is_pointer;
  as_lvar(ref)->mem.tag_kind = var->tag_kind;
  as_lvar(ref)->mem.tag_name = var->tag_name;
  as_lvar(ref)->mem.tag_len = var->tag_len;
  as_lvar(ref)->mem.is_tag_pointer = var->is_tag_pointer;
  /* タグ shadow 応用形: 変数宣言時の tag_scope_depth を node にも伝播し、後段の
   * メンバ参照経路で「最も内側 tag」ではなく「変数が見ていた tag」のメンバを引けるように
   * する (内側 shadow からの外側変数参照対応)。 */
  as_lvar(ref)->mem.tag_scope_depth_p1 = var->tag_scope_depth_p1;
  as_lvar(ref)->mem.is_const_qualified = var->is_const_qualified;
  as_lvar(ref)->mem.is_volatile_qualified = var->is_volatile_qualified;
  as_lvar(ref)->mem.is_pointer_const_qualified = var->is_pointer_const_qualified;
  as_lvar(ref)->mem.is_pointer_volatile_qualified = var->is_pointer_volatile_qualified;
  as_lvar(ref)->mem.pointer_const_qual_mask = var->pointer_const_qual_mask;
  as_lvar(ref)->mem.pointer_volatile_qual_mask = var->pointer_volatile_qual_mask;
  as_lvar(ref)->mem.pointer_qual_levels = var->pointer_qual_levels;
  as_lvar(ref)->mem.base_deref_size = var->base_deref_size;
  as_lvar(ref)->mem.is_unsigned = var->is_unsigned;
  /* 複素数 lvar 参照: is_complex を伝播して、代入/算術で複素数として扱われるように
   * する (compound literal `(double _Complex){re,im}` の値が複素数コピーされる)。 */
  ref->is_complex = var->is_complex;
  as_lvar(ref)->mem.is_complex = var->is_complex;
  /* long long / plain char の型識別を伝播 (_Generic の制御式型判定で使う)。 */
  as_lvar(ref)->mem.is_long_long = var->is_long_long;
  as_lvar(ref)->mem.is_plain_char = var->is_plain_char;
  as_lvar(ref)->mem.is_long_double = var->is_long_double;
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
  /* _Alignof では配列のアラインメント = 要素のアラインメントなので、要素数を掛けない。 */
  if (!g_parse_type_alignof_mode) {
    apply_array_abstract_suffix_size(&sz);
  } else {
    /* 配列添字は消費だけして size には反映しない (要素アラインメントを保つ)。 */
    int dummy = 1;
    apply_array_abstract_suffix_size(&dummy);
  }
  tk_expect(')');
  return sz;
}

static int parse_parenthesized_type_size(void) {
  token_t *t = curtok();
  if (t->kind == TK_LPAREN && is_type_name_start_token(t->next)) {
    /* `sizeof((int) 1)` 等の cast 式は、`(` の直後が type-name でも閉じ `)` の後ろに
     * 式が続く。内側で sz を取得しても curtok が `)` でない場合は type-name 解釈は失敗で、
     * トークンを巻き戻して -1 を返し、呼び出し側 (parse_sizeof_operand 等) の式パース経路に
     * 任せる。これがないと `(int)` を type-name として消費し、残った `1)` で E2006 になる。 */
    token_t *save = curtok();
    set_curtok(t->next);
    int sz = parse_parenthesized_type_size();
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
      return finish_parenthesized_type_size(inext, iksz);
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
    return finish_parenthesized_type_size(t, sz);
  }
  if (t->kind == TK_ENUM) {
    /* sizeof/_Alignof(enum E): enum は int 相当で 4 バイト。タグ名は任意。 */
    set_curtok(t->next);
    (void)tk_consume_ident();
    t = curtok();
    return finish_parenthesized_type_size(t, 4);
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
    if (g_parse_type_alignof_mode) {
      int al = psx_ctx_get_tag_align(tag_kind, tag->str, tag->len);
      if (al > 0) sz = al;
    }
    t = curtok();
    return finish_parenthesized_type_size(t, sz);
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

void psx_expr_set_current_func_ret_is_unsigned(int is_unsigned) {
  g_current_ret_is_unsigned = is_unsigned ? 1 : 0;
}

int psx_expr_current_func_ret_is_unsigned(void) {
  return g_current_ret_is_unsigned;
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
  node_mem_t *mem = arena_alloc(sizeof(node_mem_t));
  node_t *cvt = &mem->base;
  cvt->kind = ND_FP_TO_INT;
  cvt->lhs = operand;
  cvt->fp_kind = TK_FLOAT_KIND_NONE;
  mem->type_size = 4;
  return cvt;
}

static node_t *wrap_fp_to_int_width(node_t *operand, int width) {
  if (!operand || operand->fp_kind == TK_FLOAT_KIND_NONE) return operand;
  node_mem_t *mem = arena_alloc(sizeof(node_mem_t));
  node_t *cvt = &mem->base;
  cvt->kind = ND_FP_TO_INT;
  cvt->lhs = operand;
  cvt->fp_kind = TK_FLOAT_KIND_NONE;
  mem->type_size = (width == 8) ? 8 : 4;
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
                          token_kind_t cast_tag_kind, char *cast_tag_name, int cast_tag_len,
                          int cast_elem_size, int cast_is_unsigned) {
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
  if (is_pointer || type_kind == TK_LONG) {
    operand = wrap_fp_to_int_if_needed(operand);
    operand->fp_kind = TK_FLOAT_KIND_NONE;
    if (!is_pointer && type_kind == TK_LONG) {
      if (operand->kind == ND_NUM) {
        ((node_num_t *)operand)->int_is_long = 1;
        psx_node_set_unsigned(operand, cast_is_unsigned);
        return operand;
      }
      if (!ps_node_is_pointer(operand)) {
        node_mem_t *wrap = arena_alloc(sizeof(node_mem_t));
        wrap->base.kind = ND_PTR_CAST;
        wrap->base.lhs = operand;
        wrap->type_size = 8;
        wrap->is_unsigned = cast_is_unsigned ? 1 : 0;
        if (ps_node_is_unsigned(operand) && ps_node_type_size(operand) >= 1 && ps_node_type_size(operand) < 8)
          wrap->widen_zext_i64 = 1;
        return (node_t *)wrap;
      }
    }
    /* `(long)unsigned_int` (int 未満幅の unsigned も含む): I64 へ zero-extend する。
     * `(long)` は通常 no-op だが、その場合 `(long)u + (long)u` の二項演算が I32 のまま
     * 計算され、符号なし 32bit ラップマスクで 2^32 を超える和が切り詰められていた。
     * ND_PTR_CAST(widen_zext_i64) でラップし IR_ZEXT を明示挿入する (coerce は常に SEXT
     * で unsigned widen に乗れない)。signed の `(long)` は coerce の SEXT で正しく動くため
     * 対象外。ポインタ・8B 以上・fp は対象外。 */
    if (!is_pointer && type_kind == TK_LONG && !ps_node_is_pointer(operand) &&
        operand->fp_kind == TK_FLOAT_KIND_NONE && ps_node_is_unsigned(operand) &&
        ps_node_type_size(operand) >= 1 && ps_node_type_size(operand) < 8) {
      node_mem_t *wrap = arena_alloc(sizeof(node_mem_t));
      wrap->base.kind = ND_PTR_CAST;
      wrap->base.lhs = operand;
      wrap->type_size = 8;
      wrap->is_unsigned = 1;
      wrap->widen_zext_i64 = 1;
      return (node_t *)wrap;
    }
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
      if (cast_elem_size > 0) wrap->deref_size = (short)cast_elem_size;
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
      /* base_deref_size は立てない: `(double*)X` の指す要素はスカラ double であって
       * 「ポインタ要素」ではない。立てると `((double*)X)[i]` の添字結果が誤って
       * ポインタ扱いされ E3064 になる (`*(double*)X` の deref は deref_size/pointee_fp_kind
       * のみ見るので影響なし)。 */
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
      /* キャスト先のポインタ要素サイズを反映する。これがないと `((int*)void_p)[i]` が
       * 既定の 8 バイトストライドで添字され誤った要素を読む。base_deref_size は立てない
       * (立てると「要素自体がポインタ」扱いになり subscript 結果が誤ってポインタ化する)。 */
      if (cast_elem_size > 0) wrap->deref_size = (short)cast_elem_size;
      /* pointee_is_void は明示的にデフォルト (0) のままにする */
      return (node_t *)wrap;
    }
    /* `(int *)x` / `(char *)x` など、スカラ整数型への (単段) ポインタキャスト:
     * 後段の deref / ポインタ算術が新しい要素サイズを使うよう ND_PTR_CAST で
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
                                  operand->kind == ND_PTR_CAST) &&
                                 ((node_mem_t *)operand)->is_tag_pointer);
    if (is_pointer && cast_elem_size > 0 &&
        operand_is_ptr_or_tag &&
        psx_node_pointer_qual_levels(operand) <= 1) {
      node_mem_t *wrap = arena_alloc(sizeof(node_mem_t));
      wrap->base.kind = ND_PTR_CAST;
      wrap->base.lhs = operand;
      wrap->is_pointer = 1;
      wrap->pointer_qual_levels = 1;
      wrap->type_size = 8;
      wrap->deref_size = (short)cast_elem_size;
      /* float/double ポインタへのキャストは pointee_fp_kind を立てる。これがないと
       * deref_size=8 が「8 バイトポインタ要素」と誤解され `((double*)&d)[i]` の結果が
       * ポインタ扱いされたり整数ロードになる (`*(double*)p` / creal のメモリ経由に必要)。 */
      if (type_kind == TK_FLOAT) wrap->pointee_fp_kind = TK_FLOAT_KIND_FLOAT;
      else if (type_kind == TK_DOUBLE) wrap->pointee_fp_kind = TK_FLOAT_KIND_DOUBLE;
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
    /* `(void*)0xdeadbeefL` のように整数定数をポインタ型へキャストすると、operand は
     * folding で ND_NUM のまま返る (ND_PTR_CAST にラップされない経路)。後段の
     * 「ポインタ変数の非ゼロ整数初期化」検査 (C11 6.5.16.1) が誤発火しないよう、
     * NUM ノードにフラグを立てて「これはキャスト経由」と通知する。 */
    if (is_pointer && operand->kind == ND_NUM) {
      ((node_num_t *)operand)->from_pointer_cast = 1;
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
    /* 定数の (int) キャスト: 32bit 符号付きへ切り詰める。これがないと
     * `(int)0x100000000L == 0` が定数畳み込みで 0x100000000==0 と評価され偽になっていた
     * (戻り値や代入では store 幅で切り詰められ偶然合っていた)。 */
    if (operand->kind == ND_NUM) {
      return psx_node_new_num((long long)(int)((node_num_t *)operand)->val);
    }
    /* int 幅超 (long, 8B) の非ポインタ値の (int) キャスト: 32bit 符号付きへ切り詰める。
     * `(x << 32) >> 32` (算術右シフト) で低 32bit を 64bit へ符号拡張するため、後段の
     * 比較/演算が 64bit 幅でも正しい値になる。代入では store 幅で偶然合っていたが
     * `(int)long_var == 0` 等のインライン比較が 64bit 比較で誤っていた。
     * ポインタ→int は稀かつ別経路 (is_pointer クリア) のためここでは触れない。
     * long 戻り関数は ps_node_type_size(ND_FUNCALL) が 8 を返すためここに入る。 */
    if (ps_node_type_size(operand) > 4 &&
        !ps_node_is_pointer(operand)) {
      /* operand が unsigned 戻り値の funcall (例 `unsigned f()`) だと、SHL が符号なし
       * 演算と見なされ ir_builder の 32bit ラップマスク (& 0xffffffff) が入り、
       * `(getu()<<32)` が 0 に潰れてシフトが壊れる。`(int)` は結果を符号付きにするので
       * funcall の unsigned ラベルをクリアして算術シフトを保つ。binop ノード (`u>>60`
       * 等のシフト) は is_unsigned が LSR/ASR を兼ねるため触らない (触ると論理シフトが
       * 算術シフトに化ける)。 */
      if (operand->kind == ND_FUNCALL) psx_node_set_unsigned(operand, 0);
      node_t *shl = psx_node_new_binary(ND_SHL, operand, psx_node_new_num(32));
      node_t *shr = psx_node_new_binary(ND_SHR, shl, psx_node_new_num(32));
      psx_node_set_unsigned(shl, 0);
      psx_node_set_unsigned(shr, 0); /* 算術右シフト (符号拡張) を強制 */
      return shr;
    }
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
      if (ps_node_type_size(operand) >= 4) psx_node_set_unsigned(operand, 0);
    }
    return operand;
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
      return n;
    }
    /* int 幅超 (long, 8B) の非ポインタ値の (signed/unsigned) キャスト: 32bit へ
     * 切り詰める。`(x<<32)>>32` で低 32bit を 64bit へ拡張 (unsigned は論理シフトで
     * ゼロ拡張、signed は算術シフトで符号拡張) する。long 戻り関数は
     * ps_node_type_size(ND_FUNCALL) が 8 を返すため対象になる。 */
    if (ps_node_type_size(operand) > 4 &&
        !ps_node_is_pointer(operand)) {
      node_t *shl = psx_node_new_binary(ND_SHL, operand, psx_node_new_num(32));
      node_t *shr = psx_node_new_binary(ND_SHR, shl, psx_node_new_num(32));
      psx_node_set_unsigned(shl, target_unsigned);
      psx_node_set_unsigned(shr, target_unsigned);
      return shr;
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
      return masked;
    }
    if (operand->kind == ND_LVAR || operand->kind == ND_GVAR ||
        operand->kind == ND_DEREF || operand->kind == ND_ADDR ||
        operand->kind == ND_STRING || operand->kind == ND_PTR_CAST ||
        operand->kind == ND_ASSIGN) {
      ((node_mem_t *)operand)->is_pointer = 0;
      /* sub-int operand では is_unsigned 書き換えが load 拡張を壊すため触れない
       * (上の TK_INT と同じ理由)。int 幅以上のみ符号ラベルを更新する。 */
      if (ps_node_type_size(operand) >= 4)
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
      if (cast_is_unsigned) psx_node_set_unsigned(n, 1);
      return n;
    }
    /* unsigned char/short: & マスクでゼロ拡張し unsigned ラベルを付ける。 */
    if (cast_is_unsigned) {
      node_t *masked = psx_node_new_binary(ND_BITAND, operand, psx_node_new_num(mask));
      psx_node_set_unsigned(masked, 1);
      return masked;
    }
    /* signed char/short: `(x << (64-width)) >> (64-width)` の算術シフトで符号拡張する。
     * 従来の `& マスク` だけだとビット (width-1) が立った runtime 値が符号拡張されず、
     * インライン比較/演算で誤った正値になっていた (char/short 変数への代入では store 幅 +
     * ldrsb/ldrsh の reload で偶然符号拡張され合っていた)。`(int)long` の切り詰めと同形。 */
    int sh = 64 - width;
    node_t *shl = psx_node_new_binary(ND_SHL, operand, psx_node_new_num(sh));
    node_t *shr = psx_node_new_binary(ND_SHR, shl, psx_node_new_num(sh));
    psx_node_set_unsigned(shl, 0);
    psx_node_set_unsigned(shr, 0); /* 算術右シフト (符号拡張) を強制 */
    return shr;
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
      /* 自己代入 `x = x` の警告 (両辺が同じ ND_LVAR offset)。
       * よくあるタイプミス・デバッグ残骸を検出する (clang -Wself-assign 相当)。
       * struct メンバ (ND_DEREF) や グローバル変数の自己代入も同様に拡張可だが、
       * まずはローカル変数の単純形だけにとどめる。 */
      if (assign_target && assign_target->kind == ND_LVAR &&
          rhs && rhs->kind == ND_LVAR &&
          ((node_lvar_t *)assign_target)->offset == ((node_lvar_t *)rhs)->offset) {
        diag_warn_tokf(DIAG_WARN_PARSER_SELF_ASSIGN, NULL,
                       "変数を自身に代入しています (タイプミスの可能性)");
      }
      /* 浮動小数点 → 整数の縮小変換警告 (clang -Wliteral-conversion / -Wfloat-conversion):
       * 整数の代入先 (fp_kind==NONE かつ非ポインタ) に float 値を代入。リテラル `w = 1.5;`
       * は値も併記、変数経由 `w = d;` は型のみで警告。`w = 2.0;` (整数値リテラル) は
       * 値が変わらないため除外、明示キャスト `w = (int)d;` も cast 適用後の fp_kind が
       * NONE になるため自然と除外される。 */
      if (assign_target && rhs && !ps_node_is_pointer(assign_target) &&
          assign_target->fp_kind == TK_FLOAT_KIND_NONE &&
          rhs->fp_kind != TK_FLOAT_KIND_NONE) {
        if (rhs->kind == ND_NUM) {
          double f = ((node_num_t *)rhs)->fval;
          if (f != (double)(long long)f) {
            diag_warn_tokf(DIAG_WARN_PARSER_FLOAT_TO_INT_NARROWING, NULL,
                           "整数変数に浮動小数点リテラル %g を代入しています (小数部が切り捨てられます)",
                           f);
          }
        } else {
          diag_warn_tokf(DIAG_WARN_PARSER_FLOAT_TO_INT_NARROWING, NULL,
                         "整数変数に浮動小数点値を代入しています (小数部が切り捨てられます)");
        }
      }
      node_mem_t *assign_node = psx_node_new_assign(assign_target, rhs);
      assign_node->type_size = ps_node_type_size(assign_node->base.lhs);
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

/* 論理演算 `&&` / `||` の両辺が同じ式 (`x && x` / `x || x`) を検出する
 * (gcc -Wlogical-op 相当)。x && x は単に x、x || x も同じく x なので冗長か
 * タイプミス。ND_LVAR は offset 一致、ND_GVAR は名前一致、ND_NUM は値一致で判定。
 * 副作用のある式 (関数呼び出し等) は除外: 同じ「式テキスト」でも 2 回評価される
 * 形なので意図的なケースがある。 */
static int logical_operands_identical(node_t *lhs, node_t *rhs) {
  if (!lhs || !rhs || lhs->kind != rhs->kind) return 0;
  if (lhs->kind == ND_LVAR) {
    return ((node_lvar_t *)lhs)->offset == ((node_lvar_t *)rhs)->offset;
  }
  if (lhs->kind == ND_GVAR) {
    node_gvar_t *lg = (node_gvar_t *)lhs;
    node_gvar_t *rg = (node_gvar_t *)rhs;
    return lg->name_len == rg->name_len &&
           memcmp(lg->name, rg->name, (size_t)lg->name_len) == 0;
  }
  if (lhs->kind == ND_NUM) {
    return ((node_num_t *)lhs)->val == ((node_num_t *)rhs)->val &&
           lhs->fp_kind == rhs->fp_kind;
  }
  return 0;
}

static node_t *logical_or(void) {
  node_t *node = logical_and();
  while (curtok()->kind == TK_OROR) {
    set_curtok(curtok()->next);
    node_t *rhs = logical_and();
    if (logical_operands_identical(node, rhs)) {
      diag_warn_tokf(DIAG_WARN_PARSER_IDENTICAL_LOGICAL_OPERANDS, NULL,
                     "'||' の両辺が同じ式です (常に同じ結果、タイプミスの可能性)");
    }
    node = psx_node_new_binary(ND_LOGOR, node, rhs);
  }
  return node;
}

static node_t *logical_and(void) {
  node_t *node = bit_or();
  while (curtok()->kind == TK_ANDAND) {
    set_curtok(curtok()->next);
    node_t *rhs = bit_or();
    if (logical_operands_identical(node, rhs)) {
      diag_warn_tokf(DIAG_WARN_PARSER_IDENTICAL_LOGICAL_OPERANDS, NULL,
                     "'&&' の両辺が同じ式です (常に同じ結果、タイプミスの可能性)");
    }
    node = psx_node_new_binary(ND_LOGAND, node, rhs);
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

/* 符号付き整数と符号なし整数の比較を検出する (clang -Wsign-compare 相当)。
 * C11 6.3.1.8 通常算術変換のうち、signed 側が unsigned に変換される場合のみ警告する。
 *
 * 抑制条件:
 * - 浮動小数・ポインタは符号概念がないので対象外。
 * - 整数昇格 (C11 6.3.1.1): char/short (signed/unsigned) は int に昇格されるため、
 *   narrow unsigned は実質 signed int として扱う (size<4 を signed と見なす)。
 *   例: `unsigned char vs int` は両辺とも signed int で比較 → 警告しない。
 * - 通常算術変換: signed 側のサイズが unsigned 側より厳密に大きい場合、signed は
 *   unsigned の全値を表現できるため unsigned 側が signed に変換される (= 安全)。
 *   例: `long s vs unsigned int u` (8>4) は signed 比較 → 警告しない。
 * - 非負整数リテラル: `u == 5` は値が unsigned に変換されても変化なし → 警告しない。 */
static int sign_cmp_effective_unsigned(node_t *n) {
  if (!n) return 0;
  if (ps_node_is_pointer(n)) return 0;
  if (n->fp_kind != TK_FLOAT_KIND_NONE) return 0;
  /* size<4: integer promotion で signed int になる (unsigned char/short も含む) */
  if (ps_node_type_size(n) < 4) return 0;
  return ps_node_is_unsigned(n) ? 1 : 0;
}
static int sign_cmp_effective_size(node_t *n) {
  int sz = n ? ps_node_type_size(n) : 4;
  return sz < 4 ? 4 : sz;
}
static void warn_if_sign_compare(node_t *lhs, node_t *rhs, const char *op) {
  if (!lhs || !rhs) return;
  int lu = sign_cmp_effective_unsigned(lhs);
  int ru = sign_cmp_effective_unsigned(rhs);
  if (lu == ru) return;
  node_t *signed_side = lu ? rhs : lhs;
  node_t *unsigned_side = lu ? lhs : rhs;
  /* 非負整数リテラル側はサイズに依らず安全 */
  if (signed_side->kind == ND_NUM && ((node_num_t *)signed_side)->val >= 0) return;
  /* signed が unsigned より厳密に幅広なら C11 6.3.1.8 で unsigned→signed 変換、安全 */
  if (sign_cmp_effective_size(signed_side) > sign_cmp_effective_size(unsigned_side)) return;
  diag_warn_tokf(DIAG_WARN_PARSER_SIGN_COMPARE, NULL,
                 "符号付きと符号なしの整数を比較しています ('%s' / 負値が大きな正の値として扱われる可能性)",
                 op);
}

/* 符号なし整数と 0 の比較が常に同じ結果になる形を検出する (clang
 * -Wtautological-unsigned-zero-compare 相当)。
 *   `u >= 0`, `0 <= u`  -> 常に真
 *   `u < 0`,  `0 > u`   -> 常に偽
 * `u > 0`, `u == 0`, `u <= 0` 等は実際に値次第なので警告しない。
 *
 * unsigned char / unsigned short は C11 6.3.1.1 で signed int に昇格されるが、値域は
 * 元のまま (0..255 / 0..65535) なので比較結果はやはり同じ。is_unsigned フラグだけで判定
 * すれば十分。 */
static int tuz_is_zero_literal(node_t *n) {
  return n && n->kind == ND_NUM && n->fp_kind == TK_FLOAT_KIND_NONE &&
         ((node_num_t *)n)->val == 0;
}
static int tuz_is_unsigned_integer(node_t *n) {
  return n && !ps_node_is_pointer(n) && n->fp_kind == TK_FLOAT_KIND_NONE &&
         ps_node_is_unsigned(n);
}
static void warn_if_tautological_unsigned_zero(node_t *lhs, node_t *rhs, const char *op) {
  /* `u OP 0` */
  if (tuz_is_unsigned_integer(lhs) && tuz_is_zero_literal(rhs)) {
    if (op[0] == '>' && op[1] == '=') {
      diag_warn_tokf(DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO, NULL,
                     "符号なし整数は常に 0 以上です: '%s 0' は常に真", op);
    } else if (op[0] == '<' && op[1] == '\0') {
      diag_warn_tokf(DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO, NULL,
                     "符号なし整数は常に 0 以上です: '%s 0' は常に偽", op);
    }
  }
  /* `0 OP u` */
  if (tuz_is_zero_literal(lhs) && tuz_is_unsigned_integer(rhs)) {
    if (op[0] == '<' && op[1] == '=') {
      diag_warn_tokf(DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO, NULL,
                     "符号なし整数は常に 0 以上です: '0 %s' は常に真", op);
    } else if (op[0] == '>' && op[1] == '\0') {
      diag_warn_tokf(DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO, NULL,
                     "符号なし整数は常に 0 以上です: '0 %s' は常に偽", op);
    }
  }
}

/* 自己比較 (`x == x` / `x != x` / `x < x` 等) は常に真または偽。タイプミスの可能性が高い。
 * 両辺が同じ ND_LVAR offset または同じ ND_GVAR 名なら警告 (clang -Wtautological-compare 相当)。 */
static void warn_if_self_compare(node_t *lhs, node_t *rhs, const char *op) {
  if (!lhs || !rhs) return;
  if (lhs->kind == ND_LVAR && rhs->kind == ND_LVAR &&
      ((node_lvar_t *)lhs)->offset == ((node_lvar_t *)rhs)->offset) {
    diag_warn_tokf(DIAG_WARN_PARSER_SELF_COMPARE, NULL,
                   "自己比較 (常に同じ値): '%s'", op);
    return;
  }
  if (lhs->kind == ND_GVAR && rhs->kind == ND_GVAR) {
    node_gvar_t *lg = (node_gvar_t *)lhs;
    node_gvar_t *rg = (node_gvar_t *)rhs;
    if (lg->name_len == rg->name_len &&
        memcmp(lg->name, rg->name, (size_t)lg->name_len) == 0) {
      diag_warn_tokf(DIAG_WARN_PARSER_SELF_COMPARE, NULL,
                     "自己比較 (常に同じ値): '%s'", op);
    }
  }
}

/* ポインタを非ゼロ整数定数と比較するのは C11 6.5.16.1 の制約違反相当 (clang
 * -Wpointer-integer-compare)。`if (p == 0)` の NULL ポインタ定数比較は合法で、
 * `if (p == (void*)5)` のような明示キャスト経由 (apply_cast が from_pointer_cast を
 * 立てる) も意図表明済みなので除外。 */
static void warn_if_pointer_int_compare(node_t *lhs, node_t *rhs, const char *op) {
  if (!lhs || !rhs) return;
  node_t *p = NULL, *n = NULL;
  if (ps_node_is_pointer(lhs) && !ps_node_is_pointer(rhs) && rhs->kind == ND_NUM) {
    p = lhs; n = rhs;
  } else if (ps_node_is_pointer(rhs) && !ps_node_is_pointer(lhs) && lhs->kind == ND_NUM) {
    p = rhs; n = lhs;
  }
  if (!p) return;
  node_num_t *num = (node_num_t *)n;
  if (num->val == 0) return;            /* NULL ポインタ定数 */
  if (num->from_pointer_cast) return;   /* 明示 (void*)5 等 */
  (void)p;
  diag_warn_tokf(DIAG_WARN_PARSER_POINTER_INTEGER_COMPARE, NULL,
                 "ポインタを非ゼロ整数定数 (%lld) と '%s' で比較しています (C11 6.5.16.1)",
                 num->val, op);
}

/* `!x == y` / `!x != y` の優先順位罠を検出する (clang -Wlogical-not-parentheses 相当)。
 * `!` は `==` より優先順位が高いため `!p == 0` は `(!p) == 0` と解釈され、書き手が
 * `!(p == 0)` を期待していた場合に意図と逆になる。ag_c は `!x` を ND_EQ(x, 0) に
 * 変換するため、LHS が ND_EQ かつ from_logical_not なら警告。`(!x) == y` のように
 * 括弧で囲んでも ag_c は括弧情報を残さないため誤検出するが、これは少数の事例で
 * clang も同様に警告を出す挙動。 */
static void warn_if_logical_not_paren_trap(node_t *lhs, const char *op) {
  if (lhs && lhs->from_logical_not) {
    diag_warn_tokf(DIAG_WARN_PARSER_LOGICAL_NOT_PARENTHESES, NULL,
                   "'%s' の左辺が単項 '!' で、'!' の優先順位が '%s' より高いため "
                   "'(!x) %s y' と解釈されます ('!(x %s y)' を意図していませんか)",
                   op, op, op, op);
  }
}

static node_t *equality(void) {
  node_t *node = relational();
  for (;;) {
    if (curtok()->kind == TK_EQEQ) {
      set_curtok(curtok()->next);
      node_t *rhs = relational();
      warn_if_logical_not_paren_trap(node, "==");
      warn_if_self_compare(node, rhs, "==");
      warn_if_sign_compare(node, rhs, "==");
      warn_if_pointer_int_compare(node, rhs, "==");
      node = psx_node_new_binary(ND_EQ, node, rhs);
    } else if (curtok()->kind == TK_NEQ) {
      set_curtok(curtok()->next);
      node_t *rhs = relational();
      warn_if_logical_not_paren_trap(node, "!=");
      warn_if_self_compare(node, rhs, "!=");
      warn_if_sign_compare(node, rhs, "!=");
      warn_if_pointer_int_compare(node, rhs, "!=");
      node = psx_node_new_binary(ND_NE, node, rhs);
    }
    else return node;
  }
}

static node_t *relational(void) {
  node_t *node = shift();
  for (;;) {
    if (curtok()->kind == TK_LT) {
      set_curtok(curtok()->next);
      node_t *rhs = shift();
      warn_if_sign_compare(node, rhs, "<");
      warn_if_tautological_unsigned_zero(node, rhs, "<");
      node = psx_node_new_binary(ND_LT, node, rhs);
    } else if (curtok()->kind == TK_LE) {
      set_curtok(curtok()->next);
      node_t *rhs = shift();
      warn_if_sign_compare(node, rhs, "<=");
      warn_if_tautological_unsigned_zero(node, rhs, "<=");
      node = psx_node_new_binary(ND_LE, node, rhs);
    } else if (curtok()->kind == TK_GT) {
      set_curtok(curtok()->next);
      node_t *rhs = shift();
      warn_if_sign_compare(node, rhs, ">");
      warn_if_tautological_unsigned_zero(node, rhs, ">");
      node = psx_node_new_binary(ND_LT, rhs, node);
    } else if (curtok()->kind == TK_GE) {
      set_curtok(curtok()->next);
      node_t *rhs = shift();
      warn_if_sign_compare(node, rhs, ">=");
      warn_if_tautological_unsigned_zero(node, rhs, ">=");
      node = psx_node_new_binary(ND_LE, rhs, node);
    }
    else return node;
  }
}

/* 整数定数式の int オーバーフロー検出 (clang -Winteger-overflow 相当)。
 * 両辺がリテラル int (long/long long サフィックスなし、符号付き、float でない、ポインタでない)
 * のとき、ADD/SUB/MUL の結果が int32 範囲を超えるなら C11 6.5p5 未定義動作。
 * 片方でも long リテラルなら long 演算扱いで対象外 (`2147483647L + 1L` は long なので安全)。
 * unsigned はモジュロ演算で定義されているので対象外。
 * `-2147483648` のような形は tokenizer が `2147483648` を long 扱いするため自然と除外。 */
static int int_const_overflow_is_int_literal(node_t *n) {
  if (!n || n->kind != ND_NUM) return 0;
  if (n->fp_kind != TK_FLOAT_KIND_NONE) return 0;
  if (n->is_unsigned) return 0;
  node_num_t *num = (node_num_t *)n;
  if (num->int_is_long || num->int_is_long_long) return 0;
  /* int 範囲チェック (値自体が long になる literal は除外) */
  return num->val >= -2147483648LL && num->val <= 2147483647LL;
}
static void warn_if_int_const_overflow(node_t *lhs, node_t *rhs, node_kind_t op_kind, const char *op_name) {
  if (!int_const_overflow_is_int_literal(lhs) || !int_const_overflow_is_int_literal(rhs)) return;
  long long a = ((node_num_t *)lhs)->val;
  long long b = ((node_num_t *)rhs)->val;
  long long r;
  if (op_kind == ND_ADD)      r = a + b;
  else if (op_kind == ND_SUB) r = a - b;
  else if (op_kind == ND_MUL) r = a * b;
  else return;
  if (r < -2147483648LL || r > 2147483647LL) {
    diag_warn_tokf(DIAG_WARN_PARSER_INTEGER_OVERFLOW, NULL,
                   "整数定数式 %lld %s %lld = %lld は int の範囲を超えています (C11 6.5p5 未定義動作)",
                   a, op_name, b, r);
  }
}

/* シフト量が型の幅を超えるリテラル時に C11 6.5.7p3 違反 (未定義動作) を警告する。
 * 通常の int (ts<=4) なら 32 ビット幅、long (ts==8) なら 64 ビット幅。負値も未定義。 */
static void warn_if_shift_oob(node_t *lhs, node_t *rhs, const char *op_name) {
  if (!rhs || rhs->kind != ND_NUM) return;
  long long r = ((node_num_t *)rhs)->val;
  int lhs_ts = lhs ? ps_node_type_size(lhs) : 4;
  int width = (lhs_ts >= 8) ? 64 : 32;
  if (r < 0 || r >= width) {
    diag_warn_tokf(DIAG_WARN_PARSER_SHIFT_OUT_OF_RANGE, NULL,
                   "シフト量 %lld が型の幅 (%d ビット) を超えています (C11 6.5.7p3 未定義動作): %s",
                   r, width, op_name);
  }
}

static node_t *shift(void) {
  node_t *node = add();
  for (;;) {
    if (curtok()->kind == TK_SHL) {
      set_curtok(curtok()->next);
      node_t *rhs = add();
      warn_if_shift_oob(node, rhs, "<<");
      node = psx_node_new_binary(ND_SHL, node, rhs);
    } else if (curtok()->kind == TK_SHR) {
      set_curtok(curtok()->next);
      node_t *rhs = add();
      warn_if_shift_oob(node, rhs, ">>");
      node = psx_node_new_binary(ND_SHR, node, rhs);
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
   * type_size == deref_size == 要素サイズ)。スケールは add() 側で ps_node_deref_size(n)
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
      } else {
        warn_if_int_const_overflow(node, rhs, ND_ADD, "+");
      }
      node = psx_node_new_binary(ND_ADD, node, rhs);
    } else if (curtok()->kind == TK_MINUS) {
      set_curtok(curtok()->next);
      node_t *rhs = mul();
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
        } else {
          warn_if_int_const_overflow(node, rhs, ND_SUB, "-");
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
      node_t *rhs = cast();
      warn_if_int_const_overflow(node, rhs, ND_MUL, "*");
      node = psx_node_new_binary(ND_MUL, node, rhs);
    } else if (curtok()->kind == TK_DIV) {
      set_curtok(curtok()->next);
      node_t *rhs = cast();
      /* C11 6.5.5p5: / または % で除数が 0 は未定義動作。リテラル 0 は警告。 */
      if (rhs && rhs->kind == ND_NUM && ((node_num_t *)rhs)->val == 0 &&
          rhs->fp_kind == TK_FLOAT_KIND_NONE) {
        diag_warn_tokf(DIAG_WARN_PARSER_DIVIDE_BY_ZERO, NULL,
                       "0 による除算 (C11 6.5.5p5 未定義動作)");
      }
      node = psx_node_new_binary(ND_DIV, node, rhs);
    } else if (curtok()->kind == TK_MOD) {
      set_curtok(curtok()->next);
      node_t *rhs = cast();
      if (rhs && rhs->kind == ND_NUM && ((node_num_t *)rhs)->val == 0 &&
          rhs->fp_kind == TK_FLOAT_KIND_NONE) {
        diag_warn_tokf(DIAG_WARN_PARSER_DIVIDE_BY_ZERO, NULL,
                       "0 による剰余 (C11 6.5.5p5 未定義動作)");
      }
      node = psx_node_new_binary(ND_MOD, node, rhs);
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
  int cast_is_unsigned = 0;
  if (parse_cast_type(curtok(), &cast_kind, &cast_is_ptr, &after_rparen,
                      &cast_tag_kind, &cast_tag_name, &cast_tag_len,
                      &cast_elem_size, &cast_fp_kind, &cast_array_count, &cast_is_unsigned)) {
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
                                     cast_tag_kind, cast_tag_name, cast_tag_len,
                                     cast_elem_size, cast_is_unsigned));
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
         * 化けて garbage が混じる。ND_PTR_CAST でラップして scalar 8B unsigned long として
         * 明示し、所属判定を回避する。 */
        node_t *lvar = psx_node_new_lvar_typed(arr_var->offset + 8, 8);
        as_lvar(lvar)->mem.is_unsigned = 1;
        node_mem_t *cast = arena_alloc(sizeof(node_mem_t));
        cast->base.kind = ND_PTR_CAST;
        cast->base.lhs = lvar;
        cast->type_size = 8;
        cast->is_unsigned = 1;
        return (node_t *)cast;
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
          set_curtok(t->next);
          int slot_off = arr_var->vla_row_stride_frame_off + 8 * (sub_depth - 1);
          node_t *lvarN = psx_node_new_lvar_typed(slot_off, 8);
          as_lvar(lvarN)->mem.is_unsigned = 1;
          node_mem_t *castN = arena_alloc(sizeof(node_mem_t));
          castN->base.kind = ND_PTR_CAST;
          castN->base.lhs = lvarN;
          castN->type_size = 8;
          castN->is_unsigned = 1;
          return (node_t *)castN;
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
          set_curtok(t->next->next);  /* `]` `)` を消費 */
          /* 2D VLA の行サイズも同様に ND_PTR_CAST でラップして所属判定を回避し、scalar 8B
           * unsigned long として variadic 経路に乗せる。 */
          node_t *lvar2 = psx_node_new_lvar_typed(arr_var->vla_row_stride_frame_off, 8);
          as_lvar(lvar2)->mem.is_unsigned = 1;
          node_mem_t *cast2 = arena_alloc(sizeof(node_mem_t));
          cast2->base.kind = ND_PTR_CAST;
          cast2->base.lhs = lvar2;
          cast2->type_size = 8;
          cast2->is_unsigned = 1;
          return (node_t *)cast2;
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
          return psx_node_new_num(arr_var->size);
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
            return psx_node_new_num(sgv->type_size);
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

/* operand が指す pointee が unsigned か。`unsigned char *p` の `*p` / `*(p+i)` を
 * zero-extend load させるため、ポインタ算術 (ND_ADD/SUB) や inc/dec を辿って pointee の
 * unsigned 性を引く (psx_node_pointee_fp_kind と対称)。これがないと `*(p+2)` が ldrsb で
 * 符号拡張されていた。 */
static int node_pointee_is_unsigned(node_t *n) {
  if (!n) return 0;
  switch (n->kind) {
    case ND_LVAR: return as_lvar(n)->mem.pointee_is_unsigned;
    case ND_GVAR:
    case ND_DEREF:
    case ND_ADDR:
    case ND_PTR_CAST:
      return ((node_mem_t *)n)->pointee_is_unsigned;
    case ND_ADD:
    case ND_SUB:
      return node_pointee_is_unsigned(n->lhs) || node_pointee_is_unsigned(n->rhs);
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC:
      return node_pointee_is_unsigned(n->lhs);
    default:
      return 0;
  }
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
    if (node_pointee_is_unsigned(operand)) node->is_unsigned = 1;
  }
  if (operand && (operand->kind == ND_LVAR || operand->kind == ND_GVAR ||
                  operand->kind == ND_DEREF || operand->kind == ND_ADDR)) {
    node_mem_t *operand_mem = (node_mem_t *)operand;
    if (operand_mem->is_const_qualified) node->is_const_qualified = 1;
    if (operand_mem->is_volatile_qualified) node->is_volatile_qualified = 1;
  } else if (operand && operand->kind == ND_FUNCALL) {
    node_func_t *fn = (node_func_t *)operand;
    if (funcall_ret_pointee_const(fn)) node->is_const_qualified = 1;
    if (funcall_ret_pointee_volatile(fn)) node->is_volatile_qualified = 1;
  }
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
      lvar_t *src = psx_decl_find_lvar_by_offset(((node_lvar_t *)probe)->offset);
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
      }
    } else if (probe && probe->kind == ND_FUNCALL) {
      /* `int (*f())[N]` の `*f()` / `*(f()+k)`: 結果は行 (int[N])。subscript_base_address_of が
       * load を skip し `(*f())[i]` が要素ストライドで添字できるよう、deref_size を要素サイズに
       * する (ローカル `int (*p)[N]` の `*p` と同じ)。 ds=N*elem を first_dim で割って elem を得る。 */
      node_func_t *fn = (node_func_t *)probe;
      if (fn->callee == NULL && fn->funcname) {
        int fd = psx_ctx_get_function_ret_pointee_array_first_dim(fn->funcname, fn->funcname_len);
        int sd = psx_ctx_get_function_ret_pointee_array_second_dim(fn->funcname, fn->funcname_len);
        int rowstride = ps_node_deref_size(probe);
        if (fd > 0 && rowstride > 0) {
          int inner = rowstride / fd;
          node->deref_size = (short)inner;
          if (sd > 0 && inner > 0) node->inner_deref_size = (short)(inner / sd);
        }
      } else if (fn->callee) {
        if (fn->callee->kind == ND_LVAR || fn->callee->kind == ND_GVAR ||
            fn->callee->kind == ND_DEREF || fn->callee->kind == ND_ADDR) {
          node_mem_t *cm = (node_mem_t *)fn->callee;
          int fd = cm->funcptr_ret_pointee_array_first_dim;
          int sd = cm->funcptr_ret_pointee_array_second_dim;
          int elem = cm->funcptr_ret_pointee_array_elem_size;
          int rowstride = ps_node_deref_size(probe);
          if (fd > 0 && elem > 0) {
            int inner = (sd > 0) ? sd * elem : elem;
            node->deref_size = (short)inner;
            if (sd > 0) node->inner_deref_size = (short)elem;
          } else if (fd > 0 && rowstride > 0) {
            node->deref_size = (short)(rowstride / fd);
          }
        }
        token_kind_t fk = TK_EOF;
        char *fname = NULL;
        int flen = 0;
        psx_node_get_tag_type(fn->callee, &fk, &fname, &flen, NULL);
        if (fk != TK_EOF) {
          node->tag_kind = fk;
          node->tag_name = fname;
          node->tag_len = flen;
          node->is_tag_pointer = 0;
          int elem = psx_ctx_get_tag_size(fk, fname, flen);
          if (elem > 0) node->deref_size = (short)elem;
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
    lvar_t *src = psx_decl_find_lvar_by_offset(((node_lvar_t *)operand)->offset);
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
    if (((node_mem_t *)operand)->type_size != 8) {
      node_mem_t *cp = arena_alloc(sizeof(node_mem_t));
      *cp = *(node_mem_t *)operand;
      cp->type_size = 8;
      return (node_t *)cp;
    }
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
  /* GNU 拡張 __real__ / __imag__: 複素数の実部/虚部を取り出す単項演算子
   * (実数オペランドでは __real__ x = x, __imag__ x = 0)。キーワードではなく
   * 特殊識別子として扱う (__func__ と同様)。creal/cimag を rvalue にも効かせる。 */
  if (k == TK_IDENT) {
    token_ident_t *kid = (token_ident_t *)curtok();
    if (kid->len == 8 && (memcmp(kid->str, "__real__", 8) == 0 ||
                          memcmp(kid->str, "__imag__", 8) == 0)) {
      int is_real = (kid->str[2] == 'r');
      set_curtok(curtok()->next);
      node_t *operand = cast();
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
    g_parse_type_alignof_mode = 1;
    int type_sz = parse_parenthesized_type_size();
    g_parse_type_alignof_mode = 0;
    if (type_sz < 0) {
      psx_diag_ctx(curtok(), "alignof", "%s",
                   diag_message_for(DIAG_ERR_PARSER_ALIGNOF_TYPE_NAME_REQUIRED));
    }
    return psx_node_new_num(type_sz);
  }
  if (k == TK_INC) { set_curtok(curtok()->next); return build_pre_inc_dec_node(ND_PRE_INC, "++"); }
  if (k == TK_DEC) { set_curtok(curtok()->next); return build_pre_inc_dec_node(ND_PRE_DEC, "--"); }
  if (k == TK_PLUS)  { set_curtok(curtok()->next); return cast(); }
  if (k == TK_MINUS) {
    set_curtok(curtok()->next);
    node_t *operand = cast();
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
    node_t *eq = psx_node_new_binary(ND_EQ, cast(), psx_node_new_num(0));
    eq->from_logical_not = 1;  /* `!p == 0` の precedence-trap 警告に使う */
    return eq;
  }
  if (k == TK_TILDE) {
    set_curtok(curtok()->next);
    node_t *neg = psx_node_new_binary(ND_SUB, psx_node_new_num(0), cast());
    return psx_node_new_binary(ND_SUB, neg, psx_node_new_num(1));
  }
  if (k == TK_MUL) { set_curtok(curtok()->next); return build_unary_deref_node(cast()); }
  if (k == TK_AMP) {
    set_curtok(curtok()->next);
    /* `&(int){5}` のヒント: ファイルスコープのスカラ複合リテラルを静的 gvar として
     * 実体化させ、アドレスを取れるようにする (parse_compound_literal_from_type が読む)。 */
    g_addr_of_compound_pending = 1;
    node_t *operand = cast();
    g_addr_of_compound_pending = 0;
    return build_unary_addr_node(operand);
  }
  return apply_postfix(primary());
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
    node_func_t *fn = (node_func_t *)node;
    if (fn->callee == NULL && fn->funcname &&
        psx_ctx_get_function_ret_pointee_array_first_dim(fn->funcname, fn->funcname_len) > 0) {
      int fd = psx_ctx_get_function_ret_pointee_array_first_dim(fn->funcname, fn->funcname_len);
      int sd = psx_ctx_get_function_ret_pointee_array_second_dim(fn->funcname, fn->funcname_len);
      if (fd > 0 && ds > 0) {
        inner_ds = ds / fd;  /* 1D: elem、2D: M*elem */
        if (sd > 0 && inner_ds > 0) next_ds = inner_ds / sd;
      }
    } else if (fn->callee && (fn->callee->kind == ND_LVAR || fn->callee->kind == ND_GVAR ||
                              fn->callee->kind == ND_DEREF || fn->callee->kind == ND_ADDR)) {
      node_mem_t *cm = (node_mem_t *)fn->callee;
      int fd = cm->funcptr_ret_pointee_array_first_dim;
      int sd = cm->funcptr_ret_pointee_array_second_dim;
      int elem = cm->funcptr_ret_pointee_array_elem_size;
      if (fd > 0 && elem > 0) {
        inner_ds = (sd > 0) ? sd * elem : elem;
        if (sd > 0) next_ds = elem;
      } else if (fd > 0 && ds > 0) {
        inner_ds = ds / fd;
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
  if (pql >= 1 && bds > 0 && subscript_is_intermediate_row) {
    /* 中間行: pointer 化せず deref_size=inner_ds を保ち、pql / bds を次段へ carry して
     * 最終次元の subscript が「要素はポインタ」分岐に乗れるようにする。 */
    deref->pointer_qual_levels = pql;
    deref->base_deref_size = (short)bds;
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
         node->kind == ND_FUNCALL) && pql >= 2) {
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
  /* array-of-pointer-to-array struct メンバ (`int (*p[M])[N]`) の `s.p[i]`: 要素は
   * pointer-to-array。結果 deref を「単一 pointer-to-array」(`int (*sp)[N]`) と同じ表現に
   * 組み直す: is_tag_pointer=1、deref_size=ポインティ全バイト数 (N*elem)、inner_deref_size=
   * 要素サイズ (elem)、is_pointer=0。これで `(*s.p[i])[j]` の単項 `*` が build_unary_deref_node の
   * 既存 pointer-to-array 分岐 (operand=ND_DEREF && is_tag_pointer && inner_deref_size>0 &&
   * deref_size>inner_deref_size) に乗り、subscript_base_address_of が lhs を返す経路に乗せる。 */
  if (node->kind == ND_DEREF) {
    node_mem_t *base_mem_ptp = (node_mem_t *)node;
    if (base_mem_ptp->ptr_array_pointee_bytes > 0 && bds > 0) {
      deref->is_tag_pointer = 1;
      deref->is_pointer = 0;
      deref->deref_size = (short)base_mem_ptp->ptr_array_pointee_bytes;
      deref->inner_deref_size = (short)bds;
      deref->pointer_qual_levels = 0;
      deref->base_deref_size = 0;
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
    /* unsigned 配列/ポインタの subscript 結果: 最終スカラ要素なら is_unsigned
     * (zero-extend load)、中間 (まだ配列の行 / ポインタ) なら次段へ pointee_is_unsigned
     * を伝播。最終スカラ判定は「結果がポインタでなく、かつ多次元の中間行 (es>inner_ds)
     * でない」。旧条件 `pql==0 && inner_ds==0` は `unsigned char *p` のように単段ポインタ
     * で pql=1 / inner_ds=elem(1) になるケースを最終要素と認識できず、`p[i]` が ldrsb で
     * 符号拡張されていた (es>inner_ds 判定は fp の中間行判定と対称)。 */
    if (base_mem && base_mem->pointee_is_unsigned) {
      int is_final_scalar = !deref->is_pointer && !(inner_ds > 0 && es > inner_ds);
      if (is_final_scalar) deref->is_unsigned = 1;
      else                 deref->pointee_is_unsigned = 1;
    }
    if (base_mem) {
      if (base_mem->funcptr_param_fp_mask) {
        deref->funcptr_param_fp_mask = base_mem->funcptr_param_fp_mask;
      }
      if (base_mem->funcptr_param_int_mask) {
        deref->funcptr_param_int_mask = base_mem->funcptr_param_int_mask;
      }
      if (base_mem->funcptr_ret_pointee_array_first_dim) {
        deref->funcptr_ret_pointee_array_first_dim =
            base_mem->funcptr_ret_pointee_array_first_dim;
        deref->funcptr_ret_pointee_array_second_dim =
            base_mem->funcptr_ret_pointee_array_second_dim;
        deref->funcptr_ret_pointee_array_elem_size =
            base_mem->funcptr_ret_pointee_array_elem_size;
      }
      if (base_mem->is_const_qualified) deref->is_const_qualified = 1;
      if (base_mem->is_volatile_qualified) deref->is_volatile_qualified = 1;
    }
    /* `unsigned char *g(); g()[i]`: 関数のポインタ戻り値の pointee が unsigned なら
     * zero-extend load させる (base_mem は ND_FUNCALL を拾わないので別途)。 */
    if (node->kind == ND_FUNCALL) {
      node_func_t *fn = (node_func_t *)node;
      if (fn->callee == NULL && fn->funcname &&
          psx_ctx_get_function_ret_is_pointer(fn->funcname, fn->funcname_len) &&
          psx_ctx_get_function_ret_is_unsigned(fn->funcname, fn->funcname_len)) {
        if (pql == 0 && inner_ds == 0) deref->is_unsigned = 1;
        else                           deref->pointee_is_unsigned = 1;
      }
      if (fn->callee == NULL && fn->funcname &&
          psx_ctx_get_function_ret_is_pointer(fn->funcname, fn->funcname_len)) {
        if (funcall_ret_pointee_const(fn)) deref->is_const_qualified = 1;
        if (funcall_ret_pointee_volatile(fn)) deref->is_volatile_qualified = 1;
      } else if (fn->callee) {
        if (funcall_ret_pointee_const(fn)) deref->is_const_qualified = 1;
        if (funcall_ret_pointee_volatile(fn)) deref->is_volatile_qualified = 1;
      }
    }
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

static int expr_funcall_returns_funcptr(node_t *fcall) {
  if (!fcall || fcall->kind != ND_FUNCALL) return 0;
  node_func_t *fc = (node_func_t *)fcall;
  if (!fc->callee && fc->funcname) {
    return psx_ctx_get_function_ret_is_funcptr(fc->funcname, fc->funcname_len);
  }
  if (fc->callee && fc->callee->kind == ND_LVAR) {
    lvar_t *lv = psx_decl_find_lvar_by_offset(((node_lvar_t *)fc->callee)->offset);
    return lv && lv->funcptr_ret_is_pointer;
  }
  return 0;
}

static node_t *parse_call_postfix(node_t *callee) {
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
    node->base.fp_kind = psx_ctx_get_function_ret_fp_kind(fr->funcname, fr->funcname_len);
    node->base.ret_struct_size = psx_ctx_get_function_ret_struct_size(fr->funcname, fr->funcname_len);
    if (psx_ctx_get_function_ret_is_complex(fr->funcname, fr->funcname_len))
      node->base.is_complex = 1;
    if (psx_ctx_get_function_ret_is_unsigned(fr->funcname, fr->funcname_len))
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
   * を設定済み) も拾う。struct メンバ `s.f` (ND_DEREF) は tag メンバ経路で戻り型
   * fp_kind を伝播していないため未対応。 */
  if (callee) {
    tk_float_kind_t ret_fp = psx_node_pointee_fp_kind(callee);
    node_mem_t *cm = (callee->kind == ND_LVAR || callee->kind == ND_GVAR ||
                      callee->kind == ND_DEREF || callee->kind == ND_ADDR)
                         ? (node_mem_t *)callee : NULL;
    if (ret_fp != TK_FLOAT_KIND_NONE &&
        !(cm && cm->funcptr_ret_pointee_array_first_dim > 0)) {
      node->base.fp_kind = ret_fp;
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
  /* 関数ポインタ経由呼び出し `fp(3)`: fp 仮引数に整数実引数を渡したら昇格する。
   * 直接呼び出しは ir_builder が coerce するが、間接は funcptr 変数が仮引数型を
   * 持つ必要があるため、宣言時に記録した funcptr_param_fp_mask を見て parser 側で
   * wrap_to_fp する (既に fp の実引数なら no-op)。 */
  unsigned short fp_param_mask = 0;
  unsigned short int_param_mask = 0;
  if (callee && callee->kind == ND_LVAR) {
    lvar_t *fpv = psx_decl_find_lvar_by_offset(((node_lvar_t *)callee)->offset);
    if (fpv) {
      fp_param_mask = fpv->funcptr_param_fp_mask;
      int_param_mask = fpv->funcptr_param_int_mask;
    }
  } else if (callee && callee->kind == ND_GVAR) {
    node_gvar_t *gvn = (node_gvar_t *)callee;
    global_var_t *gv = psx_find_global_var(gvn->name, gvn->name_len);
    if (gv) {
      fp_param_mask = gv->funcptr_param_fp_mask;
      int_param_mask = gv->funcptr_param_int_mask;
    }
  } else if (callee && callee->kind == ND_DEREF) {
    fp_param_mask = ((node_mem_t *)callee)->funcptr_param_fp_mask;
    int_param_mask = ((node_mem_t *)callee)->funcptr_param_int_mask;
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
      if (code) node->args[i] = wrap_fp_to_int_width(node->args[i], code == 2 ? 8 : 4);
    }
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
                      &cast_elem_size, &cast_fp_kind, &cast_array_count, NULL) &&
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
      cast();                     // 制御式は未評価 (C11 6.5.1.1) なので operand の値は捨てる
      if (curtok()->kind == TK_COMMA) {
        control_ty = cty;
        got_cast_ty = 1;
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
    node_t *control = assign();
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
static node_t *try_parse_builtin_expect_call(token_ident_t *tok) {
  if (tok->len != 16 || memcmp(tok->str, "__builtin_expect", 16) != 0) return NULL;
  if (curtok()->kind != TK_LPAREN) return NULL;
  set_curtok(curtok()->next); // skip '('
  node_t *exp = assign();
  tk_expect(',');
  (void)assign(); // discard hint
  tk_expect(')');
  return exp;
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
  /* C99/C11 では implicit function declaration は禁止 (C89 では int 戻りで暗黙宣言可)。
   * `undecl_func()` のように未宣言関数を呼ぶ場合に診断する。clang は default で warning、
   * `-Werror=implicit-function-declaration` で error。ag_c も warning として扱う。
   * tok が関数として登録されておらず、グローバル変数 (関数ポインタ) でもないなら未宣言。 */
  if (!psx_ctx_has_function_name(tok->str, tok->len) &&
      !psx_find_global_var(tok->str, tok->len)) {
    diag_warn_tokf(DIAG_WARN_PARSER_IMPLICIT_FUNCTION_DECL, (token_t *)tok,
                   "関数 '%.*s' は宣言されていません (C99/C11 で implicit declaration は不可)",
                   tok->len, tok->str);
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
  node->base.ret_struct_size = psx_ctx_get_function_ret_struct_size(tok->str, tok->len);
  // 関数戻り値が float/double のときは call ノードに fp_kind を設定し、
  // `(int)call()` キャストで apply_cast が ND_FP_TO_INT を挿入できるようにする。
  node->base.fp_kind = psx_ctx_get_function_ret_fp_kind(tok->str, tok->len);
  /* 戻り値が _Complex のとき call ノードに is_complex を立てる。build_node_funcall が
   * HFA 戻り値 (d0/d1) を一時 slot に受け、複素数値として扱えるようにする。 */
  if (psx_ctx_get_function_ret_is_complex(tok->str, tok->len)) {
    node->base.is_complex = 1;
  }
  /* 戻り値型が unsigned のとき call ノードに is_unsigned を立てる。これがないと
   * `unsigned f(); f() <= 100` が符号付き比較 (LE) になり、32bit 比較で 0xFFFFFFFF を
   * 負数扱いして誤判定する (戻り値の符号性が funcall ノードへ伝播していなかった)。
   * ただし unsigned char/short は整数昇格 (C11 6.3.1.1) で signed int になるため
   * is_unsigned を立てない。立てると `unsigned char f(); f() > -1` が unsigned 比較に
   * なり -1 が UINT_MAX 扱いで誤って false になる (unsigned int/long のみ保持)。 */
  if (psx_ctx_get_function_ret_is_unsigned(tok->str, tok->len) &&
      !psx_ctx_get_function_ret_is_pointer(tok->str, tok->len)) {
    /* ポインタ戻り (`unsigned char *g()`) の ctx unsigned は pointee 符号 (subscript 用) で
     * あって戻り値そのものではないので、ここ (戻り値の符号化) では除外する。 */
    token_kind_t rk = psx_ctx_get_function_ret_token_kind(tok->str, tok->len);
    if (rk != TK_CHAR && rk != TK_SHORT) {
      node->base.is_unsigned = 1;
    }
  }
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
      addr->tag_kind = gv->tag_kind;
      addr->tag_name = gv->tag_name;
      addr->tag_len = gv->tag_len;
      addr->tag_scope_depth_p1 = gv->tag_scope_depth_p1;  /* shadow 対応 */
      addr->is_const_qualified = gv->is_const_qualified;
      addr->is_volatile_qualified = gv->is_volatile_qualified;
      if (gv->tag_kind != TK_EOF) addr->is_tag_pointer = 1;
      // 多次元配列: outer_stride を 1 次サブスクリプトのステップとして使う。
      // ローカル配列の build_array_lvar_addr_node と同じレイアウト。
      int stride = (gv->outer_stride > 0) ? gv->outer_stride : gv->deref_size;
      addr->type_size = stride;
      addr->deref_size = stride;
      addr->is_pointer = 1;
      /* `double a[5]` 等: 要素型 fp_kind を pointee_fp_kind に伝播し、
       * build_subscript_deref が FP load を組み立てられるようにする。
       * 関数ポインタ配列 `double (*gops[N])(double)` は要素自体がポインタなので gv->fp_kind は
       * NONE で、戻り型 fp は gv->pointee_fp_kind に入る。これを pointee_fp_kind に伝播し、
       * かつ base_deref_size=要素ポインタサイズ(8) を立てて build_subscript_deref の
       * 「不透明な関数ポインタ要素」分岐 (inner_ds==0 && bds>0) に乗せる。`gops[i]()` の戻り値が
       * d0 で読まれるようになる (ローカル funcptr 配列と同じ表現)。 */
      if (gv->fp_kind != TK_FLOAT_KIND_NONE) {
        addr->pointee_fp_kind = gv->fp_kind;
      } else if (gv->pointee_fp_kind != TK_FLOAT_KIND_NONE) {
        addr->pointee_fp_kind = (tk_float_kind_t)gv->pointee_fp_kind;
        addr->base_deref_size = 8;
      }
      /* unsigned グローバル配列: 要素 subscript 結果を zero-extend load させる。 */
      addr->pointee_is_unsigned = gv->is_unsigned ? 1 : 0;
      addr->funcptr_param_fp_mask = gv->funcptr_param_fp_mask;
      addr->funcptr_param_int_mask = gv->funcptr_param_int_mask;
      addr->funcptr_ret_pointee_array_first_dim =
          gv->funcptr_ret_pointee_array_first_dim;
      addr->funcptr_ret_pointee_array_second_dim =
          gv->funcptr_ret_pointee_array_second_dim;
      addr->funcptr_ret_pointee_array_elem_size =
          gv->funcptr_ret_pointee_array_elem_size;
      /* `char *names[N]` 等のグローバルポインタ配列: 各要素 (= スカラポインタ) の
       * pointee サイズ情報を伝播。subscript の結果 ND_DEREF に is_scalar_ptr_member
       * を立てて、struct メンバ char* (commit 6a663ed) と同じく ND_DEREF をそのまま
       * subscript base にしてポインタ値の load を引き起こす。
       * 関数ポインタ配列 (`int (*ops[N])(int)`) は ops[i](val) で deref→call され、
       * 2 段 subscript はしない (= pointee_elem_size を見ない) ので影響なし。 */
      if (gv->pointee_elem_size > 0 && gv->tag_kind == TK_EOF) {
        addr->pointee_is_scalar_ptr = 1;
        /* 2D 以上のポインタ配列 (`int *t[2][2]`) では最終 subscript の base が ND_ADDR で
         * なく中間 ND_DEREF になり gv を引けない。要素ポインタの pointee サイズ
         * (`int*` なら 4) を base_deref_size に載せて中間次元へ carry する。fp funcptr 配列
         * (base_deref_size=8 を上で設定) とは排他なので未設定時のみ。 */
        if (addr->base_deref_size == 0) addr->base_deref_size = (short)gv->pointee_elem_size;
      }
      /* グローバル struct ポインタ配列 (`struct P *parr[3]`): 要素は struct ポインタ。
       * build_subscript_deref の「要素はポインタ」分岐 (pql>=1 && bds>0) に乗せて
       * `parr[i]->m` の `->` 解決ができるよう pointer_qual_levels=1 / base_deref_size を立てる。
       * 既存はメンバ struct ポインタ配列 (db98d34) は対応していたがグローバルは漏れていた。
       * deref_size には pointee struct のサイズを使う (gv->deref_size に入っている)。 */
      if (gv->tag_kind != TK_EOF && gv->is_tag_pointer) {
        if (addr->base_deref_size == 0) addr->base_deref_size = (short)gv->deref_size;
        if (addr->pointer_qual_levels == 0) addr->pointer_qual_levels = 1;
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
    /* 配列へのポインタ `T (*pa)[N]...` グローバル: gv は 8B スカラポインタ (type_size=8) で
     * outer_stride>0。第1subscript `pa[i]` は pointee 全体 (outer_stride) をステップし、以降
     * mid_stride / extra_strides / deref_size(=elem) で内側次元をステップする。多次元配列の
     * 配列分岐 (上の ND_ADDR) と同じ outer/mid/extra マッピングを scalar ND_GVAR に適用する。
     * pa のポインタ値は type_size=8 でロードされ、それに添字が加算される。 */
    if (gv->outer_stride > 0 && !gv->is_array) {
      gvar_node->mem.deref_size = (short)gv->outer_stride;
      if (gv->mid_stride > 0) {
        gvar_node->mem.inner_deref_size = (short)gv->mid_stride;
        if (gv->extra_strides_count > 0) {
          gvar_node->mem.next_deref_size = (short)gv->extra_strides[0];
          for (int i = 1; i < gv->extra_strides_count && (i - 1) < 5; i++) {
            gvar_node->mem.extra_strides[i - 1] = gv->extra_strides[i];
          }
          gvar_node->mem.extra_strides[gv->extra_strides_count - 1] = gv->deref_size;
          gvar_node->mem.extra_strides_count = gv->extra_strides_count;
        } else {
          gvar_node->mem.next_deref_size = (short)gv->deref_size;
        }
      } else {
        gvar_node->mem.inner_deref_size = (short)gv->deref_size;
      }
    }
    /* タグ情報 (struct / union): build_member_access が `.x` を解決するときに
     * psx_node_get_tag_type 経由でここを読む。 */
    gvar_node->mem.tag_kind = gv->tag_kind;
    gvar_node->mem.tag_name = gv->tag_name;
    gvar_node->mem.tag_len = gv->tag_len;
    gvar_node->mem.tag_scope_depth_p1 = gv->tag_scope_depth_p1;  /* shadow 対応 */
    gvar_node->mem.is_tag_pointer = gv->is_tag_pointer;
    gvar_node->mem.is_const_qualified = gv->is_const_qualified;
    gvar_node->mem.is_volatile_qualified = gv->is_volatile_qualified;
    if (gv->is_tag_pointer) gvar_node->mem.is_pointer = 1;
    /* 多段ポインタグローバル (`int **gp`): `*gp` は int* (8B) を返すので、参照ノードの
     * deref_size を 8 に、base_deref_size を要素サイズ (gv->deref_size) にし、
     * pointer_qual_levels を立てる。build_unary_deref_node の pql>=2 分岐がこれを見て
     * 中間 deref を 8B ポインタロードにする (ローカル `int **lp` と同じ表現)。
     * pointer-to-array (outer_stride>0) は別表現なので除外。 */
    if (gv->pointer_qual_levels >= 2 && gv->outer_stride == 0) {
      gvar_node->mem.base_deref_size = gv->deref_size;
      gvar_node->mem.deref_size = 8;
      gvar_node->mem.pointer_qual_levels = gv->pointer_qual_levels;
      gvar_node->mem.is_pointer = 1;
    }
    /* 浮動小数スカラのグローバル: fp_kind を node に伝播。IR builder が
     * これを見て IR_TY_F32/F64 として load を発行する。 */
    gvar_node->mem.base.fp_kind = gv->fp_kind;
    /* 関数ポインタグローバル `double (*gops)(double)`: 戻り型 fp_kind を pointee_fp_kind
     * に伝播。parse_call_postfix がこれを funcall に載せ、戻り値を d0 で読む。 */
    gvar_node->mem.pointee_fp_kind = gv->pointee_fp_kind;
    gvar_node->mem.funcptr_ret_pointee_array_first_dim =
        gv->funcptr_ret_pointee_array_first_dim;
    gvar_node->mem.funcptr_ret_pointee_array_second_dim =
        gv->funcptr_ret_pointee_array_second_dim;
    gvar_node->mem.funcptr_ret_pointee_array_elem_size =
        gv->funcptr_ret_pointee_array_elem_size;
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
  for (global_var_t *gv = psx_find_global_var(var->static_global_name, var->static_global_name_len); gv; gv = NULL) {
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
  addr->is_const_qualified = var->is_const_qualified;
  addr->is_volatile_qualified = var->is_volatile_qualified;
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
  node->funcptr_param_fp_mask = var->funcptr_param_fp_mask;
  node->funcptr_param_int_mask = var->funcptr_param_int_mask;
  node->funcptr_ret_pointee_array_first_dim = var->funcptr_ret_pointee_array_first_dim;
  node->funcptr_ret_pointee_array_second_dim = var->funcptr_ret_pointee_array_second_dim;
  node->funcptr_ret_pointee_array_elem_size = var->funcptr_ret_pointee_array_elem_size;
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
  node->tag_scope_depth_p1 = var->tag_scope_depth_p1;  /* shadow 対応 */
  node->is_tag_pointer = (var->tag_kind != TK_EOF) ? 1 : 0;
  node->is_pointer = 1;
  node->is_const_qualified = var->is_const_qualified;
  node->is_volatile_qualified = var->is_volatile_qualified;
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
    /* `static struct S a` の tag 情報を ND_GVAR に伝播して `a.member` の
     * メンバアクセスを解決可能にする (tag を落とすと build_member_access が
     * 失敗する)。is_tag_pointer は実体 (= 0)。 */
    gv->mem.tag_kind = var->tag_kind;
    gv->mem.tag_name = var->tag_name;
    gv->mem.tag_len = var->tag_len;
    gv->mem.tag_scope_depth_p1 = var->tag_scope_depth_p1;  /* shadow 対応 */
    gv->mem.is_tag_pointer = 0;
    /* スカラ static local がポインタ型 (`static char *msg = "..."`) のとき、
     * size==8 かつ pointee の elem_size がそれより小さい (char=1, int=4 等) なら
     * is_pointer を立てる。非ポインタの static スカラ (`static int n = 5;`) は
     * size==elem_size==4 で偽になる。修正前は is_pointer 未設定で subscript
     * `msg[i]` が両辺非ポインタとして E3064 reject されていた。同 elem (long*)
     * のケースは pointer_qual_levels を立てる必要があるが、現状の static-local
     * 経路では追跡しないので別 follow-up。 */
    if (sz > var->elem_size && var->elem_size > 0) gv->mem.is_pointer = 1;
    gv->name = var->static_global_name;
    gv->name_len = var->static_global_name_len;
    var->is_used = 1;
    return (node_t *)gv;
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
  as_lvar(n)->mem.vla_row_stride_frame_off = var->vla_row_stride_frame_off;
  as_lvar(n)->mem.vla_strides_remaining = var->vla_strides_remaining;
  as_lvar(n)->mem.tag_kind = var->tag_kind;
  as_lvar(n)->mem.tag_name = var->tag_name;
  as_lvar(n)->mem.tag_len = var->tag_len;
  as_lvar(n)->mem.tag_scope_depth_p1 = var->tag_scope_depth_p1;  /* shadow 対応 */
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
  as_lvar(n)->mem.funcptr_ret_pointee_array_first_dim =
      var->funcptr_ret_pointee_array_first_dim;
  as_lvar(n)->mem.funcptr_ret_pointee_array_second_dim =
      var->funcptr_ret_pointee_array_second_dim;
  as_lvar(n)->mem.funcptr_ret_pointee_array_elem_size =
      var->funcptr_ret_pointee_array_elem_size;
  as_lvar(n)->mem.is_unsigned = var->is_unsigned;
  /* `unsigned *p` の `*p` を zero-extend load させるため pointee_is_unsigned を
   * 伝播する (var->is_unsigned は基底型 unsigned を表すのでポインタにも乗る)。 */
  as_lvar(n)->mem.pointee_is_unsigned = var->is_unsigned;
  as_lvar(n)->mem.is_complex = var->is_complex;
  as_lvar(n)->mem.is_atomic = var->is_atomic;
  as_lvar(n)->mem.pointee_is_void = var->pointee_is_void;
  as_lvar(n)->mem.is_bool = var->is_bool;
  /* _Generic で long/long long, char/signed char を区別するための型識別。 */
  as_lvar(n)->mem.is_long_long = var->is_long_long;
  as_lvar(n)->mem.is_plain_char = var->is_plain_char;
  as_lvar(n)->mem.is_long_double = var->is_long_double;
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
    node_t *be = try_parse_builtin_expect_call(tok);
    if (be) return be;
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

  if (curtok()->kind == TK_LPAREN && curtok()->next &&
      curtok()->next->kind == TK_LBRACE) {
    return psx_parse_statement_expression();
  }

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
