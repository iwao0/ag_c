#include "internal/stmt.h"
#include "internal/decl.h"
#include "internal/diag.h"
#include "internal/dynarray.h"
#include "internal/expr.h"
#include "internal/loop_ctx.h"
#include "internal/node_utils.h"
#include "internal/semantic_ctx.h"
#include "internal/switch_ctx.h"
#include "../tokenizer/tokenizer.h"
#include <stdlib.h>

node_t *ps_expr(void);

static int parse_tag_definition_body(token_kind_t tag_kind, char *tag_name, int tag_len, int *out_size);
static void parse_typedef_decl(void);
static int parse_decl_type_spec(int *elem_size, tk_float_kind_t *fp_kind,
                                token_kind_t *tag_kind, char **tag_name, int *tag_len,
                                int *is_pointer_base, token_kind_t *base_kind);
static token_ident_t *parse_typedef_name_decl(int *is_ptr);
static long long parse_enum_const_expr(void);
static long long parse_enum_const_add(void);
static long long parse_enum_const_mul(void);
static long long parse_enum_const_unary(void);
static long long parse_enum_const_primary(void);

static void skip_cv_qualifiers_stmt(void) {
  while (token->kind == TK_CONST || token->kind == TK_VOLATILE) token = token->next;
}

static void skip_func_params_stmt(void) {
  if (!tk_consume('(')) return;
  int depth = 1;
  while (depth > 0) {
    if (token->kind == TK_EOF) psx_diag_ctx(token, "typedef", "関数宣言子の ')' が不足しています");
    if (token->kind == TK_LPAREN) depth++;
    else if (token->kind == TK_RPAREN) depth--;
    token = token->next;
  }
}

static token_ident_t *parse_typedef_name_decl(int *is_ptr) {
  int open_parens = 0;
  while (tk_consume('(')) open_parens++;
  while (tk_consume('*')) *is_ptr = 1;
  token_ident_t *name = tk_consume_ident();
  if (!name) psx_diag_ctx(token, "typedef", "typedef名が必要です");
  while (open_parens-- > 0) tk_expect(')');
  while (token->kind == TK_LPAREN) {
    skip_func_params_stmt();
  }
  return name;
}

static int parse_struct_or_union_members_layout(token_kind_t tag_kind, char *tag_name, int tag_len, int *out_size) {
  int member_count = 0;
  int current_off = 0;
  int union_size = 0;
  int agg_align = 1;
  #define ALIGN_UP(v, a) (((v) + ((a) - 1)) / (a) * (a))
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
        nested_n = parse_tag_definition_body(member_tag_kind, member_tag_name, member_tag_len, &nested_sz);
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
      int member_align = is_ptr ? 8 : elem_size;
      if (member_align <= 0) member_align = 1;
      if (member_align > 8) member_align = 8;
      if (member_align > agg_align) agg_align = member_align;
      int off = 0;
      if (tag_kind == TK_UNION) {
        off = 0;
      } else {
        current_off = ALIGN_UP(current_off, member_align);
        off = current_off;
      }
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
  *out_size = (tag_kind == TK_UNION) ? ALIGN_UP(union_size, agg_align) : ALIGN_UP(current_off, agg_align);
  #undef ALIGN_UP
  return member_count;
}

static long long parse_enum_const_expr(void) { return parse_enum_const_add(); }

static long long parse_enum_const_add(void) {
  long long v = parse_enum_const_mul();
  while (token->kind == TK_PLUS || token->kind == TK_MINUS) {
    token_kind_t op = token->kind;
    token = token->next;
    long long r = parse_enum_const_mul();
    v = (op == TK_PLUS) ? (v + r) : (v - r);
  }
  return v;
}

static long long parse_enum_const_mul(void) {
  long long v = parse_enum_const_unary();
  while (token->kind == TK_MUL || token->kind == TK_DIV || token->kind == TK_MOD) {
    token_kind_t op = token->kind;
    token = token->next;
    long long r = parse_enum_const_unary();
    if (op == TK_MUL) v *= r;
    else if (op == TK_DIV) v /= r;
    else v %= r;
  }
  return v;
}

static long long parse_enum_const_unary(void) {
  if (token->kind == TK_PLUS) {
    token = token->next;
    return parse_enum_const_unary();
  }
  if (token->kind == TK_MINUS) {
    token = token->next;
    return -parse_enum_const_unary();
  }
  return parse_enum_const_primary();
}

static long long parse_enum_const_primary(void) {
  if (token->kind == TK_LPAREN) {
    token = token->next;
    long long v = parse_enum_const_expr();
    tk_expect(')');
    return v;
  }
  token_ident_t *id = tk_consume_ident();
  if (id) {
    long long v = 0;
    if (!psx_ctx_find_enum_const(id->str, id->len, &v)) {
      psx_diag_ctx(token, "enum", "未定義の列挙子 '%.*s' です", id->len, id->str);
    }
    return v;
  }
  return tk_expect_number();
}

static int parse_enum_members(void) {
  int member_count = 0;
  long long next_value = 0;
  while (!tk_consume('}')) {
    token_ident_t *enumerator = tk_consume_ident();
    if (!enumerator) {
      psx_diag_missing(token, "列挙子名");
    }
    long long value = next_value;
    if (tk_consume('=')) {
      value = parse_enum_const_expr();
    }
    psx_ctx_define_enum_const(enumerator->str, enumerator->len, value);
    next_value = value + 1;
    member_count++;
    if (tk_consume('}')) break;
    tk_expect(',');
    if (tk_consume('}')) break;
  }
  return member_count;
}

static int parse_tag_definition_body(token_kind_t tag_kind, char *tag_name, int tag_len, int *out_size) {
  if (tag_kind == TK_ENUM) {
    if (out_size) *out_size = 4;
    return parse_enum_members();
  }
  return parse_struct_or_union_members_layout(tag_kind, tag_name, tag_len, out_size);
}

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

  skip_cv_qualifiers_stmt();
  if (psx_ctx_is_type_token(token->kind)) {
    *base_kind = token->kind;
    psx_ctx_get_type_info(token->kind, NULL, elem_size);
    if (token->kind == TK_FLOAT) *fp_kind = TK_FLOAT_KIND_FLOAT;
    else if (token->kind == TK_DOUBLE) *fp_kind = TK_FLOAT_KIND_DOUBLE;
    token = token->next;
    return 1;
  }
  if (psx_ctx_is_tag_keyword(token->kind)) {
    *base_kind = token->kind;
    *tag_kind = token->kind;
    token = token->next;
    token_ident_t *tag = tk_consume_ident();
    if (!tag) psx_diag_missing(token, "タグ名");
    *tag_name = tag->str;
    *tag_len = tag->len;
    if (tk_consume('{')) {
      int member_count = 0;
      int tag_size = 0;
      member_count = parse_tag_definition_body(*tag_kind, *tag_name, *tag_len, &tag_size);
      psx_ctx_define_tag_type_with_layout(*tag_kind, *tag_name, *tag_len, member_count, tag_size);
    } else if (!psx_ctx_has_tag_type(*tag_kind, *tag_name, *tag_len)) {
      psx_diag_undefined_with_name(token, "のタグ型", *tag_name, *tag_len);
    }
    *elem_size = psx_ctx_get_tag_size(*tag_kind, *tag_name, *tag_len);
    return 1;
  }
  if (psx_ctx_is_typedef_name_token(token)) {
    token_ident_t *id = (token_ident_t *)token;
    if (!psx_ctx_find_typedef_name(id->str, id->len, base_kind, elem_size, fp_kind, tag_kind, tag_name, tag_len, is_pointer_base)) {
      return 0;
    }
    token = token->next;
    return 1;
  }
  return 0;
}

static void parse_typedef_decl(void) {
  if (token->kind != TK_TYPEDEF) psx_diag_ctx(token, "typedef", "'typedef' が必要です");
  token = token->next;
  int elem_size = 8;
  tk_float_kind_t fp_kind = TK_FLOAT_KIND_NONE;
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  int is_pointer_base = 0;
  token_kind_t base_kind = TK_EOF;
  if (!parse_decl_type_spec(&elem_size, &fp_kind, &tag_kind, &tag_name, &tag_len, &is_pointer_base, &base_kind)) {
    psx_diag_ctx(token, "typedef", "型名が必要です");
  }
  for (;;) {
    int is_ptr = is_pointer_base;
    while (tk_consume('*')) is_ptr = 1;
    token_ident_t *name = parse_typedef_name_decl(&is_ptr);
    if (tk_consume('[')) {
      tk_expect_number();
      tk_expect(']');
    }
    psx_ctx_define_typedef_name(name->str, name->len, base_kind, elem_size, fp_kind, tag_kind, tag_name, tag_len, is_ptr);
    if (!tk_consume(',')) break;
  }
  tk_expect(';');
}

static node_t *stmt_internal(void) {
  if (tk_consume('{')) {
    psx_ctx_enter_block_scope();
    node_block_t *node = calloc(1, sizeof(node_block_t));
    node->base.kind = ND_BLOCK;
    int i = 0;
    int cap = 16;
    node->body = calloc(cap, sizeof(node_t*));
    while (!tk_consume('}')) {
      if (i >= cap - 1) {
        cap = pda_next_cap(cap, i + 2);
        node->body = pda_xreallocarray(node->body, (size_t)cap, sizeof(node_t *));
      }
      node->body[i++] = stmt_internal();
    }
    node->body[i] = NULL;
    psx_ctx_leave_block_scope();
    return (node_t *)node;
  }

  if (token->kind == TK_TYPEDEF) {
    parse_typedef_decl();
    return psx_node_new_num(0);
  }

  if (psx_ctx_is_type_token(token->kind) || token->kind == TK_CONST || token->kind == TK_VOLATILE || psx_ctx_is_typedef_name_token(token)) {
    if (psx_ctx_is_typedef_name_token(token)) {
      token_ident_t *id = (token_ident_t *)token;
      int elem_size = 8;
      tk_float_kind_t fp_kind = TK_FLOAT_KIND_NONE;
      token_kind_t tag_kind = TK_EOF;
      char *tag_name = NULL;
      int tag_len = 0;
      int is_ptr = 0;
      token_kind_t base_kind = TK_EOF;
      psx_ctx_find_typedef_name(id->str, id->len, &base_kind, &elem_size, &fp_kind, &tag_kind, &tag_name, &tag_len, &is_ptr);
      token = token->next;
      return psx_decl_parse_declaration_after_type(elem_size, fp_kind, tag_kind, tag_name, tag_len, is_ptr);
    }
    return psx_decl_parse_declaration();
  }

  if (psx_ctx_is_tag_keyword(token->kind)) {
    token_kind_t tag_kind = token->kind;
    token = token->next;
    token_ident_t *tag = tk_consume_ident();
    if (!tag) {
      psx_diag_missing(token, "タグ名");
    }
    if (tk_consume('{')) {
      int member_count = 0;
      int tag_size = 0;
      member_count = parse_tag_definition_body(tag_kind, tag->str, tag->len, &tag_size);
      psx_ctx_define_tag_type_with_layout(tag_kind, tag->str, tag->len, member_count, tag_size);
      if (tk_consume(';')) {
        return psx_node_new_num(0);
      }
      return psx_decl_parse_declaration_after_type(tag_size, TK_FLOAT_KIND_NONE, tag_kind, tag->str, tag->len, 0);
    }
    if (tk_consume(';')) {
      psx_ctx_define_tag_type(tag_kind, tag->str, tag->len);
      return psx_node_new_num(0);
    }
    if (!psx_ctx_has_tag_type(tag_kind, tag->str, tag->len)) {
      psx_diag_undefined_with_name(token, "のタグ型", tag->str, tag->len);
    }
    int tag_size = psx_ctx_get_tag_size(tag_kind, tag->str, tag->len);
    return psx_decl_parse_declaration_after_type(tag_size > 0 ? tag_size : 8,
                                                 TK_FLOAT_KIND_NONE, tag_kind, tag->str, tag->len, 0);
  }

  if (token->kind == TK_RETURN) {
    token = token->next;
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_RETURN;
    if (tk_consume(';')) {
      if (psx_expr_current_func_ret_token_kind() != TK_VOID) {
        tk_error_tok(token, "void 以外の関数では return に式が必要です");
      }
      node->lhs = NULL;
      node->fp_kind = psx_expr_current_func_ret_fp_kind();
      return node;
    }
    node->lhs = ps_expr();
    if (psx_expr_current_func_ret_token_kind() == TK_VOID) {
      tk_error_tok(token, "void 関数では return に式を指定できません");
    }
    node->fp_kind = psx_expr_current_func_ret_fp_kind();
    tk_expect(';');
    return node;
  }

  if (token->kind == TK_IF) {
    token = token->next;
    tk_expect('(');
    node_ctrl_t *node = calloc(1, sizeof(node_ctrl_t));
    node->base.kind = ND_IF;
    node->base.lhs = ps_expr();
    tk_expect(')');
    node->base.rhs = stmt_internal();
    if (token->kind == TK_ELSE) {
      token = token->next;
      node->els = stmt_internal();
    }
    return (node_t *)node;
  }

  if (token->kind == TK_WHILE) {
    token = token->next;
    tk_expect('(');
    node_ctrl_t *node = calloc(1, sizeof(node_ctrl_t));
    node->base.kind = ND_WHILE;
    node->base.lhs = ps_expr();
    tk_expect(')');
    psx_loop_enter();
    node->base.rhs = stmt_internal();
    psx_loop_leave();
    return (node_t *)node;
  }

  if (token->kind == TK_DO) {
    token = token->next;
    node_ctrl_t *node = calloc(1, sizeof(node_ctrl_t));
    node->base.kind = ND_DO_WHILE;
    psx_loop_enter();
    node->base.rhs = stmt_internal();
    psx_loop_leave();
    if (token->kind != TK_WHILE) {
      psx_diag_missing(token, "'while'");
    }
    token = token->next;
    tk_expect('(');
    node->base.lhs = ps_expr();
    tk_expect(')');
    tk_expect(';');
    return (node_t *)node;
  }

  if (token->kind == TK_FOR) {
    token = token->next;
    tk_expect('(');
    node_ctrl_t *node = calloc(1, sizeof(node_ctrl_t));
    node->base.kind = ND_FOR;
    if (!tk_consume(';')) {
      if (psx_ctx_is_type_token(token->kind) || token->kind == TK_CONST || token->kind == TK_VOLATILE || psx_ctx_is_typedef_name_token(token)) {
        if (psx_ctx_is_typedef_name_token(token)) {
          token_ident_t *id = (token_ident_t *)token;
          int elem_size = 8;
          tk_float_kind_t fp_kind = TK_FLOAT_KIND_NONE;
          token_kind_t tag_kind = TK_EOF;
          char *tag_name = NULL;
          int tag_len = 0;
          int is_ptr = 0;
          token_kind_t base_kind = TK_EOF;
          psx_ctx_find_typedef_name(id->str, id->len, &base_kind, &elem_size, &fp_kind, &tag_kind, &tag_name, &tag_len, &is_ptr);
          token = token->next;
          node->init = psx_decl_parse_declaration_after_type(elem_size, fp_kind, tag_kind, tag_name, tag_len, is_ptr);
        } else {
          node->init = psx_decl_parse_declaration();
        }
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
    return (node_t *)node;
  }

  if (token->kind == TK_SWITCH) {
    token = token->next;
    tk_expect('(');
    node_ctrl_t *node = calloc(1, sizeof(node_ctrl_t));
    node->base.kind = ND_SWITCH;
    node->base.lhs = ps_expr();
    tk_expect(')');
    psx_switch_push_ctx();
    node->base.rhs = stmt_internal();
    psx_switch_pop_ctx();
    return (node_t *)node;
  }

  if (token->kind == TK_CASE) {
    token = token->next;
    node_case_t *node = calloc(1, sizeof(node_case_t));
    node->base.kind = ND_CASE;
    node->val = tk_expect_number();
    psx_switch_register_case(node->val, token);
    tk_expect(':');
    node->base.rhs = stmt_internal();
    return (node_t *)node;
  }

  if (token->kind == TK_DEFAULT) {
    token = token->next;
    psx_switch_register_default(token);
    node_default_t *node = calloc(1, sizeof(node_default_t));
    node->base.kind = ND_DEFAULT;
    tk_expect(':');
    node->base.rhs = stmt_internal();
    return (node_t *)node;
  }

  if (token->kind == TK_BREAK) {
    if (psx_loop_depth() == 0 && !psx_switch_has_ctx()) {
      psx_diag_only_in(token, "break", "ループまたはswitch内");
    }
    token = token->next;
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_BREAK;
    tk_expect(';');
    return node;
  }

  if (token->kind == TK_CONTINUE) {
    if (psx_loop_depth() == 0) {
      psx_diag_only_in(token, "continue", "ループ内");
    }
    token = token->next;
    node_t *node = calloc(1, sizeof(node_t));
    node->kind = ND_CONTINUE;
    tk_expect(';');
    return node;
  }

  if (token->kind == TK_GOTO) {
    token_t *goto_tok = token;
    token = token->next;
    token_ident_t *ident = tk_consume_ident();
    if (!ident) {
      psx_diag_missing(token, "goto の後のラベル名");
    }
    node_jump_t *node = calloc(1, sizeof(node_jump_t));
    node->base.kind = ND_GOTO;
    node->name = ident->str;
    node->name_len = ident->len;
    psx_ctx_register_goto_ref(ident->str, ident->len, goto_tok);
    tk_expect(';');
    return (node_t *)node;
  }

  if (token->kind == TK_IDENT && token->next && token->next->kind == TK_COLON) {
    token_ident_t *ident = tk_consume_ident();
    tk_expect(':');
    node_jump_t *node = calloc(1, sizeof(node_jump_t));
    node->base.kind = ND_LABEL;
    node->name = ident->str;
    node->name_len = ident->len;
    psx_ctx_register_label_def(ident->str, ident->len, token);
    node->base.rhs = stmt_internal();
    return (node_t *)node;
  }

  node_t *node = ps_expr();
  tk_expect(';');
  return node;
}

node_t *psx_stmt_stmt(void) {
  return stmt_internal();
}
