#include "internal/stmt.h"
#include "internal/alignas_value.h"
#include "internal/arena.h"
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

static int parse_tag_definition_body(token_kind_t tag_kind, char *tag_name, int tag_len, int *out_size);
static void parse_typedef_decl(void);
static int parse_decl_type_spec(int *elem_size, tk_float_kind_t *fp_kind,
                                token_kind_t *tag_kind, char **tag_name, int *tag_len,
                                int *is_pointer_base, token_kind_t *base_kind);
static token_ident_t *parse_typedef_name_decl(int *is_ptr);
static token_ident_t *parse_typedef_name_decl_recursive(int *is_ptr);
static token_ident_t *parse_member_decl_name_recursive_stmt(int *is_ptr, int *out_has_func_suffix,
                                                            int *out_paren_array_mul);
static void consume_stmt_member_func_suffixes(int *out_has_func_suffix);
static member_decl_head_t parse_stmt_member_decl_head(void);
static int parse_array_size_constexpr_stmt(void);
static int parse_array_size_optional_constexpr_stmt(int *out_has_size);
typedef struct {
  int arr_total;
  int is_array;
  int has_incomplete_array;
} stmt_array_suffix_t;
static stmt_array_suffix_t parse_stmt_array_suffixes(int base_mul);
static int parse_stmt_array_suffixes_constexpr_required(int base_mul);
static int parse_stmt_member_array_suffixes(int *out_is_flex_array);
static void make_anonymous_tag_name_stmt(char **out_name, int *out_len);
static node_t *stmt_internal(void);
static node_t *block_item(void);
static int is_decl_like_start_stmt(void);
static node_t *parse_decl_like_stmt(void);
static int anonymous_tag_seq_stmt = 0;

static bool is_decl_prefix_token_stmt(token_kind_t k) {
  return k == TK_CONST || k == TK_VOLATILE || k == TK_EXTERN || k == TK_STATIC ||
         k == TK_AUTO || k == TK_REGISTER || k == TK_INLINE || k == TK_NORETURN ||
         k == TK_THREAD_LOCAL || k == TK_ALIGNAS || k == TK_ATOMIC;
}

static void make_anonymous_tag_name_stmt(char **out_name, int *out_len) {
  int seq = anonymous_tag_seq_stmt++;
  int len = snprintf(NULL, 0, "__anon_tag_stmt_%d", seq);
  char *name = calloc((size_t)len + 1, 1);
  snprintf(name, (size_t)len + 1, "__anon_tag_stmt_%d", seq);
  *out_name = name;
  *out_len = len;
}

static void skip_func_params_stmt(void) {
  if (!tk_consume('(')) return;
  int depth = 1;
  while (depth > 0) {
    if (curtok()->kind == TK_EOF) {
      diag_emit_tokf(DIAG_ERR_PARSER_MISSING_FUNC_DECL_RPAREN, curtok(), "%s",
                     diag_message_for(DIAG_ERR_PARSER_MISSING_FUNC_DECL_RPAREN));
    }
    if (curtok()->kind == TK_LPAREN) depth++;
    else if (curtok()->kind == TK_RPAREN) depth--;
    set_curtok(curtok()->next);
  }
}

static token_ident_t *parse_typedef_name_decl_recursive(int *is_ptr) {
  psx_consume_pointer_prefix(is_ptr);
  token_ident_t *name = NULL;
  if (tk_consume('(')) {
    name = parse_typedef_name_decl_recursive(is_ptr);
    tk_expect(')');
  } else {
    name = tk_consume_ident();
  }
  while (curtok()->kind == TK_LPAREN) {
    skip_func_params_stmt();
  }
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

static token_ident_t *parse_member_decl_name_recursive_stmt(int *is_ptr, int *out_has_func_suffix,
                                                            int *out_paren_array_mul) {
  psx_consume_pointer_prefix(is_ptr);
  token_ident_t *name = NULL;
  int paren_array_mul = 1;
  if (tk_consume('(')) {
    name = parse_member_decl_name_recursive_stmt(is_ptr, out_has_func_suffix, &paren_array_mul);
    paren_array_mul = parse_stmt_array_suffixes_constexpr_required(paren_array_mul);
    tk_expect(')');
  } else {
    name = tk_consume_ident();
  }
  consume_stmt_member_func_suffixes(out_has_func_suffix);
  if (out_paren_array_mul) *out_paren_array_mul = paren_array_mul;
  return name;
}

static void consume_stmt_member_func_suffixes(int *out_has_func_suffix) {
  while (curtok()->kind == TK_LPAREN) {
    if (out_has_func_suffix) *out_has_func_suffix = 1;
    skip_func_params_stmt();
  }
}

static member_decl_head_t parse_stmt_member_decl_head(void) {
  member_decl_head_t out = {0};
  out.paren_array_mul = 1;
  out.member = parse_member_decl_name_recursive_stmt(&out.is_ptr, &out.has_func_suffix, &out.paren_array_mul);
  return out;
}

static int parse_stmt_member_array_suffixes_thunk(int *out_is_flex_array) {
  return parse_stmt_member_array_suffixes(out_is_flex_array);
}

static const struct_member_layout_ops_t stmt_struct_layout_ops = {
  .parse_alignas_value        = psx_parse_alignas_value,
  .make_anonymous_tag_name    = make_anonymous_tag_name_stmt,
  .parse_tag_definition_body  = parse_tag_definition_body,
  .parse_member_decl_head     = parse_stmt_member_decl_head,
  .parse_enum_const_expr      = psx_parse_enum_const_expr,
  .parse_member_array_suffixes = parse_stmt_member_array_suffixes_thunk,
};

static int parse_struct_or_union_members_layout(token_kind_t tag_kind, char *tag_name, int tag_len, int *out_size) {
  return psx_parse_struct_or_union_members_layout(tag_kind, tag_name, tag_len, out_size, &stmt_struct_layout_ops);
}

static int parse_array_size_constexpr_stmt(void) {
  long long v = psx_parse_enum_const_expr();
  if (v <= 0) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
  }
  return (int)v;
}

static int parse_array_size_optional_constexpr_stmt(int *out_has_size) {
  if (curtok()->kind == TK_RBRACKET) {
    if (out_has_size) *out_has_size = 0;
    return 0;
  }
  if (out_has_size) *out_has_size = 1;
  return parse_array_size_constexpr_stmt();
}

static stmt_array_suffix_t parse_stmt_array_suffixes(int base_mul) {
  stmt_array_suffix_t out = {0};
  out.arr_total = (base_mul > 0) ? base_mul : 1;
  out.is_array = (base_mul > 0);
  out.has_incomplete_array = 0;
  while (tk_consume('[')) {
    int has_size = 0;
    int n = parse_array_size_optional_constexpr_stmt(&has_size);
    if (!has_size) {
      out.has_incomplete_array = 1;
    } else {
      out.arr_total *= n;
    }
    out.is_array = 1;
    tk_expect(']');
  }
  return out;
}

static int parse_stmt_array_suffixes_constexpr_required(int base_mul) {
  int arr_total = (base_mul > 0) ? base_mul : 1;
  while (tk_consume('[')) {
    int n = parse_array_size_constexpr_stmt();
    if (n > 0) arr_total *= n;
    tk_expect(']');
  }
  return arr_total;
}

static int parse_stmt_member_array_suffixes(int *out_is_flex_array) {
  int arr_total = 1;
  int is_flex_array = 0;
  while (tk_consume('[')) {
    if (curtok()->kind == TK_RBRACKET) {
      is_flex_array = 1;
      arr_total = 0;
    } else {
      arr_total *= parse_array_size_constexpr_stmt();
    }
    tk_expect(']');
  }
  if (out_is_flex_array) *out_is_flex_array = is_flex_array;
  return arr_total;
}

// _Alignas( constant-expression | type-name )
static int parse_enum_members(void) {
  int member_count = 0;
  long long next_value = 0;
  while (!tk_consume('}')) {
    token_ident_t *enumerator = tk_consume_ident();
    if (!enumerator) {
      psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_ENUMERATOR_NAME));
    }
    long long value = next_value;
    if (tk_consume('=')) {
      value = psx_parse_enum_const_expr();
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
      make_anonymous_tag_name_stmt(tag_name, tag_len);
    }
    if (tk_consume('{')) {
      int member_count = 0;
      int tag_size = 0;
      member_count = parse_tag_definition_body(*tag_kind, *tag_name, *tag_len, &tag_size);
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

  for (;;) {
    int is_ptr = is_pointer_base;
    psx_consume_pointer_prefix(&is_ptr);
    token_ident_t *name = parse_typedef_name_decl(&is_ptr);
    int typedef_sizeof = is_ptr ? 8 : elem_size;
    stmt_array_suffix_t arr = parse_stmt_array_suffixes(0);
    if (!is_ptr && arr.has_incomplete_array) typedef_sizeof = 0;
    else if (!is_ptr && arr.is_array && arr.arr_total > 0) typedef_sizeof *= arr.arr_total;
    token_kind_t stored_base_kind = (td_is_unsigned && base_kind == TK_INT) ? TK_UNSIGNED : base_kind;
    psx_ctx_define_typedef_name(name->str, name->len, stored_base_kind, elem_size, fp_kind,
                                tag_kind, tag_name, tag_len, is_ptr, typedef_sizeof,
                                td_pointee_const, td_pointee_volatile, td_is_unsigned);
    if (!tk_consume(',')) break;
  }
  tk_expect(';');
}

static int is_decl_like_start_stmt(void) {
  if (curtok()->kind == TK_TYPEDEF) return 1;
  if (curtok()->kind == TK_STATIC_ASSERT) return 1;
  if (psx_ctx_is_type_token(curtok()->kind) || is_decl_prefix_token_stmt(curtok()->kind) ||
      psx_ctx_is_typedef_name_token(curtok())) return 1;
  if (psx_ctx_is_tag_keyword(curtok()->kind)) return 1;
  return 0;
}

static node_t *parse_decl_like_stmt(void) {
  if (curtok()->kind == TK_TYPEDEF) {
    parse_typedef_decl();
    return psx_node_new_num(0);
  }

  if (curtok()->kind == TK_STATIC_ASSERT ||
      psx_ctx_is_type_token(curtok()->kind) || is_decl_prefix_token_stmt(curtok()->kind) ||
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
    static int anon_tag_counter = 0;
    char anon_buf[32];
    char *tag_name = tag ? tag->str : anon_buf;
    int tag_len = tag ? tag->len : 0;
    if (!tag) {
      tag_len = snprintf(anon_buf, sizeof(anon_buf), "__anon_%d", anon_tag_counter++);
    }
    if (tk_consume('{')) {
      int member_count = 0;
      int tag_size = 0;
      member_count = parse_tag_definition_body(tag_kind, tag_name, tag_len, &tag_size);
      psx_ctx_define_tag_type_with_layout(tag_kind, tag_name, tag_len, member_count, tag_size);
      if (tk_consume(';')) {
        return psx_node_new_num(0);
      }
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

static node_t *stmt_internal(void) {
  // 空文（null statement）: C11 6.8.3 — セミコロンだけの文
  if (tk_consume(';')) {
    return psx_node_new_num(0);
  }

  if (tk_consume('{')) {
    psx_ctx_enter_block_scope();
    psx_decl_enter_scope();
    node_block_t *node = arena_alloc(sizeof(node_block_t));
    node->base.kind = ND_BLOCK;
    int i = 0;
    int cap = 16;
    node->body = calloc(cap, sizeof(node_t*));
    int prev_terminates = 0;
    while (!tk_consume('}')) {
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

  if (is_decl_like_start_stmt()) {
    return parse_decl_like_stmt();
  }

  if (curtok()->kind == TK_RETURN) {
    set_curtok(curtok()->next);
    node_t *node = arena_alloc(sizeof(node_t));
    node->kind = ND_RETURN;
    if (tk_consume(';')) {
      if (psx_expr_current_func_ret_token_kind() != TK_VOID) {
        diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, curtok(),
                       "%s",
                       diag_message_for(DIAG_ERR_PARSER_RETURN_VALUE_REQUIRED_NONVOID));
      }
      node->lhs = NULL;
      node->fp_kind = psx_expr_current_func_ret_fp_kind();
      return node;
    }
    node->lhs = ps_expr();
    if (psx_expr_current_func_ret_token_kind() == TK_VOID) {
      diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, curtok(),
                     "%s",
                     diag_message_for(DIAG_ERR_PARSER_RETURN_VALUE_FORBIDDEN_VOID));
    }
    node->fp_kind = psx_expr_current_func_ret_fp_kind();
    node->ret_struct_size = psx_expr_current_func_ret_struct_size();
    tk_expect(';');
    return node;
  }

  if (curtok()->kind == TK_IF) {
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

  if (curtok()->kind == TK_WHILE) {
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

  if (curtok()->kind == TK_DO) {
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

  if (curtok()->kind == TK_FOR) {
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

  if (curtok()->kind == TK_SWITCH) {
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

  if (curtok()->kind == TK_CASE) {
    set_curtok(curtok()->next);
    node_case_t *node = arena_alloc(sizeof(node_case_t));
    node->base.kind = ND_CASE;
    node->val = psx_parse_enum_const_expr();
    psx_switch_register_case(node->val, curtok());
    tk_expect(':');
    node->base.rhs = stmt_internal();
    return (node_t *)node;
  }

  if (curtok()->kind == TK_DEFAULT) {
    set_curtok(curtok()->next);
    psx_switch_register_default(curtok());
    node_default_t *node = arena_alloc(sizeof(node_default_t));
    node->base.kind = ND_DEFAULT;
    tk_expect(':');
    node->base.rhs = stmt_internal();
    return (node_t *)node;
  }

  if (curtok()->kind == TK_BREAK) {
    if (psx_loop_depth() == 0 && !psx_switch_has_ctx()) {
      psx_diag_only_in(curtok(), diag_text_for(DIAG_TEXT_BREAK), diag_text_for(DIAG_TEXT_LOOP_OR_SWITCH_SCOPE));
    }
    set_curtok(curtok()->next);
    node_t *node = arena_alloc(sizeof(node_t));
    node->kind = ND_BREAK;
    tk_expect(';');
    return node;
  }

  if (curtok()->kind == TK_CONTINUE) {
    if (psx_loop_depth() == 0) {
      psx_diag_only_in(curtok(), diag_text_for(DIAG_TEXT_CONTINUE), diag_text_for(DIAG_TEXT_LOOP_SCOPE));
    }
    set_curtok(curtok()->next);
    node_t *node = arena_alloc(sizeof(node_t));
    node->kind = ND_CONTINUE;
    tk_expect(';');
    return node;
  }

  if (curtok()->kind == TK_GOTO) {
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

  if (curtok()->kind == TK_IDENT && curtok()->next && curtok()->next->kind == TK_COLON) {
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

  node_t *node = ps_expr();
  tk_expect(';');
  return node;
}

node_t *psx_stmt_stmt(void) {
  return stmt_internal();
}
