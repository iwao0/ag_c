#include "internal/decl.h"
#include "internal/core.h"
#include "internal/diag.h"
#include "internal/expr.h"
#include "internal/node_utils.h"
#include "internal/semantic_ctx.h"
#include "config_runtime.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include <stdlib.h>
#include <string.h>

static lvar_t *locals;
static int locals_offset;
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

static long long eval_const_expr_decl(node_t *n, int *ok) {
  if (!n) {
    *ok = 0;
    return 0;
  }
  switch (n->kind) {
    case ND_NUM:
      return ((node_num_t *)n)->val;
    case ND_ADD: {
      long long l = eval_const_expr_decl(n->lhs, ok);
      if (!*ok) return 0;
      long long r = eval_const_expr_decl(n->rhs, ok);
      return l + r;
    }
    case ND_SUB: {
      long long l = eval_const_expr_decl(n->lhs, ok);
      if (!*ok) return 0;
      long long r = eval_const_expr_decl(n->rhs, ok);
      return l - r;
    }
    case ND_MUL: {
      long long l = eval_const_expr_decl(n->lhs, ok);
      if (!*ok) return 0;
      long long r = eval_const_expr_decl(n->rhs, ok);
      return l * r;
    }
    case ND_DIV: {
      long long l = eval_const_expr_decl(n->lhs, ok);
      if (!*ok) return 0;
      long long r = eval_const_expr_decl(n->rhs, ok);
      return l / r;
    }
    case ND_MOD: {
      long long l = eval_const_expr_decl(n->lhs, ok);
      if (!*ok) return 0;
      long long r = eval_const_expr_decl(n->rhs, ok);
      return l % r;
    }
    case ND_SHL: {
      long long l = eval_const_expr_decl(n->lhs, ok);
      if (!*ok) return 0;
      long long r = eval_const_expr_decl(n->rhs, ok);
      return l << r;
    }
    case ND_SHR: {
      long long l = eval_const_expr_decl(n->lhs, ok);
      if (!*ok) return 0;
      long long r = eval_const_expr_decl(n->rhs, ok);
      return l >> r;
    }
    case ND_BITAND: {
      long long l = eval_const_expr_decl(n->lhs, ok);
      if (!*ok) return 0;
      long long r = eval_const_expr_decl(n->rhs, ok);
      return l & r;
    }
    case ND_BITXOR: {
      long long l = eval_const_expr_decl(n->lhs, ok);
      if (!*ok) return 0;
      long long r = eval_const_expr_decl(n->rhs, ok);
      return l ^ r;
    }
    case ND_BITOR: {
      long long l = eval_const_expr_decl(n->lhs, ok);
      if (!*ok) return 0;
      long long r = eval_const_expr_decl(n->rhs, ok);
      return l | r;
    }
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
    case ND_LOGAND:
    case ND_LOGOR: {
      long long l = eval_const_expr_decl(n->lhs, ok);
      if (!*ok) return 0;
      long long r = eval_const_expr_decl(n->rhs, ok);
      if (n->kind == ND_EQ) return l == r;
      if (n->kind == ND_NE) return l != r;
      if (n->kind == ND_LT) return l < r;
      if (n->kind == ND_LE) return l <= r;
      if (n->kind == ND_LOGAND) return (l && r) ? 1 : 0;
      return (l || r) ? 1 : 0;
    }
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
    default:
      *ok = 0;
      return 0;
  }
}

static void skip_ptr_qualifiers_decl(int *is_const_qualified, int *is_volatile_qualified) {
  while (token->kind == TK_CONST || token->kind == TK_VOLATILE || token->kind == TK_RESTRICT) {
    if (token->kind == TK_CONST && is_const_qualified) *is_const_qualified = 1;
    if (token->kind == TK_VOLATILE && is_volatile_qualified) *is_volatile_qualified = 1;
    token = token->next;
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

static int parse_array_size_constexpr_decl(void) {
  node_t *n = psx_expr_assign();
  int ok = 1;
  long long v = eval_const_expr_decl(n, &ok);
  if (!ok) {
    psx_diag_ctx(token, "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_ARRAY_SIZE_CONSTEXPR_REQUIRED));
  }
  if (v <= 0) {
    psx_diag_ctx(token, "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
  }
  return (int)v;
}

static int parse_nonneg_const_expr_decl(const char *what) {
  node_t *n = psx_expr_assign();
  int ok = 1;
  long long v = eval_const_expr_decl(n, &ok);
  if (!ok) {
    psx_diag_ctx(token, "decl", diag_message_for(DIAG_ERR_PARSER_NONNEG_CONSTEXPR_REQUIRED),
                 what);
  }
  if (v < 0) {
    psx_diag_ctx(token, "decl", diag_message_for(DIAG_ERR_PARSER_NONNEG_VALUE_REQUIRED),
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
      psx_diag_ctx(token, "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_SCALAR_BRACE_SINGLE_ELEMENT_ONLY));
      }
    return rhs;
  }
  tk_expect('}');
  return rhs;
}

static node_t *new_array_elem_lvar(lvar_t *var, int idx) {
  int elem_off = var->offset - var->size + (idx + 1) * var->elem_size;
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
  if (!token || token->kind != TK_IDENT) return NULL;
  token_ident_t *id = (token_ident_t *)token;
  lvar_t *src = psx_decl_find_lvar(id->str, id->len);
  if (!src || !src->is_array) return NULL;
  if (src->elem_size != elem_size || src->size != elem_size * array_len) return NULL;
  if (!token->next || (token->next->kind != TK_COMMA && token->next->kind != TK_RBRACE)) return NULL;

  (void)psx_expr_assign();
  node_t *init_chain = NULL;
  for (int idx = 0; idx < array_len; idx++) {
    node_t *lhs = new_array_elem_lvar_at(dst_base_off, elem_size, idx);
    int src_elem_off = src->offset - src->size + (idx + 1) * src->elem_size;
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
  if (!token || token->kind != TK_STRING) return NULL;

  node_t *rhs = psx_expr_assign();
  if (!rhs || rhs->kind != ND_STRING) return NULL;

  node_string_t *s = (node_string_t *)rhs;
  string_lit_t *lit = find_string_lit_by_label(s->string_label);
  if (!lit) {
    psx_diag_ctx(token, "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_STRING_INIT_RESOLVE_FAILED));
  }

  node_t *init_chain = NULL;
  int idx = 0;
  for (; idx < lit->len && idx < array_len; idx++) {
    node_t *lhs = new_array_elem_lvar_at(dst_base_off, elem_size, idx);
    node_mem_t *assign_node = psx_node_new_assign(lhs, psx_node_new_num((unsigned char)lit->str[idx]));
    assign_node->type_size = elem_size;
    node_t *init_node = (node_t *)assign_node;
    if (!init_chain) init_chain = init_node;
    else init_chain = psx_node_new_binary(ND_COMMA, init_chain, init_node);
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

static node_t *parse_array_initializer(lvar_t *var) {
  node_t *init_chain = NULL;
  int array_len = var->elem_size > 0 ? (var->size / var->elem_size) : 0;
  if (tk_consume('{')) {
    int idx = 0;
    bool *assigned = calloc((size_t)(array_len > 0 ? array_len : 1), sizeof(bool));
    if (!tk_consume('}')) {
      for (;;) {
        int target_idx = idx;
        if (tk_consume('[')) {
          target_idx = parse_nonneg_const_expr_decl("配列designator添字");
          tk_expect(']');
          tk_expect('=');
        }
        if (target_idx >= array_len) {
          psx_diag_ctx(token, "decl", "%s",
                       diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
        }
        if (assigned[target_idx]) {
          psx_diag_ctx(token, "decl", "%s",
                       diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_DUPLICATE_ELEMENT));
        }
        node_t *lhs = new_array_elem_lvar(var, target_idx);
        node_mem_t *assign_node = psx_node_new_assign(lhs, parse_scalar_brace_initializer());
        assign_node->type_size = var->elem_size;
        assign_node->base.fp_kind = var->fp_kind;
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

  node_t *rhs = psx_expr_assign();
  if (var->elem_size == 1 && rhs->kind == ND_STRING) {
    node_string_t *s = (node_string_t *)rhs;
    string_lit_t *lit = find_string_lit_by_label(s->string_label);
    if (!lit) {
      psx_diag_ctx(token, "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_STRING_INIT_RESOLVE_FAILED));
    }
    int idx = 0;
    for (; idx < lit->len && idx < array_len; idx++) {
      node_t *lhs = new_array_elem_lvar(var, idx);
      node_mem_t *assign_node = psx_node_new_assign(lhs, psx_node_new_num((unsigned char)lit->str[idx]));
      assign_node->type_size = var->elem_size;
      assign_node->base.fp_kind = var->fp_kind;
      node_t *init_node = (node_t *)assign_node;
      if (!init_chain) init_chain = init_node;
      else init_chain = psx_node_new_binary(ND_COMMA, init_chain, init_node);
    }
    if (idx < array_len) {
      node_t *lhs = new_array_elem_lvar(var, idx);
      node_mem_t *assign_node = psx_node_new_assign(lhs, psx_node_new_num(0));
      assign_node->type_size = var->elem_size;
      assign_node->base.fp_kind = var->fp_kind;
      node_t *init_node = (node_t *)assign_node;
      if (!init_chain) init_chain = init_node;
      else init_chain = psx_node_new_binary(ND_COMMA, init_chain, init_node);
    }
    return init_chain ? init_chain : psx_node_new_num(0);
  }
  // Extension: scalar expression for array init
  //   int a[3] = 1;  => a[0]=1, a[1]=0, a[2]=0
  if (array_len > 0) {
    node_t *lhs0 = new_array_elem_lvar(var, 0);
    node_mem_t *assign0 = psx_node_new_assign(lhs0, rhs);
    assign0->type_size = var->elem_size;
    assign0->base.fp_kind = var->fp_kind;
    init_chain = (node_t *)assign0;
    for (int idx = 1; idx < array_len; idx++) {
      node_t *lhs = new_array_elem_lvar(var, idx);
      node_mem_t *assign_node = psx_node_new_assign(lhs, psx_node_new_num(0));
      assign_node->type_size = var->elem_size;
      assign_node->base.fp_kind = var->fp_kind;
      node_t *init_node = (node_t *)assign_node;
      init_chain = psx_node_new_binary(ND_COMMA, init_chain, init_node);
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
            target_idx = parse_nonneg_const_expr_decl("配列designator添字");
            tk_expect(']');
            tk_expect('=');
          }
          if (target_idx >= array_len) {
            psx_diag_ctx(token, "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
          }
          if (assigned[target_idx]) {
            psx_diag_ctx(token, "decl", "%s",
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
      psx_diag_ctx(token, "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_UNION_ARRAY_MEMBER_NONBRACE_UNSUPPORTED));
    } else {
      psx_diag_ctx(token, "decl", "%s",
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
    psx_diag_ctx(token, "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_AGGREGATE_INIT_SCALAR_SIZE_UNSUPPORTED));
  }
  return parse_scalar_brace_initializer();
}

static node_t *parse_struct_initializer(lvar_t *var) {
  if (!tk_consume('{')) {
    psx_diag_ctx(token, "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_AGGREGATE_INIT_BRACE_REQUIRED));
  }
  int member_count = psx_ctx_get_tag_member_count(var->tag_kind, var->tag_name, var->tag_len);
  node_t *init_chain = NULL;
  int ordinal = 0;
  char **assigned_names = calloc((size_t)(member_count > 0 ? member_count : 1), sizeof(char *));
  int *assigned_lens = calloc((size_t)(member_count > 0 ? member_count : 1), sizeof(int));
  int assigned_n = 0;
  if (!tk_consume('}')) {
    for (;;) {
      char *member_name = NULL;
      int member_len = 0;
      int member_offset = 0;
      int member_type_size = 0;
      int member_array_len = 0;
      token_kind_t member_tag_kind = TK_EOF;
      char *member_tag_name = NULL;
      int member_tag_len = 0;
      int member_is_tag_pointer = 0;
      bool found = false;
      if (tk_consume('.')) {
        token_ident_t *id = tk_consume_ident();
        if (!id) psx_diag_missing(token, "メンバ名");
        found = psx_ctx_find_tag_member(var->tag_kind, var->tag_name, var->tag_len,
                                        id->str, id->len,
                                        &member_offset, &member_type_size, NULL, &member_array_len,
                                        &member_tag_kind, &member_tag_name,
                                        &member_tag_len, &member_is_tag_pointer);
        member_name = id->str;
        member_len = id->len;
        if (tk_consume('[')) {
          // Nested designator: .member[idx] = val
          if (!found || member_len <= 0) {
            psx_diag_ctx(token, "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
          }
          if (member_array_len <= 0 || member_is_tag_pointer) {
            psx_diag_ctx(token, "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_NESTED_DESIG_NOT_ARRAY));
          }
          int nested_idx = parse_nonneg_const_expr_decl("配列designator添字");
          tk_expect(']');
          tk_expect('=');
          if (nested_idx < 0 || nested_idx >= member_array_len) {
            psx_diag_ctx(token, "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
          }
          node_t *lhs = new_array_elem_lvar_at(var->offset + member_offset, member_type_size, nested_idx);
          node_t *val = parse_scalar_brace_initializer();
          node_mem_t *assign_node = psx_node_new_assign(lhs, val);
          assign_node->type_size = member_type_size;
          node_t *init_node = (node_t *)assign_node;
          if (!init_chain) init_chain = init_node;
          else init_chain = psx_node_new_binary(ND_COMMA, init_chain, init_node);
          if (tk_consume('}')) break;
          tk_expect(',');
          if (tk_consume('}')) break;
          continue;
        }
        tk_expect('=');
      } else {
        while (ordinal < member_count) {
          found = psx_ctx_get_tag_member_at(var->tag_kind, var->tag_name, var->tag_len, ordinal,
                                            &member_name, &member_len,
                                            &member_offset, &member_type_size, NULL, &member_array_len,
                                            &member_tag_kind, &member_tag_name,
                                            &member_tag_len, &member_is_tag_pointer);
          ordinal++;
          if (!found) break;
          if (member_len > 0) break;
        }
      }
      if (!found || member_len <= 0) {
        psx_diag_ctx(token, "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
      }
      for (int i = 0; i < assigned_n; i++) {
        if (assigned_lens[i] == member_len && strncmp(assigned_names[i], member_name, (size_t)member_len) == 0) {
          psx_diag_ctx(token, "decl", "%s",
                       diag_message_for(DIAG_ERR_PARSER_STRUCT_INIT_DUPLICATE_MEMBER));
        }
      }
      node_t *member_init = parse_member_initializer(var, member_offset, member_type_size,
                                                     member_tag_kind, member_tag_name, member_tag_len,
                                                     member_is_tag_pointer, member_array_len);
      node_t *init_node = NULL;
      if ((member_array_len > 0 && !member_is_tag_pointer) ||
          (!member_is_tag_pointer && (member_tag_kind == TK_STRUCT || member_tag_kind == TK_UNION))) {
        // parse_member_initializer already returns assignment chain for aggregate members.
        init_node = member_init;
      } else {
        node_t *lhs = new_struct_member_lvar(var, member_offset, member_type_size,
                                             member_tag_kind, member_tag_name, member_tag_len, member_is_tag_pointer);
        node_mem_t *assign_node = psx_node_new_assign(lhs, member_init);
        assign_node->type_size = member_type_size;
        init_node = (node_t *)assign_node;
      }
      if (!init_chain) init_chain = init_node;
      else init_chain = psx_node_new_binary(ND_COMMA, init_chain, init_node);
      assigned_names[assigned_n] = member_name;
      assigned_lens[assigned_n] = member_len;
      assigned_n++;
      if (tk_consume('}')) break;
      tk_expect(',');
      if (tk_consume('}')) break;
    }
  }
  free(assigned_names);
  free(assigned_lens);
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
      psx_diag_ctx(token, "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_STRUCT_COPY_COMPAT_REQUIRED));
    }
    node_ctrl_t *copy_select = calloc(1, sizeof(node_ctrl_t));
    copy_select->base.kind = ND_TERNARY;
    copy_select->base.lhs = ternary->base.lhs;
    node_t *then_copy = build_struct_copy_chain_from_source(var, then_src);
    node_t *else_copy = build_struct_copy_chain_from_source(var, else_src);
    copy_select->base.rhs = then_prefix ? psx_node_new_binary(ND_COMMA, then_prefix, then_copy) : then_copy;
    copy_select->els = else_prefix ? psx_node_new_binary(ND_COMMA, else_prefix, else_copy) : else_copy;
    init_chain = (node_t *)copy_select;
  } else {
    psx_diag_ctx(token, "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_STRUCT_COPY_COMPAT_REQUIRED));
  }
  if (prefix) return psx_node_new_binary(ND_COMMA, prefix, init_chain);
  return init_chain;
}

static node_t *parse_union_initializer(lvar_t *var) {
  bool has_brace = tk_consume('{');
  if (has_brace && tk_consume('}')) return psx_node_new_num(0);

  if (!has_brace) {
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

    // Fallback: scalar expression initializes the first union member.
    char *member_name = NULL;
    int member_len = 0;
    int member_offset = 0;
    int member_type_size = 0;
    int member_array_len = 0;
    token_kind_t member_tag_kind = TK_EOF;
    char *member_tag_name = NULL;
    int member_tag_len = 0;
    int member_is_tag_pointer = 0;
    int member_count = psx_ctx_get_tag_member_count(var->tag_kind, var->tag_name, var->tag_len);
    bool found = false;
    for (int ordinal = 0; ordinal < member_count; ordinal++) {
      found = psx_ctx_get_tag_member_at(var->tag_kind, var->tag_name, var->tag_len, ordinal,
                                        &member_name, &member_len,
                                        &member_offset, &member_type_size, NULL, &member_array_len,
                                        &member_tag_kind, &member_tag_name,
                                        &member_tag_len, &member_is_tag_pointer);
      if (!found) break;
      if (member_len > 0) break;
    }
    if (!found || member_len <= 0) {
      psx_diag_ctx(token, "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
    }
    node_t *lhs = new_struct_member_lvar(var, member_offset, member_type_size,
                                         member_tag_kind, member_tag_name, member_tag_len, member_is_tag_pointer);
    node_mem_t *assign_node = psx_node_new_assign(lhs, rhs);
    assign_node->type_size = member_type_size;
    return (node_t *)assign_node;
  }

  char *member_name = NULL;
  int member_len = 0;
  int member_offset = 0;
  int member_type_size = 0;
  int member_array_len = 0;
  token_kind_t member_tag_kind = TK_EOF;
  char *member_tag_name = NULL;
  int member_tag_len = 0;
  int member_is_tag_pointer = 0;
  int member_count = psx_ctx_get_tag_member_count(var->tag_kind, var->tag_name, var->tag_len);
  bool found = false;
  if (tk_consume('.')) {
    token_ident_t *id = tk_consume_ident();
    if (!id) psx_diag_missing(token, "メンバ名");
    found = psx_ctx_find_tag_member(var->tag_kind, var->tag_name, var->tag_len,
                                    id->str, id->len,
                                    &member_offset, &member_type_size, NULL, &member_array_len,
                                    &member_tag_kind, &member_tag_name,
                                    &member_tag_len, &member_is_tag_pointer);
    member_name = id->str;
    member_len = id->len;
    if (tk_consume('[')) {
      // Nested designator: .member[idx] = val
      if (!found || member_len <= 0) {
        psx_diag_ctx(token, "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
      }
      if (member_array_len <= 0 || member_is_tag_pointer) {
        psx_diag_ctx(token, "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_NESTED_DESIG_NOT_ARRAY));
      }
      int nested_idx = parse_nonneg_const_expr_decl("配列designator添字");
      tk_expect(']');
      tk_expect('=');
      if (nested_idx < 0 || nested_idx >= member_array_len) {
        psx_diag_ctx(token, "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
      }
      node_t *elem_lhs = new_array_elem_lvar_at(var->offset + member_offset, member_type_size, nested_idx);
      node_t *val = parse_scalar_brace_initializer();
      node_mem_t *assign_node = psx_node_new_assign(elem_lhs, val);
      assign_node->type_size = member_type_size;
      node_t *result = (node_t *)assign_node;
      if (has_brace) {
        tk_consume(',');
        tk_expect('}');
      }
      return result;
    }
    tk_expect('=');
  } else {
    int ordinal = 0;
    while (ordinal < member_count) {
      found = psx_ctx_get_tag_member_at(var->tag_kind, var->tag_name, var->tag_len, ordinal,
                                        &member_name, &member_len,
                                        &member_offset, &member_type_size, NULL, &member_array_len,
                                        &member_tag_kind, &member_tag_name,
                                        &member_tag_len, &member_is_tag_pointer);
      ordinal++;
      if (!found) break;
      if (member_len > 0) break;
    }
  }
  if (!found || member_len <= 0) {
    psx_diag_ctx(token, "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
  }
  node_t *member_init = parse_member_initializer(var, member_offset, member_type_size,
                                                 member_tag_kind, member_tag_name, member_tag_len,
                                                 member_is_tag_pointer, member_array_len);
  node_t *init_chain = NULL;
  if ((member_array_len > 0 && !member_is_tag_pointer) ||
      (!member_is_tag_pointer && (member_tag_kind == TK_STRUCT || member_tag_kind == TK_UNION))) {
    init_chain = member_init;
  } else {
    node_t *lhs = new_struct_member_lvar(var, member_offset, member_type_size,
                                         member_tag_kind, member_tag_name, member_tag_len, member_is_tag_pointer);
    node_mem_t *assign_node = psx_node_new_assign(lhs, member_init);
    assign_node->type_size = member_type_size;
    init_chain = (node_t *)assign_node;
  }
  if (has_brace) {
    if (tk_consume(',')) {
      if (tk_consume('}')) {
        return init_chain;
      }
      if (!tk_consume('.')) {
        psx_diag_ctx(token, "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_UNION_INIT_SINGLE_ELEMENT_ONLY));
      }
      for (;;) {
        token_ident_t *id = tk_consume_ident();
        if (!id) psx_diag_missing(token, "メンバ名");
        tk_expect('=');
        found = psx_ctx_find_tag_member(var->tag_kind, var->tag_name, var->tag_len,
                                        id->str, id->len,
                                        &member_offset, &member_type_size, NULL, &member_array_len,
                                        &member_tag_kind, &member_tag_name,
                                        &member_tag_len, &member_is_tag_pointer);
        if (!found || id->len <= 0) {
          psx_diag_ctx(token, "decl", "%s",
                       diag_message_for(DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
        }
        node_t *next_lhs = new_struct_member_lvar(var, member_offset, member_type_size,
                                                  member_tag_kind, member_tag_name, member_tag_len, member_is_tag_pointer);
        node_mem_t *next_assign = psx_node_new_assign(
            next_lhs, parse_member_initializer(var, member_offset, member_type_size,
                                               member_tag_kind, member_tag_name, member_tag_len,
                                               member_is_tag_pointer, member_array_len));
        next_assign->type_size = member_type_size;
        init_chain = psx_node_new_binary(ND_COMMA, init_chain, (node_t *)next_assign);
        if (tk_consume('}')) return init_chain;
        tk_expect(',');
        if (!tk_consume('.')) {
          psx_diag_ctx(token, "decl", "%s",
                       diag_message_for(DIAG_ERR_PARSER_UNION_INIT_SINGLE_ELEMENT_ONLY));
        }
      }
    }
    tk_expect('}');
  }
  return init_chain;
}

static void skip_func_params(void) {
  if (!tk_consume('(')) return;
  int depth = 1;
  while (depth > 0) {
    if (token->kind == TK_EOF) {
      psx_diag_ctx(token, "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_MISSING_FUNC_DECL_RPAREN));
    }
    if (token->kind == TK_LPAREN) depth++;
    else if (token->kind == TK_RPAREN) depth--;
    token = token->next;
  }
}

static token_ident_t *consume_decl_name(int *is_pointer,
                                        unsigned int *const_mask, unsigned int *volatile_mask,
                                        int *levels) {
  token_ident_t *tok = NULL;
  int open_parens = 0;
  while (tk_consume('(')) open_parens++;
  consume_pointer_chain_decl(is_pointer, const_mask, volatile_mask, levels);
  tok = tk_consume_ident();
  if (!tok) {
    psx_diag_ctx(token, "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED));
  }
  while (open_parens-- > 0) tk_expect(')');
  while (token->kind == TK_LPAREN) {
    skip_func_params();
  }
  return tok;
}

void psx_decl_reset_locals(void) {
  locals = NULL;
  locals_offset = 0;
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
  lvar_t *var = calloc(1, sizeof(lvar_t));
  var->next = locals;
  var->name = name;
  var->len = len;
  locals_offset += size;
  if (align > 1) {
    locals_offset = (locals_offset + align - 1) & ~(align - 1);
  }
  var->offset = locals_offset;
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
    if (token->kind != TK_LBRACE) {
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
  ((node_lvar_t *)lvar)->mem.is_const_qualified = var->is_const_qualified;
  ((node_lvar_t *)lvar)->mem.is_volatile_qualified = var->is_volatile_qualified;
  ((node_lvar_t *)lvar)->mem.is_pointer_const_qualified = var->is_pointer_const_qualified;
  ((node_lvar_t *)lvar)->mem.is_pointer_volatile_qualified = var->is_pointer_volatile_qualified;
  ((node_lvar_t *)lvar)->mem.pointer_const_qual_mask = var->pointer_const_qual_mask;
  ((node_lvar_t *)lvar)->mem.pointer_volatile_qual_mask = var->pointer_volatile_qual_mask;
  ((node_lvar_t *)lvar)->mem.pointer_qual_levels = var->pointer_qual_levels;
  node_mem_t *assign_node = psx_node_new_assign(lvar, parse_scalar_brace_initializer());
  assign_node->type_size = is_pointer ? 8 : var->elem_size;
  assign_node->base.fp_kind = var->fp_kind;
  return (node_t *)assign_node;
}

node_t *psx_decl_parse_declaration_after_type(int elem_size, tk_float_kind_t decl_fp_kind,
                                              token_kind_t tag_kind, char *tag_name, int tag_len,
                                              int base_is_pointer,
                                              int is_const_qualified, int is_volatile_qualified) {
  node_t *init_chain = NULL;
  int alignas_val = 0;
  psx_take_alignas_value(&alignas_val);

  for (;;) {
    int is_pointer = base_is_pointer;
    unsigned int ptr_const_mask = 0;
    unsigned int ptr_volatile_mask = 0;
    int ptr_levels = 0;
    consume_pointer_chain_decl(&is_pointer, &ptr_const_mask, &ptr_volatile_mask, &ptr_levels);
    if (tag_kind != TK_EOF && !is_pointer && elem_size <= 0) {
      psx_diag_ctx(token, "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_INCOMPLETE_OBJECT_FORBIDDEN));
    }

    token_ident_t *tok = consume_decl_name(&is_pointer, &ptr_const_mask, &ptr_volatile_mask, &ptr_levels);
    int var_size = is_pointer ? 8 : elem_size;
    int total_pointer_levels = ptr_levels + (base_is_pointer ? 1 : 0);
    int pointer_deref_size = (total_pointer_levels >= 2) ? 8 : elem_size;
    int ptr_is_const_qualified = (ptr_const_mask & 1u) ? 1 : 0;
    int ptr_is_volatile_qualified = (ptr_volatile_mask & 1u) ? 1 : 0;

    lvar_t *var = psx_decl_find_lvar(tok->str, tok->len);
    if (!var) {
      if (tk_consume('[')) {
        int array_size = parse_array_size_constexpr_decl();
        tk_expect(']');
        while (tk_consume('[')) {
          array_size *= parse_array_size_constexpr_decl();
          tk_expect(']');
        }
        var = psx_decl_register_lvar_sized_align(tok->str, tok->len, array_size * elem_size, elem_size, 1, alignas_val);
        var->tag_kind = tag_kind;
        var->tag_name = tag_name;
        var->tag_len = tag_len;
        var->is_tag_pointer = 0;
        var->is_const_qualified = is_const_qualified;
        var->is_volatile_qualified = is_volatile_qualified;
        var->is_pointer_const_qualified = ptr_is_const_qualified;
        var->is_pointer_volatile_qualified = ptr_is_volatile_qualified;
        var->pointer_const_qual_mask = ptr_const_mask;
        var->pointer_volatile_qual_mask = ptr_volatile_mask;
        var->pointer_qual_levels = ptr_levels;
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
      }
    }

    if (!is_pointer) {
      var->fp_kind = decl_fp_kind;
    }

    if (tk_consume('=')) {
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
  token_kind_t type_kind = psx_consume_type_kind();
  int is_const_qualified = 0;
  int is_volatile_qualified = 0;
  psx_take_type_qualifiers(&is_const_qualified, &is_volatile_qualified);
  int elem_size = 8;
  psx_ctx_get_type_info(type_kind, NULL, &elem_size);
  tk_float_kind_t decl_fp_kind = TK_FLOAT_KIND_NONE;
  if (type_kind == TK_FLOAT) decl_fp_kind = TK_FLOAT_KIND_FLOAT;
  else if (type_kind == TK_DOUBLE) decl_fp_kind = TK_FLOAT_KIND_DOUBLE;
  return psx_decl_parse_declaration_after_type(elem_size, decl_fp_kind, TK_EOF, NULL, 0, 0,
                                               is_const_qualified, is_volatile_qualified);
}
