#include "internal/decl.h"
#include "internal/arena.h"
#include "internal/core.h"
#include "internal/diag.h"
#include "internal/expr.h"
#include "internal/node_utils.h"
#include "internal/semantic_ctx.h"
#include "config_runtime.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include "../tokenizer/escape.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lvar_t *locals;       // 現在のスコープで見えるローカル変数リスト
static lvar_t *all_locals;   // 全スコープのローカル変数リスト（未使用チェック用）
static int locals_offset;
static inline token_t *curtok(void) { return tk_get_current_token(); }
static inline void set_curtok(token_t *tok) { tk_set_current_token(tok); }

// ブロックスコープのローカル変数リスト保存スタック
#define LVAR_SCOPE_STACK_MAX 256
static lvar_t *lvar_scope_stack[LVAR_SCOPE_STACK_MAX];
static int lvar_scope_depth;
// 集合体メンバの所在を取り回すためのまとめ構造。
// 個別の out 引数（name/len/offset/...）を毎回展開していたところで使う。
typedef struct {
  char *name;
  int len;
  int offset;
  int type_size;
  int array_len;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int is_tag_pointer;
} aggregate_member_info_t;

static bool tag_find_member(lvar_t *var, char *name, int len, aggregate_member_info_t *out);
static bool tag_get_member_at(lvar_t *var, int ordinal, aggregate_member_info_t *out);
static bool tag_get_next_named_member(lvar_t *var, int *ordinal_inout,
                                      aggregate_member_info_t *out);
static node_t *parse_scalar_brace_initializer(void);
static node_t *parse_array_initializer(lvar_t *var);
static node_t *parse_struct_initializer(lvar_t *var);
static node_t *parse_union_initializer(lvar_t *var);
static node_t *parse_struct_copy_initializer(lvar_t *var);
static node_t *new_struct_member_lvar(lvar_t *var, int member_offset, int member_type_size,
                                      token_kind_t member_tag_kind, char *member_tag_name,
                                      int member_tag_len, int member_is_tag_pointer);
static int parse_nonneg_const_expr_decl(const char *what);
static int resolve_copy_source_lvar(node_t *expr, node_t **out_prefix, node_lvar_t **out_src);
static int is_supported_scalar_store_size(int size);
static int is_compatible_tag_object_lvar(node_lvar_t *src, lvar_t *var);
static node_t *build_struct_copy_chain_from_source(lvar_t *dst, node_lvar_t *src);
static node_t *try_parse_array_member_copy_initializer(int dst_base_off, int elem_size, int array_len);
static node_t *try_parse_array_member_string_initializer(int dst_base_off, int elem_size, int array_len);
static string_lit_t *find_string_lit_by_label(char *label);
typedef struct {
  token_kind_t type_kind;
  int is_unsigned;
  int elem_size;
  tk_float_kind_t fp_kind;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int base_is_pointer;
  int is_const_qualified;
  int is_volatile_qualified;
  int td_pointee_const;
  int td_pointee_volatile;
  int is_extern_decl;
  // typedef が配列型 (`typedef int M[2][3][4]`) のとき、その各次元 (dims[0] が最外側)
  // と次元数。`M m;` 宣言で配列として lvar 登録するために宣言子側で参照する。
  int td_array_dims[8];
  int td_array_dim_count;
} local_decl_spec_t;
typedef struct {
  int arr_total;
  int is_array;
  int has_incomplete_array;
  // 最初の `[N]` の N。複数次元 `typedef int M[3][4]` で
  // 仮引数 `M *p` の mid_stride を求める際に使う (= sizeof_size / first_dim)。
  // 不完全配列や 1 次元のみのときは 0。
  int first_dim;
  // 多次元 typedef 用: 解析した各次元のサイズを左から順に保持。
  // dim_count = 解析した `[N]` の個数。上限 8。
  int dims[8];
  int dim_count;
} decl_array_suffix_t;
static int parse_local_decl_spec(local_decl_spec_t *out);
static int parse_local_decl_spec_from_typedef(local_decl_spec_t *out);
static int parse_local_decl_spec_from_builtin(local_decl_spec_t *out);
static node_t *parse_typedef_declaration_local(void);
static void parse_local_extern_declarator_list(local_decl_spec_t *ds);
static void register_local_extern_decl(token_ident_t *name, int is_ptr, decl_array_suffix_t arr,
                                       int elem_size);
static void resolve_local_typedef_decl_spec(token_kind_t *base_kind, int *elem_size,
                                            tk_float_kind_t *fp_kind,
                                            token_kind_t *tag_kind, char **tag_name, int *tag_len,
                                            int *is_pointer_base);
static void define_local_typedef_from_declarator(token_ident_t *name, int is_ptr, int paren_array_mul,
                                                 token_kind_t base_kind, int elem_size,
                                                 tk_float_kind_t fp_kind,
                                                 token_kind_t tag_kind, char *tag_name, int tag_len,
                                                 int td_pointee_const, int td_pointee_volatile,
                                                 int td_is_unsigned);
static void parse_local_typedef_declarator_list(token_kind_t base_kind, int elem_size,
                                                tk_float_kind_t fp_kind,
                                                token_kind_t tag_kind, char *tag_name, int tag_len,
                                                int is_pointer_base,
                                                int td_pointee_const, int td_pointee_volatile,
                                                int td_is_unsigned);
static global_var_t *find_global_var_decl(char *name, int len);
static tk_float_kind_t fp_kind_for_type_kind(token_kind_t type_kind);
static void resolve_builtin_type_local(token_kind_t type_kind, int *out_elem_size,
                                       tk_float_kind_t *out_fp_kind);
static void init_local_decl_spec(local_decl_spec_t *out);
static void take_local_decl_prefix_flags(local_decl_spec_t *out);
static void adjust_local_decl_spec_from_typedef(local_decl_spec_t *out, token_kind_t base_kind);
static void resolve_typedef_name_ref_local(token_kind_t *out_base_kind, int *out_elem_size,
                                           tk_float_kind_t *out_fp_kind,
                                           token_kind_t *out_tag_kind, char **out_tag_name,
                                           int *out_tag_len, int *out_base_is_pointer,
                                           int *out_pointee_const, int *out_pointee_volatile,
                                           int *out_is_unsigned);

static tk_float_kind_t fp_kind_for_type_kind(token_kind_t type_kind) {
  if (type_kind == TK_FLOAT) return TK_FLOAT_KIND_FLOAT;
  if (type_kind == TK_DOUBLE) return TK_FLOAT_KIND_DOUBLE;
  return TK_FLOAT_KIND_NONE;
}

static void resolve_builtin_type_local(token_kind_t type_kind, int *out_elem_size,
                                       tk_float_kind_t *out_fp_kind) {
  psx_ctx_get_type_info(type_kind, NULL, out_elem_size);
  if (out_fp_kind) *out_fp_kind = fp_kind_for_type_kind(type_kind);
}

static void init_local_decl_spec(local_decl_spec_t *out) {
  memset(out, 0, sizeof(*out));
  out->elem_size = 8;
  out->fp_kind = TK_FLOAT_KIND_NONE;
  out->tag_kind = TK_EOF;
}

static void take_local_decl_prefix_flags(local_decl_spec_t *out) {
  psx_take_type_qualifiers(&out->is_const_qualified, &out->is_volatile_qualified);
  psx_take_extern_flag(&out->is_extern_decl);
}

static void adjust_local_decl_spec_from_typedef(local_decl_spec_t *out, token_kind_t base_kind) {
  if ((out->tag_kind == TK_STRUCT || out->tag_kind == TK_UNION) &&
      out->tag_name && out->tag_len > 0 &&
      psx_ctx_has_tag_type(out->tag_kind, out->tag_name, out->tag_len)) {
    int tag_sz = psx_ctx_get_tag_size(out->tag_kind, out->tag_name, out->tag_len);
    if (tag_sz > 0) out->elem_size = tag_sz;
  }
  out->type_kind = base_kind;
  out->is_unsigned = (base_kind == TK_UNSIGNED);
}

static void resolve_typedef_name_ref_local(token_kind_t *out_base_kind, int *out_elem_size,
                                           tk_float_kind_t *out_fp_kind,
                                           token_kind_t *out_tag_kind, char **out_tag_name,
                                           int *out_tag_len, int *out_base_is_pointer,
                                           int *out_pointee_const, int *out_pointee_volatile,
                                           int *out_is_unsigned) {
  token_ident_t *id = (token_ident_t *)curtok();
  psx_ctx_find_typedef_name(id->str, id->len,
                            out_base_kind, out_elem_size, out_fp_kind,
                            out_tag_kind, out_tag_name, out_tag_len, out_base_is_pointer,
                            out_pointee_const, out_pointee_volatile, out_is_unsigned);
  set_curtok(curtok()->next);
}

// typedef 配列の dims[] と次元数を取得する補助。dims が無い場合は dim_count=0。
static void resolve_typedef_array_dims(token_ident_t *id, int *out_dims, int *out_dim_count) {
  int is_array = 0;
  int sizeof_size = 0;
  int first_dim = 0;
  int dim_count = 0;
  psx_ctx_find_typedef_name_ex3(id->str, id->len, NULL, NULL, NULL, NULL, NULL, NULL,
                                NULL, NULL, NULL, NULL, &is_array, &sizeof_size,
                                &first_dim, out_dims, &dim_count, 8);
  if (out_dim_count) *out_dim_count = (is_array && dim_count > 0) ? dim_count : 0;
}

static long long eval_const_expr_decl(node_t *n, int *ok) {
  if (!n) { *ok = 0; return 0; }
  switch (n->kind) {
  case ND_NUM:
    return ((node_num_t *)n)->val;
  case ND_COMMA:
    (void)eval_const_expr_decl(n->lhs, ok);
    if (!*ok) return 0;
    return eval_const_expr_decl(n->rhs, ok);
  case ND_TERNARY: {
    long long c = eval_const_expr_decl(n->lhs, ok);
    if (!*ok) return 0;
    node_t *then_expr = n->rhs;
    node_t *else_expr = ((node_ctrl_t *)n)->els;
    return c ? eval_const_expr_decl(then_expr, ok) : eval_const_expr_decl(else_expr, ok);
  }
  case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD:
  case ND_SHL: case ND_SHR:
  case ND_BITAND: case ND_BITXOR: case ND_BITOR:
  case ND_EQ: case ND_NE: case ND_LT: case ND_LE:
  case ND_LOGAND: case ND_LOGOR:
    break;
  default:
    *ok = 0; return 0;
  }
  // 二項演算共通: 左→右の順で評価し、op を適用。
  long long l = eval_const_expr_decl(n->lhs, ok);
  if (!*ok) return 0;
  long long r = eval_const_expr_decl(n->rhs, ok);
  switch (n->kind) {
  case ND_ADD:    return l + r;
  case ND_SUB:    return l - r;
  case ND_MUL:    return l * r;
  case ND_DIV:    return l / r;
  case ND_MOD:    return l % r;
  case ND_SHL:    return l << r;
  case ND_SHR:    return l >> r;
  case ND_BITAND: return l & r;
  case ND_BITXOR: return l ^ r;
  case ND_BITOR:  return l | r;
  case ND_EQ:     return l == r;
  case ND_NE:     return l != r;
  case ND_LT:     return l < r;
  case ND_LE:     return l <= r;
  case ND_LOGAND: return (l && r) ? 1 : 0;
  case ND_LOGOR:  return (l || r) ? 1 : 0;
  default:        *ok = 0; return 0;
  }
}

static void skip_ptr_qualifiers_decl(int *is_const_qualified, int *is_volatile_qualified) {
  while (curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE || curtok()->kind == TK_RESTRICT) {
    if (curtok()->kind == TK_CONST && is_const_qualified) *is_const_qualified = 1;
    if (curtok()->kind == TK_VOLATILE && is_volatile_qualified) *is_volatile_qualified = 1;
    set_curtok(curtok()->next);
  }
}

static void consume_pointer_chain_decl(int *is_pointer,
                                       unsigned int *const_mask, unsigned int *volatile_mask,
                                       int *levels) {
  while (tk_consume('*')) {
    *is_pointer = 1;
    int cur_const = 0;
    int cur_volatile = 0;
    skip_ptr_qualifiers_decl(&cur_const, &cur_volatile);
    if (levels && const_mask && volatile_mask) {
      int lv = *levels;
      if (lv < 32) {
        if (cur_const) *const_mask |= (1u << lv);
        if (cur_volatile) *volatile_mask |= (1u << lv);
      }
      *levels = lv + 1;
    }
  }
}

// 配列サイズ式をパースし定数評価する。ok=0 なら VLA (可変長配列) を示す。
static long long parse_array_size_expr_decl(node_t **out_node, int *out_ok) {
  node_t *n = psx_expr_assign();
  if (out_node) *out_node = n;
  int ok = 1;
  long long v = eval_const_expr_decl(n, &ok);
  if (out_ok) *out_ok = ok;
  if (ok && v <= 0) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
  }
  return v;
}

static int parse_array_size_constexpr_decl(void) {
  int ok = 1;
  long long v = parse_array_size_expr_decl(NULL, &ok);
  if (!ok) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_ARRAY_SIZE_CONSTEXPR_REQUIRED));
  }
  return (int)v;
}

static int parse_array_size_optional_constexpr_decl(int *out_has_size) {
  if (curtok()->kind == TK_RBRACKET) {
    if (out_has_size) *out_has_size = 0;
    return 0;
  }
  if (out_has_size) *out_has_size = 1;
  return parse_array_size_constexpr_decl();
}

static void parse_decl_skip_constexpr_array_suffixes(void) {
  while (tk_consume('[')) {
    (void)parse_array_size_constexpr_decl();
    tk_expect(']');
  }
}

// 多次元配列の trailing dim 列 `[N2][N3][N4]...` を読み、各 dim を out_dims に
// 格納し、全 trailing dim の積を返す（最大 max_dims 個まで、それ以降は積に
// だけ寄与）。dim が無いときは out_dims=0 のまま 1 を返す。
static int parse_decl_constexpr_array_suffix_product_n(int *out_dims, int max_dims, int *out_count) {
  int mul = 1;
  int count = 0;
  while (tk_consume('[')) {
    int dim = parse_array_size_constexpr_decl();
    tk_expect(']');
    if (count < max_dims) out_dims[count] = dim;
    count++;
    if (dim > 0) mul *= dim;
  }
  if (out_count) *out_count = count;
  return mul;
}

static int parse_decl_array_suffixes_constexpr_required(int base_mul);

static decl_array_suffix_t parse_decl_array_suffixes(int base_mul) {
  decl_array_suffix_t out = {0};
  out.arr_total = (base_mul > 0) ? base_mul : 1;
  out.is_array = (base_mul > 0);
  out.has_incomplete_array = 0;
  out.first_dim = 0;
  out.dim_count = 0;
  int dim_count = 0;
  while (tk_consume('[')) {
    int has_size = 0;
    int n = parse_array_size_optional_constexpr_decl(&has_size);
    if (!has_size) {
      out.has_incomplete_array = 1;
    } else {
      out.arr_total *= n;
      if (dim_count == 0) out.first_dim = n;
    }
    if (dim_count < 8) {
      out.dims[dim_count] = has_size ? n : 0;
    }
    dim_count++;
    out.is_array = 1;
    tk_expect(']');
  }
  if (dim_count > 8) dim_count = 8;
  out.dim_count = dim_count;
  return out;
}

static int parse_decl_array_suffixes_constexpr_required(int base_mul) {
  int arr_total = (base_mul > 0) ? base_mul : 1;
  while (tk_consume('[')) {
    int n = parse_array_size_constexpr_decl();
    tk_expect(']');
    if (n > 0) arr_total *= n;
  }
  return arr_total;
}

// 波括弧初期化子 `{ ... }` のトップレベル要素数を数えるトークン先読みヘルパ。
// `brace_tok` は `{` を指している必要がある。curtok は変更しない。
// 指定初期化子 `[N]=` で位置がジャンプする場合は、最大位置+1 を返す。
// 推定不可（空、複雑な指定子、トークン不整合）なら 0。
long long psx_decl_count_brace_init_elements(token_t *brace_tok) {
  if (!brace_tok || brace_tok->kind != TK_LBRACE) return 0;
  token_t *t = brace_tok->next; // skip '{'
  if (t && t->kind == TK_RBRACE) return 0; // 空 `{}` は推定不可

  long long idx = 0;
  long long max_seen = -1;
  int depth = 0;
  bool seen_content = false;
  while (t) {
    if (depth == 0) {
      if (t->kind == TK_RBRACE) {
        if (seen_content && idx > max_seen) max_seen = idx;
        break;
      }
      if (t->kind == TK_COMMA) {
        if (seen_content) {
          if (idx > max_seen) max_seen = idx;
          idx++;
          seen_content = false;
        }
        t = t->next;
        continue;
      }
      if (!seen_content && t->kind == TK_LBRACKET) {
        // 指定初期化子 `[N]=...`
        t = t->next;
        if (t && t->kind == TK_NUM && tk_as_num(t)->num_kind == TK_NUM_KIND_INT) {
          idx = (long long)tk_as_num_int(t)->uval;
          t = t->next;
        } else {
          return 0; // 複雑な式は未対応
        }
        if (!t || t->kind != TK_RBRACKET) return 0;
        t = t->next;
        if (t && t->kind == TK_ASSIGN) t = t->next;
        continue;
      }
    }
    if (t->kind == TK_LBRACE || t->kind == TK_LPAREN || t->kind == TK_LBRACKET) {
      depth++;
    } else if (t->kind == TK_RBRACE || t->kind == TK_RPAREN || t->kind == TK_RBRACKET) {
      depth--;
      if (depth < 0) return 0; // 異常: 開きより閉じが多い
    }
    seen_content = true;
    t = t->next;
  }
  if (max_seen < 0) return 0;
  return max_seen + 1;
}

// `=` 直後 `{` の中身がもう一段の `{` で始まるか（ネスト初期化子）を判定する。
// 2D 推定 `int a[][N]={{...},{...}}` をフラット形式 `{1,2,3,...}` と区別するために使う。
// curtok は変更しない。
static bool init_first_element_is_brace(void) {
  token_t *t = curtok();
  if (!t || t->kind != TK_ASSIGN) return false;
  t = t->next;
  if (!t || t->kind != TK_LBRACE) return false;
  t = t->next;
  // 簡略化のため、先頭の指定初期化子 (`[N]=` `.name=`) はスキップせず、直接判定する。
  return t && t->kind == TK_LBRACE;
}

// 文字列リテラル列の合計内容長 + 1 (NUL) を返す。t は文字列開始トークン。
// 文字列の終端後のトークンが終端記号 (`}` または NULL ライク) でなければ
// 単純な文字列初期化と見なせないので 0 を返す。
/* C11 5.1.1.2: 文字列中のエスケープシーケンス (`\t` `\xNN` `\NNN` 等) は
 * 1 文字にデコードされるので、配列要素数は decode 後の文字数で数える。
 * 生バイト長 (raw len) を返してしまうと `char s[] = "\t"` で要素数が 3 (raw '\','t',NUL)
 * になり過剰確保 + 値もズレる。 */
static long long count_decoded_chars_in_string(const char *s, int len) {
  long long n = 0;
  int i = 0;
  while (i < len) {
    if (s[i] == '\\') {
      uint32_t cp = 0;
      if (!tk_parse_escape_value(s, len, &i, &cp)) i++;
    } else {
      i++;
    }
    n++;
  }
  return n;
}

static long long count_char_init_from_string_seq(token_t *t, bool require_rbrace_terminator) {
  long long total = 0;
  while (t && t->kind == TK_STRING) {
    token_string_t *st = (token_string_t *)t;
    total += count_decoded_chars_in_string(st->str, st->len);
    t = t->next;
  }
  if (require_rbrace_terminator) {
    if (!t || t->kind != TK_RBRACE) return 0;
  }
  return total + 1; // 終端 NUL
}

// `int a[] = ...` の `[]` で要素数を初期化子から推定するためのトークン先読みヘルパ。
// curtok は変更しない。`=` で始まる初期化子を想定し、推定できなければ 0 を返す。
// elem_size==1 の場合は文字列リテラル初期化子（NUL を含む長さ）にも対応する。
// 波括弧で囲まれた `{"..."}` 形式も同じく文字列初期化として扱う。
static long long infer_array_count_from_initializer(int elem_size) {
  token_t *t = curtok();
  if (!t || t->kind != TK_ASSIGN) return 0;
  t = t->next;
  if (!t) return 0;
  if (t->kind == TK_STRING && elem_size == 1) {
    return count_char_init_from_string_seq(t, false);
  }
  if (elem_size == 1 && t->kind == TK_LBRACE) {
    token_t *inside = t->next;
    if (inside && inside->kind == TK_STRING) {
      long long n = count_char_init_from_string_seq(inside, true);
      if (n > 0) return n;
    }
  }
  return psx_decl_count_brace_init_elements(t);
}

static int parse_nonneg_const_expr_decl(const char *what) {
  node_t *n = psx_expr_assign();
  int ok = 1;
  long long v = eval_const_expr_decl(n, &ok);
  if (!ok) {
    psx_diag_ctx(curtok(), "decl", diag_message_for(DIAG_ERR_PARSER_NONNEG_CONSTEXPR_REQUIRED),
                 what);
  }
  if (v < 0) {
    psx_diag_ctx(curtok(), "decl", diag_message_for(DIAG_ERR_PARSER_NONNEG_VALUE_REQUIRED),
                 what);
  }
  return (int)v;
}

static node_t *parse_scalar_brace_initializer(void) {
  if (!tk_consume('{')) {
    return psx_expr_assign();
  }
  node_t *rhs = psx_expr_assign();
  if (tk_consume(',')) {
      if (!tk_consume('}')) {
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_SCALAR_BRACE_SINGLE_ELEMENT_ONLY));
      }
    return rhs;
  }
  tk_expect('}');
  return rhs;
}

static node_t *new_array_elem_lvar(lvar_t *var, int idx) {
  int elem_off = var->offset + idx * var->elem_size;
  node_t *lvar = psx_node_new_lvar_typed(elem_off, var->elem_size);
  lvar->fp_kind = var->fp_kind;
  ((node_lvar_t *)lvar)->mem.tag_kind = var->tag_kind;
  ((node_lvar_t *)lvar)->mem.tag_name = var->tag_name;
  ((node_lvar_t *)lvar)->mem.tag_len = var->tag_len;
  ((node_lvar_t *)lvar)->mem.is_tag_pointer = var->is_tag_pointer;
  return lvar;
}

static node_t *new_array_elem_lvar_at(int base_offset, int elem_size, int idx) {
  node_t *lvar = psx_node_new_lvar_typed(base_offset + idx * elem_size, elem_size);
  return lvar;
}

static node_t *new_byte_lvar_at(int offset) {
  return psx_node_new_lvar_typed(offset, 1);
}

static node_t *build_byte_copy_chain(int dst_base_off, int src_base_off, int size, node_t *init_chain) {
  for (int i = 0; i < size; i++) {
    node_t *lhs = new_byte_lvar_at(dst_base_off + i);
    node_t *rhs = new_byte_lvar_at(src_base_off + i);
    node_mem_t *assign_node = psx_node_new_assign(lhs, rhs);
    assign_node->type_size = 1;
    node_t *init_node = (node_t *)assign_node;
    if (!init_chain) init_chain = init_node;
    else init_chain = psx_node_new_binary(ND_COMMA, init_chain, init_node);
  }
  return init_chain;
}

static int is_supported_scalar_store_size(int size) {
  return size == 1 || size == 2 || size == 4 || size == 8;
}

static int is_compatible_tag_object_lvar(node_lvar_t *src, lvar_t *var) {
  if (!src || !var) return 0;
  if (src->mem.is_tag_pointer || var->is_tag_pointer) return 0;
  if (src->mem.tag_kind != var->tag_kind) return 0;
  return src->mem.type_size > 0 && var->size > 0 && src->mem.type_size == var->size;
}

static node_t *build_struct_copy_chain_from_source(lvar_t *dst, node_lvar_t *src) {
  lvar_t src_var = {0};
  src_var.offset = src->offset;
  src_var.tag_kind = src->mem.tag_kind;
  src_var.tag_name = src->mem.tag_name;
  src_var.tag_len = src->mem.tag_len;
  src_var.is_tag_pointer = src->mem.is_tag_pointer;

  int member_count = psx_ctx_get_tag_member_count(dst->tag_kind, dst->tag_name, dst->tag_len);
  node_t *init_chain = NULL;
  for (int ordinal = 0; ordinal < member_count; ordinal++) {
    char *member_name = NULL;
    int member_len = 0;
    int member_offset = 0;
    int member_type_size = 0;
    int member_array_len = 0;
    token_kind_t member_tag_kind = TK_EOF;
    char *member_tag_name = NULL;
    int member_tag_len = 0;
    int member_is_tag_pointer = 0;
    bool found = psx_ctx_get_tag_member_at(dst->tag_kind, dst->tag_name, dst->tag_len, ordinal,
                                           &member_name, &member_len,
                                           &member_offset, &member_type_size, NULL, &member_array_len,
                                           &member_tag_kind, &member_tag_name,
                                           &member_tag_len, &member_is_tag_pointer);
    if (!found || member_len <= 0) continue;
    if (is_supported_scalar_store_size(member_type_size)) {
      node_t *lhs = new_struct_member_lvar(dst, member_offset, member_type_size,
                                           member_tag_kind, member_tag_name, member_tag_len, member_is_tag_pointer);
      node_t *rhs_member = new_struct_member_lvar(&src_var, member_offset, member_type_size,
                                                  member_tag_kind, member_tag_name, member_tag_len, member_is_tag_pointer);
      node_mem_t *assign_node = psx_node_new_assign(lhs, rhs_member);
      assign_node->type_size = member_type_size;
      node_t *init_node = (node_t *)assign_node;
      if (!init_chain) init_chain = init_node;
      else init_chain = psx_node_new_binary(ND_COMMA, init_chain, init_node);
      continue;
    }
    init_chain = build_byte_copy_chain(dst->offset + member_offset, src_var.offset + member_offset,
                                       member_type_size, init_chain);
  }
  return init_chain ? init_chain : psx_node_new_num(0);
}

static node_t *try_parse_array_member_copy_initializer(int dst_base_off, int elem_size, int array_len) {
  if (!curtok() || curtok()->kind != TK_IDENT) return NULL;
  token_ident_t *id = (token_ident_t *)curtok();
  lvar_t *src = psx_decl_find_lvar(id->str, id->len);
  if (!src || !src->is_array) return NULL;
  src->is_used = 1;
  if (src->elem_size != elem_size || src->size != elem_size * array_len) return NULL;
  if (!curtok()->next || (curtok()->next->kind != TK_COMMA && curtok()->next->kind != TK_RBRACE)) return NULL;

  (void)psx_expr_assign();
  node_t *init_chain = NULL;
  for (int idx = 0; idx < array_len; idx++) {
    node_t *lhs = new_array_elem_lvar_at(dst_base_off, elem_size, idx);
    int src_elem_off = src->offset + idx * src->elem_size;
    node_t *rhs = psx_node_new_lvar_typed(src_elem_off, elem_size);
    node_mem_t *assign_node = psx_node_new_assign(lhs, rhs);
    assign_node->type_size = elem_size;
    node_t *init_node = (node_t *)assign_node;
    if (!init_chain) init_chain = init_node;
    else init_chain = psx_node_new_binary(ND_COMMA, init_chain, init_node);
  }
  return init_chain ? init_chain : psx_node_new_num(0);
}

static node_t *try_parse_array_member_string_initializer(int dst_base_off, int elem_size, int array_len) {
  if (elem_size != 1) return NULL;
  if (!curtok() || curtok()->kind != TK_STRING) return NULL;

  node_t *rhs = psx_expr_assign();
  if (!rhs || rhs->kind != ND_STRING) return NULL;

  node_string_t *s = (node_string_t *)rhs;
  string_lit_t *lit = find_string_lit_by_label(s->string_label);
  if (!lit) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_STRING_INIT_RESOLVE_FAILED));
  }

  node_t *init_chain = NULL;
  int idx = 0;       /* 配列に書き込んだバイト数 */
  int src_pos = 0;   /* lit->str を走査するインデックス */
  while (src_pos < lit->len && idx < array_len) {
    /* C11 5.1.1.2: 文字列リテラル中のエスケープシーケンスは
     * 1 文字にデコードしてから配列に格納する。 */
    uint32_t cp = 0;
    if (lit->str[src_pos] == '\\') {
      if (!tk_parse_escape_value(lit->str, lit->len, &src_pos, &cp)) {
        cp = (unsigned char)lit->str[src_pos];
        src_pos++;
      }
    } else {
      cp = (unsigned char)lit->str[src_pos];
      src_pos++;
    }
    node_t *lhs = new_array_elem_lvar_at(dst_base_off, elem_size, idx);
    node_mem_t *assign_node = psx_node_new_assign(lhs, psx_node_new_num((unsigned char)cp));
    assign_node->type_size = elem_size;
    node_t *init_node = (node_t *)assign_node;
    if (!init_chain) init_chain = init_node;
    else init_chain = psx_node_new_binary(ND_COMMA, init_chain, init_node);
    idx++;
  }
  if (idx < array_len) {
    node_t *lhs = new_array_elem_lvar_at(dst_base_off, elem_size, idx);
    node_mem_t *assign_node = psx_node_new_assign(lhs, psx_node_new_num(0));
    assign_node->type_size = elem_size;
    node_t *init_node = (node_t *)assign_node;
    if (!init_chain) init_chain = init_node;
    else init_chain = psx_node_new_binary(ND_COMMA, init_chain, init_node);
  }
  return init_chain ? init_chain : psx_node_new_num(0);
}

static int resolve_copy_source_lvar(node_t *expr, node_t **out_prefix, node_lvar_t **out_src) {
  node_t *prefix = NULL;
  node_t *value = expr;
  while (value && value->kind == ND_COMMA) {
    if (!prefix) prefix = value->lhs;
    else prefix = psx_node_new_binary(ND_COMMA, prefix, value->lhs);
    value = value->rhs;
  }
  if (!value || value->kind != ND_LVAR) return 0;
  if (out_prefix) *out_prefix = prefix;
  if (out_src) *out_src = (node_lvar_t *)value;
  return 1;
}

static string_lit_t *find_string_lit_by_label(char *label) {
  for (string_lit_t *lit = string_literals; lit; lit = lit->next) {
    if (strcmp(lit->label, label) == 0) return lit;
  }
  return NULL;
}

static node_t *append_to_init_chain(node_t *init_chain, node_t *init_node);
static node_t *parse_array_init_chunk(lvar_t *var, int *init_elem_count, bool *assigned, int array_len,
                                      int start_idx, const int *chunk_sizes, int depth);

// `var->arr[idx] = value` を表す ASSIGN ノードを構築する。
// type_size と fp_kind は var の要素型から複製する。
static node_t *build_array_elem_assign(lvar_t *var, int idx, node_t *value) {
  node_t *lhs = new_array_elem_lvar(var, idx);
  node_mem_t *assign_node = psx_node_new_assign(lhs, value);
  assign_node->type_size = var->elem_size;
  assign_node->base.fp_kind = var->fp_kind;
  return (node_t *)assign_node;
}

/* `struct P a[3] = {{1, 2}, {3, 4}, {5, 6}};` 中の 1 要素 `{1, 2}` を、
 * 配列要素 idx のメンバ単位代入チェーンに展開する。呼出時に '{' は未消費。
 *
 *   chain = (a_idx.m0 = v0, a_idx.m1 = v1, ...)
 *
 * 書かれなかったメンバはここでは 0 埋めしない (C 仕様上は 0 だが、現状
 * struct 要素の初期化漏れは未対応とする)。 */
static node_t *parse_array_elem_struct_brace_init(lvar_t *var, int idx) {
  tk_expect('{');
  int elem_off = var->offset + idx * var->elem_size;
  node_t *chain = NULL;
  int ordinal = 0;
  if (!tk_consume('}')) {
    for (;;) {
      aggregate_member_info_t info = {0};
      info.tag_kind = TK_EOF;
      bool found = tag_get_next_named_member(var, &ordinal, &info);
      if (!found || info.len <= 0) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
      }
      node_t *value = parse_scalar_brace_initializer();
      int abs_off = elem_off + info.offset;
      node_t *lhs = psx_node_new_lvar_typed(abs_off, info.type_size);
      ((node_lvar_t *)lhs)->mem.tag_kind = info.tag_kind;
      ((node_lvar_t *)lhs)->mem.tag_name = info.tag_name;
      ((node_lvar_t *)lhs)->mem.tag_len = info.tag_len;
      ((node_lvar_t *)lhs)->mem.is_tag_pointer = info.is_tag_pointer;
      node_mem_t *assign = psx_node_new_assign(lhs, value);
      assign->type_size = info.type_size;
      chain = append_to_init_chain(chain, (node_t *)assign);
      if (tk_consume('}')) break;
      tk_expect(',');
      if (tk_consume('}')) break;
    }
  }
  /* C11 6.7.9p21: ネスト初期化子で書かれなかった残りのメンバは 0 埋め。
   * `{10}` で y が省略されたら y = 0。スカラ非タグ非配列メンバのみ対象。 */
  int member_count = psx_ctx_get_tag_member_count(var->tag_kind, var->tag_name, var->tag_len);
  for (int o = ordinal; o < member_count; o++) {
    aggregate_member_info_t info = {0};
    info.tag_kind = TK_EOF;
    int probe = o;
    if (!tag_get_next_named_member(var, &probe, &info) || info.len <= 0) continue;
    if (!is_supported_scalar_store_size(info.type_size)) continue;
    if (info.array_len > 0 || info.tag_kind == TK_STRUCT || info.tag_kind == TK_UNION) continue;
    int abs_off = elem_off + info.offset;
    node_t *lhs = psx_node_new_lvar_typed(abs_off, info.type_size);
    node_mem_t *assign = psx_node_new_assign(lhs, psx_node_new_num(0));
    assign->type_size = info.type_size;
    chain = append_to_init_chain(chain, (node_t *)assign);
  }
  return chain ? chain : psx_node_new_num(0);
}

// 初期化子の要素数を 1 増やし、上限を超えていれば診断を出す。
static void bump_initializer_count(int *count) {
  (*count)++;
  if (*count > PS_MAX_INITIALIZER_ELEMENTS) {
    psx_diag_ctx(curtok(), "decl", "初期化子要素数が多すぎます（上限 %d）",
                 PS_MAX_INITIALIZER_ELEMENTS);
  }
}

static node_t *parse_array_initializer(lvar_t *var) {
  node_t *init_chain = NULL;
  int init_elem_count = 0;
  int array_len = var->elem_size > 0 ? (var->size / var->elem_size) : 0;
  // 特例: `char a[] = {"hello"};` のように波括弧で囲まれた文字列リテラル
  // (隣接連結も含む) は C11 6.7.9p14 により素の文字列初期化と同じに扱う。
  if (var->elem_size == 1 && curtok() && curtok()->kind == TK_LBRACE) {
    token_t *peek = curtok()->next;
    if (peek && peek->kind == TK_STRING) {
      token_t *p = peek;
      while (p && p->kind == TK_STRING) p = p->next;
      if (p && p->kind == TK_RBRACE) {
        tk_consume('{');
        node_t *str_node = psx_expr_assign(); // 連結を含めて1つの ND_STRING になる
        tk_expect('}');
        if (str_node && str_node->kind == ND_STRING) {
          node_string_t *s = (node_string_t *)str_node;
          string_lit_t *lit = find_string_lit_by_label(s->string_label);
          if (!lit) {
            psx_diag_ctx(curtok(), "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_STRING_INIT_RESOLVE_FAILED));
          }
          int i = 0;
          int src_pos = 0;
          while (src_pos < lit->len && i < array_len) {
            uint32_t cp = 0;
            if (lit->str[src_pos] == '\\') {
              if (!tk_parse_escape_value(lit->str, lit->len, &src_pos, &cp)) {
                cp = (unsigned char)lit->str[src_pos];
                src_pos++;
              }
            } else {
              cp = (unsigned char)lit->str[src_pos];
              src_pos++;
            }
            init_chain = append_to_init_chain(init_chain,
                build_array_elem_assign(var, i, psx_node_new_num((unsigned char)cp)));
            i++;
          }
          if (i < array_len) {
            init_chain = append_to_init_chain(init_chain,
                build_array_elem_assign(var, i, psx_node_new_num(0)));
          }
          return init_chain ? init_chain : psx_node_new_num(0);
        }
      }
    }
  }
  if (tk_consume('{')) {
    int idx = 0;
    int row_len = (var->outer_stride > 0 && var->elem_size > 0) ? var->outer_stride / var->elem_size : 0;
    bool *assigned = calloc((size_t)(array_len > 0 ? array_len : 1), sizeof(bool));
    if (!tk_consume('}')) {
      for (;;) {
        int target_idx = idx;
        if (tk_consume('[')) {
          target_idx = parse_nonneg_const_expr_decl(diag_text_for(DIAG_TEXT_ARRAY_DESIGNATOR_INDEX));
          tk_expect(']');
          tk_expect('=');
          if (row_len > 0) target_idx *= row_len;
        }
        // 多次元配列のネストされた波括弧: {{1,2,3},{4,5,6}} など。
        // 3D / 4D / 5D ... と更にネストが深くなり得るので、各次元のチャンクサイズ
        // を組み立てて parse_array_init_chunk へ委譲する。
        if (row_len > 0 && tk_consume('{')) {
          int chunk_sizes[8] = {0};
          int depth = 0;
          chunk_sizes[depth++] = row_len;
          if (var->mid_stride > 0 && var->elem_size > 0) {
            chunk_sizes[depth++] = var->mid_stride / var->elem_size;
          }
          for (int i = 0; i < var->extra_strides_count && depth < 8; i++) {
            if (var->elem_size > 0) {
              chunk_sizes[depth++] = var->extra_strides[i] / var->elem_size;
            }
          }
          node_t *sub = parse_array_init_chunk(var, &init_elem_count, assigned, array_len,
                                               target_idx, chunk_sizes, depth);
          if (sub) {
            init_chain = init_chain ? psx_node_new_binary(ND_COMMA, init_chain, sub) : sub;
          }
          idx = target_idx + row_len;
        } else {
          bump_initializer_count(&init_elem_count);
          if (target_idx >= array_len) {
            psx_diag_ctx(curtok(), "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
          }
          if (assigned[target_idx]) {
            psx_diag_ctx(curtok(), "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_DUPLICATE_ELEMENT));
          }
          /* 配列要素が struct/union で、初期化子が `{...}` 形式のとき:
           * `struct P a[3] = {{1, 2}, ...}` のような nested brace。
           * 要素単位の代入式チェーンに展開する。 */
          if (curtok() && curtok()->kind == TK_LBRACE &&
              !var->is_tag_pointer &&
              (var->tag_kind == TK_STRUCT || var->tag_kind == TK_UNION)) {
            init_chain = append_to_init_chain(init_chain,
                parse_array_elem_struct_brace_init(var, target_idx));
          } else {
            init_chain = append_to_init_chain(init_chain,
                build_array_elem_assign(var, target_idx, parse_scalar_brace_initializer()));
          }
          assigned[target_idx] = true;
          idx = target_idx + 1;
        }
        if (tk_consume('}')) break;
        tk_expect(',');
        if (tk_consume('}')) break;
      }
    }
    /* C11 6.7.9p21: 部分初期化や指定初期化子で書かれなかった要素は 0 で
     * 初期化される。`int a[5] = {1, 2}` → a[2..4] = 0。
     * `int a[5] = {[2] = 100}` → a[0, 1, 3, 4] = 0。
     * elem_size が 1 を超えるスカラ要素 (int/short/long 等) のみ対応。
     * ネストした構造体配列は assigned[] が要素単位でないので未対応。 */
    if (array_len > 0 && var->elem_size > 0) {
      for (int i = 0; i < array_len; i++) {
        if (assigned[i]) continue;
        init_chain = append_to_init_chain(init_chain,
            build_array_elem_assign(var, i, psx_node_new_num(0)));
      }
    }
    free(assigned);
    return init_chain ? init_chain : psx_node_new_num(0);
  }

  node_t *rhs = psx_expr_assign();
  if (var->elem_size == 1 && rhs->kind == ND_STRING) {
    node_string_t *s = (node_string_t *)rhs;
    string_lit_t *lit = find_string_lit_by_label(s->string_label);
    if (!lit) {
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_STRING_INIT_RESOLVE_FAILED));
    }
    int idx = 0;
    int src_pos = 0;
    while (src_pos < lit->len && idx < array_len) {
      uint32_t cp = 0;
      if (lit->str[src_pos] == '\\') {
        if (!tk_parse_escape_value(lit->str, lit->len, &src_pos, &cp)) {
          cp = (unsigned char)lit->str[src_pos];
          src_pos++;
        }
      } else {
        cp = (unsigned char)lit->str[src_pos];
        src_pos++;
      }
      init_chain = append_to_init_chain(init_chain,
          build_array_elem_assign(var, idx, psx_node_new_num((unsigned char)cp)));
      idx++;
    }
    if (idx < array_len) {
      // 文字列が配列長より短い場合は残りに NUL を1つ入れる。
      init_chain = append_to_init_chain(init_chain,
          build_array_elem_assign(var, idx, psx_node_new_num(0)));
    }
    return init_chain ? init_chain : psx_node_new_num(0);
  }
  // Extension: scalar expression for array init
  //   int a[3] = 1;  => a[0]=1, a[1]=0, a[2]=0
  if (array_len > 0) {
    init_chain = build_array_elem_assign(var, 0, rhs);
    for (int idx = 1; idx < array_len; idx++) {
      init_chain = append_to_init_chain(init_chain,
          build_array_elem_assign(var, idx, psx_node_new_num(0)));
    }
    return init_chain;
  }
  return psx_node_new_num(0);
}

static node_t *new_struct_member_lvar(lvar_t *var, int member_offset, int member_type_size,
                                      token_kind_t member_tag_kind, char *member_tag_name,
                                      int member_tag_len, int member_is_tag_pointer) {
  node_t *lvar = psx_node_new_lvar_typed(var->offset + member_offset, member_type_size);
  ((node_lvar_t *)lvar)->mem.tag_kind = member_tag_kind;
  ((node_lvar_t *)lvar)->mem.tag_name = member_tag_name;
  ((node_lvar_t *)lvar)->mem.tag_len = member_tag_len;
  ((node_lvar_t *)lvar)->mem.is_tag_pointer = member_is_tag_pointer;
  return lvar;
}

static node_t *parse_member_initializer(lvar_t *owner, int member_offset, int member_type_size,
                                        token_kind_t member_tag_kind, char *member_tag_name,
                                        int member_tag_len, int member_is_tag_pointer,
                                        int member_array_len) {
  if (member_array_len > 0 && !member_is_tag_pointer) {
    int array_len = member_array_len;
    int elem_size = member_type_size;
    node_t *init_chain = NULL;
    if (tk_consume('{')) {
      int idx = 0;
      bool *assigned = calloc((size_t)(array_len > 0 ? array_len : 1), sizeof(bool));
      if (!tk_consume('}')) {
        for (;;) {
          int target_idx = idx;
          if (tk_consume('[')) {
            target_idx = parse_nonneg_const_expr_decl(diag_text_for(DIAG_TEXT_ARRAY_DESIGNATOR_INDEX));
            tk_expect(']');
            tk_expect('=');
          }
          if (target_idx >= array_len) {
            psx_diag_ctx(curtok(), "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
          }
          if (assigned[target_idx]) {
            psx_diag_ctx(curtok(), "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_DUPLICATE_ELEMENT));
          }
          node_t *lhs = new_array_elem_lvar_at(owner->offset + member_offset, elem_size, target_idx);
          node_mem_t *assign_node = psx_node_new_assign(lhs, parse_scalar_brace_initializer());
          assign_node->type_size = elem_size;
          node_t *init_node = (node_t *)assign_node;
          if (!init_chain) init_chain = init_node;
          else init_chain = psx_node_new_binary(ND_COMMA, init_chain, init_node);
          assigned[target_idx] = true;
          idx = target_idx + 1;
          if (tk_consume('}')) break;
          tk_expect(',');
          if (tk_consume('}')) break;
        }
      }
      free(assigned);
      return init_chain ? init_chain : psx_node_new_num(0);
    }
    if (owner->tag_kind == TK_STRUCT ||
        (owner->tag_kind == TK_UNION && ps_get_enable_union_array_member_nonbrace_init())) {
      // Brace elision for aggregate array members: allow flat scalar list.
      node_t *array_str = try_parse_array_member_string_initializer(owner->offset + member_offset, elem_size, array_len);
      if (array_str) return array_str;
      node_t *array_copy = try_parse_array_member_copy_initializer(owner->offset + member_offset, elem_size, array_len);
      if (array_copy) return array_copy;
      node_t *lhs0 = new_array_elem_lvar_at(owner->offset + member_offset, elem_size, 0);
      node_mem_t *assign0 = psx_node_new_assign(lhs0, parse_scalar_brace_initializer());
      assign0->type_size = elem_size;
      init_chain = (node_t *)assign0;
      for (int idx = 1; idx < array_len; idx++) {
        if (!tk_consume(',')) break;
        node_t *lhs = new_array_elem_lvar_at(owner->offset + member_offset, elem_size, idx);
        node_mem_t *assign_node = psx_node_new_assign(lhs, parse_scalar_brace_initializer());
        assign_node->type_size = elem_size;
        init_chain = psx_node_new_binary(ND_COMMA, init_chain, (node_t *)assign_node);
      }
      return init_chain;
    }
    if (owner->tag_kind == TK_UNION) {
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_UNION_ARRAY_MEMBER_NONBRACE_UNSUPPORTED));
    } else {
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_UNSUPPORTED_FORM));
    }
  }
  if (!member_is_tag_pointer && member_tag_kind == TK_STRUCT) {
    lvar_t nested = {0};
    nested.offset = owner->offset + member_offset;
    nested.elem_size = member_type_size;
    nested.tag_kind = TK_STRUCT;
    nested.tag_name = member_tag_name;
    nested.tag_len = member_tag_len;
    return parse_struct_initializer(&nested);
  }
  if (!member_is_tag_pointer && member_tag_kind == TK_UNION) {
    lvar_t nested = {0};
    nested.offset = owner->offset + member_offset;
    nested.elem_size = member_type_size;
    nested.tag_kind = TK_UNION;
    nested.tag_name = member_tag_name;
    nested.tag_len = member_tag_len;
    return parse_union_initializer(&nested);
  }
  if (!is_supported_scalar_store_size(member_type_size)) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_AGGREGATE_INIT_SCALAR_SIZE_UNSUPPORTED));
  }
  return parse_scalar_brace_initializer();
}

static bool tag_find_member(lvar_t *var, char *name, int len, aggregate_member_info_t *out) {
  out->name = name;
  out->len = len;
  return psx_ctx_find_tag_member(var->tag_kind, var->tag_name, var->tag_len, name, len,
                                 &out->offset, &out->type_size, NULL, &out->array_len,
                                 &out->tag_kind, &out->tag_name, &out->tag_len,
                                 &out->is_tag_pointer);
}

static bool tag_get_member_at(lvar_t *var, int ordinal, aggregate_member_info_t *out) {
  return psx_ctx_get_tag_member_at(var->tag_kind, var->tag_name, var->tag_len, ordinal,
                                   &out->name, &out->len,
                                   &out->offset, &out->type_size, NULL, &out->array_len,
                                   &out->tag_kind, &out->tag_name, &out->tag_len,
                                   &out->is_tag_pointer);
}

// 次の名前付きメンバまで ordinal を前進。見つかれば true。
// 見つからなかった場合の最終 ordinal は member_count（または途中で「found=false」になった値）。
static bool tag_get_next_named_member(lvar_t *var, int *ordinal_inout,
                                      aggregate_member_info_t *out) {
  int ordinal = *ordinal_inout;
  int member_count = psx_ctx_get_tag_member_count(var->tag_kind, var->tag_name, var->tag_len);
  while (ordinal < member_count) {
    bool found = tag_get_member_at(var, ordinal, out);
    ordinal++;
    if (!found) { *ordinal_inout = ordinal; return false; }
    if (out->len > 0) { *ordinal_inout = ordinal; return true; }
  }
  *ordinal_inout = ordinal;
  return false;
}

// チェーン末尾に init_node を追加する。先頭時は init_node 自身が新しい先頭。
static node_t *append_to_init_chain(node_t *init_chain, node_t *init_node) {
  if (!init_chain) return init_node;
  return psx_node_new_binary(ND_COMMA, init_chain, init_node);
}

// 多次元配列のネスト初期化子 `{...}` を 1 段分処理する。呼び出し時点で
// 該当階層の `{` は既に消費済み。
// `chunk_sizes[0]` がこの階層のチャンク長、`chunk_sizes[1]` がさらに 1 段
// 内側のチャンク長、... `depth` は呼出側が用意した chunk_sizes の有効長。
// `{` を再度見たら 1 段深く再帰する。
static node_t *parse_array_init_chunk(lvar_t *var, int *init_elem_count, bool *assigned, int array_len,
                                      int start_idx, const int *chunk_sizes, int depth) {
  node_t *init_chain = NULL;
  int chunk_len = chunk_sizes[0];
  int ci = 0;
  while (ci < chunk_len) {
    if (depth > 1 && curtok() && curtok()->kind == TK_LBRACE) {
      // さらに内側のネスト `{...}`
      tk_consume('{');
      node_t *sub = parse_array_init_chunk(var, init_elem_count, assigned, array_len,
                                           start_idx + ci, chunk_sizes + 1, depth - 1);
      if (sub) init_chain = init_chain ? psx_node_new_binary(ND_COMMA, init_chain, sub) : sub;
      ci += chunk_sizes[1];
    } else {
      bump_initializer_count(init_elem_count);
      int flat_idx = start_idx + ci;
      if (flat_idx < array_len) {
        init_chain = append_to_init_chain(init_chain,
            build_array_elem_assign(var, flat_idx, psx_expr_assign()));
        assigned[flat_idx] = true;
      }
      ci++;
    }
    if (tk_consume('}')) return init_chain;
    tk_expect(',');
    if (tk_consume('}')) return init_chain;
  }
  return init_chain;
}

// .name[idx] = val の組み立て。lhs は member の offset+idx 要素を指す lvar。
static node_t *build_nested_array_designator_assign(lvar_t *var,
                                                    const aggregate_member_info_t *info,
                                                    int nested_idx) {
  node_t *lhs = new_array_elem_lvar_at(var->offset + info->offset, info->type_size, nested_idx);
  node_t *val = parse_scalar_brace_initializer();
  node_mem_t *assign_node = psx_node_new_assign(lhs, val);
  assign_node->type_size = info->type_size;
  return (node_t *)assign_node;
}

// member_init の意味を見て、必要なら ASSIGN ノードで包む。
// 配列メンバや struct/union メンバの場合、parse_member_initializer が
// すでに代入チェーンを返しているのでそのまま使う。
static node_t *wrap_member_init_as_assign(lvar_t *var,
                                          const aggregate_member_info_t *info,
                                          node_t *member_init) {
  if ((info->array_len > 0 && !info->is_tag_pointer) ||
      (!info->is_tag_pointer && (info->tag_kind == TK_STRUCT || info->tag_kind == TK_UNION))) {
    return member_init;
  }
  node_t *lhs = new_struct_member_lvar(var, info->offset, info->type_size,
                                       info->tag_kind, info->tag_name, info->tag_len,
                                       info->is_tag_pointer);
  node_mem_t *assign_node = psx_node_new_assign(lhs, member_init);
  assign_node->type_size = info->type_size;
  return (node_t *)assign_node;
}

static node_t *parse_struct_initializer(lvar_t *var) {
  if (!tk_consume('{')) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_AGGREGATE_INIT_BRACE_REQUIRED));
  }
  int member_count = psx_ctx_get_tag_member_count(var->tag_kind, var->tag_name, var->tag_len);
  node_t *init_chain = NULL;
  int ordinal = 0;
  // assigned_kind[i]: 0=full assignment ('.m = v' or ordinal), 1=indexed-only ('.m[i]=v')。
  // 完全代入とインデックス指定が同じメンバ名で混在したら重複と扱う。
  char **assigned_names = calloc((size_t)(member_count > 0 ? member_count : 1), sizeof(char *));
  int *assigned_lens = calloc((size_t)(member_count > 0 ? member_count : 1), sizeof(int));
  int *assigned_kind = calloc((size_t)(member_count > 0 ? member_count : 1), sizeof(int));
  int assigned_n = 0;
  if (!tk_consume('}')) {
    for (;;) {
      aggregate_member_info_t info = {0};
      info.tag_kind = TK_EOF;
      bool found = false;
      if (tk_consume('.')) {
        token_ident_t *id = tk_consume_ident();
        if (!id) psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
        found = tag_find_member(var, id->str, id->len, &info);
        if (tk_consume('[')) {
          // Nested designator: .member[idx] = val
          if (!found || info.len <= 0) {
            psx_diag_ctx(curtok(), "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
          }
          if (info.array_len <= 0 || info.is_tag_pointer) {
            psx_diag_ctx(curtok(), "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_NESTED_DESIG_NOT_ARRAY));
          }
          int nested_idx = parse_nonneg_const_expr_decl(diag_text_for(DIAG_TEXT_ARRAY_DESIGNATOR_INDEX));
          tk_expect(']');
          tk_expect('=');
          if (nested_idx < 0 || nested_idx >= info.array_len) {
            psx_diag_ctx(curtok(), "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
          }
          // 同名 full 代入の後にこの indexed が来ているなら重複。
          // indexed 同士は別 index の可能性があるので許す。
          for (int i = 0; i < assigned_n; i++) {
            if (assigned_kind[i] == 0 &&
                assigned_lens[i] == info.len &&
                strncmp(assigned_names[i], info.name, (size_t)info.len) == 0) {
              psx_diag_ctx(curtok(), "decl", "%s",
                           diag_message_for(DIAG_ERR_PARSER_STRUCT_INIT_DUPLICATE_MEMBER));
              break;
            }
          }
          init_chain = append_to_init_chain(init_chain,
              build_nested_array_designator_assign(var, &info, nested_idx));
          assigned_names[assigned_n] = info.name;
          assigned_lens[assigned_n] = info.len;
          assigned_kind[assigned_n] = 1; // indexed-only
          assigned_n++;
          if (tk_consume('}')) break;
          tk_expect(',');
          if (tk_consume('}')) break;
          continue;
        }
        tk_expect('=');
      } else {
        found = tag_get_next_named_member(var, &ordinal, &info);
      }
      if (!found || info.len <= 0) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
      }
      // full 代入: 同名の full または indexed が既にあれば重複。
      for (int i = 0; i < assigned_n; i++) {
        if (assigned_lens[i] == info.len && strncmp(assigned_names[i], info.name, (size_t)info.len) == 0) {
          psx_diag_ctx(curtok(), "decl", "%s",
                       diag_message_for(DIAG_ERR_PARSER_STRUCT_INIT_DUPLICATE_MEMBER));
          break;
        }
      }
      node_t *member_init = parse_member_initializer(var, info.offset, info.type_size,
                                                     info.tag_kind, info.tag_name, info.tag_len,
                                                     info.is_tag_pointer, info.array_len);
      init_chain = append_to_init_chain(init_chain,
          wrap_member_init_as_assign(var, &info, member_init));
      assigned_names[assigned_n] = info.name;
      assigned_lens[assigned_n] = info.len;
      assigned_kind[assigned_n] = 0; // full assignment
      assigned_n++;
      if (tk_consume('}')) break;
      tk_expect(',');
      if (tk_consume('}')) break;
    }
  }
  /* C11 6.7.9p21: 部分初期化や指定初期化子で書かれなかったメンバは 0 で
   * 初期化される (struct S s = {10, 20}; なら c, d = 0)。
   * スカラ (is_supported_scalar_store_size を満たす) メンバのみ 0 を書く。
   * 構造体メンバや配列メンバはこの実装では未対応。 */
  for (int o = 0; o < member_count; o++) {
    aggregate_member_info_t info = {0};
    info.tag_kind = TK_EOF;
    int probe_ordinal = o;
    int probe_value = o;
    if (!tag_get_next_named_member(var, &probe_ordinal, &info)) continue;
    if (info.len <= 0) continue;
    if (probe_ordinal != probe_value + 1) {
      /* 無名 padding 等で 1 ordinal 進まなかったらスキップ */
    }
    int already = 0;
    for (int i = 0; i < assigned_n; i++) {
      if (assigned_lens[i] == info.len &&
          strncmp(assigned_names[i], info.name, (size_t)info.len) == 0) {
        already = 1; break;
      }
    }
    if (already) continue;
    if (!is_supported_scalar_store_size(info.type_size)) continue;
    if (info.array_len > 0 || info.tag_kind == TK_STRUCT || info.tag_kind == TK_UNION) continue;
    node_t *zero = psx_node_new_num(0);
    init_chain = append_to_init_chain(init_chain,
        wrap_member_init_as_assign(var, &info, zero));
  }
  free(assigned_names);
  free(assigned_lens);
  free(assigned_kind);
  return init_chain ? init_chain : psx_node_new_num(0);
}

static node_t *parse_struct_copy_initializer(lvar_t *var) {
  node_t *rhs = psx_expr_assign();
  node_t *prefix = NULL;
  node_t *value = rhs;
  while (value && value->kind == ND_COMMA) {
    if (!prefix) prefix = value->lhs;
    else prefix = psx_node_new_binary(ND_COMMA, prefix, value->lhs);
    value = value->rhs;
  }
  node_t *init_chain = NULL;
  if (value && value->kind == ND_LVAR && is_compatible_tag_object_lvar((node_lvar_t *)value, var)) {
    init_chain = build_struct_copy_chain_from_source(var, (node_lvar_t *)value);
  } else if (value && value->kind == ND_TERNARY) {
    node_ctrl_t *ternary = (node_ctrl_t *)value;
    node_t *then_prefix = NULL;
    node_t *else_prefix = NULL;
    node_lvar_t *then_src = NULL;
    node_lvar_t *else_src = NULL;
    resolve_copy_source_lvar(ternary->base.rhs, &then_prefix, &then_src);
    resolve_copy_source_lvar(ternary->els, &else_prefix, &else_src);
    if (!is_compatible_tag_object_lvar(then_src, var) || !is_compatible_tag_object_lvar(else_src, var)) {
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_STRUCT_COPY_COMPAT_REQUIRED));
    }
    node_ctrl_t *copy_select = arena_alloc(sizeof(node_ctrl_t));
    copy_select->base.kind = ND_TERNARY;
    copy_select->base.lhs = ternary->base.lhs;
    node_t *then_copy = build_struct_copy_chain_from_source(var, then_src);
    node_t *else_copy = build_struct_copy_chain_from_source(var, else_src);
    copy_select->base.rhs = then_prefix ? psx_node_new_binary(ND_COMMA, then_prefix, then_copy) : then_copy;
    copy_select->els = else_prefix ? psx_node_new_binary(ND_COMMA, else_prefix, else_copy) : else_copy;
    init_chain = (node_t *)copy_select;
  } else if (var->size <= 8 && value && value->kind == ND_FUNCALL) {
    // ≤8B struct: 関数呼び出し結果の非lvar式を 8B assign で初期化
    node_t *lhs_var = psx_node_new_lvar_typed(var->offset, var->size);
    node_mem_t *assign_node = psx_node_new_assign(lhs_var, value);
    assign_node->type_size = var->size;
    init_chain = (node_t *)assign_node;
  } else if (var->size > 8 && var->size <= 16 && value && value->kind == ND_FUNCALL) {
    // 9-16B struct: 関数呼び出し結果を x0/x1 ペアで受け取り、2ワード代入で初期化
    node_t *lhs_var = psx_node_new_lvar_typed(var->offset, var->size);
    node_mem_t *assign_node = psx_node_new_assign(lhs_var, value);
    assign_node->type_size = var->size;
    init_chain = (node_t *)assign_node;
  } else if (var->size > 16 && value && value->kind == ND_FUNCALL) {
    // >16B struct: indirect return (x8) 経由で呼び出し先が直接代入先に書き込む
    node_t *lhs_var = psx_node_new_lvar_typed(var->offset, var->size);
    node_mem_t *assign_node = psx_node_new_assign(lhs_var, value);
    assign_node->type_size = var->size;
    init_chain = (node_t *)assign_node;
  } else {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_STRUCT_COPY_COMPAT_REQUIRED));
  }
  if (prefix) return psx_node_new_binary(ND_COMMA, prefix, init_chain);
  return init_chain;
}

// 波カッコなしの `union U u = expr;` 経路。
//  - 互換型からの copy 初期化を試みる
//  - そうでなければ scalar 値を最初の名前付きメンバへ代入
static node_t *parse_union_initializer_no_brace(lvar_t *var) {
  node_t *rhs = psx_expr_assign();
  node_t *prefix = NULL;
  node_lvar_t *src = NULL;
  if (resolve_copy_source_lvar(rhs, &prefix, &src)) {
    if (is_compatible_tag_object_lvar(src, var)) {
      node_t *copy = build_byte_copy_chain(var->offset, src->offset, var->size, NULL);
      if (prefix) return psx_node_new_binary(ND_COMMA, prefix, copy);
      return copy;
    }
  }
  // Fallback: scalar が先頭の名前付きメンバを初期化する。
  aggregate_member_info_t info = {0};
  info.tag_kind = TK_EOF;
  int ordinal = 0;
  bool found = tag_get_next_named_member(var, &ordinal, &info);
  if (!found || info.len <= 0) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
  }
  node_t *lhs = new_struct_member_lvar(var, info.offset, info.type_size,
                                       info.tag_kind, info.tag_name, info.tag_len, info.is_tag_pointer);
  node_mem_t *assign_node = psx_node_new_assign(lhs, rhs);
  assign_node->type_size = info.type_size;
  return (node_t *)assign_node;
}

static node_t *parse_union_initializer(lvar_t *var) {
  bool has_brace = tk_consume('{');
  if (has_brace && tk_consume('}')) return psx_node_new_num(0);
  if (!has_brace) return parse_union_initializer_no_brace(var);

  aggregate_member_info_t info = {0};
  info.tag_kind = TK_EOF;
  bool found = false;
  if (tk_consume('.')) {
    token_ident_t *id = tk_consume_ident();
    if (!id) psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
    found = tag_find_member(var, id->str, id->len, &info);
    if (tk_consume('[')) {
      // Nested designator: .member[idx] = val
      if (!found || info.len <= 0) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
      }
      if (info.array_len <= 0 || info.is_tag_pointer) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_NESTED_DESIG_NOT_ARRAY));
      }
      int nested_idx = parse_nonneg_const_expr_decl(diag_text_for(DIAG_TEXT_ARRAY_DESIGNATOR_INDEX));
      tk_expect(']');
      tk_expect('=');
      if (nested_idx < 0 || nested_idx >= info.array_len) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
      }
      node_t *result = build_nested_array_designator_assign(var, &info, nested_idx);
      tk_consume(',');
      tk_expect('}');
      return result;
    }
    tk_expect('=');
  } else {
    int ordinal = 0;
    found = tag_get_next_named_member(var, &ordinal, &info);
  }
  if (!found || info.len <= 0) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
  }
  node_t *member_init = parse_member_initializer(var, info.offset, info.type_size,
                                                 info.tag_kind, info.tag_name, info.tag_len,
                                                 info.is_tag_pointer, info.array_len);
  node_t *init_chain = wrap_member_init_as_assign(var, &info, member_init);
  if (!tk_consume(',')) {
    tk_expect('}');
    return init_chain;
  }
  if (tk_consume('}')) return init_chain;
  // 仕様外: `{ .a = 1, .b = 2 }` のように union に複数初期化子。
  // 各エントリは診断を出しつつパースを継続する。
  if (!tk_consume('.')) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_UNION_INIT_SINGLE_ELEMENT_ONLY));
  }
  for (;;) {
    token_ident_t *id = tk_consume_ident();
    if (!id) psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
    tk_expect('=');
    found = tag_find_member(var, id->str, id->len, &info);
    if (!found || info.len <= 0) {
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
    }
    node_t *extra_init = parse_member_initializer(var, info.offset, info.type_size,
                                                  info.tag_kind, info.tag_name, info.tag_len,
                                                  info.is_tag_pointer, info.array_len);
    node_t *extra_assign = wrap_member_init_as_assign(var, &info, extra_init);
    init_chain = append_to_init_chain(init_chain, extra_assign);
    if (tk_consume('}')) return init_chain;
    tk_expect(',');
    if (!tk_consume('.')) {
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_UNION_INIT_SINGLE_ELEMENT_ONLY));
    }
  }
}

static void skip_func_params(void) {
  if (!tk_consume('(')) return;
  int depth = 1;
  while (depth > 0) {
    if (curtok()->kind == TK_EOF) {
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_MISSING_FUNC_DECL_RPAREN));
    }
    if (curtok()->kind == TK_LPAREN) depth++;
    else if (curtok()->kind == TK_RPAREN) depth--;
    set_curtok(curtok()->next);
  }
}

static void skip_bracket_group(void) {
  if (!tk_consume('[')) return;
  int depth = 1;
  while (depth > 0) {
    if (curtok()->kind == TK_EOF) {
      psx_diag_missing(curtok(), "]");
    }
    if (curtok()->kind == TK_LBRACKET) depth++;
    else if (curtok()->kind == TK_RBRACKET) depth--;
    set_curtok(curtok()->next);
  }
}

static global_var_t *find_global_var_decl(char *name, int len) {
  for (global_var_t *gv = global_vars; gv; gv = gv->next) {
    if (gv->name_len == len && memcmp(gv->name, name, (size_t)len) == 0) {
      return gv;
    }
  }
  return NULL;
}

static token_ident_t *consume_decl_name_recursive(int *is_pointer,
                                                  unsigned int *const_mask, unsigned int *volatile_mask,
                                                  int *levels, int *out_paren_array_mul,
                                                  int *had_parens,
                                                  int *out_inner_array_mul) {
  consume_pointer_chain_decl(is_pointer, const_mask, volatile_mask, levels);
  token_ident_t *tok = NULL;
  int local_had_parens = 0;
  if (tk_consume('(')) {
    local_had_parens = 1;
    tok = consume_decl_name_recursive(is_pointer, const_mask, volatile_mask, levels,
                                      out_paren_array_mul, NULL, out_inner_array_mul);
    // パレン内の `[N]` を捕捉する: `int (*ops[N])(...)` の N は関数ポインタ配列の要素数。
    // 空 `[]` の場合は -1 を伝え、呼び出し側で初期化子から推定させる。
    while (curtok()->kind == TK_LBRACKET) {
      if (out_inner_array_mul && tk_consume('[')) {
        bool empty_bracket = (curtok() && curtok()->kind == TK_RBRACKET);
        int n = 0;
        if (!empty_bracket) {
          n = parse_array_size_constexpr_decl();
        }
        tk_expect(']');
        if (empty_bracket) {
          *out_inner_array_mul = -1; // size unspecified
        } else if (n > 0) {
          if (*out_inner_array_mul == 0) *out_inner_array_mul = 1;
          if (*out_inner_array_mul > 0) *out_inner_array_mul *= n;
        }
      } else {
        skip_bracket_group();
      }
    }
    tk_expect(')');
  } else {
    tok = tk_consume_ident();
  }
  while (curtok()->kind == TK_LPAREN) {
    skip_func_params();
  }
  if (local_had_parens && out_paren_array_mul) {
    *out_paren_array_mul = parse_decl_array_suffixes_constexpr_required(1);
  }
  if (had_parens) *had_parens = local_had_parens;
  return tok;
}

static token_ident_t *consume_decl_name_ex(int *is_pointer,
                                           unsigned int *const_mask, unsigned int *volatile_mask,
                                           int *levels, int *out_paren_array_mul,
                                           int *out_inner_array_mul) {
  token_ident_t *tok = consume_decl_name_recursive(is_pointer, const_mask, volatile_mask,
                                                   levels, out_paren_array_mul, NULL,
                                                   out_inner_array_mul);
  if (!tok) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED));
  }
  return tok;
}

static token_ident_t *consume_decl_name(int *is_pointer,
                                        unsigned int *const_mask, unsigned int *volatile_mask,
                                        int *levels, int *out_paren_array_mul) {
  token_ident_t *tok = consume_decl_name_recursive(is_pointer, const_mask, volatile_mask,
                                                   levels, out_paren_array_mul, NULL, NULL);
  if (!tok) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED));
  }
  return tok;
}

void psx_decl_reset_locals(void) {
  locals = NULL;
  all_locals = NULL;
  locals_offset = 0;
  lvar_scope_depth = 0;
}

void psx_decl_enter_scope(void) {
  if (lvar_scope_depth < LVAR_SCOPE_STACK_MAX) {
    lvar_scope_stack[lvar_scope_depth] = locals;
  }
  lvar_scope_depth++;
}

void psx_decl_leave_scope(void) {
  if (lvar_scope_depth <= 0) return;
  lvar_scope_depth--;
  if (lvar_scope_depth < LVAR_SCOPE_STACK_MAX) {
    locals = lvar_scope_stack[lvar_scope_depth];
  }
}

// For variadic functions: reserve slots for all 8 argument registers
// (8 regs × 8 bytes = 64 bytes at offsets 8..64) so that body-local
// variables don't overlap with the variadic register save area.
void psx_decl_reserve_variadic_regs(void) {
  if (locals_offset < 64) locals_offset = 64;
}

lvar_t *psx_decl_get_locals(void) { return all_locals; }

lvar_t *psx_decl_find_lvar_by_offset(int offset) {
  for (lvar_t *var = all_locals; var; var = var->next_all) {
    if (var->offset == offset) return var;
  }
  return NULL;
}

lvar_t *psx_decl_find_lvar(char *name, int len) {
  for (lvar_t *var = locals; var; var = var->next) {
    if (var->len == len && memcmp(var->name, name, len) == 0) {
      return var;
    }
  }
  return NULL;
}

lvar_t *psx_decl_register_lvar_sized(char *name, int len, int size, int elem_size, int is_array) {
  return psx_decl_register_lvar_sized_align(name, len, size, elem_size, is_array, 0);
}

lvar_t *psx_decl_register_lvar_sized_align(char *name, int len, int size, int elem_size, int is_array, int align) {
  /* C11 6.7p3: 同一スコープで同名のオブジェクト/関数を重複宣言してはならない。
   * 現在のスコープは locals 連結リストの head から、1 段外側のスコープに
   * 入る直前の locals (= lvar_scope_stack[lvar_scope_depth - 1]) まで。
   * lvar_scope_depth == 0 のとき (関数引数列など) はリスト全体を見る。
   *
   * psx_decl_find_lvar を使うと外側スコープまで見てしまうのでここでは
   * 自前に scope を walk して同名を探す。 */
  lvar_t *scope_end = (lvar_scope_depth > 0 && lvar_scope_depth <= LVAR_SCOPE_STACK_MAX)
                         ? lvar_scope_stack[lvar_scope_depth - 1] : NULL;
  for (lvar_t *v = locals; v != scope_end; v = v->next) {
    if (v->len == len && memcmp(v->name, name, (size_t)len) == 0) {
      psx_diag_duplicate_with_name(curtok(), "variable", name, len);
      /* psx_diag_duplicate_with_name は exit するため後続には到達しない */
    }
  }

  lvar_t *var = calloc(1, sizeof(lvar_t));
  var->next = locals;
  var->next_all = all_locals;
  all_locals = var;
  var->name = name;
  var->len = len;
  if (align > 1) {
    locals_offset = (locals_offset + align - 1) & ~(align - 1);
  }
  var->offset = locals_offset;  // BASE of variable (address = x29 + 16 + var->offset)
  locals_offset += size;
  var->size = size;
  var->elem_size = elem_size;
  var->is_array = is_array;
  var->align_bytes = align;
  locals = var;
  return var;
}

lvar_t *psx_decl_register_lvar(char *name, int len) {
  return psx_decl_register_lvar_sized(name, len, 8, 8, 0);
}

node_t *psx_decl_parse_initializer_for_var(lvar_t *var, int is_pointer) {
  if (var->is_array) {
    return parse_array_initializer(var);
  }
  if (!is_pointer && var->tag_kind == TK_STRUCT) {
    if (curtok()->kind != TK_LBRACE) {
      return parse_struct_copy_initializer(var);
    }
    return parse_struct_initializer(var);
  }
  if (!is_pointer && var->tag_kind == TK_UNION) {
    return parse_union_initializer(var);
  }
  node_t *lvar = psx_node_new_lvar_typed(var->offset, is_pointer ? 8 : var->elem_size);
  lvar->fp_kind = var->fp_kind;
  ((node_lvar_t *)lvar)->mem.tag_kind = var->tag_kind;
  ((node_lvar_t *)lvar)->mem.tag_name = var->tag_name;
  ((node_lvar_t *)lvar)->mem.tag_len = var->tag_len;
  ((node_lvar_t *)lvar)->mem.is_tag_pointer = var->is_tag_pointer;
  ((node_lvar_t *)lvar)->mem.is_complex = var->is_complex;
  ((node_lvar_t *)lvar)->mem.is_atomic = var->is_atomic;
  lvar->is_complex = var->is_complex;
  lvar->is_atomic = var->is_atomic;
  ((node_lvar_t *)lvar)->mem.is_const_qualified = var->is_const_qualified;
  ((node_lvar_t *)lvar)->mem.is_volatile_qualified = var->is_volatile_qualified;
  ((node_lvar_t *)lvar)->mem.is_pointer_const_qualified = var->is_pointer_const_qualified;
  ((node_lvar_t *)lvar)->mem.is_pointer_volatile_qualified = var->is_pointer_volatile_qualified;
  ((node_lvar_t *)lvar)->mem.pointer_const_qual_mask = var->pointer_const_qual_mask;
  ((node_lvar_t *)lvar)->mem.pointer_volatile_qual_mask = var->pointer_volatile_qual_mask;
  ((node_lvar_t *)lvar)->mem.pointer_qual_levels = var->pointer_qual_levels;
  node_t *init_expr = parse_scalar_brace_initializer();
  if (is_pointer) {
    psx_node_reject_const_qual_discard(lvar, init_expr);
    /* C11 6.5.16.1: ポインタ変数を非ゼロ整数定数で初期化するのは制約違反。
     * NULL ポインタ定数 (整数 0) のみ例外として許可する。 */
    if (init_expr && init_expr->kind == ND_NUM) {
      node_num_t *num = (node_num_t *)init_expr;
      if (num->val != 0) {
        psx_diag_ctx(curtok(), "init",
                     "ポインタ変数を非ゼロ整数定数 (%lld) で初期化できません (C11 6.5.16.1)",
                     num->val);
      }
    }
  } else if (var->tag_kind == TK_EOF && !var->is_array && init_expr) {
    /* C11 6.5.16.1: スカラ非ポインタ変数を文字列リテラル (char*) など
     * ポインタ型で初期化するのは互換性のない型の制約違反。
     * 明示キャスト (int)"hello" は apply_cast で is_pointer がクリアされるので
     * ここでは psx_node_is_pointer を見て暗黙変換のみを検出する。 */
    if (psx_node_is_pointer(init_expr)) {
      psx_diag_ctx(curtok(), "init",
                   "スカラ変数をポインタ型で初期化できません (C11 6.5.16.1)");
    }
    /* C11 6.5.16.1: struct/union 値をスカラに代入することはできない。
     * RHS の node_mem_t::tag_kind が TK_STRUCT/TK_UNION かつ is_tag_pointer=0
     * の場合、構造体実体を整数に変換しようとしているので拒否する。 */
    if ((init_expr->kind == ND_LVAR || init_expr->kind == ND_GVAR ||
         init_expr->kind == ND_DEREF || init_expr->kind == ND_FUNCALL) &&
        (init_expr->kind != ND_FUNCALL)) {
      node_mem_t *m = (node_mem_t *)init_expr;
      if ((m->tag_kind == TK_STRUCT || m->tag_kind == TK_UNION) &&
          !m->is_tag_pointer && !m->is_pointer) {
        psx_diag_ctx(curtok(), "init",
                     "スカラ変数を %s 値で初期化できません (C11 6.5.16.1)",
                     m->tag_kind == TK_STRUCT ? "struct" : "union");
      }
    }
  }
  node_mem_t *assign_node = psx_node_new_assign(lvar, init_expr);
  assign_node->type_size = is_pointer ? 8 : var->elem_size;
  assign_node->base.fp_kind = var->fp_kind;
  return (node_t *)assign_node;
}

node_t *psx_decl_parse_declaration_after_type(int elem_size, tk_float_kind_t decl_fp_kind,
                                              token_kind_t tag_kind, char *tag_name, int tag_len,
                                              int base_is_pointer,
                                              int is_const_qualified, int is_volatile_qualified,
                                              int decl_is_unsigned_hint) {
  return psx_decl_parse_declaration_after_type_ex(elem_size, decl_fp_kind,
                                                  tag_kind, tag_name, tag_len,
                                                  base_is_pointer,
                                                  is_const_qualified, is_volatile_qualified,
                                                  decl_is_unsigned_hint, NULL, 0,
                                                  /* decl_base_is_void = */ 0);
}

/* `static int n = 5;` のような単純スカラ static ローカルをグローバルに lowering する。
 * 戻り値: 1 = 処理した (登録 + alias 作成済)、0 = 非対応形式なので呼び出し側で fallback。
 * 対応範囲: スカラ整数 / 浮動小数点 / ポインタ。`=` の右辺は ND_NUM 定数のみ。
 * 配列・struct・union・複合型は未対応。 */
static int try_lower_static_local_scalar(token_ident_t *tok, int var_size, int deref_size,
                                          tk_float_kind_t fp_kind, int is_unsigned) {
  if (var_size <= 0) return 0;
  /* mangled name: `<funcname>.<varname>.<seq>` を arena に組み立てる。seq は重複防止用。 */
  static int g_static_seq = 0;
  char *funcname = NULL;
  int funcname_len = 0;
  psx_expr_get_current_funcname(&funcname, &funcname_len);
  int seq = g_static_seq++;
  char seq_buf[12];
  int seq_len = snprintf(seq_buf, sizeof(seq_buf), "%d", seq);
  /* funcname (or "anon") + "." + tok->name + "." + seq */
  const char *fname = funcname && funcname_len > 0 ? funcname : "anon";
  int fname_len = funcname && funcname_len > 0 ? funcname_len : 4;
  int total_len = fname_len + 1 + tok->len + 1 + seq_len;
  char *mangled = arena_alloc((size_t)total_len + 1);
  int off = 0;
  memcpy(mangled + off, fname, (size_t)fname_len); off += fname_len;
  mangled[off++] = '.';
  memcpy(mangled + off, tok->str, (size_t)tok->len); off += tok->len;
  mangled[off++] = '.';
  memcpy(mangled + off, seq_buf, (size_t)seq_len); off += seq_len;
  mangled[off] = '\0';

  /* 初期化子 (`= N`) があれば NUM をパースして init_val に取り込む。 */
  long long init_val = 0;
  int has_init = 0;
  if (tk_consume('=')) {
    node_t *e = psx_expr_assign();
    if (e && e->kind == ND_NUM) {
      init_val = ((node_num_t *)e)->val;
      has_init = 1;
    } else {
      /* 非定数 init は未対応 — silently 0 で初期化 (将来課題)。 */
      has_init = 1;
    }
  }

  /* global_var_t を作って global_vars に追加。 */
  global_var_t *gv = calloc(1, sizeof(global_var_t));
  gv->name = mangled;
  gv->name_len = total_len;
  gv->type_size = (short)var_size;
  gv->deref_size = (short)deref_size;
  gv->has_init = has_init;
  gv->init_val = init_val;
  gv->next = global_vars;
  global_vars = gv;

  /* lvar を「alias」として登録 — frame には置かないが、short name で引けるよう
   * locals に挿入する。is_static_local を立てて、識別子解決時に ND_GVAR に
   * 切り替える。size=0、offset は意味を持たない (=0)。 */
  lvar_t *var = calloc(1, sizeof(lvar_t));
  var->next = locals;
  var->next_all = all_locals;
  all_locals = var;
  var->name = tok->str;
  var->len = tok->len;
  var->offset = 0;
  var->size = var_size;
  var->elem_size = deref_size;
  var->fp_kind = fp_kind;
  var->is_unsigned = is_unsigned ? 1 : 0;
  var->is_static_local = 1;
  var->is_initialized = 1;
  var->static_global_name = mangled;
  var->static_global_name_len = total_len;
  locals = var;
  return 1;
}

node_t *psx_decl_parse_declaration_after_type_ex(int elem_size, tk_float_kind_t decl_fp_kind,
                                                 token_kind_t tag_kind, char *tag_name, int tag_len,
                                                 int base_is_pointer,
                                                 int is_const_qualified, int is_volatile_qualified,
                                                 int decl_is_unsigned_hint,
                                                 const int *td_array_dims, int td_array_dim_count,
                                                 int decl_base_is_void) {
  node_t *init_chain = NULL;
  int alignas_val = 0;
  int decl_is_unsigned = psx_last_type_is_unsigned() || decl_is_unsigned_hint;
  int decl_is_complex = psx_last_type_is_complex();
  int decl_is_atomic = psx_last_type_is_atomic();
  psx_take_alignas_value(&alignas_val);
  int decl_is_static = 0;
  psx_take_static_flag(&decl_is_static);

  // _Complex 型: サイズを基底型の2倍にする（実部 + 虚部）
  if (decl_is_complex && !base_is_pointer) {
    elem_size *= 2;
  }

  int declarator_count = 0;
  for (;;) {
    declarator_count++;
    if (declarator_count > PS_MAX_DECLARATOR_COUNT) {
      psx_diag_ctx(curtok(), "decl", "宣言子列が多すぎます（上限 %d）", PS_MAX_DECLARATOR_COUNT);
    }
    int is_pointer = base_is_pointer;
    unsigned int ptr_const_mask = 0;
    unsigned int ptr_volatile_mask = 0;
    int ptr_levels = 0;
    consume_pointer_chain_decl(&is_pointer, &ptr_const_mask, &ptr_volatile_mask, &ptr_levels);
    if (tag_kind != TK_EOF && !is_pointer && elem_size <= 0) {
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_INCOMPLETE_OBJECT_FORBIDDEN));
    }

    int paren_array_mul = 0;
    int inner_array_mul = 0;
    token_ident_t *tok = consume_decl_name_ex(&is_pointer, &ptr_const_mask, &ptr_volatile_mask, &ptr_levels,
                                              &paren_array_mul, &inner_array_mul);
    int var_size = is_pointer ? 8 : elem_size;
    int total_pointer_levels = ptr_levels + (base_is_pointer ? 1 : 0);
    int pointer_deref_size = (total_pointer_levels >= 2) ? 8 : elem_size;
    int ptr_is_const_qualified = (ptr_const_mask & 1u) ? 1 : 0;
    int ptr_is_volatile_qualified = (ptr_volatile_mask & 1u) ? 1 : 0;

    /* C11 6.7.2p2: void は不完全型なので、それ自体でオブジェクトを宣言できない。
     * `void x;` はエラー、`void *p;` は可。is_pointer は宣言子のポインタチェーン
     * (`*` 列) を含んだ後の値なので、ここで判定できる。 */
    if (decl_base_is_void && !is_pointer) {
      psx_diag_ctx(curtok(), "decl", "void 型のオブジェクトは宣言できません: '%.*s'",
                   tok ? tok->len : 0, tok ? tok->str : "");
    }

    /* `static` ローカル: 配列や struct でない単純スカラ (int/long/short/char/pointer)
     * はグローバルに lowering する。配列・struct 等の複雑形は現状フォールバック
     * (= 既存の auto と同じ挙動になる; 既知の制約)。 */
    if (decl_is_static && tag_kind == TK_EOF &&
        inner_array_mul == 0 && paren_array_mul == 0 &&
        curtok()->kind != TK_LBRACKET && td_array_dim_count == 0) {
      if (try_lower_static_local_scalar(tok, var_size,
                                         is_pointer ? pointer_deref_size : var_size,
                                         is_pointer ? TK_FLOAT_KIND_NONE : decl_fp_kind,
                                         decl_is_unsigned)) {
        if (!tk_consume(',')) break;
        continue;
      }
    }

    lvar_t *var = NULL;
    {
      if ((inner_array_mul > 0 || inner_array_mul == -1) && is_pointer) {
        // `int (*ops[N])(int,int)` パターン: 関数ポインタの配列。
        // 各要素は 8 バイトの関数ポインタなので elem_size=8 で is_array を立てる。
        // inner_array_mul==-1 は `int (*ops[])(...)={f,g,...}` の形で、
        // 要素数を初期化子から推定する必要がある。
        int effective_count = inner_array_mul;
        if (inner_array_mul == -1) {
          long long inferred = infer_array_count_from_initializer(8);
          if (inferred > 0) {
            effective_count = (int)inferred;
          } else {
            psx_diag_ctx(curtok(), "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
            effective_count = 1;
          }
        }
        int arr_total_bytes = effective_count * 8;
        var = psx_decl_register_lvar_sized_align(tok->str, tok->len, arr_total_bytes, 8, 1, alignas_val);
        var->tag_kind = tag_kind;
        var->tag_name = tag_name;
        var->tag_len = tag_len;
        var->is_tag_pointer = (tag_kind != TK_EOF) ? 1 : 0;
        var->base_deref_size = 8;
        var->is_const_qualified = is_const_qualified;
        var->is_volatile_qualified = is_volatile_qualified;
      } else if (paren_array_mul > 0) {
        // (*p)[N] パターン: 配列へのポインタ
        int row_size = paren_array_mul * elem_size;
        var = psx_decl_register_lvar_sized_align(tok->str, tok->len, 8, elem_size, 0, alignas_val);
        var->tag_kind = tag_kind;
        var->tag_name = tag_name;
        var->tag_len = tag_len;
        var->is_tag_pointer = 0;
        var->base_deref_size = (short)elem_size;
        var->outer_stride = row_size;
      } else if (!is_pointer && td_array_dim_count > 0 && curtok()->kind != TK_LBRACKET) {
        // typedef が配列型 (`typedef int M[2][3][4]; M m;`):
        // td_array_dims をそのまま多次元配列の dims として扱い、
        // outer_stride/mid_stride/extra_strides を計算する。
        int arr_total = 1;
        for (int di = 0; di < td_array_dim_count; di++) {
          if (td_array_dims[di] > 0) arr_total *= td_array_dims[di];
        }
        int arr_elem_size = elem_size;
        var = psx_decl_register_lvar_sized_align(tok->str, tok->len,
                                                  arr_total * arr_elem_size, arr_elem_size, 1, alignas_val);
        if (td_array_dim_count >= 2) {
          int outer_mul = 1;
          for (int i = 1; i < td_array_dim_count; i++) {
            if (td_array_dims[i] > 0) outer_mul *= td_array_dims[i];
          }
          var->outer_stride = outer_mul * arr_elem_size;
        }
        if (td_array_dim_count >= 3) {
          int mid_mul = 1;
          for (int i = 2; i < td_array_dim_count; i++) {
            if (td_array_dims[i] > 0) mid_mul *= td_array_dims[i];
          }
          var->mid_stride = mid_mul * arr_elem_size;
        }
        if (td_array_dim_count >= 4) {
          int idx_in_extras = 0;
          for (int start = 3; start < td_array_dim_count && idx_in_extras < 5; start++) {
            int rest_mul = 1;
            for (int j = start; j < td_array_dim_count; j++) {
              if (td_array_dims[j] > 0) rest_mul *= td_array_dims[j];
            }
            var->extra_strides[idx_in_extras++] = rest_mul * arr_elem_size;
          }
          var->extra_strides_count = (unsigned char)idx_in_extras;
        }
        var->tag_kind = tag_kind;
        var->tag_name = tag_name;
        var->tag_len = tag_len;
        var->is_tag_pointer = 0;
        var->is_const_qualified = is_const_qualified;
        var->is_volatile_qualified = is_volatile_qualified;
      } else if (tk_consume('[')) {
        node_t *size_node = NULL;
        int size_ok = 1;
        bool size_inferred_from_init = (curtok() && curtok()->kind == TK_RBRACKET);
        long long array_size = size_inferred_from_init
                                   ? 1
                                   : parse_array_size_expr_decl(&size_node, &size_ok);
        tk_expect(']');
        if (!size_ok) {
          // 可変長配列 (VLA)
          // 内側次元を確認 (2D VLA サポート)
          node_t *inner_size_node = NULL;
          int inner_size_ok = 1;
          long long inner_const_size = 0;
          int has_inner_dim = tk_consume('[') != 0;
          if (has_inner_dim) {
            inner_const_size = parse_array_size_expr_decl(&inner_size_node, &inner_size_ok);
            tk_expect(']');
            // さらに多次元は非サポート (消費のみ)
            parse_decl_skip_constexpr_array_suffixes();
          }
          // VLA フレームスロット割り当て
          // 1D/2D定数内側: 16B ([offset]=baseptr, [offset+8]=bytesize)
          // 2D実行時内側:  24B ([offset]=baseptr, [offset+8]=bytesize, [offset+16]=row_stride)
          int vla_row_stride_frame_off = 0;
          int outer_stride = 0;
          if (!has_inner_dim) {
            // 1D VLA: int a[n]
            outer_stride = elem_size;
            var = psx_decl_register_lvar_sized_align(tok->str, tok->len, 16, elem_size, 1, 0);
          } else if (inner_size_ok) {
            // 2D VLA constant inner: int a[n][M]
            outer_stride = (int)inner_const_size * elem_size;
            var = psx_decl_register_lvar_sized_align(tok->str, tok->len, 16, elem_size, 1, 0);
          } else {
            // 2D VLA runtime inner: int a[n][m]
            var = psx_decl_register_lvar_sized_align(tok->str, tok->len, 24, elem_size, 1, 0);
            vla_row_stride_frame_off = var->offset + 16;
          }
          var->is_vla = 1;
          var->outer_stride = outer_stride;
          var->vla_row_stride_frame_off = vla_row_stride_frame_off;
          // VLA確保ノード
          node_mem_t *alloc_node = arena_alloc(sizeof(node_mem_t));
          alloc_node->base.kind = ND_VLA_ALLOC;
          alloc_node->type_size = var->offset; // ベースポインタを格納するフレームオフセット
          alloc_node->vla_row_stride_frame_off = vla_row_stride_frame_off;
          if (!has_inner_dim || inner_size_ok) {
            // 1D: byte_size = n * elem_size
            // 2D constant: byte_size = n * outer_stride
            int stride = outer_stride ? outer_stride : elem_size;
            alloc_node->base.lhs = psx_node_new_binary(ND_MUL, size_node, psx_node_new_num(stride));
          } else {
            // 2D runtime: lhs=outer_count(n), rhs=row_stride_expr(m*elem_size)
            alloc_node->base.lhs = size_node;
            alloc_node->base.rhs = psx_node_new_binary(ND_MUL, inner_size_node, psx_node_new_num(elem_size));
          }
          if (!init_chain) init_chain = (node_t *)alloc_node;
          else init_chain = psx_node_new_binary(ND_COMMA, init_chain, (node_t *)alloc_node);
          if (!tk_consume(',')) break;
          continue;
        }
        // 多次元配列の trailing dim 列を全て読む（最大 7 段、配列全体では 8 次元まで）。
        int trailing_dims[7] = {0};
        int trailing_count = 0;
        int trailing_mul = parse_decl_constexpr_array_suffix_product_n(trailing_dims, 7, &trailing_count);
        // typedef が配列型のとき (`typedef int M[3][4]; M arr[2];`) は、
        // ユーザーが書いた suffix `[2]` の後ろに typedef dims `[3][4]` を連結する。
        // これにより arr は int[2][3][4] として扱われる。
        if (!is_pointer && td_array_dim_count > 0) {
          for (int di = 0; di < td_array_dim_count && trailing_count < 7; di++) {
            int dim = td_array_dims[di];
            if (dim > 0) {
              trailing_dims[trailing_count++] = dim;
              trailing_mul *= dim;
            }
          }
        }
        int inner_dim_size = trailing_count >= 1 ? trailing_dims[0] : 0; // 内側次元の要素数（0: 1次元配列）
        if (size_inferred_from_init) {
          // 外側 `[]` を初期化子から推定する。trailing dims を消費した直後の
          // curtok 位置（`=` を指す）で推定するため、ここで呼ぶ必要がある。
          long long top_count = infer_array_count_from_initializer(elem_size);
          if (top_count <= 0) {
            psx_diag_ctx(curtok(), "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
            array_size = 1; // フォールバック
          } else if (inner_dim_size > 0 && !init_first_element_is_brace()) {
            // 多次元配列のフラット初期化 `int a[][3]={1,2,3,4,5,6}`:
            // 推定値は総要素数なので trailing_mul を掛けない。
            array_size = top_count;
          } else {
            // ネスト初期化 `{{...},{...}}` または 1D `int a[]={...}`:
            // top_count は外側次元の要素数なので trailing_mul を掛ける。
            array_size = top_count * trailing_mul;
          }
        } else {
          array_size *= trailing_mul;
        }
        {
        int arr_elem_size = is_pointer ? 8 : elem_size;
        var = psx_decl_register_lvar_sized_align(tok->str, tok->len, (int)array_size * arr_elem_size, arr_elem_size, 1, alignas_val);
        if (inner_dim_size > 0) {
          // outer_stride = 1 次サブスクリプトのストライド (= 内側全体のバイト数)。
          // 2D: outer_stride = N2 * elem
          // 3D: outer_stride = N2 * N3 * elem
          // 4D: outer_stride = N2 * N3 * N4 * elem = trailing_mul * elem
          var->outer_stride = trailing_mul * arr_elem_size;
        }
        if (trailing_count >= 2) {
          // mid_stride = 2 次サブスクリプトのストライド = trailing_dims[1..]*elem。
          // 3D: mid_stride = N3 * elem
          // 4D: mid_stride = N3 * N4 * elem
          int mid_mul = 1;
          for (int i = 1; i < trailing_count; i++) {
            if (trailing_dims[i] > 0) mid_mul *= trailing_dims[i];
          }
          var->mid_stride = mid_mul * arr_elem_size;
        }
        // 4 次元以上: 3 段目以降のストライドを順に extra_strides に格納する。
        // extra_strides[k] = trailing_dims[(k+2)..end] の積 * elem。
        if (trailing_count >= 3) {
          int idx_in_extras = 0;
          for (int start = 2; start < trailing_count && idx_in_extras < 5; start++) {
            int rest_mul = 1;
            for (int j = start; j < trailing_count; j++) {
              if (trailing_dims[j] > 0) rest_mul *= trailing_dims[j];
            }
            var->extra_strides[idx_in_extras++] = rest_mul * arr_elem_size;
          }
          var->extra_strides_count = (unsigned char)idx_in_extras;
        }
        }
        var->tag_kind = tag_kind;
        var->tag_name = tag_name;
        var->tag_len = tag_len;
        var->is_tag_pointer = is_pointer ? 1 : 0;
        var->is_const_qualified = is_const_qualified;
        var->is_volatile_qualified = is_volatile_qualified;
        var->is_pointer_const_qualified = ptr_is_const_qualified;
        var->is_pointer_volatile_qualified = ptr_is_volatile_qualified;
        var->pointer_const_qual_mask = ptr_const_mask;
        var->pointer_volatile_qual_mask = ptr_volatile_mask;
        var->pointer_qual_levels = ptr_levels;
        if (is_pointer) {
          var->base_deref_size = (short)elem_size;
        }
      } else {
        var = psx_decl_register_lvar_sized_align(tok->str, tok->len, var_size,
                                           is_pointer ? pointer_deref_size : var_size, 0, alignas_val);
        var->tag_kind = tag_kind;
        var->tag_name = tag_name;
        var->tag_len = tag_len;
        var->is_tag_pointer = is_pointer ? 1 : 0;
        var->is_const_qualified = is_const_qualified;
        var->is_volatile_qualified = is_volatile_qualified;
        var->is_pointer_const_qualified = ptr_is_const_qualified;
        var->is_pointer_volatile_qualified = ptr_is_volatile_qualified;
        var->pointer_const_qual_mask = ptr_const_mask;
        var->pointer_volatile_qual_mask = ptr_volatile_mask;
        var->pointer_qual_levels = ptr_levels;
        if (is_pointer && total_pointer_levels >= 2) {
          var->base_deref_size = (short)elem_size;
        }
      }
    }

    if (!is_pointer) {
      var->fp_kind = decl_fp_kind;
      var->pointee_fp_kind = TK_FLOAT_KIND_NONE;
    } else {
      var->fp_kind = TK_FLOAT_KIND_NONE;
      var->pointee_fp_kind = (total_pointer_levels == 1) ? decl_fp_kind : TK_FLOAT_KIND_NONE;
    }
    var->is_unsigned = decl_is_unsigned;
    if (decl_is_complex) var->is_complex = 1;
    if (decl_is_atomic) var->is_atomic = 1;
    /* `void *p` (基底型 void + ポインタ宣言): pointee_is_void を立てる。
     * deref のエラー検出 (C11 6.5.3.2) で必要。 */
    if (decl_base_is_void && is_pointer && total_pointer_levels == 1) {
      var->pointee_is_void = 1;
    }

    if (tk_consume('=')) {
      var->is_initialized = 1;
      node_t *init_node = psx_decl_parse_initializer_for_var(var, is_pointer);
      if (!init_chain) init_chain = init_node;
      else init_chain = psx_node_new_binary(ND_COMMA, init_chain, init_node);
      if (!tk_consume(',')) break;
      continue;
    }

    if (!tk_consume(',')) break;
  }

  tk_expect(';');
  return init_chain ? init_chain : psx_node_new_num(0);
}

node_t *psx_decl_parse_declaration(void) {
  if (curtok()->kind == TK_TYPEDEF) {
    return parse_typedef_declaration_local();
  }

  if (curtok()->kind == TK_STATIC_ASSERT) {
    set_curtok(curtok()->next);
    tk_expect('(');
    int const_ok = 1;
    long long cond_val = eval_const_expr_decl(psx_expr_assign(), &const_ok);
    tk_expect(',');
    if (curtok()->kind != TK_STRING) {
      diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_MSG_NOT_STRING, curtok(), "%s",
                     diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_MSG_NOT_STRING));
    }
    set_curtok(curtok()->next);
    tk_expect(')');
    tk_expect(';');
    if (!const_ok) {
      diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_COND_NOT_CONST, curtok(), "%s",
                     diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_COND_NOT_CONST));
    }
    if (cond_val == 0) {
      diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_FAILED, curtok(), "%s",
                     diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_FAILED));
    }
    return psx_node_new_num(0);
  }

  local_decl_spec_t ds = {0};
  if (!parse_local_decl_spec(&ds)) {
    diag_emit_tokf(DIAG_ERR_PARSER_TYPE_NAME_REQUIRED, curtok(), "%s",
                   diag_message_for(DIAG_ERR_PARSER_TYPE_NAME_REQUIRED));
  }

  if (ds.is_extern_decl) {
    // ローカルextern宣言: グローバルテーブルに登録してローカル変数は作らない
    parse_local_extern_declarator_list(&ds);
    tk_expect(';');
    return psx_node_new_num(0);
  }

  return psx_decl_parse_declaration_after_type_ex(ds.elem_size, ds.fp_kind,
                                                  ds.tag_kind, ds.tag_name, ds.tag_len,
                                                  ds.base_is_pointer,
                                                  ds.is_const_qualified ? 1 : ds.td_pointee_const,
                                                  ds.is_volatile_qualified ? 1 : ds.td_pointee_volatile,
                                                  ds.is_unsigned,
                                                  ds.td_array_dims, ds.td_array_dim_count,
                                                  ds.type_kind == TK_VOID ? 1 : 0);
}

static int parse_local_decl_spec(local_decl_spec_t *out) {
  init_local_decl_spec(out);

  out->type_kind = psx_consume_type_kind();
  out->is_unsigned = psx_last_type_is_unsigned();
  take_local_decl_prefix_flags(out);
  if (out->type_kind == TK_EOF) return parse_local_decl_spec_from_typedef(out);
  return parse_local_decl_spec_from_builtin(out);
}

static int parse_local_decl_spec_from_typedef(local_decl_spec_t *out) {
  if (!psx_ctx_is_typedef_name_token(curtok())) return 0;
  token_kind_t base_kind = TK_EOF;
  token_ident_t *id = (token_ident_t *)curtok();
  // 多次元配列 typedef (`typedef int M[2][3][4]`) の dims を取得して保持する。
  resolve_typedef_array_dims(id, out->td_array_dims, &out->td_array_dim_count);
  resolve_typedef_name_ref_local(&base_kind, &out->elem_size, &out->fp_kind,
                                 &out->tag_kind, &out->tag_name, &out->tag_len,
                                 &out->base_is_pointer,
                                 &out->td_pointee_const, &out->td_pointee_volatile, &out->is_unsigned);
  adjust_local_decl_spec_from_typedef(out, base_kind);
  return 1;
}

static int parse_local_decl_spec_from_builtin(local_decl_spec_t *out) {
  resolve_builtin_type_local(out->type_kind, &out->elem_size, &out->fp_kind);
  return 1;
}

static void parse_local_extern_declarator_list(local_decl_spec_t *ds) {
  int declarator_count = 0;
  for (;;) {
    declarator_count++;
    if (declarator_count > PS_MAX_DECLARATOR_COUNT) {
      psx_diag_ctx(curtok(), "decl", "宣言子列が多すぎます（上限 %d）", PS_MAX_DECLARATOR_COUNT);
    }
    int is_ptr = ds->base_is_pointer;
    unsigned int ptr_const_mask = 0;
    unsigned int ptr_volatile_mask = 0;
    int ptr_levels = 0;
    int paren_array_dim = 0;
    consume_pointer_chain_decl(&is_ptr, &ptr_const_mask, &ptr_volatile_mask, &ptr_levels);
    token_ident_t *name = consume_decl_name(&is_ptr, &ptr_const_mask, &ptr_volatile_mask, &ptr_levels, &paren_array_dim);
    decl_array_suffix_t arr = parse_decl_array_suffixes(paren_array_dim);
    register_local_extern_decl(name, is_ptr, arr, ds->elem_size);
    if (curtok()->kind == TK_ASSIGN) {
      set_curtok(curtok()->next);
      psx_expr_assign();
    }
    if (curtok()->kind != TK_COMMA) break;
    set_curtok(curtok()->next);
  }
}

static void register_local_extern_decl(token_ident_t *name, int is_ptr, decl_array_suffix_t arr,
                                       int elem_size) {
  if (find_global_var_decl(name->str, name->len)) return;
  global_var_t *gv = calloc(1, sizeof(global_var_t));
  gv->name = name->str;
  gv->name_len = name->len;
  gv->type_size = arr.has_incomplete_array ? 0 :
                  (arr.is_array ? (is_ptr ? 8 : elem_size) * arr.arr_total : (is_ptr ? 8 : elem_size));
  gv->deref_size = elem_size;
  gv->is_array = arr.is_array;
  gv->is_extern_decl = 1;
  gv->next = global_vars;
  global_vars = gv;
}

static node_t *parse_typedef_declaration_local(void) {
  set_curtok(curtok()->next); // consume typedef

  token_kind_t base_kind = TK_EOF;
  int elem_size = 8;
  tk_float_kind_t fp_kind = TK_FLOAT_KIND_NONE;
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int is_pointer_base = 0;
  resolve_local_typedef_decl_spec(&base_kind, &elem_size, &fp_kind,
                                  &tag_kind, &tag_name, &tag_len, &is_pointer_base);

  int td_pointee_const = 0;
  int td_pointee_volatile = 0;
  psx_take_type_qualifiers(&td_pointee_const, &td_pointee_volatile);
  int td_is_unsigned = (base_kind == TK_UNSIGNED) || psx_last_type_is_unsigned();

  parse_local_typedef_declarator_list(base_kind, elem_size, fp_kind, tag_kind, tag_name, tag_len,
                                      is_pointer_base,
                                      td_pointee_const, td_pointee_volatile, td_is_unsigned);
  tk_expect(';');
  return psx_node_new_num(0);
}

static void resolve_local_typedef_decl_spec(token_kind_t *base_kind, int *elem_size,
                                            tk_float_kind_t *fp_kind,
                                            token_kind_t *tag_kind, char **tag_name, int *tag_len,
                                            int *is_pointer_base) {
  if (psx_ctx_is_tag_keyword(curtok()->kind)) {
    *tag_kind = curtok()->kind;
    *base_kind = *tag_kind;
    set_curtok(curtok()->next);
    token_ident_t *tag = tk_consume_ident();
    if (!tag) {
      psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_TAG_NAME));
    }
    *tag_name = tag->str;
    *tag_len = tag->len;
    if (!psx_ctx_has_tag_type(*tag_kind, *tag_name, *tag_len)) {
      if (*tag_kind == TK_STRUCT || *tag_kind == TK_UNION) {
        psx_ctx_define_tag_type(*tag_kind, *tag_name, *tag_len);
      } else {
        psx_diag_undefined_with_name(curtok(), diag_text_for(DIAG_TEXT_TAG_TYPE_SUFFIX), *tag_name, *tag_len);
      }
    }
    *elem_size = psx_ctx_get_tag_size(*tag_kind, *tag_name, *tag_len);
    return;
  }

  token_kind_t builtin_kind = psx_consume_type_kind();
  if (builtin_kind != TK_EOF) {
    *base_kind = builtin_kind;
    resolve_builtin_type_local(builtin_kind, elem_size, fp_kind);
    return;
  }
  if (psx_ctx_is_typedef_name_token(curtok())) {
    resolve_typedef_name_ref_local(base_kind, elem_size, fp_kind,
                                   tag_kind, tag_name, tag_len, is_pointer_base,
                                   NULL, NULL, NULL);
    return;
  }
  diag_emit_tokf(DIAG_ERR_PARSER_TYPE_NAME_REQUIRED, curtok(), "%s",
                 diag_message_for(DIAG_ERR_PARSER_TYPE_NAME_REQUIRED));
}

static void define_local_typedef_from_declarator(token_ident_t *name, int is_ptr, int paren_array_mul,
                                                 token_kind_t base_kind, int elem_size,
                                                 tk_float_kind_t fp_kind,
                                                 token_kind_t tag_kind, char *tag_name, int tag_len,
                                                 int td_pointee_const, int td_pointee_volatile,
                                                 int td_is_unsigned) {
  int typedef_sizeof = is_ptr ? 8 : elem_size;
  decl_array_suffix_t arr = parse_decl_array_suffixes(paren_array_mul);
  if (!is_ptr && arr.has_incomplete_array) typedef_sizeof = 0;
  else if (!is_ptr && arr.is_array && arr.arr_total > 0) typedef_sizeof *= arr.arr_total;
  token_kind_t stored_base_kind = (td_is_unsigned && base_kind == TK_INT) ? TK_UNSIGNED : base_kind;
  // `typedef int row_t[3]` のように配列型を typedef した場合は is_array=1 で記録する。
  // 不完全配列 `typedef int A[]` も is_array=1 (sizeof_size は 0)。
  int td_is_array = (!is_ptr && (arr.is_array || arr.has_incomplete_array)) ? 1 : 0;
  int td_first_dim = td_is_array ? arr.first_dim : 0;
  int td_dim_count = (td_is_array && !is_ptr) ? arr.dim_count : 0;
  if (!psx_ctx_define_typedef_name_ex3(name->str, name->len, stored_base_kind, elem_size, fp_kind,
                                  tag_kind, tag_name, tag_len, is_ptr, typedef_sizeof,
                                  td_pointee_const, td_pointee_volatile, td_is_unsigned,
                                  td_is_array, td_first_dim, arr.dims, td_dim_count)) {
    psx_diag_duplicate_with_name(curtok(), "typedef", name->str, name->len);
  }
}

static void parse_local_typedef_declarator_list(token_kind_t base_kind, int elem_size,
                                                tk_float_kind_t fp_kind,
                                                token_kind_t tag_kind, char *tag_name, int tag_len,
                                                int is_pointer_base,
                                                int td_pointee_const, int td_pointee_volatile,
                                                int td_is_unsigned) {
  for (;;) {
    int is_ptr = is_pointer_base;
    unsigned int ptr_const_mask = 0;
    unsigned int ptr_volatile_mask = 0;
    int ptr_levels = 0;
    int paren_array_mul = 0;
    consume_pointer_chain_decl(&is_ptr, &ptr_const_mask, &ptr_volatile_mask, &ptr_levels);
    token_ident_t *name = consume_decl_name(&is_ptr, &ptr_const_mask, &ptr_volatile_mask, &ptr_levels, &paren_array_mul);
    define_local_typedef_from_declarator(name, is_ptr, paren_array_mul,
                                         base_kind, elem_size, fp_kind,
                                         tag_kind, tag_name, tag_len,
                                         td_pointee_const, td_pointee_volatile, td_is_unsigned);
    if (!tk_consume(',')) break;
  }
}
