#include "internal/stmt.h"
#include "internal/alignas_value.h"
#include "internal/anon_tag.h"
#include "internal/arena.h"
#include "internal/array_suffixes.h"
#include "internal/core.h"
#include "internal/decl.h"
#include "internal/diag.h"
#include "internal/dynarray.h"
#include "internal/enum_const.h"
#include "internal/expr.h"
#include "internal/loop_ctx.h"
#include "internal/node_utils.h"
#include "internal/semantic_ctx.h"
#include "internal/struct_layout.h"
#include "internal/switch_ctx.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include <stdio.h>
#include <stdlib.h>

node_t *ps_expr(void);

static inline token_t *curtok(void) {
  return tk_get_current_token();
}

static inline void set_curtok(token_t *tok) {
  tk_set_current_token(tok);
}

static void parse_typedef_decl(void);
static int parse_decl_type_spec(int *elem_size, tk_float_kind_t *fp_kind,
                                token_kind_t *tag_kind, char **tag_name, int *tag_len,
                                int *is_pointer_base, token_kind_t *base_kind);
static token_ident_t *parse_typedef_name_decl(int *is_ptr);
static token_ident_t *parse_typedef_name_decl_recursive(int *is_ptr);
typedef struct {
  int arr_total;
  int is_array;
  int has_incomplete_array;
  // pointer-to-array typedef `typedef int (*PA)[3][4]` のポインティ各次元 (dims[0] が最外)。
  int dims[8];
  int dim_count;
  int first_dim;
} stmt_array_suffix_t;
/* 直近にパースした typedef 宣言子で `*` が括弧内に現れたか (`(*PA)`)。pointer-to-array /
 * pointer-to-function の識別に使う。宣言子ごとに parse_typedef_decl でリセットする。 */
static int g_stmt_typedef_ptr_in_paren = 0;
static stmt_array_suffix_t parse_stmt_array_suffixes(int base_mul);
static node_t *stmt_internal(void);
static node_t *block_item(void);
static int is_decl_like_start_stmt(void);
static node_t *parse_decl_like_stmt(void);

static token_ident_t *parse_typedef_name_decl_recursive(int *is_ptr) {
  psx_consume_pointer_prefix(is_ptr);
  token_ident_t *name = NULL;
  if (tk_consume('(')) {
    int ptr_before = *is_ptr;
    name = parse_typedef_name_decl_recursive(is_ptr);
    /* 括弧内で初めて `*` が立った (`(*PA)`): pointer-to-array / 関数ポインタの指標。 */
    if (*is_ptr && !ptr_before) g_stmt_typedef_ptr_in_paren = 1;
    tk_expect(')');
  } else {
    name = tk_consume_ident();
  }
  psx_skip_func_suffix_groups(NULL);
  return name;
}

static token_ident_t *parse_typedef_name_decl(int *is_ptr) {
  token_ident_t *name = parse_typedef_name_decl_recursive(is_ptr);
  if (!name) {
    diag_emit_tokf(DIAG_ERR_PARSER_TYPEDEF_NAME_REQUIRED, curtok(), "%s",
                   diag_message_for(DIAG_ERR_PARSER_TYPEDEF_NAME_REQUIRED));
  }
  return name;
}


static stmt_array_suffix_t parse_stmt_array_suffixes(int base_mul) {
  stmt_array_suffix_t out = {0};
  out.arr_total = (base_mul > 0) ? base_mul : 1;
  out.is_array = (base_mul > 0);
  out.has_incomplete_array = 0;
  int dim_count = 0;
  while (tk_consume('[')) {
    int has_size = 0;
    int n = psx_parse_array_size_optional_constexpr(&has_size);
    if (!has_size) {
      out.has_incomplete_array = 1;
    } else {
      out.arr_total *= n;
    }
    if (dim_count == 0) out.first_dim = has_size ? n : 0;
    if (dim_count < 8) out.dims[dim_count] = has_size ? n : 0;
    dim_count++;
    out.is_array = 1;
  }
  out.dim_count = dim_count;
  return out;
}


// _Alignas( constant-expression | type-name )


/* 基底型がポインタ typedef のときの段数 (`typedef int **PP; typedef PP X;` の合成用)。
 * parse_decl_type_spec の typedef 分岐で設定し、parse_typedef_decl が読む。 */
static int g_stmt_base_ptr_levels = 0;

static int parse_decl_type_spec(int *elem_size, tk_float_kind_t *fp_kind,
                                token_kind_t *tag_kind, char **tag_name, int *tag_len,
                                int *is_pointer_base, token_kind_t *base_kind) {
  *elem_size = 8;
  *fp_kind = TK_FLOAT_KIND_NONE;
  *tag_kind = TK_EOF;
  *tag_name = NULL;
  *tag_len = 0;
  *is_pointer_base = 0;
  *base_kind = TK_EOF;
  g_stmt_base_ptr_levels = 0;

  token_kind_t builtin_kind = psx_consume_type_kind();
  if (builtin_kind != TK_EOF) {
    *base_kind = builtin_kind;
    psx_ctx_get_type_info(builtin_kind, NULL, elem_size);
    if (builtin_kind == TK_FLOAT) *fp_kind = TK_FLOAT_KIND_FLOAT;
    else if (builtin_kind == TK_DOUBLE) *fp_kind = TK_FLOAT_KIND_DOUBLE;
    return 1;
  }
  if (psx_ctx_is_tag_keyword(curtok()->kind)) {
    *base_kind = curtok()->kind;
    *tag_kind = curtok()->kind;
    set_curtok(curtok()->next);
    token_ident_t *tag = tk_consume_ident();
    if (!tag && curtok()->kind != TK_LBRACE) {
      psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_TAG_NAME));
    }
    *tag_name = tag ? tag->str : NULL;
    *tag_len = tag ? tag->len : 0;
    if (!tag) {
      psx_make_anonymous_tag_name(tag_name, tag_len);
    }
    if (tk_consume('{')) {
      int member_count = 0;
      int tag_size = 0;
      member_count = psx_parse_tag_definition_body(*tag_kind, *tag_name, *tag_len, &tag_size);
      psx_ctx_define_tag_type_with_layout(*tag_kind, *tag_name, *tag_len, member_count, tag_size);
    } else if (!psx_ctx_has_tag_type(*tag_kind, *tag_name, *tag_len)) {
      if (*tag_kind == TK_STRUCT || *tag_kind == TK_UNION) {
        psx_ctx_define_tag_type(*tag_kind, *tag_name, *tag_len);
      } else {
        psx_diag_undefined_with_name(curtok(), diag_text_for(DIAG_TEXT_TAG_TYPE_SUFFIX), *tag_name, *tag_len);
      }
    }
    *elem_size = psx_ctx_get_tag_size(*tag_kind, *tag_name, *tag_len);
    return 1;
  }
  if (psx_ctx_is_typedef_name_token(curtok())) {
    token_ident_t *id = (token_ident_t *)curtok();
    if (!psx_ctx_find_typedef_name(id->str, id->len, base_kind, elem_size, fp_kind,
                                   tag_kind, tag_name, tag_len, is_pointer_base, NULL, NULL, NULL)) {
      return 0;
    }
    /* 基底がポインタ typedef なら段数を捕捉 (合成 typedef の段数加算用)。 */
    g_stmt_base_ptr_levels = psx_ctx_get_typedef_pointer_levels(id->str, id->len);
    set_curtok(curtok()->next);
    return 1;
  }
  return 0;
}

static void parse_typedef_decl(void) {
  if (curtok()->kind != TK_TYPEDEF) {
    psx_diag_ctx(curtok(), "typedef", "%s",
                 diag_message_for(DIAG_ERR_PARSER_TYPEDEF_KEYWORD_REQUIRED));
  }
  set_curtok(curtok()->next);
  int elem_size = 8;
  tk_float_kind_t fp_kind = TK_FLOAT_KIND_NONE;
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int is_pointer_base = 0;
  token_kind_t base_kind = TK_EOF;
  if (!parse_decl_type_spec(&elem_size, &fp_kind, &tag_kind, &tag_name, &tag_len, &is_pointer_base, &base_kind)) {
    diag_emit_tokf(DIAG_ERR_PARSER_TYPE_NAME_REQUIRED, curtok(), "%s",
                   diag_message_for(DIAG_ERR_PARSER_TYPE_NAME_REQUIRED));
  }
  int td_pointee_const = 0;
  int td_pointee_volatile = 0;
  psx_take_type_qualifiers(&td_pointee_const, &td_pointee_volatile);
  int td_is_unsigned = (base_kind == TK_UNSIGNED) || psx_last_type_is_unsigned();

  int base_ptr_levels = g_stmt_base_ptr_levels;
  for (;;) {
    int is_ptr = is_pointer_base;
    g_stmt_typedef_ptr_in_paren = 0;
    int decl_stars = psx_consume_pointer_prefix_counted(&is_ptr);
    token_ident_t *name = parse_typedef_name_decl(&is_ptr);
    int typedef_sizeof = is_ptr ? 8 : elem_size;
    stmt_array_suffix_t arr = parse_stmt_array_suffixes(0);
    if (!is_ptr && arr.has_incomplete_array) typedef_sizeof = 0;
    else if (!is_ptr && arr.is_array && arr.arr_total > 0) typedef_sizeof *= arr.arr_total;
    token_kind_t stored_base_kind = (td_is_unsigned && base_kind == TK_INT) ? TK_UNSIGNED : base_kind;
    /* pointer-to-array typedef `typedef int (*PA)[3]` (is_ptr=1 かつ `*` が括弧内) のみ、
     * 括弧の後ろの `[3]` をポインティ配列の extent として dims に記録する (is_array=0 の
     * まま)。これがないと `PA p; p+1 / p[i]` が要素 1 個 (4B) しか進まず直書き `int(*p)[3]`
     * と食い違う。その他 (スカラ / 配列 typedef) は従来の psx_ctx_define_typedef_name
     * 相当 (is_array=0, dims なし) を維持して退行を避ける。 */
    int is_pta = (is_ptr && g_stmt_typedef_ptr_in_paren && arr.is_array && arr.dim_count > 0);
    int td_first_dim = is_pta ? arr.first_dim : 0;
    int td_dim_count = is_pta ? arr.dim_count : 0;
    const int *td_dims = is_pta ? arr.dims : NULL;
    if (!psx_ctx_define_typedef_name_ex3(name->str, name->len, stored_base_kind, elem_size, fp_kind,
                                tag_kind, tag_name, tag_len, is_ptr, typedef_sizeof,
                                td_pointee_const, td_pointee_volatile, td_is_unsigned,
                                0, td_first_dim, td_dims, td_dim_count)) {
      psx_diag_duplicate_with_name(curtok(), "typedef", name->str, name->len);
    }
    /* 多段ポインタ typedef (`typedef int **PP`) の段数を記録する。単段や pointer-to-array
     * は getter のデフォルト (is_pointer→1) に任せ、2 段以上だけ明示保存して退行を避ける。
     * 段数 = 基底ポインタ typedef の段数 + 宣言子の prefix `*` 数。 */
    int td_ptr_levels = base_ptr_levels + decl_stars;
    if (is_ptr && td_ptr_levels >= 2) {
      psx_ctx_set_typedef_pointer_levels(name->str, name->len, td_ptr_levels);
    }
    if (!tk_consume(',')) break;
  }
  tk_expect(';');
}

static int is_decl_like_start_stmt(void) {
  if (curtok()->kind == TK_TYPEDEF) return 1;
  if (curtok()->kind == TK_STATIC_ASSERT) return 1;
  if (psx_ctx_is_type_token(curtok()->kind) || psx_is_decl_prefix_token(curtok()->kind) ||
      psx_ctx_is_typedef_name_token(curtok())) return 1;
  if (psx_ctx_is_tag_keyword(curtok()->kind)) return 1;
  return 0;
}

static node_t *parse_decl_like_stmt(void) {
  if (curtok()->kind == TK_TYPEDEF) {
    parse_typedef_decl();
    return psx_node_new_num(0);
  }

  /* `const struct T x;` のように cv 修飾子 / storage class を先に書いてから
   * struct/union/enum が続く場合、修飾子をスキップして tag-keyword 経路に
   * 入れるようにする。psx_decl_parse_declaration は struct を type-spec
   * として直接処理できないため、ここで先に分岐する必要がある。 */
  int tag_path_saw_static = 0;
  int tag_path_alignas = 0;
  {
    token_t *peek = curtok();
    while (peek && psx_is_decl_prefix_token(peek->kind)) {
      if (peek->kind == TK_ALIGNAS && peek->next && peek->next->kind == TK_LPAREN) {
        /* `_Alignas(...)` は 1 単位。続く `(...)` を釣り合った括弧で読み飛ばさないと
         * `(` で止まり、後ろの struct/union/enum を検出できない。 */
        peek = peek->next->next;
        int depth = 1;
        while (peek && depth > 0) {
          if (peek->kind == TK_LPAREN) depth++;
          else if (peek->kind == TK_RPAREN) depth--;
          peek = peek->next;
        }
      } else {
        peek = peek->next;
      }
    }
    if (peek && psx_ctx_is_tag_keyword(peek->kind)) {
      /* 修飾子を先に飲み込む (parse_decl_like 側の cv 状態を更新する)。
       * `static struct T x;` の storage class はここで素通りスキップすると
       * skip_cv_qualifiers を経由せず g_last_decl_is_static が立たない。static を
       * 検出して記録し、tag (定義) パース後に psx_set_static_flag で復元する
       * (インライン定義 `static struct {..} s` のメンバ解析が skip_cv_qualifiers で
       * フラグをリセットするため、after_type 呼出直前に再適用する)。これがないと
       * static struct/union 局所がグローバルへ lowering されず auto 扱いで
       * 呼び出し跨ぎで永続しなかった。 */
      while (psx_is_decl_prefix_token(curtok()->kind)) {
        if (curtok()->kind == TK_STATIC) tag_path_saw_static = 1;
        if (curtok()->kind == TK_ALIGNAS) {
          /* `_Alignas(N) struct T x;`: _Alignas トークンと続く `(N)` を正しく消費し、
           * 値を捕捉する。素朴に set_curtok(next) すると `(N)` が残り `struct` 検出前で
           * E3015 になっていた。値は tag 定義パース後に psx_set_alignas_value で復元する。 */
          set_curtok(curtok()->next);
          int av = psx_parse_alignas_value();
          if (av > tag_path_alignas) tag_path_alignas = av;
          continue;
        }
        set_curtok(curtok()->next);
      }
      /* tag 経路へフォールスルー */
    }
  }

  if (curtok()->kind == TK_STATIC_ASSERT ||
      psx_ctx_is_type_token(curtok()->kind) || psx_is_decl_prefix_token(curtok()->kind) ||
      psx_ctx_is_typedef_name_token(curtok())) {
    return psx_decl_parse_declaration();
  }

  if (psx_ctx_is_tag_keyword(curtok()->kind)) {
    token_kind_t tag_kind = curtok()->kind;
    set_curtok(curtok()->next);
    token_ident_t *tag = tk_consume_ident();
    // 匿名タグ（enum { A=1 }; など）: タグ名なしで '{' が来る場合
    if (!tag && curtok()->kind != TK_LBRACE) {
      psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_TAG_NAME));
    }
    /* 無名タグ名は永続確保する。スタックバッファだと宣言文の解析後に解放され、
     * 後続文での `u.member` アクセス時にタグ検索が dangling ポインタを引いて失敗する。 */
    char *tag_name = tag ? tag->str : NULL;
    int tag_len = tag ? tag->len : 0;
    if (!tag) {
      psx_make_anonymous_tag_name(&tag_name, &tag_len);
    }
    if (tk_consume('{')) {
      int member_count = 0;
      int tag_size = 0;
      member_count = psx_parse_tag_definition_body(tag_kind, tag_name, tag_len, &tag_size);
      psx_ctx_define_tag_type_with_layout(tag_kind, tag_name, tag_len, member_count, tag_size);
      if (tk_consume(';')) {
        return psx_node_new_num(0);
      }
      /* メンバ定義の解析で skip_cv_qualifiers が static / alignas をリセットするため復元。 */
      if (tag_path_saw_static) psx_set_static_flag(1);
      if (tag_path_alignas) psx_set_alignas_value(tag_path_alignas);
      return psx_decl_parse_declaration_after_type(tag_size, TK_FLOAT_KIND_NONE, tag_kind, tag_name, tag_len, 0, 0, 0, 0);
    }
    if (tk_consume(';')) {
      psx_ctx_define_tag_type(tag_kind, tag_name, tag_len);
      return psx_node_new_num(0);
    }
    if (!psx_ctx_has_tag_type(tag_kind, tag_name, tag_len)) {
      psx_diag_undefined_with_name(curtok(), diag_text_for(DIAG_TEXT_TAG_TYPE_SUFFIX), tag_name, tag_len);
    }
    int tag_size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
    if (tag_path_saw_static) psx_set_static_flag(1);
    if (tag_path_alignas) psx_set_alignas_value(tag_path_alignas);
    return psx_decl_parse_declaration_after_type(tag_size > 0 ? tag_size : 8,
                                                 TK_FLOAT_KIND_NONE, tag_kind, tag_name, tag_len, 0, 0, 0, 0);
  }

  return NULL;
}

static node_t *block_item(void) {
  if (is_decl_like_start_stmt()) {
    return parse_decl_like_stmt();
  }

  return stmt_internal();
}

/* 文 (statement) 分岐ヘルパ群: stmt_internal の dispatch から呼ばれる。
 * 各ヘルパは対応するキーワードトークンを消費して文を構築する。
 * (block / return / if / while / do-while / for / switch / case /
 *  default / break / continue / goto / label) */
static node_t *parse_stmt_block(void);
static node_t *parse_stmt_return(void);
static node_t *parse_stmt_if(void);
static node_t *parse_stmt_while(void);
static node_t *parse_stmt_do_while(void);
static node_t *parse_stmt_for(void);
static node_t *parse_stmt_switch(void);
static node_t *parse_stmt_case(void);
static node_t *parse_stmt_default(void);
static node_t *parse_stmt_break(void);
static node_t *parse_stmt_continue(void);
static node_t *parse_stmt_goto(void);
static node_t *parse_stmt_label(void);

static node_t *stmt_internal(void) {
  // 空文（null statement）: C11 6.8.3 — セミコロンだけの文
  if (tk_consume(';')) return psx_node_new_num(0);
  if (curtok()->kind == TK_LBRACE) return parse_stmt_block();
  if (is_decl_like_start_stmt()) return parse_decl_like_stmt();
  switch (curtok()->kind) {
    case TK_RETURN:   return parse_stmt_return();
    case TK_IF:       return parse_stmt_if();
    case TK_WHILE:    return parse_stmt_while();
    case TK_DO:       return parse_stmt_do_while();
    case TK_FOR:      return parse_stmt_for();
    case TK_SWITCH:   return parse_stmt_switch();
    case TK_CASE:     return parse_stmt_case();
    case TK_DEFAULT:  return parse_stmt_default();
    case TK_BREAK:    return parse_stmt_break();
    case TK_CONTINUE: return parse_stmt_continue();
    case TK_GOTO:     return parse_stmt_goto();
    default: break;
  }
  /* `<ident>:` 形のラベル文 */
  if (curtok()->kind == TK_IDENT && curtok()->next && curtok()->next->kind == TK_COLON) {
    return parse_stmt_label();
  }
  /* 式文 (式を評価して結果を捨てる) */
  node_t *node = ps_expr();
  tk_expect(';');
  return node;
}

static node_t *parse_stmt_block(void) {
  tk_consume('{');
  psx_ctx_enter_block_scope();
  psx_decl_enter_scope();
  node_block_t *node = arena_alloc(sizeof(node_block_t));
  node->base.kind = ND_BLOCK;
  int i = 0;
  int cap = 16;
  node->body = calloc(cap, sizeof(node_t*));
  int prev_terminates = 0;
  while (!tk_consume('}')) {
    // #pragma pack マーカーはブロック内でも透過的に処理（AST には載せない）。
    if (psx_try_consume_pragma_pack_marker()) continue;
    if (prev_terminates && curtok()->kind != TK_CASE && curtok()->kind != TK_DEFAULT &&
        !(curtok()->kind == TK_IDENT && curtok()->next && curtok()->next->kind == TK_COLON)) {
      diag_warn_tokf(DIAG_WARN_PARSER_UNREACHABLE_CODE, curtok(),
                     "%s", diag_warn_message_for(DIAG_WARN_PARSER_UNREACHABLE_CODE));
      prev_terminates = 0;
    }
    if (i >= cap - 1) {
      cap = pda_next_cap(cap, i + 2);
      node->body = pda_xreallocarray(node->body, (size_t)cap, sizeof(node_t *));
    }
    node->body[i] = block_item();
    node_kind_t k = node->body[i]->kind;
    prev_terminates = (k == ND_RETURN || k == ND_BREAK || k == ND_CONTINUE || k == ND_GOTO);
    i++;
  }
  node->body[i] = NULL;
  psx_decl_leave_scope();
  psx_ctx_leave_block_scope();
  return (node_t *)node;
}

static node_t *parse_stmt_return(void) {
  set_curtok(curtok()->next);
  node_t *node = arena_alloc(sizeof(node_t));
  node->kind = ND_RETURN;
  if (tk_consume(';')) {
    /* `void *` などポインタ戻り型は void ではない。ret_token_kind は TK_VOID でも
     * is_pointer が立つので、値なし return は値要求エラーにする。 */
    if (psx_expr_current_func_ret_token_kind() != TK_VOID ||
        psx_expr_current_func_ret_is_pointer()) {
      diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, curtok(),
                     "%s",
                     diag_message_for(DIAG_ERR_PARSER_RETURN_VALUE_REQUIRED_NONVOID));
    }
    node->lhs = NULL;
    node->fp_kind = psx_expr_current_func_ret_fp_kind();
    return node;
  }
  node->lhs = ps_expr();
  /* `void *` 等のポインタ戻り型は void ではないので、return に式を許可する。
   * is_pointer が立つときは TK_VOID でも void 関数扱いしない。 */
  if (psx_expr_current_func_ret_token_kind() == TK_VOID &&
      !psx_expr_current_func_ret_is_pointer()) {
    diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, curtok(),
                   "%s",
                   diag_message_for(DIAG_ERR_PARSER_RETURN_VALUE_FORBIDDEN_VOID));
  }
  /* C11 6.8.6.4 / 6.5.16.1: ポインタ戻り値型の関数で非ゼロ整数値を返すのは
   * 互換性のない型の制約違反 (NULL ポインタ定数 0 は許可)。
   * 明示キャスト (int*)x は apply_cast が is_pointer を設定するためここでは通る。 */
  if (psx_expr_current_func_ret_is_pointer() && node->lhs) {
    if (node->lhs->kind == ND_NUM) {
      node_num_t *num = (node_num_t *)node->lhs;
      if (num->val != 0) {
        psx_diag_ctx(curtok(), "return",
                     "ポインタを返す関数から非ゼロ整数定数 (%lld) を返却できません (C11 6.8.6.4)",
                     num->val);
      }
    }
  }
  /* C11 6.3.1.2: 関数戻り値型が _Bool なら return 値も (rhs != 0) に正規化。
   * `_Bool f() { return 200; }` で 200 をそのまま返すと caller が真偽以外の
   * 値を見て誤動作する (`flag * 7` が 1400 になる等)。 */
  if (psx_expr_current_func_ret_token_kind() == TK_BOOL &&
      !psx_expr_current_func_ret_is_pointer() && node->lhs) {
    node->lhs = psx_node_new_binary(ND_NE, node->lhs, psx_node_new_num(0));
  }
  /* C11 6.8.6.4: sub-int (char/short) 戻り型は宣言幅へ変換して返す。codegen は値を
   * 64bit レジスタで持ち戻り型を I32 へ widening するため自動で切り詰めず、callee が
   * int 値をそのまま返していた (`char f(int x){return x;}` の f(300) が 300 を返し
   * 比較が化ける)。明示 (char)/(short) と同じ 64bit 算術シフトで低 8/16bit を符号拡張。
   * plain char は本 ABI で signed。unsigned char/short は戻り型トークンが TK_INT に
   * 潰れて TK_CHAR/TK_SHORT にならないため対象外 (符号拡張で改悪しない)。 */
  {
    token_kind_t rk = psx_expr_current_func_ret_token_kind();
    if (!psx_expr_current_func_ret_is_pointer() && node->lhs &&
        (rk == TK_CHAR || rk == TK_SHORT)) {
      if (psx_expr_current_func_ret_is_unsigned()) {
        /* unsigned char/short: ゼロ拡張 (& 0xff / 0xffff)。符号拡張すると bit 7/15 が
         * 立つ値 (`unsigned short f(){return 40000;}`) が負に化ける。 */
        long long mask = (rk == TK_CHAR) ? 0xffLL : 0xffffLL;
        node_t *m = psx_node_new_binary(ND_BITAND, node->lhs, psx_node_new_num(mask));
        psx_node_set_unsigned(m, 1);
        node->lhs = m;
      } else {
        /* signed char/short: 算術シフトで符号拡張。 */
        int sh = (rk == TK_CHAR) ? 56 : 48;
        node_t *shl = psx_node_new_binary(ND_SHL, node->lhs, psx_node_new_num(sh));
        node_t *shr = psx_node_new_binary(ND_SHR, shl, psx_node_new_num(sh));
        psx_node_set_unsigned(shl, 0);
        psx_node_set_unsigned(shr, 0); /* 算術右シフト (符号拡張) */
        node->lhs = shr;
      }
    }
  }
  node->fp_kind = psx_expr_current_func_ret_fp_kind();
  node->ret_struct_size = psx_expr_current_func_ret_struct_size();
  tk_expect(';');
  return node;
}

static node_t *parse_stmt_if(void) {
  set_curtok(curtok()->next);
  tk_expect('(');
  node_ctrl_t *node = arena_alloc(sizeof(node_ctrl_t));
  node->base.kind = ND_IF;
  node->base.lhs = ps_expr();
  tk_expect(')');
  node->base.rhs = stmt_internal();
  if (curtok()->kind == TK_ELSE) {
    set_curtok(curtok()->next);
    node->els = stmt_internal();
  }
  return (node_t *)node;
}

static node_t *parse_stmt_while(void) {
  set_curtok(curtok()->next);
  tk_expect('(');
  node_ctrl_t *node = arena_alloc(sizeof(node_ctrl_t));
  node->base.kind = ND_WHILE;
  node->base.lhs = ps_expr();
  tk_expect(')');
  psx_loop_enter();
  node->base.rhs = stmt_internal();
  psx_loop_leave();
  return (node_t *)node;
}

static node_t *parse_stmt_do_while(void) {
  set_curtok(curtok()->next);
  node_ctrl_t *node = arena_alloc(sizeof(node_ctrl_t));
  node->base.kind = ND_DO_WHILE;
  psx_loop_enter();
  node->base.rhs = stmt_internal();
  psx_loop_leave();
  if (curtok()->kind != TK_WHILE) {
    psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_WHILE));
  }
  set_curtok(curtok()->next);
  tk_expect('(');
  node->base.lhs = ps_expr();
  tk_expect(')');
  tk_expect(';');
  return (node_t *)node;
}

static node_t *parse_stmt_for(void) {
  set_curtok(curtok()->next);
  tk_expect('(');
  node_ctrl_t *node = arena_alloc(sizeof(node_ctrl_t));
  node->base.kind = ND_FOR;
  int for_has_decl = 0;
  if (!tk_consume(';')) {
    if (is_decl_like_start_stmt()) {
      for_has_decl = 1;
      psx_decl_enter_scope();
      node->init = parse_decl_like_stmt();
    } else {
      node->init = ps_expr();
      tk_expect(';');
    }
  }
  if (!tk_consume(';')) {
    node->base.lhs = ps_expr();
    tk_expect(';');
  }
  if (!tk_consume(')')) {
    node->inc = ps_expr();
    tk_expect(')');
  }
  psx_loop_enter();
  node->base.rhs = stmt_internal();
  psx_loop_leave();
  if (for_has_decl) psx_decl_leave_scope();
  return (node_t *)node;
}

static node_t *parse_stmt_switch(void) {
  set_curtok(curtok()->next);
  tk_expect('(');
  node_ctrl_t *node = arena_alloc(sizeof(node_ctrl_t));
  node->base.kind = ND_SWITCH;
  node->base.lhs = ps_expr();
  tk_expect(')');
  psx_switch_push_ctx();
  node->base.rhs = stmt_internal();
  psx_switch_pop_ctx();
  return (node_t *)node;
}

static node_t *parse_stmt_case(void) {
  set_curtok(curtok()->next);
  node_case_t *node = arena_alloc(sizeof(node_case_t));
  node->base.kind = ND_CASE;
  node->val = psx_parse_case_const_expr();
  psx_switch_register_case(node->val, curtok());
  tk_expect(':');
  node->base.rhs = stmt_internal();
  return (node_t *)node;
}

static node_t *parse_stmt_default(void) {
  set_curtok(curtok()->next);
  psx_switch_register_default(curtok());
  node_default_t *node = arena_alloc(sizeof(node_default_t));
  node->base.kind = ND_DEFAULT;
  tk_expect(':');
  node->base.rhs = stmt_internal();
  return (node_t *)node;
}

static node_t *parse_stmt_break(void) {
  if (psx_loop_depth() == 0 && !psx_switch_has_ctx()) {
    psx_diag_only_in(curtok(), diag_text_for(DIAG_TEXT_BREAK), diag_text_for(DIAG_TEXT_LOOP_OR_SWITCH_SCOPE));
  }
  set_curtok(curtok()->next);
  node_t *node = arena_alloc(sizeof(node_t));
  node->kind = ND_BREAK;
  tk_expect(';');
  return node;
}

static node_t *parse_stmt_continue(void) {
  if (psx_loop_depth() == 0) {
    psx_diag_only_in(curtok(), diag_text_for(DIAG_TEXT_CONTINUE), diag_text_for(DIAG_TEXT_LOOP_SCOPE));
  }
  set_curtok(curtok()->next);
  node_t *node = arena_alloc(sizeof(node_t));
  node->kind = ND_CONTINUE;
  tk_expect(';');
  return node;
}

static node_t *parse_stmt_goto(void) {
  token_t *goto_tok = curtok();
  set_curtok(curtok()->next);
  token_ident_t *ident = tk_consume_ident();
  if (!ident) {
    psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_GOTO_LABEL_AFTER));
  }
  node_jump_t *node = arena_alloc(sizeof(node_jump_t));
  node->base.kind = ND_GOTO;
  node->name = ident->str;
  node->name_len = ident->len;
  psx_ctx_register_goto_ref(ident->str, ident->len, goto_tok);
  tk_expect(';');
  return (node_t *)node;
}

static node_t *parse_stmt_label(void) {
  token_ident_t *ident = tk_consume_ident();
  tk_expect(':');
  node_jump_t *node = arena_alloc(sizeof(node_jump_t));
  node->base.kind = ND_LABEL;
  node->name = ident->str;
  node->name_len = ident->len;
  psx_ctx_register_label_def(ident->str, ident->len, curtok());
  node->base.rhs = stmt_internal();
  return (node_t *)node;
}

node_t *psx_stmt_stmt(void) {
  return stmt_internal();
}
