#include "decl.h"
#include "arena.h"
#include "core.h"
#include "diag.h"
#include "expr.h"
#include "initializer_syntax.h"
#include "node_utils.h"
#include "ret_pointee_array.h"
#include "semantic_ctx.h"
#include "static_assert_declaration.h"
#include "tag_declaration.h"
#include "typedef_declaration.h"
#include "config_runtime.h"
#include "../diag/diag.h"
#include "../lowering/initializer_lowering.h"
#include "../lowering/global_object_lowering.h"
#include "../lowering/local_object_lowering.h"
#include "../lowering/static_local_lowering.h"
#include "../lowering/vla_lowering.h"
#include "../semantic/declaration_resolution.h"
#include "../semantic/constant_expression.h"
#include "../semantic/function_parameter_resolution.h"
#include "../tokenizer/tokenizer.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *current_funcname;
static int current_funcname_len;
static inline token_t *curtok(void) { return tk_get_current_token(); }
static inline void set_curtok(token_t *tok) { tk_set_current_token(tok); }
static void warn_unsupported_gnu_extension_name(const token_t *tok, const char *name);

void psx_decl_set_current_funcname(char *name, int len) {
  current_funcname = name;
  current_funcname_len = len;
}

void psx_decl_get_current_funcname(char **out_name, int *out_len) {
  if (out_name) *out_name = current_funcname;
  if (out_len) *out_len = current_funcname_len;
}

static psx_type_t *lvar_public_decl_type(const lvar_t *var) {
  return var ? psx_lvar_get_decl_type((lvar_t *)var) : NULL;
}

static const psx_type_t *lvar_public_skip_arrays(const psx_type_t *type) {
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
  return type;
}

static const psx_type_t *lvar_public_pointee_type(const psx_type_t *type) {
  return type && type->kind == PSX_TYPE_POINTER ? type->base : NULL;
}

static token_kind_t lvar_public_tag_kind_from_type(const psx_type_t *type) {
  if (!type) return TK_EOF;
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
  if (type && type->kind == PSX_TYPE_POINTER) type = type->base;
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
  if (!type) return TK_EOF;
  if (type->kind == PSX_TYPE_STRUCT) return TK_STRUCT;
  if (type->kind == PSX_TYPE_UNION) return TK_UNION;
  return TK_EOF;
}

int ps_lvar_storage_size(const lvar_t *var, int fallback_size) {
  int decl_size = ps_lvar_decl_sizeof(var, 0);
  int storage_size = (var && var->size > 0) ? var->size : 0;
  if (storage_size > decl_size) return storage_size;
  if (decl_size > 0) return decl_size;
  return storage_size > 0 ? storage_size : fallback_size;
}

int ps_lvar_decl_sizeof(const lvar_t *var, int fallback_size) {
  psx_type_t *type = lvar_public_decl_type(var);
  int decl_size = ps_type_sizeof(type);
  return decl_size > 0 ? decl_size : fallback_size;
}

int ps_lvar_elem_size(const lvar_t *var, int fallback_size) {
  psx_type_t *type = lvar_public_decl_type(var);
  int size = ps_type_deref_size(type);
  return size > 0 ? size : fallback_size;
}

static int lvar_array_shape_from_decl_type(const psx_type_t *type,
                                           int *type_size,
                                           int *scalar_elem_size,
                                           int *stride_elems,
                                           int max_strides) {
  if (type_size) *type_size = 0;
  if (scalar_elem_size) *scalar_elem_size = 0;
  if (stride_elems) {
    for (int i = 0; i < max_strides; i++) stride_elems[i] = 0;
  }
  if (!type || type->kind != PSX_TYPE_ARRAY) return 0;

  int strides[10];
  int n = 0;
  const psx_type_t *cur = type;
  while (cur && cur->kind == PSX_TYPE_ARRAY && n < 10) {
    int stride = cur->base ? ps_type_sizeof(cur->base) : 0;
    if (stride <= 0) stride = ps_type_deref_size(cur);
    if (stride <= 0) break;
    strides[n++] = stride;
    cur = cur->base;
  }
  if (n <= 0) return 0;

  int elem = strides[n - 1];
  if (elem <= 0) return 0;
  if (type_size) *type_size = ps_type_sizeof(type);
  if (scalar_elem_size) *scalar_elem_size = elem;
  int out_count = n - 1;
  if (out_count > max_strides) out_count = max_strides;
  for (int i = 0; i < out_count; i++) {
    stride_elems[i] = strides[i] / elem;
    if (stride_elems[i] <= 0) stride_elems[i] = 1;
  }
  return n;
}

static int lvar_array_shape(const lvar_t *var, int *type_size,
                            int *scalar_elem_size, int *stride_elems,
                            int max_strides) {
  if (type_size) *type_size = 0;
  if (scalar_elem_size) *scalar_elem_size = 0;
  if (stride_elems) {
    for (int i = 0; i < max_strides; i++) stride_elems[i] = 0;
  }
  if (!var) return 0;
  psx_type_t *type = lvar_public_decl_type(var);
  int depth = lvar_array_shape_from_decl_type(type, type_size,
                                              scalar_elem_size,
                                              stride_elems,
                                              max_strides);
  return depth;
}

int ps_lvar_array_flat_element_count(const lvar_t *var) {
  if (!ps_lvar_is_array(var)) return 0;
  int type_size = 0;
  int elem = 0;
  (void)lvar_array_shape(var, &type_size, &elem, NULL, 0);
  if (type_size <= 0 || elem <= 0) return 0;
  return type_size / elem;
}

int ps_lvar_array_scalar_element_size(const lvar_t *var) {
  if (!ps_lvar_is_array(var)) return ps_lvar_elem_size(var, 0);
  int elem = 0;
  (void)lvar_array_shape(var, NULL, &elem, NULL, 0);
  if (elem > 0) return elem;
  return ps_lvar_elem_size(var, 0);
}

int ps_lvar_array_designator_stride_elements(const lvar_t *var, int depth) {
  if (depth < 0) return 1;
  int strides[8] = {0};
  int type_size = 0;
  int elem = 0;
  (void)lvar_array_shape(var, &type_size, &elem, strides, 8);
  (void)type_size;
  (void)elem;
  if (depth < 8 && strides[depth] > 0) return strides[depth];
  return 1;
}

int ps_lvar_align_bytes(const lvar_t *var) {
  return var ? var->align_bytes : 0;
}

int ps_lvar_is_param(const lvar_t *var) {
  return (var && var->is_param) ? 1 : 0;
}

int ps_lvar_is_static_local(const lvar_t *var) {
  return (var && var->is_static_local) ? 1 : 0;
}

int ps_lvar_is_vla(const lvar_t *var) {
  psx_type_t *type = lvar_public_decl_type(var);
  return type && type->is_vla ? 1 : 0;
}

int ps_lvar_is_array(const lvar_t *var) {
  psx_type_t *type = lvar_public_decl_type(var);
  return type && type->kind == PSX_TYPE_ARRAY ? 1 : 0;
}

int ps_lvar_is_complex(const lvar_t *var) {
  psx_type_t *type = lvar_public_decl_type(var);
  const psx_type_t *leaf = lvar_public_skip_arrays(type);
  return leaf && leaf->kind == PSX_TYPE_COMPLEX ? 1 : 0;
}

int ps_lvar_is_tag_pointer(const lvar_t *var) {
  psx_type_t *type = lvar_public_decl_type(var);
  const psx_type_t *base = lvar_public_pointee_type(type);
  return base ? ps_type_is_tag_aggregate(lvar_public_skip_arrays(base)) : 0;
}

int ps_lvar_pointer_qual_levels(const lvar_t *var) {
  psx_type_t *type = lvar_public_decl_type(var);
  return ps_type_pointer_view_structural_qual_levels(type);
}

token_kind_t ps_lvar_tag_kind(const lvar_t *var) {
  psx_type_t *type = lvar_public_decl_type(var);
  return lvar_public_tag_kind_from_type(type);
}

tk_float_kind_t ps_lvar_fp_kind(const lvar_t *var) {
  psx_type_t *type = lvar_public_decl_type(var);
  const psx_type_t *leaf = lvar_public_skip_arrays(type);
  if (leaf && (leaf->kind == PSX_TYPE_FLOAT || leaf->kind == PSX_TYPE_COMPLEX))
    return leaf->fp_kind;
  return TK_FLOAT_KIND_NONE;
}

int ps_lvar_vla_row_stride_frame_off(const lvar_t *var) {
  const psx_type_t *type = lvar_public_decl_type(var);
  return psx_type_pointer_view_vla_row_stride_frame_off(type);
}

int ps_lvar_vla_row_stride_elem_size(const lvar_t *var) {
  const psx_type_t *type = lvar_public_decl_type(var);
  return psx_type_vla_row_stride_elem_size(type);
}

int ps_lvar_vla_row_stride_src_offset(const lvar_t *var) {
  const psx_type_t *type = lvar_public_decl_type(var);
  return psx_type_vla_row_stride_src_offset(type);
}

int ps_lvar_vla_param_inner_dim_count(const lvar_t *var) {
  const psx_type_t *type = lvar_public_decl_type(var);
  return psx_type_vla_param_inner_dim_count(type);
}

int ps_lvar_vla_param_inner_dim_const(const lvar_t *var, int idx) {
  const psx_type_t *type = lvar_public_decl_type(var);
  return psx_type_vla_param_inner_dim_const(type, idx);
}

int ps_lvar_vla_param_inner_dim_src_offset(const lvar_t *var, int idx) {
  const psx_type_t *type = lvar_public_decl_type(var);
  return psx_type_vla_param_inner_dim_src_offset(type, idx);
}

void psx_decl_set_gvar_decl_type(global_var_t *gv,
                                 const psx_type_t *decl_type) {
  if (!gv || !decl_type) return;
  gv->decl_type = psx_type_clone_persistent(decl_type);
}

void psx_decl_reset_translation_unit_state(void) {
  psx_decl_reset_locals();
  current_funcname = NULL;
  current_funcname_len = 0;
  psx_static_local_lowering_reset();
}

/* 集合体メンバ情報は semantic_ctx 側の統合 API (tag_member_info_t) を
 * そのまま再利用する (Phase A1 リファクタリング)。 */

typedef struct {
  psx_type_spec_result_t type_spec;
  token_kind_t type_kind;
  int is_unsigned;
  int is_long_long;   // long long (_Generic で long と区別)
  int is_plain_char;  // plain char (_Generic で signed/unsigned char と区別)
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
  const psx_type_t *base_decl_type;
} local_decl_spec_t;
typedef struct decl_declarator_state_t decl_declarator_state_t;
static int parse_local_decl_spec(local_decl_spec_t *out);
static int parse_local_decl_spec_from_typedef(local_decl_spec_t *out);
static int parse_local_decl_spec_from_builtin(local_decl_spec_t *out);
static node_t *parse_typedef_declaration_local(void);
static void parse_local_extern_declarator_list(local_decl_spec_t *ds);
static void register_local_extern_decl(token_ident_t *name,
                                       psx_type_t *canonical_type);
static void resolve_local_typedef_decl_spec(token_kind_t *base_kind, int *elem_size,
                                            tk_float_kind_t *fp_kind,
                                            token_kind_t *tag_kind, char **tag_name, int *tag_len,
                                            int *is_pointer_base, int *is_long_double_base,
                                            int *base_pointer_levels,
                                            const psx_type_t **base_decl_type,
                                            psx_type_spec_result_t *type_spec);
static void define_local_typedef_from_declarator(token_ident_t *name,
                                                 token_kind_t base_kind, int elem_size,
                                                 tk_float_kind_t fp_kind,
                                                 token_kind_t tag_kind, char *tag_name, int tag_len,
                                                 int td_pointee_const, int td_pointee_volatile,
                                                 int td_is_unsigned, int td_is_long_double,
                                                 int td_is_complex,
                                                 const psx_type_t *base_decl_type,
                                                 decl_declarator_state_t *decl_state);
static void parse_local_typedef_declarator_list(token_kind_t base_kind, int elem_size,
                                                tk_float_kind_t fp_kind,
                                                token_kind_t tag_kind, char *tag_name, int tag_len,
                                                int is_pointer_base,
                                                const psx_type_t *base_decl_type,
                                                int td_pointee_const, int td_pointee_volatile,
                                                int td_is_unsigned, int td_is_long_double,
                                                int td_is_complex);
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
                                           int *out_is_unsigned, int *out_is_long_double);

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
  out->type_spec.kind = TK_EOF;
  out->elem_size = 8;
  out->fp_kind = TK_FLOAT_KIND_NONE;
  out->tag_kind = TK_EOF;
}

static void take_local_decl_prefix_flags(local_decl_spec_t *out) {
  out->is_const_qualified = out->type_spec.is_const_qualified ? 1 : 0;
  out->is_volatile_qualified = out->type_spec.is_volatile_qualified ? 1 : 0;
  out->is_extern_decl = out->type_spec.is_extern ? 1 : 0;
}

static void adjust_local_decl_spec_from_typedef(local_decl_spec_t *out, token_kind_t base_kind) {
  if (psx_ctx_is_tag_aggregate_kind(out->tag_kind) &&
      out->tag_name && out->tag_len > 0 &&
      psx_ctx_has_tag_type(out->tag_kind, out->tag_name, out->tag_len)) {
    int tag_sz = psx_ctx_get_tag_size(out->tag_kind, out->tag_name, out->tag_len);
    if (tag_sz > 0) out->elem_size = tag_sz;
  }
  out->type_kind = base_kind;
  /* typedef 由来の unsigned 性 (例: `typedef unsigned char uint8_t` は
   * base_kind=TK_CHAR だが unsigned) を保持する。base_kind が TK_UNSIGNED の
   * ときも unsigned。上書きで捨てると 1byte ロードが ldrsb (符号付き) になり
   * uint8_t の 200 が -56 に化ける。 */
  out->is_unsigned = out->is_unsigned || (base_kind == TK_UNSIGNED);
}

static void resolve_typedef_name_ref_local(token_kind_t *out_base_kind, int *out_elem_size,
                                           tk_float_kind_t *out_fp_kind,
                                           token_kind_t *out_tag_kind, char **out_tag_name,
                                           int *out_tag_len, int *out_base_is_pointer,
                                           int *out_pointee_const, int *out_pointee_volatile,
                                           int *out_is_unsigned, int *out_is_long_double) {
  token_ident_t *id = (token_ident_t *)curtok();
  psx_typedef_info_t _ti;
  if (psx_ctx_find_typedef_name(id->str, id->len, &_ti)) {
    if (out_base_kind) *out_base_kind = _ti.base_kind;
    if (out_elem_size) *out_elem_size = _ti.elem_size;
    if (out_fp_kind) *out_fp_kind = _ti.fp_kind;
    if (out_tag_kind) *out_tag_kind = _ti.tag_kind;
    if (out_tag_name) *out_tag_name = _ti.tag_name;
    if (out_tag_len) *out_tag_len = _ti.tag_len;
    if (out_base_is_pointer) *out_base_is_pointer = _ti.is_pointer;
    if (out_pointee_const) *out_pointee_const = _ti.pointee_const_qualified;
    if (out_pointee_volatile) *out_pointee_volatile = _ti.pointee_volatile_qualified;
    if (out_is_unsigned) *out_is_unsigned = _ti.is_unsigned;
    if (out_is_long_double) *out_is_long_double = _ti.is_long_double;
  }
  set_curtok(curtok()->next);
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
  long long v = psx_eval_const_int(n, &ok);
  if (out_ok) *out_ok = ok;
  if (ok && v < 0) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
  } else if (ok && v == 0) {
    warn_unsupported_gnu_extension_name(curtok(), "zero-length array");
  }
  return v;
}

enum { DECL_MAX_VLA_DIMS = 8 };

/* ARRAY operatorの型形状と分離して保持する宣言解析中の次元値。
 * 定数次元も同じ列に記録し、VLA登録時だけAST式を使う。 */
typedef struct {
  node_t *nodes[DECL_MAX_VLA_DIMS];
  long long const_values[DECL_MAX_VLA_DIMS];
  unsigned char is_const[DECL_MAX_VLA_DIMS];
  unsigned char is_incomplete[DECL_MAX_VLA_DIMS];
  int count;
} decl_vla_dims_t;

struct decl_declarator_state_t {
  /* `T (*p)[N][M]...` (配列へのポインタ) 局所宣言子で、paren の後ろの `[N][M]`
   * の先頭次元 (N) と次元数を捕捉する。 */
  int paren_array_first_dim;
  int paren_array_second_dim;
  int paren_array_dim_count;
  /* `T (*(*p)[N])[M]`: the inner `(*p)[N]` is the outer dimension of the
   * pointer array that the variable points at. Keep it separate from the
   * trailing pointee row dimension `M`. */
  int pointer_array_outer_dim;
  /* 関数ポインタ配列 `int (*t[2][2])(void)` の括弧内 `[N][M]` 個別次元。 */
  int inner_array_dims[8];
  int inner_array_dim_count;
  /* `int (*p)[m]` (配列へのポインタの VLA 形) の先頭次元のランタイム式。 */
  node_t *paren_array_vla_dim;
  /* 宣言子の trailing 部に関数シグネチャ `(args...)` があれば 1。 */
  int trailing_func_suffix;
  /* 宣言子に paren グループ `(*...)` があれば 1。 */
  int had_paren_group;
  /* trailing `()` の個数 (pointer-to-function が戻り funcptr を持つとき 2 以上)。 */
  int func_suffix_count;
  psx_parsed_function_suffix_t function_suffixes[24];
  /* Pointer stars consumed inside the paren-grouped declarator (`(*p)` /
   * `(**pp)`). These build the function-pointer object itself, not the
   * function return type. */
  int paren_pointer_levels;
  int funcptr_object_pointer_levels;
  psx_declarator_shape_t declarator_shape;
  decl_vla_dims_t array_dims;
};

static void reset_decl_declarator_state(decl_declarator_state_t *state) {
  if (!state) return;
  memset(state, 0, sizeof(*state));
  psx_declarator_shape_init(&state->declarator_shape);
}

static void resolve_decl_declarator_shape(
    decl_declarator_state_t *state, psx_declarator_shape_t *out,
    token_t *diagnostic_token) {
  *out = state->declarator_shape;
  for (int i = 0; i < state->func_suffix_count; i++) {
    psx_parsed_function_suffix_t *suffix = &state->function_suffixes[i];
    if (suffix->declarator_op_index < 0 ||
        suffix->declarator_op_index >= out->count ||
        out->ops[suffix->declarator_op_index].kind != PSX_DECL_OP_FUNCTION) {
      psx_diag_ctx(diagnostic_token, "decl",
                   "invalid local function suffix target");
    }
    psx_resolve_function_parameters_syntax(
        suffix->parameters, &out->ops[suffix->declarator_op_index],
        diagnostic_token);
  }
}

static void dispose_decl_declarator_state(decl_declarator_state_t *state) {
  if (!state) return;
  for (int i = 0; i < state->func_suffix_count; i++) {
    psx_parsed_function_parameters_t *parameters =
        state->function_suffixes[i].parameters;
    if (!parameters) continue;
    psx_dispose_function_parameters_syntax(parameters);
    free(parameters);
    state->function_suffixes[i].parameters = NULL;
  }
}
/* 基底 typedef が配列型 (`typedef BinOp OpArr3[3]`) のとき、要素 1 個のバイト数。
 * `OpArr3 *pa` (typedef 配列型へのポインタ + 要素がポインタ) で要素サイズを 8 と判定する
 * のに使う。pointer-to-array typedef (`typedef int (*PA)[3]`) では 0 のまま (PA p の要素は
 * int=4 で elem_size を使うため、上書きしない)。parse_local_decl_spec_from_typedef がセット。 */
unsigned char psx_funcptr_ret_int_width_from_kind(token_kind_t kind, int is_pointer,
                                                  tk_float_kind_t fp_kind) {
  if (is_pointer || fp_kind != TK_FLOAT_KIND_NONE || kind == TK_VOID ||
      psx_ctx_is_tag_aggregate_kind(kind) || kind == TK_EOF) {
    return 0;
  }
  return ps_ctx_scalar_type_size(kind) >= 8 ? 8 : 4;
}

int psx_funcptr_signature_has_payload(psx_funcptr_signature_t sig) {
  return sig.param_fp_mask || sig.param_int_mask || sig.is_variadic ||
         sig.nargs_fixed;
}

int psx_funcptr_return_shape_has_payload(psx_funcptr_return_shape_t ret) {
  return ret.int_width ||
         ret.fp_kind != TK_FLOAT_KIND_NONE ||
         ret.pointee_fp_kind != TK_FLOAT_KIND_NONE ||
         ret.is_void || ret.is_data_pointer || ret.is_complex ||
         psx_ret_pointee_array_has_dims(ret.pointee_array);
}

int psx_funcptr_return_shape_matches(psx_funcptr_return_shape_t a,
                                     psx_funcptr_return_shape_t b) {
  return a.int_width == b.int_width &&
         a.fp_kind == b.fp_kind &&
         a.pointee_fp_kind == b.pointee_fp_kind &&
         a.is_void == b.is_void &&
         a.is_data_pointer == b.is_data_pointer &&
         a.is_complex == b.is_complex &&
         psx_ret_pointee_array_equal(a.pointee_array, b.pointee_array);
}

psx_funcptr_return_shape_t psx_decl_funcptr_direct_return_shape(
    psx_decl_funcptr_sig_t sig) {
  return sig.function.callable.return_shape;
}

psx_funcptr_return_shape_t psx_funcptr_return_shape_merge_missing(
    psx_funcptr_return_shape_t merged, psx_funcptr_return_shape_t src) {
  if (!merged.int_width && src.int_width) merged.int_width = src.int_width;
  if (merged.fp_kind == TK_FLOAT_KIND_NONE &&
      src.fp_kind != TK_FLOAT_KIND_NONE) {
    merged.fp_kind = src.fp_kind;
  }
  if (merged.pointee_fp_kind == TK_FLOAT_KIND_NONE &&
      src.pointee_fp_kind != TK_FLOAT_KIND_NONE) {
    merged.pointee_fp_kind = src.pointee_fp_kind;
  }
  if (src.is_void) merged.is_void = 1;
  if (src.is_data_pointer) merged.is_data_pointer = 1;
  if (src.is_complex) merged.is_complex = 1;
  if (!psx_ret_pointee_array_has_dims(merged.pointee_array) &&
      psx_ret_pointee_array_has_dims(src.pointee_array)) {
    merged.pointee_array = src.pointee_array;
  }
  return merged;
}

int psx_funcptr_callable_shape_has_payload(psx_funcptr_callable_shape_t fn) {
  return psx_funcptr_signature_has_payload(fn.signature) ||
         psx_funcptr_return_shape_has_payload(fn.return_shape);
}

int psx_funcptr_callable_shape_matches(psx_funcptr_callable_shape_t a,
                                       psx_funcptr_callable_shape_t b) {
  return a.signature.param_fp_mask == b.signature.param_fp_mask &&
         a.signature.param_int_mask == b.signature.param_int_mask &&
         a.signature.is_variadic == b.signature.is_variadic &&
         (!a.signature.is_variadic ||
          a.signature.nargs_fixed == b.signature.nargs_fixed) &&
         (a.signature.nargs_fixed <= 0 || b.signature.nargs_fixed <= 0 ||
          a.signature.nargs_fixed == b.signature.nargs_fixed) &&
         psx_funcptr_return_shape_matches(a.return_shape, b.return_shape);
}

psx_funcptr_callable_shape_t psx_funcptr_callable_shape_merge_missing(
    psx_funcptr_callable_shape_t merged, psx_funcptr_callable_shape_t src,
    int copy_variadic) {
  if (!merged.signature.param_fp_mask && src.signature.param_fp_mask)
    merged.signature.param_fp_mask = src.signature.param_fp_mask;
  if (!merged.signature.param_int_mask && src.signature.param_int_mask)
    merged.signature.param_int_mask = src.signature.param_int_mask;
  if (copy_variadic && src.signature.is_variadic)
    merged.signature.is_variadic = 1;
  if (!merged.signature.nargs_fixed && src.signature.nargs_fixed)
    merged.signature.nargs_fixed = src.signature.nargs_fixed;
  merged.return_shape =
      psx_funcptr_return_shape_merge_missing(merged.return_shape,
                                             src.return_shape);
  return merged;
}

int psx_funcptr_returned_func_has_payload(psx_funcptr_returned_func_t ret) {
  return ret.is_funcptr ||
         (ret.type && psx_funcptr_type_shape_has_payload(*ret.type));
}

static psx_funcptr_type_shape_t *psx_funcptr_type_shape_clone_heap(
    psx_funcptr_type_shape_t fn);

psx_funcptr_type_shape_t psx_funcptr_returned_func_as_type_shape(
    psx_funcptr_returned_func_t ret) {
  return ret.type ? *ret.type : (psx_funcptr_type_shape_t){0};
}

psx_funcptr_returned_func_t psx_funcptr_returned_func_from_type_shape(
    psx_funcptr_type_shape_t fn) {
  psx_funcptr_returned_func_t ret = {0};
  ret.is_funcptr = psx_funcptr_type_shape_has_payload(fn) ? 1 : 0;
  if (ret.is_funcptr) ret.type = psx_funcptr_type_shape_clone_heap(fn);
  return ret;
}

psx_funcptr_returned_func_t psx_funcptr_returned_func_mark(
    psx_funcptr_returned_func_t ret) {
  ret.is_funcptr = 1;
  return ret;
}

psx_funcptr_returned_func_t psx_funcptr_returned_func_clone(
    psx_funcptr_returned_func_t ret) {
  psx_funcptr_returned_func_t copy = {0};
  copy.is_funcptr = ret.is_funcptr;
  if (ret.type) copy.type = psx_funcptr_type_shape_clone_heap(*ret.type);
  return copy;
}

int psx_funcptr_returned_func_matches(psx_funcptr_returned_func_t a,
                                      psx_funcptr_returned_func_t b) {
  psx_funcptr_type_shape_t a_fn = psx_funcptr_returned_func_as_type_shape(a);
  psx_funcptr_type_shape_t b_fn = psx_funcptr_returned_func_as_type_shape(b);
  return a.is_funcptr == b.is_funcptr &&
         psx_funcptr_type_shape_matches(a_fn, b_fn);
}

psx_funcptr_returned_func_t psx_funcptr_returned_func_merge_missing(
    psx_funcptr_returned_func_t merged, psx_funcptr_returned_func_t src,
    int copy_variadic) {
  int merged_is_funcptr = merged.is_funcptr || src.is_funcptr;
  psx_funcptr_type_shape_t merged_fn =
      psx_funcptr_returned_func_as_type_shape(merged);
  psx_funcptr_type_shape_t src_fn =
      psx_funcptr_returned_func_as_type_shape(src);
  merged_fn = psx_funcptr_type_shape_merge_missing(
      merged_fn, src_fn, copy_variadic);
  merged = psx_funcptr_returned_func_from_type_shape(merged_fn);
  if (merged_is_funcptr) merged = psx_funcptr_returned_func_mark(merged);
  return merged;
}

static psx_funcptr_type_shape_t *psx_funcptr_type_shape_clone_heap(
    psx_funcptr_type_shape_t fn) {
  psx_funcptr_type_shape_t *copy = calloc(1, sizeof(*copy));
  if (!copy) return NULL;
  *copy = psx_funcptr_type_shape_clone(fn);
  return copy;
}

int psx_funcptr_type_shape_has_payload(psx_funcptr_type_shape_t fn) {
  return psx_funcptr_callable_shape_has_payload(fn.callable) ||
         psx_funcptr_returned_func_has_payload(fn.returned_funcptr);
}

int psx_funcptr_type_shape_matches(psx_funcptr_type_shape_t a,
                                   psx_funcptr_type_shape_t b) {
  int has_returned =
      psx_funcptr_returned_func_has_payload(a.returned_funcptr) ||
      psx_funcptr_returned_func_has_payload(b.returned_funcptr);
  return psx_funcptr_callable_shape_matches(a.callable, b.callable) &&
         (!has_returned ||
          psx_funcptr_returned_func_matches(a.returned_funcptr,
                                            b.returned_funcptr));
}

psx_funcptr_type_shape_t psx_funcptr_type_shape_merge_missing(
    psx_funcptr_type_shape_t merged, psx_funcptr_type_shape_t src,
    int copy_variadic) {
  merged.callable = psx_funcptr_callable_shape_merge_missing(
      merged.callable, src.callable, copy_variadic);
  if (psx_funcptr_returned_func_has_payload(merged.returned_funcptr) ||
      psx_funcptr_returned_func_has_payload(src.returned_funcptr)) {
    merged.returned_funcptr = psx_funcptr_returned_func_merge_missing(
        merged.returned_funcptr, src.returned_funcptr, copy_variadic);
  }
  return merged;
}

psx_funcptr_type_shape_t psx_funcptr_type_shape_clone(
    psx_funcptr_type_shape_t fn) {
  psx_funcptr_type_shape_t copy = {0};
  copy.callable = fn.callable;
  copy.returned_funcptr =
      psx_funcptr_returned_func_clone(fn.returned_funcptr);
  return copy;
}

psx_decl_funcptr_sig_t ps_decl_funcptr_sig_clone(psx_decl_funcptr_sig_t sig) {
  psx_decl_funcptr_sig_t copy = {0};
  copy.function = psx_funcptr_type_shape_clone(sig.function);
  return copy;
}

int ps_decl_funcptr_sig_has_payload(psx_decl_funcptr_sig_t sig) {
  return psx_funcptr_type_shape_has_payload(sig.function);
}

psx_decl_funcptr_sig_t psx_decl_make_funcptr_sig(const psx_funcptr_signature_t *suffix_sig,
                                                 unsigned char ret_int_width,
                                                 tk_float_kind_t ret_fp_kind,
                                                 psx_ret_pointee_array_t ret_pointee_array,
                                                 int ret_is_void,
                                                 int ret_is_data_pointer,
                                                 int ret_is_funcptr,
                                                 int ret_is_complex) {
  psx_decl_funcptr_sig_t sig = {0};
  if (suffix_sig) {
    sig.function.callable.signature = *suffix_sig;
  }
  sig.function.callable.return_shape.int_width = ret_int_width;
  sig.function.callable.return_shape.pointee_array = ret_pointee_array;
  int ret_is_pointer_like =
      ret_is_data_pointer || psx_ret_pointee_array_has_dims(ret_pointee_array);
  sig.function.callable.return_shape.fp_kind =
      ret_is_pointer_like ? TK_FLOAT_KIND_NONE : ret_fp_kind;
  sig.function.callable.return_shape.pointee_fp_kind =
      ret_is_pointer_like ? ret_fp_kind : TK_FLOAT_KIND_NONE;
  sig.function.callable.return_shape.is_void =
      (ret_is_void && !ret_is_data_pointer) ? 1 : 0;
  sig.function.callable.return_shape.is_data_pointer = ret_is_data_pointer ? 1 : 0;
  if (ret_is_funcptr) {
    sig.function.returned_funcptr =
        psx_funcptr_returned_func_mark(sig.function.returned_funcptr);
  }
  sig.function.callable.return_shape.is_complex =
      (ret_is_complex && !ret_is_data_pointer) ? 1 : 0;
  return sig;
}

psx_decl_funcptr_sig_t psx_decl_make_funcptr_sig_from_kind(
    const psx_funcptr_signature_t *suffix_sig, token_kind_t ret_kind,
    tk_float_kind_t fp_kind, int ret_is_data_pointer, int ret_is_funcptr,
    int ret_is_complex, psx_ret_pointee_array_t ret_pointee_array) {
  return psx_decl_make_funcptr_sig(
      suffix_sig,
      psx_funcptr_ret_int_width_from_kind(ret_kind, ret_is_data_pointer, fp_kind),
      fp_kind, ret_pointee_array, ret_kind == TK_VOID, ret_is_data_pointer,
      ret_is_funcptr, ret_is_complex);
}

void psx_decl_funcptr_sig_promote_return_to_funcptr(
    psx_decl_funcptr_sig_t *sig, const psx_funcptr_signature_t *returned_sig) {
  if (!sig) return;
  psx_funcptr_type_shape_t returned_fn = {0};
  if (returned_sig) returned_fn.callable.signature = *returned_sig;
  returned_fn.callable.return_shape = psx_decl_funcptr_direct_return_shape(*sig);
  sig->function.returned_funcptr =
      psx_funcptr_returned_func_from_type_shape(returned_fn);
  sig->function.returned_funcptr =
      psx_funcptr_returned_func_mark(sig->function.returned_funcptr);
  sig->function.callable.return_shape = (psx_funcptr_return_shape_t){0};
}

static void warn_unsupported_gnu_extension_name(const token_t *tok, const char *name) {
  psx_ctx_record_unsupported_gnu_extension_warning(tok, name);
}

/* `[` 消費後の1次元を解析し、ARRAY operatorと実行時次元記述を
 * 同じイベントから生成する。型本体にAST式は所有させない。 */
static void consume_declarator_array_suffix(decl_declarator_state_t *decl_state) {
  int dim_index = decl_state->array_dims.count;
  int is_incomplete = curtok()->kind == TK_RBRACKET;
  node_t *node = NULL;
  int is_const = 0;
  long long value = 0;
  if (!is_incomplete) {
    is_const = 1;
    value = parse_array_size_expr_decl(&node, &is_const);
  }
  tk_expect(']');

  if (is_incomplete) {
    psx_declarator_shape_append_array_ex(
        &decl_state->declarator_shape, 0, 1);
  } else if (is_const) {
    psx_declarator_shape_append_array(
        &decl_state->declarator_shape, (int)value);
  } else {
    psx_declarator_shape_append_vla_array(
        &decl_state->declarator_shape);
  }

  if (dim_index >= DECL_MAX_VLA_DIMS) return;
  decl_state->array_dims.nodes[dim_index] =
      is_const ? psx_node_new_num((int)value) : node;
  decl_state->array_dims.const_values[dim_index] = is_const ? value : 0;
  decl_state->array_dims.is_const[dim_index] = is_const ? 1 : 0;
  decl_state->array_dims.is_incomplete[dim_index] = is_incomplete ? 1 : 0;
  decl_state->array_dims.count++;
}

static token_ident_t *consume_direct_declarator_name_recursive(
    decl_declarator_state_t *decl_state, int *is_pointer,
    unsigned int *const_mask, unsigned int *volatile_mask,
    int *levels, int nesting_depth) {
  int level_start = levels ? *levels : 0;
  consume_pointer_chain_decl(is_pointer, const_mask, volatile_mask, levels);
  int level_end = levels ? *levels : level_start;
  if (nesting_depth > 0 && level_end > level_start)
    decl_state->paren_pointer_levels += level_end - level_start;

  psx_skip_gnu_attributes();
  token_ident_t *name = NULL;
  int direct_was_parenthesized = 0;
  if (tk_consume('(')) {
    direct_was_parenthesized = 1;
    decl_state->had_paren_group = 1;
    psx_skip_gnu_attributes();
    name = consume_direct_declarator_name_recursive(
        decl_state, is_pointer, const_mask, volatile_mask, levels,
        nesting_depth + 1);
    tk_expect(')');
  } else {
    name = tk_consume_ident();
  }

  for (;;) {
    if (tk_consume('[')) {
      int dim_index = decl_state->array_dims.count;
      consume_declarator_array_suffix(decl_state);
      int has_size = dim_index < decl_state->array_dims.count &&
                     !decl_state->array_dims.is_incomplete[dim_index];
      int dim = has_size && decl_state->array_dims.is_const[dim_index]
                    ? (int)decl_state->array_dims.const_values[dim_index]
                    : 0;
      if (nesting_depth > 0) {
        if (decl_state->inner_array_dim_count < 8)
          decl_state->inner_array_dims[decl_state->inner_array_dim_count] =
              has_size ? dim : 0;
        decl_state->inner_array_dim_count++;
      }
      if (direct_was_parenthesized) {
        if (decl_state->paren_array_dim_count == 0)
          decl_state->paren_array_first_dim = has_size ? dim : 0;
        else if (decl_state->paren_array_dim_count == 1)
          decl_state->paren_array_second_dim = has_size ? dim : 0;
        decl_state->paren_array_dim_count++;
      }
      continue;
    }
    if (curtok()->kind == TK_LPAREN) {
      int op_index = decl_state->declarator_shape.count;
      psx_declarator_shape_append_function(
          &decl_state->declarator_shape, (psx_decl_funcptr_sig_t){0});
      if (decl_state->func_suffix_count >= 24) {
        psx_diag_ctx(curtok(), "decl", "declarator is too complex");
      }
      psx_parsed_function_parameters_t *parameters =
          calloc(1, sizeof(*parameters));
      if (!parameters) {
        psx_diag_ctx(curtok(), "decl",
                     "function parameter syntax allocation failed");
      }
      psx_parse_function_parameters_syntax(parameters);
      decl_state->function_suffixes[decl_state->func_suffix_count] =
          (psx_parsed_function_suffix_t){
              .declarator_op_index = op_index,
              .parameters = parameters,
          };
      decl_state->trailing_func_suffix = 1;
      decl_state->func_suffix_count++;
      continue;
    }
    break;
  }

  for (int level = level_end - 1; level >= level_start; level--) {
    psx_declarator_shape_append_pointer(
        &decl_state->declarator_shape,
        const_mask && level < 32 && (*const_mask & (1u << level)),
        volatile_mask && level < 32 && (*volatile_mask & (1u << level)));
  }
  return name;
}

static token_ident_t *consume_direct_declarator_name(
    decl_declarator_state_t *decl_state, int *is_pointer,
    unsigned int *const_mask, unsigned int *volatile_mask, int *levels) {
  token_ident_t *name = consume_direct_declarator_name_recursive(
      decl_state, is_pointer, const_mask, volatile_mask, levels, 0);
  if (!name) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED));
  }
  for (int i = 0; i < decl_state->declarator_shape.count; i++) {
    if (decl_state->declarator_shape.ops[i].kind != PSX_DECL_OP_FUNCTION)
      continue;
    int object_pointer_levels = 0;
    for (int j = 0; j < i; j++) {
      if (decl_state->declarator_shape.ops[j].kind == PSX_DECL_OP_POINTER)
        object_pointer_levels++;
    }
    decl_state->funcptr_object_pointer_levels = object_pointer_levels;
    break;
  }
  return name;
}

static int decl_shape_array_product(
    const decl_declarator_state_t *state, int first_op, int *out_dim_count,
    int *out_first_dim, int *out_second_dim, node_t **out_vla_dim) {
  int array_ordinal = 0;
  for (int i = 0; i < first_op; i++) {
    if (state->declarator_shape.ops[i].kind == PSX_DECL_OP_ARRAY)
      array_ordinal++;
  }
  int product = 1;
  int count = 0;
  int incomplete = 0;
  if (out_first_dim) *out_first_dim = 0;
  if (out_second_dim) *out_second_dim = 0;
  if (out_vla_dim) *out_vla_dim = NULL;
  for (int i = first_op; i < state->declarator_shape.count; i++) {
    const psx_declarator_op_t *op = &state->declarator_shape.ops[i];
    if (op->kind != PSX_DECL_OP_ARRAY) break;
    const decl_vla_dims_t *dims = &state->array_dims;
    if (op->is_vla_array) {
      if (out_vla_dim && array_ordinal < dims->count && !*out_vla_dim)
        *out_vla_dim = dims->nodes[array_ordinal];
    } else if (op->is_incomplete_array) {
      incomplete = 1;
    } else {
      if (count == 0 && out_first_dim) *out_first_dim = op->array_len;
      if (count == 1 && out_second_dim) *out_second_dim = op->array_len;
      product *= op->array_len;
    }
    count++;
    array_ordinal++;
  }
  if (out_dim_count) *out_dim_count = count;
  if (count == 0) return 0;
  if (incomplete) return -1;
  return product;
}

/* 旧storage/layout分岐が必要とする配列積は、構文イベントではなく
 * identifier-outward operator列からの一方向compat viewとして生成する。 */
static void derive_decl_legacy_array_views(
    decl_declarator_state_t *state, int *out_object_array_mul,
    int *out_paren_array_mul) {
  *out_object_array_mul = decl_shape_array_product(
      state, 0, &state->inner_array_dim_count, NULL, NULL, NULL);
  for (int i = 0; i < state->inner_array_dim_count && i < 8; i++) {
    state->inner_array_dims[i] =
        state->array_dims.is_const[i]
            ? (int)state->array_dims.const_values[i]
            : 0;
  }

  *out_paren_array_mul = 0;
  int op = state->inner_array_dim_count;
  while (op < state->declarator_shape.count &&
         state->declarator_shape.ops[op].kind != PSX_DECL_OP_ARRAY) {
    op++;
  }
  if (op < state->declarator_shape.count) {
    *out_paren_array_mul = decl_shape_array_product(
        state, op, &state->paren_array_dim_count,
        &state->paren_array_first_dim, &state->paren_array_second_dim,
        &state->paren_array_vla_dim);
  }
}

static int decl_leading_array_has_vla(
    const decl_declarator_state_t *state) {
  for (int i = 0; i < state->declarator_shape.count; i++) {
    const psx_declarator_op_t *op = &state->declarator_shape.ops[i];
    if (op->kind != PSX_DECL_OP_ARRAY) break;
    if (op->is_vla_array) return 1;
  }
  return 0;
}

void psx_decl_set_gvar_type_size(global_var_t *gv, int type_size) {
  if (!gv) return;
  psx_type_t *type = psx_gvar_get_decl_type(gv);
  gv->type_size = type_size;
  if (!type || type_size < 0) return;
  if (type->kind == PSX_TYPE_ARRAY) {
    int elem_size = type->elem_size;
    if (elem_size <= 0 && type->base)
      elem_size = ps_type_sizeof(type->base);
    type->size = type_size;
    if (elem_size > 0 && type_size % elem_size == 0)
      type->array_len = type_size / elem_size;
  } else if (type->kind != PSX_TYPE_POINTER) {
    type->size = type_size;
    type->align = type_size >= 8 ? 8
                                 : (type_size >= 4 ? 4
                                                   : (type_size >= 2 ? 2 : 1));
  }
}

void psx_decl_set_gvar_type_sig(global_var_t *gv, char *type_sig) {
  if (!gv) return;
  if (gv->decl_type) gv->decl_type->type_sig = type_sig;
}

node_t *psx_decl_bind_initializer_for_var(
    lvar_t *var, int is_pointer, node_t *initializer,
    psx_decl_init_kind_t initializer_kind, token_t *init_tok) {
  node_t *target =
      ps_lvar_is_array(var) || ps_lvar_is_tag_aggregate(var)
          ? psx_node_new_lvar_object_ref_for(var)
          : psx_node_new_lvar_expr_ref_for(var, is_pointer);
  return psx_node_new_decl_initializer(
      target, initializer, initializer_kind, init_tok);
}

node_t *psx_decl_parse_initializer_for_var(lvar_t *var, int is_pointer) {
  if (curtok() && curtok()->kind == TK_LBRACE) {
    token_t *init_tok = curtok();
    node_t *syntax = psx_parse_initializer_syntax_list();
    return psx_decl_bind_initializer_for_var(
        var, is_pointer, syntax, PSX_DECL_INIT_LIST, init_tok);
  }
  token_t *init_tok = curtok();
  return psx_decl_bind_initializer_for_var(
      var, is_pointer, psx_expr_assign(), PSX_DECL_INIT_EXPR, init_tok);
}

node_t *psx_decl_parse_declaration_after_type(int elem_size, tk_float_kind_t decl_fp_kind,
                                              token_kind_t tag_kind, char *tag_name, int tag_len,
                                              int base_is_pointer,
                                              int is_const_qualified, int is_volatile_qualified,
                                              int decl_is_unsigned_hint) {
  psx_type_spec_result_t empty_type_spec = {0};
  empty_type_spec.kind = TK_EOF;
  return psx_decl_parse_declaration_after_type_ex(elem_size, decl_fp_kind,
                                                  tag_kind, tag_name, tag_len,
                                                  base_is_pointer,
                                                  is_const_qualified, is_volatile_qualified,
                                                  decl_is_unsigned_hint,
                                                  &empty_type_spec,
                                                  NULL,
                                                  NULL,
                                                  /* decl_base_is_void = */ 0);
}

/* `static int n = 5;` のような単純スカラ static ローカルをグローバルに lowering する。
 * 戻り値: 1 = 処理した (登録 + alias 作成済)、0 = 非対応形式なので呼び出し側で fallback。
 * 対応範囲: スカラ整数 / 浮動小数点 / ポインタ。`=` の右辺は数値定数、
 * またはポインタ用のアドレス定数 (`&g` / 関数参照 / 文字列リテラル等)。
 * 配列・struct・union・複合型は未対応。 */
static int try_lower_static_local_scalar(token_ident_t *tok,
                                         psx_type_t *canonical_type) {
  int var_size = ps_type_sizeof(canonical_type);
  if (var_size <= 0 || !canonical_type) return 0;
  int element_size = canonical_type->kind == PSX_TYPE_POINTER
                         ? ps_type_deref_size(canonical_type)
                         : var_size;
  if (element_size <= 0) element_size = var_size;
  int has_init = 0;
  psx_decl_init_kind_t initializer_kind = PSX_DECL_INIT_EXPR;
  node_t *initializer = NULL;
  token_t *init_tok = NULL;
  if (tk_consume('=')) {
    has_init = 1;
    init_tok = curtok();
    initializer_kind = curtok()->kind == TK_LBRACE
                           ? PSX_DECL_INIT_LIST : PSX_DECL_INIT_EXPR;
    initializer = initializer_kind == PSX_DECL_INIT_LIST
                      ? psx_parse_initializer_syntax_list()
                      : psx_expr_assign();
  }

  return lower_static_local_declaration(
             &(psx_static_local_declaration_request_t){
                 .kind = PSX_STATIC_LOCAL_SCALAR,
                 .function_name = current_funcname,
                 .function_name_len = current_funcname_len,
                 .name = tok->str,
                 .name_len = tok->len,
                 .alias_size = var_size,
                 .alias_element_size = element_size,
                 .type = canonical_type,
                 .has_initializer = has_init,
                 .initializer_kind = initializer_kind,
                 .initializer = initializer,
                 .diag_tok = init_tok,
             },
             NULL);
}

static int try_lower_static_local_array(
    token_ident_t *tok, psx_type_t *canonical_type) {
  if (!tok || !canonical_type || canonical_type->kind != PSX_TYPE_ARRAY)
    return 0;
  const psx_type_t *leaf = canonical_type;
  for (const psx_type_t *cursor = canonical_type;
       cursor && cursor->kind == PSX_TYPE_ARRAY; cursor = cursor->base) {
    if (cursor->is_vla) return 0;
    leaf = cursor->base;
  }
  int elem_size = ps_type_sizeof(leaf);
  if (elem_size <= 0) return 0;
  if (leaf && ps_type_is_tag_aggregate(leaf) && leaf->tag_name &&
      leaf->tag_len >= 11 &&
      memcmp(leaf->tag_name, "__anon_tag_", 11) == 0) {
    psx_ctx_promote_tag_to_file_scope(
        leaf->tag_kind, leaf->tag_name, leaf->tag_len);
  }
  int has_init = 0;
  psx_decl_init_kind_t initializer_kind = PSX_DECL_INIT_LIST;
  if (curtok()->kind == TK_ASSIGN) {
    token_t *after_eq = curtok()->next;
    if (!after_eq) return 0;
    if (after_eq->kind == TK_STRING) {
      initializer_kind = PSX_DECL_INIT_EXPR;
    } else if (after_eq->kind == TK_LBRACE) {
      initializer_kind = PSX_DECL_INIT_LIST;
    } else {
      return 0;
    }
    has_init = 1;
  } else if (curtok()->kind == TK_COMMA || curtok()->kind == TK_SEMI) {
    if (canonical_type->array_len <= 0) return 0;
  } else {
    return 0;
  }
  node_t *initializer = NULL;
  token_t *init_tok = NULL;
  if (has_init) {
    tk_expect('=');
    init_tok = curtok();
    initializer = initializer_kind == PSX_DECL_INIT_LIST
                      ? psx_parse_initializer_syntax_list()
                      : psx_expr_assign();
  }

  return lower_static_local_declaration(
             &(psx_static_local_declaration_request_t){
                 .kind = leaf && ps_type_is_tag_aggregate(leaf)
                             ? PSX_STATIC_LOCAL_AGGREGATE_ARRAY
                             : PSX_STATIC_LOCAL_CONSUMED_ARRAY,
                 .function_name = current_funcname,
                 .function_name_len = current_funcname_len,
                 .name = tok->str,
                 .name_len = tok->len,
                 .alias_size = 0,
                 .alias_element_size = elem_size,
                 .type = canonical_type,
                 .has_initializer = has_init,
                 .initializer_kind = initializer_kind,
                 .initializer = initializer,
                 .diag_tok = init_tok,
             },
             NULL);
}

/* `static struct S a = {...};` / `static union U u = {...};` の struct/union
 * static local をグローバルに lowering する。スカラ/配列の static local と同じく
 * mangled global へ実体を置き、識別子は alias lvar (is_static_local) 経由で
 * ND_GVAR に解決する。これがないと auto 局所として扱われ呼び出し跨ぎで永続せず、
 * 毎回初期化子で再初期化されていた。
 * 初期化子 (`= {...}`) は raw initializer syntax を static-data lowering で flat 値列へ
 * 落とし、codegen の emit_global_struct_init がレイアウトに沿って出力する。
 * ポインタ・配列の struct (`static struct S *p` / `static struct S arr[N]`) は呼び出し側
 * ゲートで除外済み。前提: tag_kind が struct/union、is_pointer==0、配列でない。 */
static int try_lower_static_local_struct(token_ident_t *tok, token_kind_t tag_kind,
                                         char *tag_name, int tag_len,
                                         psx_type_t *canonical_type) {
  int struct_size = ps_type_sizeof(canonical_type);
  if (struct_size <= 0) return 0;
  if (tag_name && tag_len >= 11 && memcmp(tag_name, "__anon_tag_", 11) == 0) {
    psx_ctx_promote_tag_to_file_scope(tag_kind, tag_name, tag_len);
  }

  int has_initializer = 0;
  node_t *initializer = NULL;
  token_t *init_tok = NULL;
  if (curtok()->kind == TK_ASSIGN && curtok()->next &&
      curtok()->next->kind == TK_LBRACE) {
    tk_expect('=');
    has_initializer = 1;
    init_tok = curtok();
    initializer = psx_parse_initializer_syntax_list();
  }
  /* `= 式` (非 brace) の struct コピー初期化や init 無しは has_init=0 のまま
   * (codegen が .zero でゼロ初期化)。前者は将来課題。 */

  return lower_static_local_declaration(
             &(psx_static_local_declaration_request_t){
                 .kind = PSX_STATIC_LOCAL_AGGREGATE,
                 .function_name = current_funcname,
                 .function_name_len = current_funcname_len,
                 .name = tok->str,
                 .name_len = tok->len,
                 .alias_size = struct_size,
                 .alias_element_size = struct_size,
                 .type = canonical_type,
                 .has_initializer = has_initializer,
                 .initializer_kind = PSX_DECL_INIT_LIST,
                 .initializer = initializer,
                 .diag_tok = init_tok,
             },
             NULL);
}


static psx_vla_lowering_result_t lower_vla_lvar_from_dims(
    token_ident_t *tok, int elem_size, const decl_vla_dims_t *dims,
    const psx_type_t *canonical_type, int requested_alignment,
    node_t **init_chain_inout) {
  psx_vla_lowering_request_t request = {0};
  request.name = tok->str;
  request.name_len = tok->len;
  request.element_size = elem_size;
  request.dimension_count = dims ? dims->count : 0;
  request.type = canonical_type;
  request.requested_alignment = requested_alignment;
  request.diag_tok = curtok();
  for (int i = 0; dims && i < dims->count && i < PSX_VLA_MAX_DIMS; i++) {
    request.dimensions[i] = dims->nodes[i];
    request.const_values[i] = dims->const_values[i];
    request.is_const[i] = dims->is_const[i];
  }
  psx_vla_lowering_result_t result = lower_vla_declaration(&request);
  if (result.init) {
    *init_chain_inout = *init_chain_inout
                            ? psx_node_new_binary(
                                  ND_COMMA, *init_chain_inout, result.init)
                            : result.init;
  }
  return result;
}

/* ---- _Generic 用: 複雑な派生型の正規化トークン文字列を作る ----
 * 関数ポインタ (`int(*)(int,int)`) や深いネスト型 (`int(*(*)(void))[3]`) は
 * generic_type_t の構造的フィールドでは区別できないため、宣言子を「型をそのまま
 * トークン列で書き起こした正規化文字列」にして decl_type の付帯情報へ載せ、
 * _Generic の照合で比較する。 */

/* [start, end) のトークン綴りを単一スペースで連結する。skip のトークンは除外。
 * '(' を含まない単純型 (scalar / `int*` / `int[3]`) は NULL を返し、従来の構造的照合に
 * 委ねる (修飾子/typedef の細かな差を string 化で誤判定しないため)。malloc した文字列を返す。 */
char *psx_serialize_decl_type_tokens(token_t *start, token_t *end, token_t *skip) {
  int has_paren = 0;
  for (token_t *t = start; t && t != end; t = t->next) {
    if (t->kind == TK_LPAREN) { has_paren = 1; break; }
  }
  if (!has_paren) return NULL;
  size_t cap = 64, len = 0;
  char *buf = malloc(cap);
  if (!buf) return NULL;
  buf[0] = '\0';
  for (token_t *t = start; t && t != end; t = t->next) {
    if (t == skip) continue;
    const char *sp = NULL;
    int sl = 0;
    if (t->kind == TK_IDENT) { token_ident_t *id = (token_ident_t *)t; sp = id->str; sl = id->len; }
    else if (t->kind == TK_NUM) { token_num_t *n = (token_num_t *)t; sp = n->str; sl = n->len; }
    else { sp = tk_token_kind_str((token_kind_t)t->kind, &sl); }
    if (!sp || sl <= 0) continue;
    if (len + (size_t)sl + 2 >= cap) {
      cap = (len + (size_t)sl + 2) * 2;
      char *nb = realloc(buf, cap);
      if (!nb) { free(buf); return NULL; }
      buf = nb;
    }
    if (len > 0) buf[len++] = ' ';
    memcpy(buf + len, sp, (size_t)sl);
    len += (size_t)sl;
    buf[len] = '\0';
  }
  return buf;
}

node_t *psx_decl_parse_declaration_after_type_ex(int elem_size, tk_float_kind_t decl_fp_kind,
                                                 token_kind_t tag_kind, char *tag_name, int tag_len,
                                                 int base_is_pointer,
                                                 int is_const_qualified, int is_volatile_qualified,
                                                 int decl_is_unsigned_hint,
                                                 const psx_type_spec_result_t *type_spec,
                                                 const psx_type_t *base_decl_type,
                                                 token_t *typespec_start,
                                                 int decl_base_is_void) {
  node_t *init_chain = NULL;
  token_t *ts_start = typespec_start;
  psx_type_spec_result_t empty_type_spec = {0};
  empty_type_spec.kind = TK_EOF;
  if (!type_spec) type_spec = &empty_type_spec;
  int decl_is_unsigned = type_spec->is_unsigned || decl_is_unsigned_hint;
  int decl_is_complex = type_spec->is_complex;
  int decl_is_long_long = type_spec->is_long_long;
  int decl_is_plain_char = type_spec->is_plain_char;
  int decl_is_long_double = type_spec->is_long_double;
  int decl_is_atomic = type_spec->is_atomic;
  int alignas_val = type_spec->alignas_value;
  int decl_is_extern = type_spec->is_extern;
  if (decl_is_extern) {
    local_decl_spec_t ds = {0};
    init_local_decl_spec(&ds);
    ds.elem_size = elem_size;
    ds.fp_kind = decl_fp_kind;
    ds.tag_kind = tag_kind;
    ds.tag_name = tag_name;
    ds.tag_len = tag_len;
    ds.base_is_pointer = base_is_pointer;
    ds.is_unsigned = decl_is_unsigned;
    parse_local_extern_declarator_list(&ds);
    tk_expect(';');
    return psx_node_new_num(0);
  }
  int decl_is_static = type_spec->is_static ? 1 : 0;

  // _Complex 型: サイズを基底型の2倍にする（実部 + 虚部）
  if (decl_is_complex && !base_is_pointer) {
    elem_size *= 2;
  }

  int declarator_count = 0;
  for (;;) {
    declarator_count++;
    if (declarator_count > PS_MAX_DECLARATOR_COUNT) {
      psx_diag_ctx(curtok(), "decl",
                   diag_message_for(DIAG_ERR_PARSER_DECLARATOR_LIST_TOO_LONG),
                   PS_MAX_DECLARATOR_COUNT);
    }
    int is_pointer = base_is_pointer;
    unsigned int ptr_const_mask = 0;
    unsigned int ptr_volatile_mask = 0;
    int ptr_levels = 0;

    int paren_array_mul = 0;
    int inner_array_mul = 0;
    decl_declarator_state_t decl_state;
    reset_decl_declarator_state(&decl_state);
    /* td_array_elem_size は宣言文 (spec 共有) ごとに valid なので、各 declarator では
     * リセットしない。type spec 解析時に parse_local_decl_spec_from_typedef が立てる。 */
    token_ident_t *tok = consume_direct_declarator_name(
        &decl_state, &is_pointer, &ptr_const_mask, &ptr_volatile_mask,
        &ptr_levels);
    derive_decl_legacy_array_views(
        &decl_state, &inner_array_mul, &paren_array_mul);
    int object_array_has_vla = decl_leading_array_has_vla(&decl_state);
    if (tag_kind != TK_EOF && !is_pointer && elem_size <= 0) {
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_INCOMPLETE_OBJECT_FORBIDDEN));
    }
    /* `int (* (*p)(int,int))(int,int)` は declarator 上 `*` が 2 つあるが、p は
     * pointer-to-function 1 段のみ (戻り funcptr)。`**pp` や `struct S *(*f)(int)` は
     * 対象外 (後者は戻りデータポインタで pql=2 が要る)。 */
    if (ptr_levels >= 2 && decl_state.trailing_func_suffix && decl_state.had_paren_group &&
        paren_array_mul == 0 && inner_array_mul == 0 &&
        decl_state.func_suffix_count >= 2) {
      ptr_levels = 1;
    }
    /* 関数内ローカル関数プロトタイプ宣言 (`int f1(char *);`): C11 6.2.2p5 で暗黙 extern。
     * declarator が non-pointer の関数 (`(...)` 付きで `*` なし) のとき、ローカル変数として
     * 登録せず宣言を読み飛ばすだけにする。グローバル関数テーブルには別途関数定義
     * (`int f1(char *p){...}`) で登録されているので、呼び出し時は通常の関数呼び出し経路。
     * 関数ポインタ変数 (`int (*fp)(char *);`) は is_pointer=1 なので除外。 */
    if (tok && decl_state.trailing_func_suffix && !is_pointer) {
      /* 初期化子は許されないが防御的に skip、次の declarator または `;` へ。 */
      if (curtok()->kind == TK_ASSIGN) {
        set_curtok(curtok()->next);
        psx_expr_assign();
      }
      if (curtok()->kind == TK_COMMA) {
        dispose_decl_declarator_state(&decl_state);
        set_curtok(curtok()->next);
        continue;
      }
      dispose_decl_declarator_state(&decl_state);
      tk_expect(';');
      return init_chain ? init_chain : psx_node_new_num(0);
    }
    psx_declarator_shape_t resolved_declarator_shape;
    resolve_decl_declarator_shape(
        &decl_state, &resolved_declarator_shape, (token_t *)tok);
    psx_type_t *canonical_type = psx_resolve_decl_type(
        &(psx_decl_type_request_t){
            .base_kind = type_spec->kind,
            .elem_size = elem_size,
            .fp_kind = decl_fp_kind,
            .tag_kind = tag_kind,
            .tag_name = tag_name,
            .tag_len = tag_len,
            .is_unsigned = decl_is_unsigned,
            .is_complex = decl_is_complex,
            .is_const_qualified = !base_decl_type && is_const_qualified,
            .is_volatile_qualified = !base_decl_type && is_volatile_qualified,
            .is_atomic = decl_is_atomic,
            .is_long_long = decl_is_long_long,
            .is_plain_char = !base_decl_type && decl_is_plain_char,
            .override_plain_char = !base_decl_type,
            .is_long_double = decl_is_long_double,
            .base_decl_type = base_decl_type,
            .declarator_shape = &resolved_declarator_shape,
        });
    dispose_decl_declarator_state(&decl_state);
    is_pointer = canonical_type && canonical_type->kind == PSX_TYPE_POINTER;

    if (decl_base_is_void && canonical_type &&
        canonical_type->kind == PSX_TYPE_VOID) {
      psx_diag_ctx(curtok(), "decl",
                   diag_message_for(DIAG_ERR_PARSER_VOID_OBJECT_FORBIDDEN),
                   tok ? tok->len : 0, tok ? tok->str : "");
    }

    if (decl_is_static && canonical_type &&
        canonical_type->kind == PSX_TYPE_ARRAY &&
        try_lower_static_local_array(tok, canonical_type)) {
      if (!tk_consume(',')) break;
      continue;
    }

    /* `static` ローカル: 配列や struct でない単純スカラ (int/long/short/char/pointer)
     * はグローバルに lowering する。配列・struct 等の複雑形は現状フォールバック
     * (= 既存の auto と同じ挙動になる; 既知の制約)。 */
    if (decl_is_static && canonical_type &&
        canonical_type->kind != PSX_TYPE_ARRAY &&
        !ps_type_is_tag_aggregate(canonical_type) &&
        canonical_type->kind != PSX_TYPE_FUNCTION) {
      if (try_lower_static_local_scalar(tok, canonical_type)) {
        if (!tk_consume(',')) break;
        continue;
      }
    }
    /* `static struct S a = {...};` / `static union U u = {...};` の struct/union
     * static local をグローバル化。ポインタ (`static struct S *p`、上の scalar 経路で
     * 処理) と、上の aggregate array 経路に入らない配列形は除外する。 */
    if (decl_is_static && canonical_type &&
        ps_type_is_tag_aggregate(canonical_type)) {
      if (try_lower_static_local_struct(
              tok, tag_kind, tag_name, tag_len, canonical_type)) {
        if (!tk_consume(',')) break;
        continue;
      }
    }

    lvar_t *var = NULL;
    int type_attached = 0;
    node_t *pending_initializer = NULL;
    psx_decl_init_kind_t pending_initializer_kind = PSX_DECL_INIT_EXPR;
    token_t *pending_initializer_tok = NULL;
    token_t *pending_assign_tok = NULL;
    if (!decl_is_static && canonical_type &&
        canonical_type->kind == PSX_TYPE_ARRAY &&
        canonical_type->array_len <= 0 && !canonical_type->is_vla) {
      psx_local_object_result_t declared = {0};
      if (!declare_incomplete_local_object(
              &(psx_local_object_request_t){
                  .name = tok->str,
                  .name_len = tok->len,
                  .type = canonical_type,
                  .requested_alignment = alignas_val,
              },
              &declared)) {
        psx_diag_ctx(curtok(), "decl",
                     "incomplete local declaration failed for '%.*s'",
                     tok->len, tok->str);
      }
      var = declared.var;
      type_attached = declared.type_attached;
      pending_assign_tok = curtok();
      if (!tk_consume('=')) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(
                         DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
      }
      pending_initializer_tok = curtok();
      pending_initializer_kind = curtok()->kind == TK_LBRACE
                                     ? PSX_DECL_INIT_LIST
                                     : PSX_DECL_INIT_EXPR;
      pending_initializer = pending_initializer_kind == PSX_DECL_INIT_LIST
                                ? psx_parse_initializer_syntax_list()
                                : psx_expr_assign();
      if (!psx_resolve_incomplete_array_initializer(
              canonical_type, pending_initializer_kind,
              pending_initializer)) {
        psx_diag_ctx(pending_initializer_tok, "decl", "%s",
                     diag_message_for(
                         DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
      }
      psx_local_object_result_t completed = {0};
      if (!complete_declared_local_object(
              var,
              &(psx_local_object_request_t){
                  .name = tok->str,
                  .name_len = tok->len,
                  .type = canonical_type,
                  .requested_alignment = alignas_val,
              },
              &completed)) {
        psx_diag_ctx(pending_initializer_tok, "decl",
                     "incomplete local storage completion failed for '%.*s'",
                     tok->len, tok->str);
      }
      type_attached = completed.type_attached;
    }

    if (!var) {
      psx_local_object_result_t object_result = {0};
      if (curtok()->kind != TK_LBRACKET &&
          decl_state.paren_array_vla_dim == NULL &&
          lower_complete_local_object(
              &(psx_local_object_request_t){
                  .name = tok->str,
                  .name_len = tok->len,
                  .type = canonical_type,
                  .requested_alignment = alignas_val,
              },
              &object_result)) {
        var = object_result.var;
        type_attached = object_result.type_attached;
      } else if (decl_state.inner_array_dim_count > 0 && object_array_has_vla) {
        decl_vla_dims_t object_dims = {0};
        object_dims.count = decl_state.inner_array_dim_count;
        for (int i = 0; i < object_dims.count && i < DECL_MAX_VLA_DIMS; i++) {
          object_dims.nodes[i] = decl_state.array_dims.nodes[i];
          object_dims.const_values[i] = decl_state.array_dims.const_values[i];
          object_dims.is_const[i] = decl_state.array_dims.is_const[i];
          object_dims.is_incomplete[i] = decl_state.array_dims.is_incomplete[i];
        }
        int array_elem_size = ps_type_sizeof(
            lvar_public_skip_arrays(canonical_type));
        psx_vla_lowering_result_t vla_result = lower_vla_lvar_from_dims(
            tok, array_elem_size, &object_dims, canonical_type,
            alignas_val, &init_chain);
        var = vla_result.var;
        type_attached = vla_result.type_attached;
      } else if (paren_array_mul > 0 && decl_state.paren_array_vla_dim != NULL) {
        psx_vla_lowering_result_t pointer_vla =
            lower_pointer_to_vla_declaration(
                &(psx_pointer_vla_lowering_request_t){
                    .name = tok->str,
                    .name_len = tok->len,
                    .element_size = elem_size,
                    .row_dimension = decl_state.paren_array_vla_dim,
                    .type = canonical_type,
                    .requested_alignment = alignas_val,
                    .diag_tok = curtok(),
                });
        var = pointer_vla.var;
        type_attached = pointer_vla.type_attached;
        if (pointer_vla.init) {
          init_chain = init_chain
                           ? psx_node_new_binary(
                                 ND_COMMA, init_chain, pointer_vla.init)
                           : pointer_vla.init;
        }
        decl_state.paren_array_vla_dim = NULL;
      } else {
        psx_diag_ctx(curtok(), "decl",
                     "canonical local storage planning failed for '%.*s'",
                     tok->len, tok->str);
      }
    }

    if (canonical_type && !type_attached) {
      psx_type_copy_vla_runtime_metadata(
          canonical_type, psx_lvar_get_decl_type(var));
      psx_decl_set_lvar_decl_type(var, canonical_type);
    }
    /* _Generic 用: 先頭宣言子の型を name 抜きでトークン文字列化し、decl_type の付帯情報へ
     * 寄せる。 */
    if (declarator_count == 1 && ts_start && var && tok) {
      token_t *decl_end = pending_assign_tok ? pending_assign_tok : curtok();
      char *sig = psx_serialize_decl_type_tokens(
          ts_start, decl_end, (token_t *)tok);
      if (sig) {
        psx_decl_set_lvar_type_sig(var, sig);
      }
    }

    if (pending_initializer) {
      node_t *init_node = psx_decl_bind_initializer_for_var(
          var, is_pointer, pending_initializer,
          pending_initializer_kind, pending_initializer_tok);
      if (!init_chain) init_chain = init_node;
      else init_chain = psx_node_new_binary(ND_COMMA, init_chain, init_node);
      if (!tk_consume(',')) break;
      continue;
    }
    if (tk_consume('=')) {
      node_t *init_node = psx_decl_parse_initializer_for_var(
          var, is_pointer);
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
    psx_parse_static_assert_declaration();
    return psx_node_new_num(0);
  }

  /* _Generic 用: 先頭宣言子の [型開始, 宣言子終端) を name 抜きで文字列化する。 */
  token_t *typespec_start = curtok();
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
                                                  &ds.type_spec,
                                                  ds.base_decl_type,
                                                  typespec_start,
                                                  ds.type_kind == TK_VOID ? 1 : 0);
}

static int parse_local_decl_spec(local_decl_spec_t *out) {
  init_local_decl_spec(out);

  out->type_kind = psx_consume_type_kind_ex(&out->type_spec);
  out->is_unsigned = out->type_spec.is_unsigned;
  out->is_long_long = out->type_spec.is_long_long;
  out->is_plain_char = out->type_spec.is_plain_char;
  take_local_decl_prefix_flags(out);
  if (out->type_kind == TK_EOF) return parse_local_decl_spec_from_typedef(out);
  return parse_local_decl_spec_from_builtin(out);
}

static int parse_local_decl_spec_from_typedef(local_decl_spec_t *out) {
  if (!psx_ctx_is_typedef_name_token(curtok())) return 0;
  token_kind_t base_kind = TK_EOF;
  token_ident_t *id = (token_ident_t *)curtok();
  {
    psx_typedef_info_t _ti;
    if (psx_ctx_find_typedef_name(id->str, id->len, &_ti)) {
      out->base_decl_type = psx_ctx_typedef_decl_type(&_ti);
    }
  }
  resolve_typedef_name_ref_local(&base_kind, &out->elem_size, &out->fp_kind,
                                 &out->tag_kind, &out->tag_name, &out->tag_len,
                                 &out->base_is_pointer,
                                 &out->td_pointee_const, &out->td_pointee_volatile,
                                 &out->is_unsigned, NULL);
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
      psx_diag_ctx(curtok(), "decl",
                   diag_message_for(DIAG_ERR_PARSER_DECLARATOR_LIST_TOO_LONG),
                   PS_MAX_DECLARATOR_COUNT);
    }
    int is_ptr = ds->base_is_pointer;
    unsigned int ptr_const_mask = 0;
    unsigned int ptr_volatile_mask = 0;
    int ptr_levels = 0;
    decl_declarator_state_t decl_state;
    reset_decl_declarator_state(&decl_state);
    token_ident_t *name = consume_direct_declarator_name(
        &decl_state, &is_ptr, &ptr_const_mask, &ptr_volatile_mask,
        &ptr_levels);
    psx_declarator_shape_t resolved_declarator_shape;
    resolve_decl_declarator_shape(
        &decl_state, &resolved_declarator_shape, (token_t *)name);
    psx_type_t *canonical_type = psx_resolve_decl_type(
        &(psx_decl_type_request_t){
            .base_kind = ds->type_kind,
            .elem_size = ds->elem_size,
            .fp_kind = ds->fp_kind,
            .tag_kind = ds->tag_kind,
            .tag_name = ds->tag_name,
            .tag_len = ds->tag_len,
            .is_unsigned = ds->is_unsigned,
            .is_complex = ds->type_spec.is_complex,
            .base_decl_type = ds->base_decl_type,
            .declarator_shape = &resolved_declarator_shape,
        });
    dispose_decl_declarator_state(&decl_state);
    int is_function_prototype =
        canonical_type && canonical_type->kind == PSX_TYPE_FUNCTION;
    if (!is_function_prototype) {
      register_local_extern_decl(name, canonical_type);
    }
    if (curtok()->kind == TK_ASSIGN) {
      set_curtok(curtok()->next);
      psx_expr_assign();
    }
    if (curtok()->kind != TK_COMMA) break;
    set_curtok(curtok()->next);
  }
}

static void register_local_extern_decl(token_ident_t *name,
                                       psx_type_t *canonical_type) {
  if (!name || !canonical_type) return;
  psx_global_object_result_t result = {0};
  if (!lower_global_object_declaration(
          &(psx_global_object_request_t){
              .name = name->str,
              .name_len = name->len,
              .type = canonical_type,
              .is_extern_decl = 1,
              .diag_tok = (token_t *)name,
          },
          &result)) {
    psx_diag_ctx((token_t *)name, "decl",
                 "local extern declaration lowering failed");
  }
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
  int is_long_double_base = 0;
  int base_pointer_levels = 0;
  const psx_type_t *base_decl_type = NULL;
  psx_type_spec_result_t type_spec = {0};
  type_spec.kind = TK_EOF;
  resolve_local_typedef_decl_spec(&base_kind, &elem_size, &fp_kind,
                                  &tag_kind, &tag_name, &tag_len, &is_pointer_base,
                                  &is_long_double_base, &base_pointer_levels,
                                  &base_decl_type, &type_spec);

  int td_pointee_const = type_spec.is_const_qualified ? 1 : 0;
  int td_pointee_volatile = type_spec.is_volatile_qualified ? 1 : 0;
  int td_is_unsigned = (base_kind == TK_UNSIGNED) || type_spec.is_unsigned;
  int td_is_long_double = type_spec.is_long_double || is_long_double_base;
  int td_is_complex = type_spec.is_complex;

  parse_local_typedef_declarator_list(base_kind, elem_size, fp_kind, tag_kind, tag_name, tag_len,
                                      is_pointer_base,
                                      base_decl_type,
                                      td_pointee_const, td_pointee_volatile,
                                      td_is_unsigned, td_is_long_double,
                                      td_is_complex);
  tk_expect(';');
  return psx_node_new_num(0);
}

static void resolve_local_typedef_decl_spec(token_kind_t *base_kind, int *elem_size,
                                            tk_float_kind_t *fp_kind,
                                            token_kind_t *tag_kind, char **tag_name, int *tag_len,
                                            int *is_pointer_base, int *is_long_double_base,
                                            int *base_pointer_levels,
                                            const psx_type_t **base_decl_type,
                                            psx_type_spec_result_t *type_spec) {
  if (base_pointer_levels) *base_pointer_levels = 0;
  if (base_decl_type) *base_decl_type = NULL;
  if (type_spec) {
    memset(type_spec, 0, sizeof(*type_spec));
    type_spec->kind = TK_EOF;
  }
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
    psx_apply_parsed_tag_declaration(
        *tag_kind, *tag_name, *tag_len,
        PSX_TAG_DECLARATION_REFERENCE, 0, 0, 0, curtok());
    *elem_size = psx_ctx_get_tag_size(*tag_kind, *tag_name, *tag_len);
    return;
  }

  psx_type_spec_result_t builtin_spec;
  token_kind_t builtin_kind = psx_consume_type_kind_ex(&builtin_spec);
  if (type_spec) *type_spec = builtin_spec;
  if (builtin_kind != TK_EOF) {
    *base_kind = builtin_kind;
    resolve_builtin_type_local(builtin_kind, elem_size, fp_kind);
    if (is_long_double_base) *is_long_double_base = builtin_spec.is_long_double;
    return;
  }
  if (psx_ctx_is_typedef_name_token(curtok())) {
    token_ident_t *id = (token_ident_t *)curtok();
    psx_typedef_info_t info = {0};
    if (base_decl_type &&
        psx_ctx_find_typedef_name(id->str, id->len, &info)) {
      *base_decl_type = psx_ctx_typedef_decl_type(&info);
    }
    if (base_pointer_levels) {
      *base_pointer_levels = psx_ctx_get_typedef_pointer_levels(id->str, id->len);
    }
    resolve_typedef_name_ref_local(base_kind, elem_size, fp_kind,
                                   tag_kind, tag_name, tag_len, is_pointer_base,
                                   NULL, NULL, NULL, is_long_double_base);
    return;
  }
  diag_emit_tokf(DIAG_ERR_PARSER_TYPE_NAME_REQUIRED, curtok(), "%s",
                 diag_message_for(DIAG_ERR_PARSER_TYPE_NAME_REQUIRED));
}

static void define_local_typedef_from_declarator(token_ident_t *name,
                                                 token_kind_t base_kind, int elem_size,
                                                 tk_float_kind_t fp_kind,
                                                 token_kind_t tag_kind, char *tag_name, int tag_len,
                                                 int td_pointee_const, int td_pointee_volatile,
                                                 int td_is_unsigned, int td_is_long_double,
                                                 int td_is_complex,
                                                 const psx_type_t *base_decl_type,
                                                 decl_declarator_state_t *decl_state) {
  psx_declarator_shape_t resolved_declarator_shape;
  resolve_decl_declarator_shape(
      decl_state, &resolved_declarator_shape, (token_t *)name);
  psx_type_t *canonical_type = psx_resolve_decl_type(
      &(psx_decl_type_request_t){
          .base_kind = base_kind,
          .elem_size = elem_size,
          .fp_kind = fp_kind,
          .tag_kind = tag_kind,
          .tag_name = tag_name,
          .tag_len = tag_len,
          .is_unsigned = td_is_unsigned,
          .is_complex = td_is_complex,
          .is_const_qualified = td_pointee_const,
          .is_volatile_qualified = td_pointee_volatile,
          .is_long_double = td_is_long_double,
          .base_decl_type = base_decl_type,
          .declarator_shape = &resolved_declarator_shape,
      });
  psx_apply_parsed_typedef_declaration(
      name->str, name->len, canonical_type, curtok());
}

static void parse_local_typedef_declarator_list(token_kind_t base_kind, int elem_size,
                                                tk_float_kind_t fp_kind,
                                                token_kind_t tag_kind, char *tag_name, int tag_len,
                                                int is_pointer_base,
                                                const psx_type_t *base_decl_type,
                                                int td_pointee_const, int td_pointee_volatile,
                                                int td_is_unsigned, int td_is_long_double,
                                                int td_is_complex) {
  for (;;) {
    int is_ptr = is_pointer_base;
    unsigned int ptr_const_mask = 0;
    unsigned int ptr_volatile_mask = 0;
    int ptr_levels = 0;
    decl_declarator_state_t decl_state;
    reset_decl_declarator_state(&decl_state);
    token_ident_t *name = consume_direct_declarator_name(
        &decl_state, &is_ptr, &ptr_const_mask, &ptr_volatile_mask, &ptr_levels);
    /* declarator が `*` を 1 つでも追加していれば decl_added_pointer=1。is_ptr が base 由来
     * のみか declarator 由来かを判別する (ptr_levels は declarator 側の `*` 個数を持つ)。 */
    define_local_typedef_from_declarator(name,
                                         base_kind, elem_size, fp_kind,
                                         tag_kind, tag_name, tag_len,
                                         td_pointee_const, td_pointee_volatile,
                                         td_is_unsigned, td_is_long_double, td_is_complex,
                                         base_decl_type,
                                         &decl_state);
    dispose_decl_declarator_state(&decl_state);
    if (!tk_consume(',')) break;
  }
}
