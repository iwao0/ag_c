#include "stmt.h"
#include "alignas_value.h"
#include "anon_tag.h"
#include "arena.h"
#include "array_suffixes.h"
#include "core.h"
#include "decl.h"
#include "diag.h"
#include "dynarray.h"
#include "enum_const.h"
#include "expr.h"
#include "node_utils.h"
#include "semantic_ctx.h"
#include "struct_layout.h"
#include "tag_declaration.h"
#include "typedef_declaration.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

node_t *ps_expr(void);

static inline token_t *curtok(void) {
  return tk_get_current_token();
}

static inline void set_curtok(token_t *tok) {
  tk_set_current_token(tok);
}

static void parse_typedef_decl(void);
typedef struct {
  psx_type_spec_result_t type_spec;
  const psx_type_t *base_decl_type;
} stmt_decl_type_state_t;
typedef struct {
  int ptr_levels;
  int funcptr_object_pointer_levels;
  psx_declarator_shape_t declarator_shape;
} stmt_typedef_declarator_state_t;
static int parse_decl_type_spec(int *elem_size, tk_float_kind_t *fp_kind,
                                token_kind_t *tag_kind, char **tag_name, int *tag_len,
                                int *is_pointer_base, token_kind_t *base_kind,
                                stmt_decl_type_state_t *type_state);
static token_ident_t *parse_typedef_name_decl(stmt_typedef_declarator_state_t *decl_state,
                                              int *is_ptr);
static token_ident_t *parse_typedef_name_decl_recursive(stmt_typedef_declarator_state_t *decl_state,
                                                        int *is_ptr);
static node_t *stmt_internal(void);
static node_t *parse_stmt_label(void);
static node_t *block_item(void);
static int is_decl_like_start_stmt(void);
static node_t *parse_decl_like_stmt(void);

static token_ident_t *parse_typedef_name_decl_recursive(stmt_typedef_declarator_state_t *decl_state,
                                                        int *is_ptr) {
  int level_start = decl_state->ptr_levels;
  int stars = psx_consume_pointer_prefix_counted(is_ptr);
  decl_state->ptr_levels += stars;
  int level_end = decl_state->ptr_levels;
  token_ident_t *name = NULL;
  if (tk_consume('(')) {
    name = parse_typedef_name_decl_recursive(decl_state, is_ptr);
    tk_expect(')');
  } else {
    name = tk_consume_ident();
  }

  for (;;) {
    if (tk_consume('[')) {
      int has_size = 0;
      int dim = psx_parse_array_size_optional_constexpr(&has_size);
      psx_declarator_shape_append_array_ex(
          &decl_state->declarator_shape, has_size ? dim : 0, !has_size);
      continue;
    }
    if (curtok()->kind == TK_LPAREN) {
      psx_funcptr_signature_t suffix = {0};
      psx_skip_func_param_list(&suffix);
      psx_decl_funcptr_sig_t op_sig = {0};
      op_sig.function.callable.signature = suffix;
      psx_declarator_shape_append_function(
          &decl_state->declarator_shape, op_sig);
      continue;
    }
    break;
  }
  for (int level = level_end - 1; level >= level_start; level--) {
    psx_declarator_shape_append_pointer(
        &decl_state->declarator_shape, 0, 0);
  }
  return name;
}

static token_ident_t *parse_typedef_name_decl(stmt_typedef_declarator_state_t *decl_state,
                                              int *is_ptr) {
  int initial_ptr_levels = decl_state ? decl_state->ptr_levels : 0;
  if (decl_state) {
    memset(decl_state, 0, sizeof(*decl_state));
    decl_state->ptr_levels = initial_ptr_levels;
    psx_declarator_shape_init(&decl_state->declarator_shape);
  }
  token_ident_t *name = parse_typedef_name_decl_recursive(decl_state, is_ptr);
  if (!name) {
    diag_emit_tokf(DIAG_ERR_PARSER_TYPEDEF_NAME_REQUIRED, curtok(), "%s",
                   diag_message_for(DIAG_ERR_PARSER_TYPEDEF_NAME_REQUIRED));
  }
  for (int i = 0; i < decl_state->declarator_shape.count; i++) {
    if (decl_state->declarator_shape.ops[i].kind != PSX_DECL_OP_FUNCTION)
      continue;
    int object_levels = 0;
    for (int j = 0; j < i; j++) {
      if (decl_state->declarator_shape.ops[j].kind == PSX_DECL_OP_POINTER)
        object_levels++;
    }
    decl_state->funcptr_object_pointer_levels = object_levels;
    break;
  }
  return name;
}


static psx_type_t *stmt_typedef_base_type(
    token_kind_t base_kind, int elem_size, tk_float_kind_t fp_kind,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int is_unsigned, int is_complex, const psx_type_t *base_decl_type) {
  if (base_decl_type) return psx_type_clone(base_decl_type);
  if (psx_ctx_is_tag_aggregate_kind(tag_kind))
    return psx_type_new_tag(tag_kind, tag_name, tag_len, 0, elem_size);
  if (is_complex) {
    psx_type_t *type = psx_type_new(PSX_TYPE_COMPLEX);
    type->fp_kind = fp_kind != TK_FLOAT_KIND_NONE
                        ? fp_kind
                        : TK_FLOAT_KIND_DOUBLE;
    type->size = elem_size;
    type->align = elem_size >= 8 ? 8 : elem_size;
    return type;
  }
  if (fp_kind != TK_FLOAT_KIND_NONE)
    return psx_type_new_float(fp_kind, elem_size);
  if (base_kind == TK_VOID) {
    psx_type_t *type = psx_type_new(PSX_TYPE_VOID);
    type->scalar_kind = TK_VOID;
    return type;
  }
  return psx_type_new_integer(base_kind, elem_size, is_unsigned);
}

// _Alignas( constant-expression | type-name )


static int parse_decl_type_spec(int *elem_size, tk_float_kind_t *fp_kind,
                                token_kind_t *tag_kind, char **tag_name, int *tag_len,
                                int *is_pointer_base, token_kind_t *base_kind,
                                stmt_decl_type_state_t *type_state) {
  *elem_size = 8;
  *fp_kind = TK_FLOAT_KIND_NONE;
  *tag_kind = TK_EOF;
  *tag_name = NULL;
  *tag_len = 0;
  *is_pointer_base = 0;
  *base_kind = TK_EOF;
  if (type_state) memset(type_state, 0, sizeof(*type_state));

  psx_type_spec_result_t builtin_spec;
  token_kind_t builtin_kind = psx_consume_type_kind_ex(&builtin_spec);
  if (type_state) type_state->type_spec = builtin_spec;
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
    psx_skip_gnu_attributes();
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
      int tag_align = 0;
      member_count = psx_parse_tag_definition_body(*tag_kind, *tag_name, *tag_len, &tag_size, &tag_align);
      psx_apply_parsed_tag_declaration(
          *tag_kind, *tag_name, *tag_len,
          PSX_TAG_DECLARATION_DEFINITION, member_count, tag_size,
          tag_align, curtok());
    } else {
      psx_apply_parsed_tag_declaration(
          *tag_kind, *tag_name, *tag_len,
          PSX_TAG_DECLARATION_REFERENCE, 0, 0, 0, curtok());
    }
    while (curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE) {
      set_curtok(curtok()->next);
    }
    *elem_size = psx_ctx_get_tag_size(*tag_kind, *tag_name, *tag_len);
    return 1;
  }
  if (psx_ctx_is_typedef_name_token(curtok())) {
    token_ident_t *id = (token_ident_t *)curtok();
    psx_typedef_info_t _ti;
    if (!psx_ctx_find_typedef_name(id->str, id->len, &_ti)) {
      return 0;
    }
    if (base_kind) *base_kind = _ti.base_kind;
    if (elem_size) *elem_size = _ti.elem_size;
    if (fp_kind) *fp_kind = _ti.fp_kind;
    if (tag_kind) *tag_kind = _ti.tag_kind;
    if (tag_name) *tag_name = _ti.tag_name;
    if (tag_len) *tag_len = _ti.tag_len;
    if (is_pointer_base) *is_pointer_base = _ti.is_pointer;
    if (type_state)
      type_state->base_decl_type = psx_ctx_typedef_decl_type(&_ti);
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
  stmt_decl_type_state_t type_state = {0};
  if (!parse_decl_type_spec(&elem_size, &fp_kind, &tag_kind, &tag_name, &tag_len,
                            &is_pointer_base, &base_kind, &type_state)) {
    diag_emit_tokf(DIAG_ERR_PARSER_TYPE_NAME_REQUIRED, curtok(), "%s",
                   diag_message_for(DIAG_ERR_PARSER_TYPE_NAME_REQUIRED));
  }
  int td_pointee_const = 0;
  int td_pointee_volatile = 0;
  td_pointee_const = type_state.type_spec.is_const_qualified ? 1 : 0;
  td_pointee_volatile = type_state.type_spec.is_volatile_qualified ? 1 : 0;
  int td_is_unsigned = (base_kind == TK_UNSIGNED) || type_state.type_spec.is_unsigned;

  for (;;) {
    int is_ptr = is_pointer_base;
    stmt_typedef_declarator_state_t decl_state = {0};
    token_ident_t *name = parse_typedef_name_decl(&decl_state, &is_ptr);
    psx_type_t *canonical_type = stmt_typedef_base_type(
        base_kind, elem_size, fp_kind, tag_kind, tag_name, tag_len,
        td_is_unsigned, type_state.type_spec.is_complex,
        type_state.base_decl_type);
    if (canonical_type && !type_state.base_decl_type) {
      if (td_pointee_const) canonical_type->is_const_qualified = 1;
      if (td_pointee_volatile) canonical_type->is_volatile_qualified = 1;
    }
    canonical_type = psx_type_apply_declarator_shape(
        canonical_type, &decl_state.declarator_shape);
    if (canonical_type && type_state.type_spec.is_long_double)
      canonical_type->is_long_double = 1;
    psx_apply_parsed_typedef_declaration(
        name->str, name->len, canonical_type, curtok());
    if (!tk_consume(',')) break;
  }
  tk_expect(';');
}

static int is_label_start_stmt(void) {
  return curtok()->kind == TK_IDENT && curtok()->next &&
         curtok()->next->kind == TK_COLON;
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
  int tag_path_saw_extern = 0;
  int tag_path_saw_const = 0;
  int tag_path_saw_volatile = 0;
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
      /* 修飾子を先に飲み込み、tag 経路専用の type-spec result として保持する。
       * `static struct T x;` をここで単に読み飛ばすと storage class を失い、
       * static struct/union 局所がグローバルへ lowering されず auto 扱いになる。 */
      while (psx_is_decl_prefix_token(curtok()->kind)) {
        if (curtok()->kind == TK_CONST) tag_path_saw_const = 1;
        if (curtok()->kind == TK_VOLATILE) tag_path_saw_volatile = 1;
        if (curtok()->kind == TK_STATIC) tag_path_saw_static = 1;
        if (curtok()->kind == TK_EXTERN) tag_path_saw_extern = 1;
        if (curtok()->kind == TK_ALIGNAS) {
          /* `_Alignas(N) struct T x;`: _Alignas トークンと続く `(N)` を正しく消費し、
           * 値を捕捉する。素朴に set_curtok(next) すると `(N)` が残り `struct` 検出前で
           * E3015 になっていた。値は後段の type-spec result に載せる。 */
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
    psx_skip_gnu_attributes();
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
      int tag_align = 0;
      member_count = psx_parse_tag_definition_body(tag_kind, tag_name, tag_len, &tag_size, &tag_align);
      psx_apply_parsed_tag_declaration(
          tag_kind, tag_name, tag_len, PSX_TAG_DECLARATION_DEFINITION,
          member_count, tag_size, tag_align, curtok());
      if (tk_consume(';')) {
        return psx_node_new_num(0);
      }
      while (curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE) {
        if (curtok()->kind == TK_CONST) tag_path_saw_const = 1;
        if (curtok()->kind == TK_VOLATILE) tag_path_saw_volatile = 1;
        set_curtok(curtok()->next);
      }
      psx_type_spec_result_t tag_type_spec = {0};
      tag_type_spec.kind = tag_kind;
      tag_type_spec.is_const_qualified = tag_path_saw_const ? 1 : 0;
      tag_type_spec.is_volatile_qualified = tag_path_saw_volatile ? 1 : 0;
      tag_type_spec.is_extern = tag_path_saw_extern ? 1 : 0;
      tag_type_spec.is_static = tag_path_saw_static ? 1 : 0;
      tag_type_spec.alignas_value = tag_path_alignas;
      return psx_decl_parse_declaration_after_type_ex(tag_size, TK_FLOAT_KIND_NONE, tag_kind, tag_name,
                                                      tag_len, 0, tag_path_saw_const,
                                                      tag_path_saw_volatile, 0,
                                                      &tag_type_spec,
                                                      NULL,
                                                      NULL,
                                                      0);
    }
    if (tk_consume(';')) {
      psx_apply_parsed_tag_declaration(
          tag_kind, tag_name, tag_len, PSX_TAG_DECLARATION_FORWARD,
          0, 0, 0, curtok());
      return psx_node_new_num(0);
    }
    psx_apply_parsed_tag_declaration(
        tag_kind, tag_name, tag_len, PSX_TAG_DECLARATION_REFERENCE,
        0, 0, 0, curtok());
    while (curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE) {
      if (curtok()->kind == TK_CONST) tag_path_saw_const = 1;
      if (curtok()->kind == TK_VOLATILE) tag_path_saw_volatile = 1;
      set_curtok(curtok()->next);
    }
    int tag_size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
    int tag_members = ps_ctx_get_tag_member_count(tag_kind, tag_name, tag_len);
    int elem_size = (tag_members > 0) ? (tag_size > 0 ? tag_size : 8) : 0;
    psx_type_spec_result_t tag_type_spec = {0};
    tag_type_spec.kind = tag_kind;
    tag_type_spec.is_const_qualified = tag_path_saw_const ? 1 : 0;
    tag_type_spec.is_volatile_qualified = tag_path_saw_volatile ? 1 : 0;
    tag_type_spec.is_extern = tag_path_saw_extern ? 1 : 0;
    tag_type_spec.is_static = tag_path_saw_static ? 1 : 0;
    tag_type_spec.alignas_value = tag_path_alignas;
    return psx_decl_parse_declaration_after_type_ex(elem_size,
                                                    TK_FLOAT_KIND_NONE, tag_kind, tag_name, tag_len, 0,
                                                    tag_path_saw_const, tag_path_saw_volatile, 0,
                                                    &tag_type_spec,
                                                    NULL,
                                                    NULL,
                                                    0);
  }

  return NULL;
}

static node_t *block_item(void) {
  if (is_label_start_stmt()) {
    return parse_stmt_label();
  }
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
  if (is_label_start_stmt()) return parse_stmt_label();
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
  while (!tk_consume('}')) {
    // #pragma pack マーカーはブロック内でも透過的に処理（AST には載せない）。
    if (psx_try_consume_pragma_pack_marker()) continue;
    if (i >= cap - 1) {
      cap = pda_next_cap(cap, i + 2);
      node->body = pda_xreallocarray(node->body, (size_t)cap, sizeof(node_t *));
    }
    token_t *stmt_tok = curtok();
    psx_lvar_usage_region_t *region = psx_decl_begin_lvar_usage_region();
    node->body[i] = block_item();
    psx_decl_end_lvar_usage_region(region);
    if (node->body[i]) {
      node->body[i]->tok = stmt_tok;
      node->body[i]->usage_region = region;
    }
    i++;
  }
  node->body[i] = NULL;
  psx_decl_leave_scope();
  psx_ctx_leave_block_scope();
  return (node_t *)node;
}

static int is_stmt_expr_value_stmt(node_t *s) {
  if (!s || s->kind == ND_NUM) return 0;
  switch (s->kind) {
    case ND_IF:
    case ND_WHILE:
    case ND_DO_WHILE:
    case ND_FOR:
    case ND_SWITCH:
    case ND_CASE:
    case ND_DEFAULT:
    case ND_BREAK:
    case ND_CONTINUE:
    case ND_GOTO:
    case ND_LABEL:
    case ND_RETURN:
    case ND_BLOCK:
      return 0;
    default:
      return 1;
  }
}

node_t *psx_parse_statement_expression(void) {
  tk_expect('(');
  node_t *block = parse_stmt_block();
  tk_expect(')');
  node_block_t *b = (node_block_t *)block;
  node_t *value = NULL;
  if (b->body) {
    for (int i = 0; b->body[i]; i++) {
      if (is_stmt_expr_value_stmt(b->body[i])) value = b->body[i];
    }
  }
  if (!value) value = psx_node_new_num(0);
  node_t *node = calloc(1, sizeof(node_t));
  node->kind = ND_STMT_EXPR;
  node->lhs = block;
  node->rhs = value;
  return node;
}

static node_t *parse_stmt_return(void) {
  token_t *return_tok = curtok();
  set_curtok(curtok()->next);
  node_t *node = arena_alloc(sizeof(node_t));
  node->kind = ND_RETURN;
  node->tok = return_tok;
  if (tk_consume(';')) {
    node->lhs = NULL;
    return node;
  }
  node->lhs = ps_expr();
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
  /* `if (cond);` のように `)` の直後に `;` が来たら空本体を警告
   * (clang -Wempty-body 相当)。 */
  if (curtok()->kind == TK_SEMI) node->base.has_empty_body = 1;
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
  node->base.rhs = stmt_internal();
  return (node_t *)node;
}

static node_t *parse_stmt_do_while(void) {
  set_curtok(curtok()->next);
  node_ctrl_t *node = arena_alloc(sizeof(node_ctrl_t));
  node->base.kind = ND_DO_WHILE;
  node->base.rhs = stmt_internal();
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
  node->base.rhs = stmt_internal();
  if (for_has_decl) psx_decl_leave_scope();
  return (node_t *)node;
}

static node_t *parse_stmt_switch(void) {
  token_t *switch_tok = curtok();
  set_curtok(curtok()->next);
  tk_expect('(');
  node_ctrl_t *node = arena_alloc(sizeof(node_ctrl_t));
  node->base.kind = ND_SWITCH;
  node->base.tok = switch_tok;
  node->base.lhs = ps_expr();
  tk_expect(')');
  node->base.rhs = stmt_internal();
  return (node_t *)node;
}

static node_t *parse_stmt_case(void) {
  token_t *case_tok = curtok();
  set_curtok(curtok()->next);
  node_case_t *node = arena_alloc(sizeof(node_case_t));
  node->base.kind = ND_CASE;
  node->base.tok = case_tok;
  node->val = psx_parse_case_const_expr();
  tk_expect(':');
  node->base.rhs = stmt_internal();
  return (node_t *)node;
}

static node_t *parse_stmt_default(void) {
  token_t *default_tok = curtok();
  set_curtok(curtok()->next);
  node_default_t *node = arena_alloc(sizeof(node_default_t));
  node->base.kind = ND_DEFAULT;
  node->base.tok = default_tok;
  tk_expect(':');
  node->base.rhs = stmt_internal();
  return (node_t *)node;
}

static node_t *parse_stmt_break(void) {
  set_curtok(curtok()->next);
  node_t *node = arena_alloc(sizeof(node_t));
  node->kind = ND_BREAK;
  tk_expect(';');
  return node;
}

static node_t *parse_stmt_continue(void) {
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
