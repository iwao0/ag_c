#include "parser.h"
#include "internal/node_utils.h"
#include "internal/semantic_ctx.h"
#include "internal/decl.h"
#include "internal/diag.h"
#include "internal/dynarray.h"
#include "internal/expr.h"
#include "internal/loop_ctx.h"
#include "internal/stmt.h"
#include "internal/switch_ctx.h"
#include "../tokenizer/tokenizer.h"
#include <stdio.h>
#include <stdlib.h>

string_lit_t *string_literals = NULL;
float_lit_t *float_literals = NULL;

static node_t *funcdef(void);
static void parse_toplevel_decl_after_type(void);
static void parse_toplevel_tag_decl(void);
static void parse_toplevel_typedef_decl(void);
static int is_toplevel_function_signature(token_t *tok);
static int parse_tag_definition_body_toplevel(token_kind_t tag_kind, char *tag_name, int tag_len, int *out_size);

// program = funcdef*
node_t **ps_program(void) {
  int cap = 16;
  node_t **codes = calloc(cap, sizeof(node_t*));
  int i = 0;
  while (!tk_at_eof()) {
    if (psx_ctx_is_tag_keyword(token->kind)) {
      parse_toplevel_tag_decl();
      continue;
    }
    if (token->kind == TK_TYPEDEF) {
      parse_toplevel_typedef_decl();
      continue;
    }
    if ((psx_ctx_is_type_token(token->kind) || psx_ctx_is_typedef_name_token(token)) && !is_toplevel_function_signature(token)) {
      if (psx_ctx_is_typedef_name_token(token)) {
        token = token->next;
      } else {
        token = token->next; // base type
      }
      parse_toplevel_decl_after_type();
      continue;
    }
    node_t *fn = funcdef();
    if (!fn) continue; // 関数プロトタイプ宣言はASTへ載せない
    if (i >= cap - 1) { // NULL終端用
      cap = pda_next_cap(cap, i + 2);
      codes = pda_xreallocarray(codes, (size_t)cap, sizeof(node_t *));
    }
    codes[i++] = fn;
  }
  codes[i] = NULL;
  return codes;
}

static int is_toplevel_function_signature(token_t *tok) {
  if (!tok || (!psx_ctx_is_type_token(tok->kind) && !psx_ctx_is_typedef_name_token(tok))) return 0;
  token_t *t = tok->next;
  while (t && t->kind == TK_MUL) t = t->next;
  if (!t || t->kind != TK_IDENT) return 0;
  return t->next && t->next->kind == TK_LPAREN;
}

static void parse_toplevel_declarator_list(void) {
  for (;;) {
    while (tk_consume('*')) {}
    token_ident_t *name = tk_consume_ident();
    if (!name) psx_diag_ctx(token, "decl", "変数名が期待されます");
    if (tk_consume('[')) {
      tk_expect_number();
      tk_expect(']');
    }
    if (tk_consume('=')) {
      psx_expr_assign();
    }
    if (!tk_consume(',')) break;
  }
}

static void parse_toplevel_decl_after_type(void) {
  parse_toplevel_declarator_list();
  tk_expect(';');
}

static void parse_toplevel_typedef_decl(void) {
  if (token->kind != TK_TYPEDEF) psx_diag_ctx(token, "typedef", "'typedef' が必要です");
  token = token->next;
  token_kind_t base_kind = TK_EOF;
  int elem_size = 8;
  tk_float_kind_t fp_kind = TK_FLOAT_KIND_NONE;
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int is_ptr_base = 0;

  if (psx_ctx_is_type_token(token->kind)) {
    base_kind = token->kind;
    psx_ctx_get_type_info(token->kind, NULL, &elem_size);
    if (token->kind == TK_FLOAT) fp_kind = TK_FLOAT_KIND_FLOAT;
    else if (token->kind == TK_DOUBLE) fp_kind = TK_FLOAT_KIND_DOUBLE;
    token = token->next;
  } else if (psx_ctx_is_tag_keyword(token->kind)) {
    base_kind = token->kind;
    tag_kind = token->kind;
    token = token->next;
    token_ident_t *tag = tk_consume_ident();
    if (!tag) psx_diag_missing(token, "タグ名");
    tag_name = tag->str;
    tag_len = tag->len;
    if (tk_consume('{')) {
      int member_count = 0;
      int tag_size = 0;
      member_count = parse_tag_definition_body_toplevel(tag_kind, tag_name, tag_len, &tag_size);
      psx_ctx_define_tag_type_with_layout(tag_kind, tag_name, tag_len, member_count, tag_size);
    } else if (!psx_ctx_has_tag_type(tag_kind, tag_name, tag_len)) {
      psx_diag_undefined_with_name(token, "のタグ型", tag_name, tag_len);
    }
    elem_size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
  } else if (psx_ctx_is_typedef_name_token(token)) {
    token_ident_t *id = (token_ident_t *)token;
    psx_ctx_find_typedef_name(id->str, id->len, &base_kind, &elem_size, &fp_kind, &tag_kind, &tag_name, &tag_len, &is_ptr_base);
    token = token->next;
  } else {
    psx_diag_ctx(token, "typedef", "型名が必要です");
  }

  for (;;) {
    int is_ptr = is_ptr_base;
    while (tk_consume('*')) is_ptr = 1;
    token_ident_t *name = tk_consume_ident();
    if (!name) psx_diag_ctx(token, "typedef", "typedef名が必要です");
    if (tk_consume('[')) {
      tk_expect_number();
      tk_expect(']');
    }
    psx_ctx_define_typedef_name(name->str, name->len, base_kind, elem_size, fp_kind, tag_kind, tag_name, tag_len, is_ptr);
    if (!tk_consume(',')) break;
  }
  tk_expect(';');
}

static int parse_struct_or_union_members_layout_toplevel(token_kind_t tag_kind, char *tag_name, int tag_len, int *out_size) {
  int member_count = 0;
  int current_off = 0;
  int union_size = 0;
  while (!tk_consume('}')) {
    int elem_size = 8;
    token_kind_t member_tag_kind = TK_EOF;
    char *member_tag_name = NULL;
    int member_tag_len = 0;
    if (psx_ctx_is_type_token(token->kind)) {
      psx_ctx_get_type_info(token->kind, NULL, &elem_size);
      token = token->next;
    } else if (psx_ctx_is_tag_keyword(token->kind)) {
      member_tag_kind = token->kind;
      token = token->next;
      token_ident_t *nested_tag = tk_consume_ident();
      if (!nested_tag) psx_diag_missing(token, "タグ名");
      member_tag_name = nested_tag->str;
      member_tag_len = nested_tag->len;
      if (tk_consume('{')) {
        int nested_n = 0;
        int nested_sz = 0;
        nested_n = parse_tag_definition_body_toplevel(member_tag_kind, member_tag_name, member_tag_len, &nested_sz);
        psx_ctx_define_tag_type_with_layout(member_tag_kind, member_tag_name, member_tag_len, nested_n, nested_sz);
      } else if (!psx_ctx_has_tag_type(member_tag_kind, member_tag_name, member_tag_len)) {
        psx_diag_undefined_with_name(token, "のタグ型", member_tag_name, member_tag_len);
      }
      elem_size = psx_ctx_get_tag_size(member_tag_kind, member_tag_name, member_tag_len);
      if (elem_size <= 0) psx_diag_ctx(token, "decl", "不完全型のメンバは定義できません");
    } else {
      psx_diag_ctx(token, "decl", "メンバ型が期待されます");
    }

    for (;;) {
      int is_ptr = 0;
      while (tk_consume('*')) is_ptr = 1;
      token_ident_t *member = tk_consume_ident();
      if (!member) psx_diag_missing(token, "メンバ名");
      int arr_size = 1;
      if (tk_consume('[')) {
        arr_size = tk_expect_number();
        tk_expect(']');
      }
      int total_size = is_ptr ? 8 : elem_size * arr_size;
      int deref_size = is_ptr ? elem_size : 0;
      int off = (tag_kind == TK_UNION) ? 0 : current_off;
      psx_ctx_add_tag_member(tag_kind, tag_name, tag_len,
                             member->str, member->len, off, is_ptr ? 8 : elem_size, deref_size,
                             member_tag_kind, member_tag_name, member_tag_len, is_ptr ? 1 : 0);
      member_count++;
      if (tag_kind == TK_UNION) {
        if (total_size > union_size) union_size = total_size;
      } else {
        current_off += total_size;
      }
      if (!tk_consume(',')) break;
    }
    tk_expect(';');
  }
  *out_size = (tag_kind == TK_UNION) ? union_size : current_off;
  return member_count;
}

static int parse_enum_members_toplevel(void) {
  int member_count = 0;
  long long next_value = 0;
  while (!tk_consume('}')) {
    token_ident_t *enumerator = tk_consume_ident();
    if (!enumerator) psx_diag_missing(token, "列挙子名");
    long long value = next_value;
    member_count++;
    if (tk_consume('=')) {
      value = tk_expect_number();
    }
    psx_ctx_define_enum_const(enumerator->str, enumerator->len, value);
    next_value = value + 1;
    if (tk_consume('}')) break;
    tk_expect(',');
    if (tk_consume('}')) break;
  }
  return member_count;
}

static int parse_tag_definition_body_toplevel(token_kind_t tag_kind, char *tag_name, int tag_len, int *out_size) {
  if (tag_kind == TK_ENUM) {
    if (out_size) *out_size = 4;
    return parse_enum_members_toplevel();
  }
  return parse_struct_or_union_members_layout_toplevel(tag_kind, tag_name, tag_len, out_size);
}

static void parse_toplevel_tag_decl(void) {
  token_kind_t tag_kind = token->kind;
  token = token->next;
  token_ident_t *tag = tk_consume_ident();
  if (!tag) psx_diag_missing(token, "タグ名");

  if (tk_consume('{')) {
    int member_count = 0;
    int tag_size = 0;
    member_count = parse_tag_definition_body_toplevel(tag_kind, tag->str, tag->len, &tag_size);
    psx_ctx_define_tag_type_with_layout(tag_kind, tag->str, tag->len, member_count, tag_size);
    if (tk_consume(';')) return;
    parse_toplevel_declarator_list();
    tk_expect(';');
    return;
  }
  if (tk_consume(';')) {
    psx_ctx_define_tag_type(tag_kind, tag->str, tag->len);
    return;
  }
  if (!psx_ctx_has_tag_type(tag_kind, tag->str, tag->len)) {
    psx_diag_undefined_with_name(token, "のタグ型", tag->str, tag->len);
  }
  parse_toplevel_declarator_list();
  tk_expect(';');
}

// consume_type: 型キーワードがあれば読み進め、そのトークン種別を返す（0=型なし）
token_kind_t psx_consume_type_kind(void) {
  if (token->kind == TK_SIGNED || token->kind == TK_UNSIGNED) {
    token_kind_t sign_kind = token->kind;
    token = token->next;
    if (token->kind == TK_LONG) {
      token = token->next;
      if (token->kind == TK_INT) token = token->next;
      (void)sign_kind;
      return TK_LONG;
    }
    if (token->kind == TK_SHORT) {
      token = token->next;
      if (token->kind == TK_INT) token = token->next;
      (void)sign_kind;
      return TK_SHORT;
    }
    if (token->kind == TK_INT) token = token->next;
    return TK_INT;
  }
  if (token->kind == TK_LONG) {
    token = token->next;
    if (token->kind == TK_INT) token = token->next;
    return TK_LONG;
  }
  if (token->kind == TK_SHORT) {
    token = token->next;
    if (token->kind == TK_INT) token = token->next;
    return TK_SHORT;
  }
  if (token->kind == TK_INT || token->kind == TK_CHAR || token->kind == TK_VOID ||
      token->kind == TK_FLOAT || token->kind == TK_DOUBLE || token->kind == TK_BOOL) {
    token_kind_t kind = token->kind;
    token = token->next;
    return kind;
  }
  return TK_EOF; // 型なし
}

static bool consume_type(void) {
  if (psx_consume_type_kind() != TK_EOF) return true;
  if (psx_ctx_is_typedef_name_token(token)) {
    token = token->next;
    return true;
  }
  return false;
}

// funcdef = "int"? ident "(" params? ")" (";" | "{" stmt* "}")
// params  = "int"? ident ("," "int"? ident)*
static node_t *funcdef(void) {
  token_kind_t ret_kind = psx_consume_type_kind(); // 戻り値の型（省略可）
  token_kind_t ret_token_kind = (ret_kind == TK_EOF) ? TK_INT : ret_kind;
  tk_float_kind_t ret_fp_kind = TK_FLOAT_KIND_NONE;
  if (ret_kind == TK_FLOAT) ret_fp_kind = TK_FLOAT_KIND_FLOAT;
  else if (ret_kind == TK_DOUBLE) ret_fp_kind = TK_FLOAT_KIND_DOUBLE;
  psx_expr_set_current_func_ret_type(ret_token_kind, ret_fp_kind);
  token_ident_t *tok = tk_consume_ident();
  if (!tok) {
    psx_diag_ctx(token, "funcdef", "関数定義が期待されます");
  }
  node_func_t *node = calloc(1, sizeof(node_func_t));
  node->base.kind = ND_FUNCDEF;
  node->funcname = tok->str;
  node->funcname_len = tok->len;

  // 関数ごとにローカル変数テーブルをリセット
  psx_decl_reset_locals();
  psx_ctx_reset_function_scope();
  psx_loop_reset();

  tk_expect('(');
  // 仮引数のパース
  int arg_cap = 16;
  node->args = calloc(arg_cap, sizeof(node_t*));
  int nargs = 0;
  if (!tk_consume(')')) {
    consume_type(); // 仮引数の型
    while (tk_consume('*')) {} // ポインタの * を読み飛ばす
    token_ident_t *param = tk_consume_ident();
    if (param) {
      lvar_t *var = psx_decl_register_lvar(param->str, param->len);
      node->args[nargs++] = psx_node_new_lvar(var->offset);
    }
    while (tk_consume(',')) {
      if (nargs >= arg_cap) {
        arg_cap = pda_next_cap(arg_cap, nargs + 1);
        node->args = pda_xreallocarray(node->args, (size_t)arg_cap, sizeof(node_t *));
      }
      consume_type(); // 仮引数の型
      while (tk_consume('*')) {} // ポインタの * を読み飛ばす
      param = tk_consume_ident();
      if (param) {
        lvar_t *var = psx_decl_register_lvar(param->str, param->len);
        node->args[nargs++] = psx_node_new_lvar(var->offset);
      }
    }
    tk_expect(')');
  }
  node->nargs = nargs;

  // 関数プロトタイプ宣言（本体なし）
  if (tk_consume(';')) {
    return NULL;
  }

  // 関数本体 (ブロック)
  tk_expect('{');
  psx_ctx_enter_block_scope();
  node_block_t *body = calloc(1, sizeof(node_block_t));
  body->base.kind = ND_BLOCK;
  int i = 0;
  int body_cap = 16;
  body->body = calloc(body_cap, sizeof(node_t*));
  while (!tk_consume('}')) {
    if (i >= body_cap - 1) {
      body_cap = pda_next_cap(body_cap, i + 2);
      body->body = pda_xreallocarray(body->body, (size_t)body_cap, sizeof(node_t *));
    }
    body->body[i++] = psx_stmt_stmt();
  }
  body->body[i] = NULL;
  psx_ctx_leave_block_scope();
  node->base.rhs = (node_t *)body;
  psx_ctx_validate_goto_refs();

  return (node_t *)node;
}

// expr = assign ("," assign)*
node_t *ps_expr(void) {
  node_t *node = psx_expr_expr();
  return node;
}
