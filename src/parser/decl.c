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
} local_decl_spec_t;
static int parse_local_decl_spec(local_decl_spec_t *out);
static node_t *parse_typedef_declaration_local(void);
static global_var_t *find_global_var_decl(char *name, int len);
static tk_float_kind_t fp_kind_for_type_kind(token_kind_t type_kind);
static void init_local_decl_spec(local_decl_spec_t *out);
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

static void init_local_decl_spec(local_decl_spec_t *out) {
  memset(out, 0, sizeof(*out));
  out->elem_size = 8;
  out->fp_kind = TK_FLOAT_KIND_NONE;
  out->tag_kind = TK_EOF;
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

static int parse_decl_constexpr_array_suffix_product(int *out_first_dim) {
  int mul = 1;
  int first = 0;
  while (tk_consume('[')) {
    int dim = parse_array_size_constexpr_decl();
    tk_expect(']');
    if (first == 0) first = dim;
    if (dim > 0) mul *= dim;
  }
  if (out_first_dim) *out_first_dim = first;
  return mul;
}

typedef struct {
  int arr_total;
  int is_array;
  int has_incomplete_array;
} decl_array_suffix_t;
static int parse_decl_array_suffixes_constexpr_required(int base_mul);

static decl_array_suffix_t parse_decl_array_suffixes(int base_mul) {
  decl_array_suffix_t out = {0};
  out.arr_total = (base_mul > 0) ? base_mul : 1;
  out.is_array = (base_mul > 0);
  out.has_incomplete_array = 0;
  while (tk_consume('[')) {
    int has_size = 0;
    int n = parse_array_size_optional_constexpr_decl(&has_size);
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

static int parse_decl_array_suffixes_constexpr_required(int base_mul) {
  int arr_total = (base_mul > 0) ? base_mul : 1;
  while (tk_consume('[')) {
    int n = parse_array_size_constexpr_decl();
    tk_expect(']');
    if (n > 0) arr_total *= n;
  }
  return arr_total;
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
  int init_elem_count = 0;
  int array_len = var->elem_size > 0 ? (var->size / var->elem_size) : 0;
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
        // 多次元配列のネストされた波括弧: {{1,2,3},{4,5,6}}
        if (row_len > 0 && tk_consume('{')) {
          for (int ri = 0; ri < row_len; ri++) {
            init_elem_count++;
            if (init_elem_count > PS_MAX_INITIALIZER_ELEMENTS) {
              psx_diag_ctx(curtok(), "decl", "初期化子要素数が多すぎます（上限 %d）",
                           PS_MAX_INITIALIZER_ELEMENTS);
            }
            int flat_idx = target_idx + ri;
            if (flat_idx < array_len) {
              node_t *lhs = new_array_elem_lvar(var, flat_idx);
              node_mem_t *assign_node = psx_node_new_assign(lhs, psx_expr_assign());
              assign_node->type_size = var->elem_size;
              assign_node->base.fp_kind = var->fp_kind;
              if (!init_chain) init_chain = (node_t *)assign_node;
              else init_chain = psx_node_new_binary(ND_COMMA, init_chain, (node_t *)assign_node);
              assigned[flat_idx] = true;
            }
            if (tk_consume('}')) break;
            tk_expect(',');
            if (tk_consume('}')) break;
          }
          idx = target_idx + row_len;
        } else {
          init_elem_count++;
          if (init_elem_count > PS_MAX_INITIALIZER_ELEMENTS) {
            psx_diag_ctx(curtok(), "decl", "初期化子要素数が多すぎます（上限 %d）",
                         PS_MAX_INITIALIZER_ELEMENTS);
          }
          if (target_idx >= array_len) {
            psx_diag_ctx(curtok(), "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS));
          }
          if (assigned[target_idx]) {
            psx_diag_ctx(curtok(), "decl", "%s",
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
        }
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
      psx_diag_ctx(curtok(), "decl", "%s",
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

static node_t *parse_struct_initializer(lvar_t *var) {
  if (!tk_consume('{')) {
    psx_diag_ctx(curtok(), "decl", "%s",
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
        if (!id) psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
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
            psx_diag_ctx(curtok(), "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
          }
          if (member_array_len <= 0 || member_is_tag_pointer) {
            psx_diag_ctx(curtok(), "decl", "%s",
                         diag_message_for(DIAG_ERR_PARSER_NESTED_DESIG_NOT_ARRAY));
          }
          int nested_idx = parse_nonneg_const_expr_decl(diag_text_for(DIAG_TEXT_ARRAY_DESIGNATOR_INDEX));
          tk_expect(']');
          tk_expect('=');
          if (nested_idx < 0 || nested_idx >= member_array_len) {
            psx_diag_ctx(curtok(), "decl", "%s",
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
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
      }
      for (int i = 0; i < assigned_n; i++) {
        if (assigned_lens[i] == member_len && strncmp(assigned_names[i], member_name, (size_t)member_len) == 0) {
          psx_diag_ctx(curtok(), "decl", "%s",
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
      psx_diag_ctx(curtok(), "decl", "%s",
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
    if (!id) psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
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
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND));
      }
      if (member_array_len <= 0 || member_is_tag_pointer) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_NESTED_DESIG_NOT_ARRAY));
      }
      int nested_idx = parse_nonneg_const_expr_decl(diag_text_for(DIAG_TEXT_ARRAY_DESIGNATOR_INDEX));
      tk_expect(']');
      tk_expect('=');
      if (nested_idx < 0 || nested_idx >= member_array_len) {
        psx_diag_ctx(curtok(), "decl", "%s",
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
    psx_diag_ctx(curtok(), "decl", "%s",
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
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_UNION_INIT_SINGLE_ELEMENT_ONLY));
      }
      for (;;) {
        token_ident_t *id = tk_consume_ident();
        if (!id) psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
        tk_expect('=');
        found = psx_ctx_find_tag_member(var->tag_kind, var->tag_name, var->tag_len,
                                        id->str, id->len,
                                        &member_offset, &member_type_size, NULL, &member_array_len,
                                        &member_tag_kind, &member_tag_name,
                                        &member_tag_len, &member_is_tag_pointer);
        if (!found || id->len <= 0) {
          psx_diag_ctx(curtok(), "decl", "%s",
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
          psx_diag_ctx(curtok(), "decl", "%s",
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
                                                  int *had_parens) {
  consume_pointer_chain_decl(is_pointer, const_mask, volatile_mask, levels);
  token_ident_t *tok = NULL;
  int local_had_parens = 0;
  if (tk_consume('(')) {
    local_had_parens = 1;
    tok = consume_decl_name_recursive(is_pointer, const_mask, volatile_mask, levels, out_paren_array_mul, NULL);
    while (curtok()->kind == TK_LBRACKET) {
      skip_bracket_group();
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

static token_ident_t *consume_decl_name(int *is_pointer,
                                        unsigned int *const_mask, unsigned int *volatile_mask,
                                        int *levels, int *out_paren_array_mul) {
  token_ident_t *tok = consume_decl_name_recursive(is_pointer, const_mask, volatile_mask,
                                                   levels, out_paren_array_mul, NULL);
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
  node_t *init_chain = NULL;
  int alignas_val = 0;
  int decl_is_unsigned = psx_last_type_is_unsigned() || decl_is_unsigned_hint;
  int decl_is_complex = psx_last_type_is_complex();
  int decl_is_atomic = psx_last_type_is_atomic();
  psx_take_alignas_value(&alignas_val);

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
    token_ident_t *tok = consume_decl_name(&is_pointer, &ptr_const_mask, &ptr_volatile_mask, &ptr_levels, &paren_array_mul);
    int var_size = is_pointer ? 8 : elem_size;
    int total_pointer_levels = ptr_levels + (base_is_pointer ? 1 : 0);
    int pointer_deref_size = (total_pointer_levels >= 2) ? 8 : elem_size;
    int ptr_is_const_qualified = (ptr_const_mask & 1u) ? 1 : 0;
    int ptr_is_volatile_qualified = (ptr_volatile_mask & 1u) ? 1 : 0;

    lvar_t *var = NULL;
    {
      if (paren_array_mul > 0) {
        // (*p)[N] パターン: 配列へのポインタ
        int row_size = paren_array_mul * elem_size;
        var = psx_decl_register_lvar_sized_align(tok->str, tok->len, 8, elem_size, 0, alignas_val);
        var->tag_kind = tag_kind;
        var->tag_name = tag_name;
        var->tag_len = tag_len;
        var->is_tag_pointer = 0;
        var->base_deref_size = (short)elem_size;
        var->outer_stride = row_size;
      } else if (tk_consume('[')) {
        node_t *size_node = NULL;
        int size_ok = 1;
        long long array_size = parse_array_size_expr_decl(&size_node, &size_ok);
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
        int inner_dim_size = 0;  // 内側次元の要素数（0: 1次元配列）
        int trailing_mul = parse_decl_constexpr_array_suffix_product(&inner_dim_size);
        array_size *= trailing_mul;
        {
        int arr_elem_size = is_pointer ? 8 : elem_size;
        var = psx_decl_register_lvar_sized_align(tok->str, tok->len, (int)array_size * arr_elem_size, arr_elem_size, 1, alignas_val);
        if (inner_dim_size > 0) var->outer_stride = inner_dim_size * arr_elem_size;
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
    int declarator_count = 0;
    for (;;) {
      declarator_count++;
      if (declarator_count > PS_MAX_DECLARATOR_COUNT) {
        psx_diag_ctx(curtok(), "decl", "宣言子列が多すぎます（上限 %d）", PS_MAX_DECLARATOR_COUNT);
      }
      int is_ptr = ds.base_is_pointer;
      unsigned int ptr_const_mask = 0;
      unsigned int ptr_volatile_mask = 0;
      int ptr_levels = 0;
      int paren_array_dim = 0;
      consume_pointer_chain_decl(&is_ptr, &ptr_const_mask, &ptr_volatile_mask, &ptr_levels);
      token_ident_t *name = consume_decl_name(&is_ptr, &ptr_const_mask, &ptr_volatile_mask, &ptr_levels, &paren_array_dim);
      decl_array_suffix_t arr = parse_decl_array_suffixes(paren_array_dim);
      // グローバルテーブルに登録（既存エントリがなければ）
      if (!find_global_var_decl(name->str, name->len)) {
        global_var_t *gv = calloc(1, sizeof(global_var_t));
        gv->name = name->str;
        gv->name_len = name->len;
        gv->type_size = arr.has_incomplete_array ? 0 :
                        (arr.is_array ? (is_ptr ? 8 : ds.elem_size) * arr.arr_total : (is_ptr ? 8 : ds.elem_size));
        gv->deref_size = ds.elem_size;
        gv->is_array = arr.is_array;
        gv->is_extern_decl = 1;
        gv->next = global_vars;
        global_vars = gv;
      }
      if (curtok()->kind == TK_ASSIGN) {
        set_curtok(curtok()->next);
        psx_expr_assign();
      }
      if (curtok()->kind != TK_COMMA) break;
      set_curtok(curtok()->next);
    }
    tk_expect(';');
    return psx_node_new_num(0);
  }

  return psx_decl_parse_declaration_after_type(ds.elem_size, ds.fp_kind,
                                               ds.tag_kind, ds.tag_name, ds.tag_len,
                                               ds.base_is_pointer,
                                               ds.is_const_qualified ? 1 : ds.td_pointee_const,
                                               ds.is_volatile_qualified ? 1 : ds.td_pointee_volatile,
                                               ds.is_unsigned);
}

static int parse_local_decl_spec(local_decl_spec_t *out) {
  init_local_decl_spec(out);

  out->type_kind = psx_consume_type_kind();
  out->is_unsigned = psx_last_type_is_unsigned();
  psx_take_type_qualifiers(&out->is_const_qualified, &out->is_volatile_qualified);
  psx_take_extern_flag(&out->is_extern_decl);
  if (out->type_kind == TK_EOF) {
    if (!psx_ctx_is_typedef_name_token(curtok())) return 0;
    token_kind_t base_kind = TK_EOF;
    resolve_typedef_name_ref_local(&base_kind, &out->elem_size, &out->fp_kind,
                                   &out->tag_kind, &out->tag_name, &out->tag_len,
                                   &out->base_is_pointer,
                                   &out->td_pointee_const, &out->td_pointee_volatile, &out->is_unsigned);
    adjust_local_decl_spec_from_typedef(out, base_kind);
    return 1;
  }

  psx_ctx_get_type_info(out->type_kind, NULL, &out->elem_size);
  out->fp_kind = fp_kind_for_type_kind(out->type_kind);
  return 1;
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

  if (psx_ctx_is_tag_keyword(curtok()->kind)) {
    tag_kind = curtok()->kind;
    base_kind = tag_kind;
    set_curtok(curtok()->next);
    token_ident_t *tag = tk_consume_ident();
    if (!tag) {
      psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_TAG_NAME));
    }
    tag_name = tag->str;
    tag_len = tag->len;
    if (!psx_ctx_has_tag_type(tag_kind, tag_name, tag_len)) {
      // C11: typedef struct S S; のような不完全型タグ宣言を許可
      if (tag_kind == TK_STRUCT || tag_kind == TK_UNION) {
        psx_ctx_define_tag_type(tag_kind, tag_name, tag_len);
      } else {
        psx_diag_undefined_with_name(curtok(), diag_text_for(DIAG_TEXT_TAG_TYPE_SUFFIX), tag_name, tag_len);
      }
    }
    elem_size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
  } else {
    token_kind_t builtin_kind = psx_consume_type_kind();
    if (builtin_kind != TK_EOF) {
      base_kind = builtin_kind;
      psx_ctx_get_type_info(builtin_kind, NULL, &elem_size);
      fp_kind = fp_kind_for_type_kind(builtin_kind);
    } else if (psx_ctx_is_typedef_name_token(curtok())) {
      resolve_typedef_name_ref_local(&base_kind, &elem_size, &fp_kind,
                                     &tag_kind, &tag_name, &tag_len, &is_pointer_base,
                                     NULL, NULL, NULL);
    } else {
      diag_emit_tokf(DIAG_ERR_PARSER_TYPE_NAME_REQUIRED, curtok(), "%s",
                     diag_message_for(DIAG_ERR_PARSER_TYPE_NAME_REQUIRED));
    }
  }

  int td_pointee_const = 0;
  int td_pointee_volatile = 0;
  psx_take_type_qualifiers(&td_pointee_const, &td_pointee_volatile);
  int td_is_unsigned = (base_kind == TK_UNSIGNED) || psx_last_type_is_unsigned();

  for (;;) {
    int is_ptr = is_pointer_base;
    unsigned int ptr_const_mask = 0;
    unsigned int ptr_volatile_mask = 0;
    int ptr_levels = 0;
    int paren_array_mul = 0;
    consume_pointer_chain_decl(&is_ptr, &ptr_const_mask, &ptr_volatile_mask, &ptr_levels);
    token_ident_t *name = consume_decl_name(&is_ptr, &ptr_const_mask, &ptr_volatile_mask, &ptr_levels, &paren_array_mul);
    int typedef_sizeof = is_ptr ? 8 : elem_size;
    decl_array_suffix_t arr = parse_decl_array_suffixes(paren_array_mul);
    if (!is_ptr && arr.has_incomplete_array) typedef_sizeof = 0;
    else if (!is_ptr && arr.is_array && arr.arr_total > 0) typedef_sizeof *= arr.arr_total;
    token_kind_t stored_base_kind = (td_is_unsigned && base_kind == TK_INT) ? TK_UNSIGNED : base_kind;
    psx_ctx_define_typedef_name(name->str, name->len, stored_base_kind, elem_size, fp_kind,
                                tag_kind, tag_name, tag_len, is_ptr, typedef_sizeof,
                                td_pointee_const, td_pointee_volatile, td_is_unsigned);
    if (!tk_consume(',')) break;
  }
  tk_expect(';');
  return psx_node_new_num(0);
}
