#include "parser.h"
#include "parser_public.h"  /* ps_iter_globals prototype */
#include "arena.h"
#include "node_utils.h"
#include "semantic_ctx.h"
#include "semantic_pass.h"
#include "decl.h"
#include "core.h"
#include "alignas_value.h"
#include "anon_tag.h"
#include "array_suffixes.h"
#include "diag.h"
#include "dynarray.h"
#include "enum_const.h"
#include "expr.h"
#include "global_registry.h"
#include "initializer_syntax.h"
#include "ret_pointee_array.h"
#include "stmt.h"
#include "struct_layout.h"
#include "type.h"
#include "../diag/diag.h"
#include "../lowering/global_object_lowering.h"
#include "../lowering/parameter_lowering.h"
#include "../lowering/static_data_initializer.h"
#include "../lowering/vla_lowering.h"
#include "../tokenizer/tokenizer.h"
#include "../pragma_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ps_gvar_is_extern_decl(const global_var_t *gv) {
  return (gv && gv->is_extern_decl) ? 1 : 0;
}

static const psx_type_t *gvar_view_skip_arrays(const psx_type_t *type) {
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
  return type;
}

static int gvar_view_array_strides_from_type(const psx_type_t *type,
                                             int *deref_size,
                                             int *outer_stride,
                                             int *mid_stride,
                                             int *extra_strides,
                                             int *extra_count) {
  if (deref_size) *deref_size = 0;
  if (outer_stride) *outer_stride = 0;
  if (mid_stride) *mid_stride = 0;
  if (extra_count) *extra_count = 0;
  if (extra_strides) {
    for (int i = 0; i < 5; i++) extra_strides[i] = 0;
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

  if (deref_size) *deref_size = strides[n - 1];
  if (n >= 2 && outer_stride) *outer_stride = strides[0];
  if (n >= 3 && mid_stride) *mid_stride = strides[1];
  int count = 0;
  for (int i = 2; i < n - 1 && count < 5; i++) {
    if (extra_strides) extra_strides[count] = strides[i];
    count++;
  }
  if (extra_count) *extra_count = count;
  return 1;
}

static void psx_gvar_view_apply_decl_type(psx_gvar_view_t *view,
                                          const psx_type_t *type) {
  if (!view || !type) return;
  int type_size = ps_type_sizeof(type);
  if (type_size > 0 || type->kind == PSX_TYPE_ARRAY) view->type_size = type_size;
  view->is_array = type->kind == PSX_TYPE_ARRAY ? 1 : 0;
  view->fp_kind = TK_FLOAT_KIND_NONE;
  view->deref_size = 0;
  view->outer_stride = 0;
  view->mid_stride = 0;
  view->extra_strides_count = 0;
  for (int i = 0; i < 5; i++) view->extra_strides[i] = 0;
  int deref_size = 0;
  int outer_stride = 0;
  int mid_stride = 0;
  int extra_strides[5] = {0};
  int extra_count = 0;
  if (gvar_view_array_strides_from_type(type, &deref_size, &outer_stride,
                                        &mid_stride, extra_strides,
                                        &extra_count)) {
    view->deref_size = deref_size;
    view->outer_stride = outer_stride;
    view->mid_stride = mid_stride;
    view->extra_strides_count = (unsigned char)extra_count;
    for (int i = 0; i < 5; i++) view->extra_strides[i] = extra_strides[i];
  } else {
    int type_deref_size = ps_type_deref_size(type);
    if (type_deref_size > 0) view->deref_size = type_deref_size;
    if (type->outer_stride > 0) view->outer_stride = type->outer_stride;
    if (type->mid_stride > 0) view->mid_stride = type->mid_stride;
    if (type->extra_strides_count > 0) {
      view->extra_strides_count = type->extra_strides_count;
      for (int i = 0; i < type->extra_strides_count && i < 5; i++)
        view->extra_strides[i] = type->extra_strides[i];
    }
  }

  const psx_type_t *base = gvar_view_skip_arrays(type);
  int is_tag_pointer = 0;
  view->tag_kind = TK_EOF;
  view->tag_name = NULL;
  view->tag_len = 0;
  view->is_tag_pointer = 0;
  if (base && base->kind == PSX_TYPE_POINTER) {
    is_tag_pointer = 1;
    base = base->base;
    base = gvar_view_skip_arrays(base);
  }
  if (!base) return;

  if (base->kind == PSX_TYPE_FLOAT || base->kind == PSX_TYPE_COMPLEX) {
    if (!is_tag_pointer && type->kind != PSX_TYPE_POINTER) view->fp_kind = base->fp_kind;
    return;
  }
  if (base->kind == PSX_TYPE_STRUCT || base->kind == PSX_TYPE_UNION) {
    view->tag_kind = base->tag_kind;
    view->tag_name = base->tag_name;
    view->tag_len = base->tag_len;
    view->is_tag_pointer = is_tag_pointer ? 1 : 0;
  }
}

psx_gvar_view_t psx_gvar_view(const global_var_t *gv) {
  if (!gv) return (psx_gvar_view_t){.tag_kind = TK_EOF, .fp_kind = TK_FLOAT_KIND_NONE};
  psx_gvar_view_t view = {
      .name = gv->name,
      .name_len = gv->name_len,
      .tag_kind = TK_EOF,
      .type_size = gv->type_size,
      .init_count = gv->init_count,
      .has_init = gv->has_init,
      .init_val = gv->init_val,
      .init_symbol = gv->init_symbol,
      .init_symbol_len = gv->init_symbol_len,
      .init_symbol_offset = gv->init_symbol_offset,
      .fval = gv->fval,
      .fp_kind = TK_FLOAT_KIND_NONE,
      .is_extern_decl = gv->is_extern_decl ? 1 : 0,
      .is_static = gv->is_static ? 1 : 0,
      .is_thread_local = gv->is_thread_local ? 1 : 0,
      .has_init_fvalues = gv->init_fvalues ? 1 : 0,
  };
  psx_gvar_view_apply_decl_type(&view,
                                psx_gvar_get_decl_type((global_var_t *)gv));
  return view;
}

int ps_gvar_is_thread_local(const global_var_t *gv) {
  return (gv && gv->is_thread_local) ? 1 : 0;
}

int ps_gvar_is_static_storage(const global_var_t *gv) {
  return (gv && gv->is_static) ? 1 : 0;
}

int ps_gvar_is_extern_decl_by_name(char *name, int len) {
  return ps_gvar_is_extern_decl(ps_find_global_var(name, len));
}

int ps_gvar_is_thread_local_by_name(char *name, int len) {
  return ps_gvar_is_thread_local(ps_find_global_var(name, len));
}

int ps_gvar_is_static_storage_by_name(char *name, int len) {
  return ps_gvar_is_static_storage(ps_find_global_var(name, len));
}

char *ps_gvar_name(const global_var_t *gv) {
  return gv ? gv->name : NULL;
}

int ps_gvar_name_len(const global_var_t *gv) {
  return gv ? gv->name_len : 0;
}

psx_string_lit_view_t ps_string_lit_view(const string_lit_t *lit) {
  if (!lit) return (psx_string_lit_view_t){0};
  return (psx_string_lit_view_t){
      .label = lit->label,
      .str = lit->str,
      .len = lit->len,
      .char_width = lit->char_width,
  };
}

psx_float_lit_view_t ps_float_lit_view(const float_lit_t *lit) {
  if (!lit) return (psx_float_lit_view_t){.fp_kind = TK_FLOAT_KIND_NONE};
  return (psx_float_lit_view_t){
      .fval = lit->fval,
      .id = lit->id,
      .fp_kind = lit->fp_kind,
  };
}

typedef struct {
  /* funcdef の外側 declarator (`int (*f(...))(...)`) で `(*` を見たら 1。
   * 戻り値型を関数ポインタ (= ポインタ) として扱うため、declarator parse から
   * funcdef へ明示的に渡す。 */
  int outer_declarator_is_ptr;
  /* 通常の関数戻り値型基底の `*` 段数 (`int **g()` で 2)。
   * `int *(*f(void))(void)` のように関数ポインタを返す関数では、
   * この段数は「返された関数ポインタの戻り値側」に属する。 */
  int ret_pointer_levels;
  /* 関数ポインタを返す関数の outer declarator object 段数:
   * `int (*f(void))(int)` は 1、`int (**f(void))(int)` は 2。 */
  int funcptr_object_pointer_levels;
  int is_funcptr;
  psx_decl_funcptr_sig_t funcptr_sig;
  psx_type_spec_result_t type_spec;
  /* 戻り値型が「配列へのポインタ」`int (*f())[N]` / `int (*f())[N][M]` のとき、
   * pointee 配列の先頭次元 N と第2次元 M、`[` suffix の個数を捕捉する。 */
  int pointee_first_dim;
  int pointee_second_dim;
  int pointee_dim_count;
} func_ret_parse_state_t;
typedef struct {
  psx_type_spec_result_t type_spec;
  token_t *typespec_start;
  int elem_size;
  int is_extern;
  int is_static;
  int is_thread_local;
  int is_typedef;
  token_kind_t base_kind;
  int is_unsigned;
  int is_long_double;
  tk_float_kind_t fp_kind;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int base_is_ptr;
  int is_complex;
  /* 基底型がポインタ typedef のときの段数 (`typedef int **PP; PP gp;` で 2)。 */
  int base_pointer_levels;
  /* typedef 参照の正本。派生 typedef はこの型木へ declarator operator を適用し、
   * is_pointer/array_dims/funcptr_sig から型を再構築しない。 */
  const psx_type_t *base_decl_type;
  psx_decl_funcptr_sig_t base_funcptr_sig;
  int pointee_const;
  int pointee_volatile;
  // typedef 由来の配列型の dims (使用側 `M2 g;` で typedef の `[2][3]` を保持)。
  int td_array_dims[8];
  int td_array_dim_count;
  /* pointer-to-array typedef (`typedef int (*PA)[3]; PA gp;`) のポインティ各次元数。
   * 配列サフィックスへ連結せず、pointer-to-array 経路でのみ使う。 */
  int td_ptr_pointee_dim_count;
} toplevel_decl_spec_t;

typedef struct {
  int arr_total;
  int is_array;
  int has_incomplete_array;
  int first_dim;
  int dims[8];
  int dim_count;
} toplevel_array_suffix_t;

static node_t *funcdef(void);
static void parse_toplevel_decl_after_type(const toplevel_decl_spec_t *spec);
static int has_next_toplevel_declarator(void);
static token_kind_t resolve_toplevel_typedef_base_kind_for_store(const toplevel_decl_spec_t *spec);
typedef struct {
  token_ident_t *name;
  int is_ptr;
  int paren_array_mul;
  int has_func_suffix;
  psx_funcptr_signature_t func_suffix_sig;
  int ptr_levels;
  int funcptr_object_pointer_levels;
  int ptr_in_paren_group;
  int paren_array_present;
  int paren_array_dims[8];
  int paren_array_dim_count;
  psx_funcptr_signature_t returned_funcptr_suffix_sig;
  int func_suffix_count;
  /* Identifier-outward operators emitted by the recursive declarator parser.
   * The scalar fields above remain compatibility mirrors for registration. */
  psx_declarator_shape_t declarator_shape;
} toplevel_declarator_head_t;
static toplevel_declarator_head_t new_toplevel_declarator_head(int base_is_ptr);
static toplevel_declarator_head_t parse_toplevel_declarator_head(const toplevel_decl_spec_t *spec,
                                                                 int base_is_ptr, int require_name);
static void parse_toplevel_declarator_stmt(const toplevel_decl_spec_t *spec, int base_is_ptr,
                                           void (*apply)(const toplevel_decl_spec_t *,
                                                         toplevel_declarator_head_t));
static void parse_toplevel_declarator_list_with_apply(const toplevel_decl_spec_t *spec, int base_is_ptr,
                                                      void (*apply)(const toplevel_decl_spec_t *,
                                                                    toplevel_declarator_head_t));
static void apply_toplevel_typedef_from_head(const toplevel_decl_spec_t *spec,
                                             toplevel_declarator_head_t head);
static void define_toplevel_typedef_from_declarator(const toplevel_decl_spec_t *spec,
                                                    toplevel_declarator_head_t head);
static void register_toplevel_typedef_name(token_ident_t *name, token_kind_t stored_base_kind,
                                           const toplevel_decl_spec_t *spec,
                                           toplevel_declarator_head_t head,
                                           int typedef_sizeof, int td_is_array,
                                           int td_first_dim,
                                           const int *td_dims, int td_dim_count);
static psx_type_t *build_toplevel_derived_typedef_type(
    const toplevel_decl_spec_t *spec, toplevel_declarator_head_t head,
    const int *decl_dims, int decl_dim_count);
static int is_toplevel_typedef_unsigned(token_kind_t stored_base_kind, const toplevel_decl_spec_t *spec);
static void guard_toplevel_declarator_count(int declarator_count);
static void apply_toplevel_object_from_head(const toplevel_decl_spec_t *spec,
                                            toplevel_declarator_head_t head);
static void finalize_toplevel_object_declarator(const toplevel_decl_spec_t *spec, global_var_t *gv);
static void apply_toplevel_object_initializer(global_var_t *gv);
static void consume_toplevel_extern_initializer_if_any(void);
static int parse_toplevel_declaration_like(void);
static void parse_toplevel_decl_spec(toplevel_decl_spec_t *spec);
static int is_toplevel_decl_like_start(token_t *tok);
static void consume_toplevel_typedef_storage_class(toplevel_decl_spec_t *spec);
static void apply_toplevel_builtin_decl_spec(toplevel_decl_spec_t *spec,
                                             token_kind_t type_kind,
                                             const psx_type_spec_result_t *type_spec);
static void apply_toplevel_typedef_decl_spec(toplevel_decl_spec_t *spec,
                                             token_kind_t td_base, int td_elem, tk_float_kind_t td_fp,
                                             token_kind_t td_tag, char *td_tag_name, int td_tag_len,
                                             int td_is_ptr, int td_is_unsigned, int td_is_long_double);
static void apply_toplevel_typedef_prefix_flags(toplevel_decl_spec_t *spec,
                                                const psx_type_spec_result_t *prefix_spec);
static void resolve_toplevel_tag_decl_layout_or_ref(toplevel_decl_spec_t *spec);
static void reset_toplevel_decl_spec_state(toplevel_decl_spec_t *spec);
static void skip_post_type_cv_qualifiers_into(psx_type_spec_result_t *out);
static int parse_toplevel_tag_decl_spec(toplevel_decl_spec_t *spec,
                                        psx_type_spec_result_t *prefix_spec);
static int parse_toplevel_typedef_name_spec(toplevel_decl_spec_t *spec,
                                            const psx_type_spec_result_t *prefix_spec);
static void parse_toplevel_tag_head(token_kind_t *out_kind, char **out_name, int *out_len);
static void parse_toplevel_tag_decl(void);
static token_ident_t *parse_toplevel_decl_name(const toplevel_decl_spec_t *spec,
                                               toplevel_declarator_head_t *head);
static token_ident_t *consume_decl_ident_or_error(int require_name);
static void emit_decl_name_required_diag(void);
static void consume_toplevel_decl_suffixes(toplevel_declarator_head_t *head,
                                           int nesting_depth,
                                           int direct_was_parenthesized);
static token_ident_t *parse_decl_name_recursive(const toplevel_decl_spec_t *spec,
                                                toplevel_declarator_head_t *head,
                                                int require_name, int nesting_depth);
static int is_toplevel_function_signature(token_t *tok);
static int is_tag_return_function_signature(token_t *tok);
static void skip_balanced_group(token_kind_t lkind, token_kind_t rkind);
typedef struct {
  psx_declarator_shape_t declarator_shape;
  int inner_dim_consts[7];
  token_ident_t *inner_dim_idents[7];
  int inner_dim_count;
  int pointer_array_outer_dim;
  psx_funcptr_signature_t func_suffix_sig;
  psx_funcptr_signature_t returned_funcptr_suffix_sig;
  int func_suffix_count;
  int funcptr_object_pointer_levels;
} param_declarator_state_t;
static token_ident_t *parse_param_declarator_name(param_declarator_state_t *decl_state,
                                                  int *out_is_array_declarator, int *out_is_pointer_declarator,
                                                  int *out_pointer_levels,
                                                  int *out_inner_first_dim, int *out_inner_second_dim,
                                                  token_ident_t **out_inner_first_dim_ident,
                                                  token_ident_t **out_inner_second_dim_ident,
                                                  int *out_has_func_suffix);
static token_ident_t *parse_param_declarator_name_recursive(param_declarator_state_t *decl_state,
                                                            int *out_is_array_declarator,
                                                            int *out_is_pointer_declarator,
                                                            int *out_pointer_levels,
                                                            int *out_inner_first_dim,
                                                            int *out_inner_second_dim,
                                                            token_ident_t **out_inner_first_dim_ident,
                                                            token_ident_t **out_inner_second_dim_ident,
                                                            int *out_has_func_suffix);
static int parse_param_decl(node_func_t *node, int *nargs, int *arg_cap, int count_unnamed);
static int is_param_decl_spec_start(void);
typedef struct {
  psx_type_spec_result_t type_spec;
  token_kind_t base_type_kind;
  int saw_typedef_name;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int struct_size;
  int elem_size;
  // typedef した型が配列の場合に typedef 名から取り出した情報を保持する。
  // 例: `typedef int row_t[3]` → typedef_is_array=1, typedef_sizeof_size=12
  // 仮引数 `row_t *a` で pointer-to-array として扱うため。
  int typedef_is_array;
  int typedef_sizeof_size;
  // 多次元 typedef array (`typedef int M[3][4]`) のとき M *p で
  // mid_stride = sizeof_size / first_dim = 16 を計算するのに使う。
  int typedef_array_first_dim;
  // 多次元 typedef array (3+ 次元) の dims を仮引数 (`M *p`) に
  // 反映するため保持する。
  int typedef_array_dims[8];
  int typedef_array_dim_count;
  // float/double 仮引数を ABI に従い d0..d7 で受け取るための種別。
  tk_float_kind_t fp_kind;
  // `double _Complex` / `float _Complex` 仮引数。HFA として 2 FP レジスタで受ける。
  int is_complex;
  // 基底型 (typedef 名) がポインタのとき (`typedef char* Str; f(Str s)`) のポインタ段数。
  // 宣言子側の `*` (param_is_ptr) と合成して実効ポインタ性を決める。非ポインタは 0。
  int base_is_pointer;
  int base_pointer_levels;
  const psx_type_t *base_decl_type;
  psx_decl_funcptr_sig_t funcptr_sig;
  // 基底型が unsigned か (`unsigned char* p` の pointee zero-extend に使う)。
  int is_unsigned;
  int is_long_double;
} param_decl_spec_t;
static int parse_param_tag_decl_spec(param_decl_spec_t *out);
static void parse_param_scalar_decl_spec(param_decl_spec_t *out);
static void parse_param_decl_spec(param_decl_spec_t *out);
static void parse_func_decl_spec(func_ret_parse_state_t *ret_state,
                                 token_kind_t *ret_kind, tk_float_kind_t *ret_fp_kind,
                                 token_ident_t **ret_tag, int *ret_is_ptr,
                                 int *ret_is_unsigned);
static void parse_pointer_suffix_flags(func_ret_parse_state_t *ret_state, int *out_is_ptr);
static void resolve_func_ret_typedef(func_ret_parse_state_t *ret_state,
                                     token_kind_t *ret_kind, tk_float_kind_t *ret_fp_kind,
                                     token_ident_t **ret_tag, int *ret_is_ptr,
                                     int *ret_is_unsigned);
static void resolve_func_ret_tag_spec(token_kind_t *ret_kind, token_ident_t **ret_tag);
static token_ident_t *parse_func_declarator(func_ret_parse_state_t *ret_state,
                                            int *out_is_variadic, int *out_has_unnamed_param,
                                            node_t ***out_args, int *out_nargs);
static token_ident_t *parse_func_name_declarator_recursive(void);
static void parse_static_assert_toplevel(void);
static token_t *skip_decl_prefix_lookahead(token_t *t);
static token_kind_t parse_atomic_type_specifier(void);
static tk_float_kind_t fp_kind_for_type_kind_toplevel(token_kind_t type_kind);
static void psx_type_spec_result_reset(psx_type_spec_result_t *out);
static void skip_cv_qualifiers_into(psx_type_spec_result_t *out);
static void resolve_toplevel_typedef_ref(toplevel_decl_spec_t *spec);
typedef struct {
  token_ident_t *name;
  const char *diag_context;
  int ret_struct_size;
  tk_float_kind_t ret_fp_kind;
  token_kind_t ret_token_kind;
  int ret_is_ptr;
  int ret_base_unsigned;
  int ret_pointee_const;
  int ret_pointee_volatile;
  int ret_is_complex;
  int ret_is_void;
  token_kind_t ret_tag_kind;
  char *ret_tag_name;
  int ret_tag_len;
  int ret_pointer_levels;
  int ret_pointee_first_dim;
  int ret_pointee_second_dim;
  int ret_pointee_dim_count;
  int is_variadic;
  int nargs;
  node_t **args;
  int ret_is_funcptr;
  psx_decl_funcptr_sig_t funcptr_sig;
  node_func_t *func_node;
} psx_function_signature_t;

static int function_ret_scalar_size(token_kind_t kind) {
  switch (kind) {
    case TK_BOOL:
    case TK_CHAR:
      return 1;
    case TK_SHORT:
      return 2;
    case TK_LONG:
    case TK_DOUBLE:
      return 8;
    case TK_INT:
    case TK_SIGNED:
    case TK_UNSIGNED:
    case TK_FLOAT:
      return 4;
    default:
      return 4;
  }
}

static psx_type_t *function_signature_ret_base_type(const psx_function_signature_t *sig) {
  if (!sig) return NULL;
  if (psx_ctx_is_tag_aggregate_kind(sig->ret_tag_kind)) {
    int size = sig->ret_struct_size;
    if (size <= 0 && sig->ret_tag_name) {
      size = psx_ctx_get_tag_size(sig->ret_tag_kind, sig->ret_tag_name,
                                  sig->ret_tag_len);
    }
    int scope_depth = ps_ctx_get_tag_scope_depth(
        sig->ret_tag_kind, sig->ret_tag_name, sig->ret_tag_len);
    return psx_type_new_tag(
        sig->ret_tag_kind, sig->ret_tag_name, sig->ret_tag_len,
        scope_depth >= 0 ? scope_depth + 1 : 0, size);
  }
  if (sig->ret_is_complex) {
    tk_float_kind_t fp = sig->ret_fp_kind != TK_FLOAT_KIND_NONE
                             ? sig->ret_fp_kind
                             : TK_FLOAT_KIND_DOUBLE;
    psx_type_t *type = psx_type_new(PSX_TYPE_COMPLEX);
    type->fp_kind = fp;
    type->size = fp == TK_FLOAT_KIND_FLOAT ? 8 : 16;
    type->align = 8;
    return type;
  }
  tk_float_kind_t fp = sig->ret_fp_kind;
  if (fp == TK_FLOAT_KIND_NONE) {
    if (sig->ret_token_kind == TK_FLOAT) fp = TK_FLOAT_KIND_FLOAT;
    else if (sig->ret_token_kind == TK_DOUBLE) fp = TK_FLOAT_KIND_DOUBLE;
  }
  if (fp != TK_FLOAT_KIND_NONE)
    return psx_type_new_float(fp, fp == TK_FLOAT_KIND_FLOAT ? 4 : 8);
  if (sig->ret_is_void || sig->ret_token_kind == TK_VOID) {
    psx_type_t *type = psx_type_new(PSX_TYPE_VOID);
    type->scalar_kind = TK_VOID;
    return type;
  }
  token_kind_t kind = sig->ret_token_kind != TK_EOF ? sig->ret_token_kind : TK_INT;
  psx_type_t *type =
      psx_type_new_integer(kind, function_ret_scalar_size(kind),
                           sig->ret_base_unsigned);
  return type;
}

static void function_type_apply_pointee_qualifiers(psx_type_t *type,
                                                   int is_const,
                                                   int is_volatile) {
  if (!type || type->kind != PSX_TYPE_POINTER || !type->base) return;
  if (is_const) type->base->is_const_qualified = 1;
  if (is_volatile) type->base->is_volatile_qualified = 1;
  psx_type_t *leaf = type->base;
  while (leaf && leaf->kind == PSX_TYPE_ARRAY && leaf->base) leaf = leaf->base;
  if (leaf && leaf != type->base) {
    if (is_const) leaf->is_const_qualified = 1;
    if (is_volatile) leaf->is_volatile_qualified = 1;
  }
}

static psx_type_t *function_wrap_ret_pointee_array(psx_type_t *base,
                                                   int first_dim,
                                                   int second_dim) {
  if (!base || first_dim <= 0) return base;
  int elem_size = ps_type_sizeof(base);
  if (elem_size <= 0) return base;
  if (second_dim > 0) {
    int inner_size = second_dim * elem_size;
    psx_type_t *inner =
        psx_type_new_array(base, second_dim, inner_size, elem_size, 0);
    inner->outer_stride = elem_size;
    int outer_size = first_dim * inner_size;
    psx_type_t *outer =
        psx_type_new_array(inner, first_dim, outer_size, inner_size, 0);
    outer->outer_stride = inner_size;
    outer->mid_stride = elem_size;
    return outer;
  }
  int array_size = first_dim * elem_size;
  psx_type_t *array =
      psx_type_new_array(base, first_dim, array_size, elem_size, 0);
  array->outer_stride = elem_size;
  return array;
}

static psx_type_t *function_signature_ret_type(
    const psx_function_signature_t *sig,
    psx_decl_funcptr_sig_t effective_funcptr_sig) {
  if (!sig) return NULL;
  psx_type_t *base = function_signature_ret_base_type(sig);
  int levels = sig->ret_pointer_levels > 0 ? sig->ret_pointer_levels
               : (sig->ret_is_ptr ? 1 : 0);
  if (levels <= 0) {
    if (base && sig->ret_is_funcptr &&
        ps_decl_funcptr_sig_has_payload(effective_funcptr_sig)) {
      base->funcptr_sig = ps_decl_funcptr_sig_clone(effective_funcptr_sig);
    }
    return base;
  }
  psx_type_t *pointee = function_wrap_ret_pointee_array(
      base, sig->ret_pointee_first_dim, sig->ret_pointee_second_dim);
  int deref_size = levels >= 2 ? 8 : ps_type_sizeof(pointee);
  if (deref_size <= 0) deref_size = 8;
  psx_type_t *type =
      psx_type_wrap_pointer_levels(pointee, levels, deref_size,
                                   base ? ps_type_sizeof(base) : 0, 0, 0);
  type->base_deref_size = base ? ps_type_sizeof(base) : 0;
  function_type_apply_pointee_qualifiers(type, sig->ret_pointee_const,
                                         sig->ret_pointee_volatile);
  if (sig->ret_is_funcptr &&
      ps_decl_funcptr_sig_has_payload(effective_funcptr_sig)) {
    type = psx_type_attach_funcptr_signature(type, effective_funcptr_sig);
  }
  return type;
}

static void validate_toplevel_object_array_suffix(const toplevel_decl_spec_t *spec,
                                                 toplevel_array_suffix_t arr);
static toplevel_array_suffix_t finish_toplevel_array_suffixes(
    const toplevel_decl_spec_t *spec, const toplevel_declarator_head_t *head);
static int compute_toplevel_typedef_sizeof(const toplevel_decl_spec_t *spec,
                                           toplevel_declarator_head_t head,
                                           int effective_is_ptr,
                                           toplevel_array_suffix_t arr);
static void register_toplevel_function_prototype(const toplevel_decl_spec_t *spec,
                                                 toplevel_declarator_head_t head);
static void register_function_signature(const psx_function_signature_t *sig);
static global_var_t *register_toplevel_object_from_declarator(const toplevel_decl_spec_t *spec,
                                                              toplevel_declarator_head_t head,
                                                              toplevel_array_suffix_t arr);
static int current_toplevel_extern_flag(const toplevel_decl_spec_t *spec);
static inline token_t *curtok(void);
static inline void set_curtok(token_t *tok);

static tk_float_kind_t fp_kind_for_type_kind_toplevel(token_kind_t type_kind) {
  if (type_kind == TK_FLOAT) return TK_FLOAT_KIND_FLOAT;
  if (type_kind == TK_DOUBLE) return TK_FLOAT_KIND_DOUBLE;
  return TK_FLOAT_KIND_NONE;
}

static void apply_toplevel_type_result_prefix_flags(toplevel_decl_spec_t *spec,
                                                    const psx_type_spec_result_t *type_spec) {
  spec->type_spec.is_const_qualified |= type_spec->is_const_qualified;
  spec->type_spec.is_volatile_qualified |= type_spec->is_volatile_qualified;
  spec->type_spec.is_atomic |= type_spec->is_atomic;
  spec->type_spec.is_unsigned |= type_spec->is_unsigned;
  spec->type_spec.is_long_long |= type_spec->is_long_long;
  spec->type_spec.is_plain_char |= type_spec->is_plain_char;
  spec->type_spec.is_long_double |= type_spec->is_long_double;
  spec->type_spec.is_complex |= type_spec->is_complex;
  spec->pointee_const = type_spec->is_const_qualified ? 1 : 0;
  spec->pointee_volatile = type_spec->is_volatile_qualified ? 1 : 0;
  spec->is_extern = type_spec->is_extern ? 1 : 0;
  spec->is_static = type_spec->is_static ? 1 : 0;
  spec->is_thread_local = type_spec->is_thread_local ? 1 : 0;
}

static void resolve_toplevel_typedef_ref(toplevel_decl_spec_t *spec) {
  token_ident_t *id = (token_ident_t *)curtok();
  token_kind_t td_base = TK_EOF;
  int td_elem = 8;
  tk_float_kind_t td_fp = TK_FLOAT_KIND_NONE;
  token_kind_t td_tag = TK_EOF;
  char *td_tag_name = NULL;
  int td_tag_len = 0;
  int td_is_ptr = 0;
  int td_is_array = 0;
  int td_dim_count = 0;
  int td_is_unsigned = 0;
  int td_is_long_double = 0;
  psx_typedef_info_t _ti;
  if (psx_ctx_find_typedef_name(id->str, id->len, &_ti)) {
    td_base = _ti.base_kind; td_elem = _ti.elem_size; td_fp = _ti.fp_kind;
    td_tag = _ti.tag_kind; td_tag_name = _ti.tag_name; td_tag_len = _ti.tag_len;
    td_is_ptr = _ti.is_pointer; td_is_unsigned = _ti.is_unsigned;
    td_is_long_double = _ti.is_long_double;
    td_is_array = _ti.is_array; td_dim_count = _ti.array_dim_count;
    spec->base_decl_type = psx_ctx_typedef_decl_type(&_ti);
    spec->base_funcptr_sig = psx_ctx_typedef_funcptr_sig(&_ti);
    for (int i = 0; i < td_dim_count && i < 8; i++) spec->td_array_dims[i] = _ti.array_dims[i];
  }
  /* 多段ポインタ typedef の段数 (`typedef int **PP` で 2)。単段/非ポインタは 1/0。 */
  spec->base_pointer_levels = psx_ctx_get_typedef_pointer_levels(id->str, id->len);
  spec->td_array_dim_count = (td_is_array && td_dim_count > 0) ? td_dim_count : 0;
  /* pointer-to-array typedef (is_ptr かつ is_array でないのに dims を持つ): ポインティ
   * dims を別管理する。object-array互換mirrorには混ぜない。 */
  spec->td_ptr_pointee_dim_count =
      (td_is_ptr && !td_is_array && td_dim_count > 0) ? td_dim_count : 0;
  set_curtok(curtok()->next);
  apply_toplevel_typedef_decl_spec(spec, td_base, td_elem, td_fp, td_tag, td_tag_name, td_tag_len,
                                   td_is_ptr, td_is_unsigned, td_is_long_double);
}

static void apply_toplevel_typedef_decl_spec(toplevel_decl_spec_t *spec,
                                             token_kind_t td_base, int td_elem, tk_float_kind_t td_fp,
                                             token_kind_t td_tag, char *td_tag_name, int td_tag_len,
                                             int td_is_ptr, int td_is_unsigned, int td_is_long_double) {
  spec->base_kind = td_base;
  spec->is_unsigned = td_is_unsigned ? 1 : 0;
  spec->is_long_double = td_is_long_double ? 1 : 0;
  spec->fp_kind = td_fp;
  spec->tag_kind = td_tag;
  spec->tag_name = td_tag_name;
  spec->tag_len = td_tag_len;
  spec->base_is_ptr = td_is_ptr;
  spec->elem_size = td_elem;
  if (psx_ctx_is_tag_aggregate_kind(td_tag) &&
      td_tag_name && td_tag_len > 0 &&
      psx_ctx_has_tag_type(td_tag, td_tag_name, td_tag_len)) {
    int tag_sz = psx_ctx_get_tag_size(td_tag, td_tag_name, td_tag_len);
    if (tag_sz > 0) spec->elem_size = tag_sz;
  }
}

static void reset_toplevel_decl_spec_state(toplevel_decl_spec_t *spec) {
  /* storage class フラグを宣言ごとにクリアする。tag/typedef の「修飾子なし」経路は
   * skip_cv_qualifiers を通らない (line 662 の条件付き呼び出しのみ) ため、前の宣言の
   * extern/static が漏れる。例: `extern struct S es; struct S es={7};` の 2 行目が
   * extern 扱いされ finalize が extern 分岐 (consume_toplevel_extern_initializer_if_any)
   * に入り `={7}` の brace を psx_expr_assign で食べて E3064 になっていた。builtin 経路は
   * psx_consume_type_kind 内の skip_cv_qualifiers が毎回リセットするので元から漏れない。 */
  memset(spec, 0, sizeof(*spec));
  spec->elem_size = 8;
  spec->base_kind = TK_EOF;
  spec->fp_kind = TK_FLOAT_KIND_NONE;
  spec->tag_kind = TK_EOF;
  spec->base_decl_type = NULL;
  spec->base_funcptr_sig = (psx_decl_funcptr_sig_t){0};
}

void ps_reset_translation_unit_state(void) {
  psx_global_registry_reset_translation_unit();
  toplevel_decl_spec_t spec;
  reset_toplevel_decl_spec_state(&spec);
  psx_anon_tag_reset_translation_unit_state();
  psx_expr_reset_translation_unit_state();
  psx_decl_reset_translation_unit_state();
  psx_ctx_reset_translation_unit_scope();
  pragma_pack_reset();
  arena_free_all();
}

static int parse_toplevel_tag_decl_spec(toplevel_decl_spec_t *spec,
                                        psx_type_spec_result_t *prefix_spec) {
  if (!psx_ctx_is_tag_keyword(curtok()->kind)) return 0;
  parse_toplevel_tag_head(&spec->tag_kind, &spec->tag_name, &spec->tag_len);
  spec->base_kind = spec->tag_kind;
  resolve_toplevel_tag_decl_layout_or_ref(spec);
  skip_post_type_cv_qualifiers_into(prefix_spec);
  spec->elem_size = psx_ctx_get_tag_size(spec->tag_kind, spec->tag_name, spec->tag_len);
  apply_toplevel_type_result_prefix_flags(spec, prefix_spec);
  return 1;
}

static void resolve_toplevel_tag_decl_layout_or_ref(toplevel_decl_spec_t *spec) {
  if (tk_consume('{')) {
    int member_count = 0;
    int tag_size = 0;
    int tag_align = 0;
    member_count = psx_parse_tag_definition_body(spec->tag_kind, spec->tag_name,
                                                 spec->tag_len, &tag_size, &tag_align);
    psx_ctx_define_tag_type_with_layout(spec->tag_kind, spec->tag_name,
                                        spec->tag_len, member_count, tag_size, tag_align);
    return;
  }
  if (psx_ctx_has_tag_type(spec->tag_kind, spec->tag_name, spec->tag_len)) return;
  if (spec->is_typedef &&
      psx_ctx_is_tag_aggregate_kind(spec->tag_kind)) {
    psx_ctx_define_tag_type(spec->tag_kind, spec->tag_name, spec->tag_len);
    return;
  }
  /* 未完了タグの前方宣言 (`enum E *e;` / `struct S *s;` 等)。`enum E;` と同様に登録する。 */
  psx_ctx_define_tag_type(spec->tag_kind, spec->tag_name, spec->tag_len);
}

static void parse_toplevel_tag_head(token_kind_t *out_kind, char **out_name, int *out_len) {
  *out_kind = curtok()->kind;
  set_curtok(curtok()->next);
  psx_skip_gnu_attributes();
  token_ident_t *tag = tk_consume_ident();
  if (!tag && curtok()->kind != TK_LBRACE) {
    psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_TAG_NAME));
  }
  if (tag) {
    *out_name = tag->str;
    *out_len = tag->len;
  } else {
    psx_make_anonymous_tag_name(out_name, out_len);
  }
}

static int parse_toplevel_typedef_name_spec(toplevel_decl_spec_t *spec,
                                            const psx_type_spec_result_t *prefix_spec) {
  if (!psx_ctx_is_typedef_name_token(curtok())) return 0;
  resolve_toplevel_typedef_ref(spec);
  apply_toplevel_typedef_prefix_flags(spec, prefix_spec);
  return 1;
}

static void apply_toplevel_typedef_prefix_flags(toplevel_decl_spec_t *spec,
                                                const psx_type_spec_result_t *prefix_spec) {
  /* extern/static を伝播する (builtin/tag 経路の prefix result と同じ)。
   * 以前は extern を無条件に 0 にしていたため `extern S es; S es={9};` (S は typedef) の
   * extern 宣言が tentative 定義扱いになり `.comm _es` を出し、本定義の data 出力と重複
   * シンボルで ASSEMBLE_FAIL していた。前宣言からのフラグ漏れは reset_toplevel_decl_spec_state
   * が宣言ごとに 0 クリアするので、ここで prefix result を伝播しても誤って extern にはならない。 */
  apply_toplevel_type_result_prefix_flags(spec, prefix_spec);
}

bool psx_is_decl_prefix_token(token_kind_t k) {
  return k == TK_CONST || k == TK_VOLATILE || k == TK_EXTERN || k == TK_STATIC ||
         k == TK_AUTO || k == TK_REGISTER || k == TK_INLINE || k == TK_NORETURN ||
         k == TK_THREAD_LOCAL || k == TK_ALIGNAS || k == TK_ATOMIC;
}

bool psx_is_gnu_attribute_token(const token_t *t) {
  if (!t || t->kind != TK_IDENT) return 0;
  const token_ident_t *id = (const token_ident_t *)t;
  return id->len == 13 && memcmp(id->str, "__attribute__", 13) == 0;
}

void psx_skip_gnu_attributes_at(token_t **t) {
  while (*t && psx_is_gnu_attribute_token(*t)) {
    *t = (*t)->next;
    if (!*t || (*t)->kind != TK_LPAREN) continue;
    int depth = 0;
    while (*t) {
      if ((*t)->kind == TK_LPAREN) depth++;
      else if ((*t)->kind == TK_RPAREN) {
        depth--;
        *t = (*t)->next;
        if (depth == 0) break;
        continue;
      }
      *t = (*t)->next;
    }
  }
}

void psx_skip_gnu_attributes(void) {
  while (psx_is_gnu_attribute_token(curtok())) {
    token_t *t = curtok();
    psx_skip_gnu_attributes_at(&t);
    set_curtok(t);
  }
}

static inline token_t *curtok(void) {
  return tk_get_current_token();
}

static inline void set_curtok(token_t *tok) {
  tk_set_current_token(tok);
}

static void skip_cv_qualifiers_into(psx_type_spec_result_t *out) {
  psx_type_spec_result_reset(out);
  /* C11 6.7.1p2: 宣言指定子に storage class 指定子は高々 1 個。
   * 例外として _Thread_local は static / extern と一緒に書ける。 */
  int storage_count = 0;
  int saw_thread_local = 0;
  token_t *first_storage_tok = NULL;
  while (psx_is_decl_prefix_token(curtok()->kind)) {
    if (curtok()->kind == TK_CONST) out->is_const_qualified = 1;
    if (curtok()->kind == TK_VOLATILE) out->is_volatile_qualified = 1;
    if (curtok()->kind == TK_EXTERN) out->is_extern = 1;
    if (curtok()->kind == TK_STATIC) out->is_static = 1;
    if (curtok()->kind == TK_EXTERN || curtok()->kind == TK_STATIC ||
        curtok()->kind == TK_AUTO || curtok()->kind == TK_REGISTER) {
      if (!first_storage_tok) first_storage_tok = curtok();
      storage_count++;
    }
    if (curtok()->kind == TK_THREAD_LOCAL) saw_thread_local = 1;
    if (curtok()->kind == TK_ALIGNAS) {
      set_curtok(curtok()->next);
      if (curtok()->kind != TK_LPAREN) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_ALIGNAS_LPAREN_REQUIRED));
      }
      int av = psx_parse_alignas_value();
      if (av > out->alignas_value) out->alignas_value = av;
      continue;
    }
    if (curtok()->kind == TK_ATOMIC && curtok()->next && curtok()->next->kind == TK_LPAREN) {
      return;
    }
    if (curtok()->kind == TK_ATOMIC) {
      out->is_atomic = 1;
    }
    if (curtok()->kind == TK_THREAD_LOCAL) {
      out->is_thread_local = 1;
    }
    set_curtok(curtok()->next);
  }
  /* storage class が 2 個以上同時指定されているとエラー。
   * `_Thread_local` 単独は storage_count に数えていないので
   * `_Thread_local int x;` は 0 で通り、`static _Thread_local int x;` は 1 で通る。 */
  if (storage_count > 1) {
    psx_diag_ctx(first_storage_tok, "decl",
                 "storage class 指定子は1つまでです (C11 6.7.1p2)");
  }
  (void)saw_thread_local;
  psx_skip_gnu_attributes();
}

static void skip_ptr_qualifiers(void) {
  while (curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE || curtok()->kind == TK_RESTRICT) {
    set_curtok(curtok()->next);
  }
}

/* 型指定子の直後 (`enum E const *p` 等) の cv 修飾子を読み飛ばす。 */
static void skip_post_type_cv_qualifiers_into(psx_type_spec_result_t *out) {
  while (curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE ||
         curtok()->kind == TK_RESTRICT) {
    if (curtok()->kind == TK_CONST && out) out->is_const_qualified = 1;
    if (curtok()->kind == TK_VOLATILE && out) out->is_volatile_qualified = 1;
    set_curtok(curtok()->next);
  }
  if (curtok()->kind == TK_ATOMIC && curtok()->next && curtok()->next->kind != TK_LPAREN) {
    if (out) out->is_atomic = 1;
    set_curtok(curtok()->next);
  }
  psx_skip_gnu_attributes();
}

int psx_consume_pointer_prefix_counted(int *is_ptr) {
  int count = 0;
  while (tk_consume('*')) {
    if (is_ptr) *is_ptr = 1;
    count++;
    skip_ptr_qualifiers();
  }
  return count;
}

void psx_consume_pointer_prefix(int *is_ptr) {
  (void)psx_consume_pointer_prefix_counted(is_ptr);
}

static void parse_static_assert_toplevel(void) {
  if (curtok()->kind != TK_STATIC_ASSERT) {
    diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_EXPECTED, curtok(), "%s",
                   diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_EXPECTED));
  }
  set_curtok(curtok()->next);
  tk_expect('(');
  long long cond_val = psx_parse_enum_const_expr();
  tk_expect(',');
  if (curtok()->kind != TK_STRING) {
    diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_MSG_NOT_STRING, curtok(), "%s",
                   diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_MSG_NOT_STRING));
  }
  set_curtok(curtok()->next);
  tk_expect(')');
  tk_expect(';');
  if (cond_val == 0) {
    diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_FAILED, curtok(), "%s",
                   diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_FAILED));
  }
}

static token_t *skip_decl_prefix_lookahead(token_t *t) {
  while (t && psx_is_decl_prefix_token(t->kind)) {
    if (t->kind == TK_ALIGNAS) {
      t = t->next;
      if (!t || t->kind != TK_LPAREN) return t;
      int depth = 1;
      t = t->next;
      while (t && depth > 0) {
        if (t->kind == TK_LPAREN) depth++;
        else if (t->kind == TK_RPAREN) depth--;
        t = t->next;
      }
      continue;
    }
    if (t->kind == TK_ATOMIC && t->next && t->next->kind == TK_LPAREN) {
      int depth = 0;
      t = t->next;
      while (t) {
        if (t->kind == TK_LPAREN) depth++;
        else if (t->kind == TK_RPAREN) {
          depth--;
          if (depth == 0) {
            t = t->next;
            break;
          }
        }
        t = t->next;
      }
      continue;
    }
    t = t->next;
  }
  return t;
}

static token_kind_t parse_atomic_type_specifier(void) {
  if (curtok()->kind != TK_ATOMIC) return TK_EOF;
  set_curtok(curtok()->next);
  if (!tk_consume('(')) {
    // qualifier-form: "_Atomic int" は前置指定子として扱う
    return TK_EOF;
  }
  psx_type_spec_result_t inner_spec;
  token_kind_t inner = psx_consume_type_kind_ex(&inner_spec);
  if (inner == TK_EOF) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_ATOMIC_TYPE_NAME_REQUIRED));
  }
  // Minimal support for derived declarators in _Atomic(type), e.g. _Atomic(int*).
  while (tk_consume('*')) {
    while (curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE || curtok()->kind == TK_RESTRICT) {
      set_curtok(curtok()->next);
    }
  }
  tk_expect(')');
  return inner;
}

static token_t *skip_decl_prefix_tokens_for_lookahead(token_t *t) {
  while (t && psx_is_decl_prefix_token(t->kind)) {
    if (t->kind == TK_ALIGNAS && t->next && t->next->kind == TK_LPAREN) {
      t = t->next->next;
      int depth = 1;
      while (t && depth > 0) {
        if (t->kind == TK_LPAREN) depth++;
        else if (t->kind == TK_RPAREN) depth--;
        t = t->next;
      }
      continue;
    }
    if (t->kind == TK_ATOMIC && t->next && t->next->kind == TK_LPAREN) break;
    t = t->next;
  }
  return t;
}

/* 先頭の storage-class / cv 修飾子を読み飛ばした先に tag キーワード
 * (struct/union/enum) が来るか先読みする。`static struct S g` のように
 * 修飾子が tag の前にある形を判定する。builtin 型 (`static int`) は
 * psx_consume_type_kind_ex が内部で skip するためここでは対象にしない。 */
static int toplevel_prefix_precedes_tag(void) {
  if (!psx_is_decl_prefix_token(curtok()->kind)) return 0;
  token_t *t = skip_decl_prefix_tokens_for_lookahead(curtok());
  return t && psx_ctx_is_tag_keyword(t->kind);
}

/* tag と同様、storage class / cv 修飾が typedef 名の前にあるか。`static Point p;` で
 * skip しないと Point が型と認識されず E2006 (`;` 期待) になっていた。 */
static int toplevel_prefix_precedes_typedef_name(void) {
  if (!psx_is_decl_prefix_token(curtok()->kind)) return 0;
  token_t *t = skip_decl_prefix_tokens_for_lookahead(curtok());
  return t && t != curtok() && psx_ctx_is_typedef_name_token(t);
}

static void parse_toplevel_decl_spec(toplevel_decl_spec_t *spec) {
  reset_toplevel_decl_spec_state(spec);
  consume_toplevel_typedef_storage_class(spec);
  psx_type_spec_result_t prefix_spec;
  psx_type_spec_result_reset(&prefix_spec);

  /* `static struct S g;` 等、storage class が tag の前にある場合は先に修飾子を
   * 消費する。これをしないと parse_toplevel_tag_decl_spec が tag キーワードを
   * 見つけられず E3016 になっていた (static int 等の builtin 経路は別処理)。 */
  if (toplevel_prefix_precedes_tag() || toplevel_prefix_precedes_typedef_name()) {
    skip_cv_qualifiers_into(&prefix_spec);
  }

  if (parse_toplevel_tag_decl_spec(spec, &prefix_spec)) return;

  if (parse_toplevel_typedef_name_spec(spec, &prefix_spec)) return;

  psx_type_spec_result_t type_spec;
  token_kind_t tl_kind = psx_consume_type_kind_ex(&type_spec);
  apply_toplevel_builtin_decl_spec(spec, tl_kind, &type_spec);
  apply_toplevel_type_result_prefix_flags(spec, &type_spec);
}

static void consume_toplevel_typedef_storage_class(toplevel_decl_spec_t *spec) {
  if (curtok()->kind != TK_TYPEDEF) return;
  spec->is_typedef = 1;
  set_curtok(curtok()->next);
}

static void apply_toplevel_builtin_decl_spec(toplevel_decl_spec_t *spec,
                                             token_kind_t type_kind,
                                             const psx_type_spec_result_t *type_spec) {
  spec->base_kind = type_kind;
  /* unsigned 修飾を保持する。`unsigned int` は base_kind=TK_UNSIGNED にするが、
   * `unsigned long/char/short` は base_kind=TK_LONG 等のままなので別フラグで覚える。 */
  spec->is_unsigned = (type_kind == TK_UNSIGNED) || type_spec->is_unsigned;
  spec->is_long_double = type_spec->is_long_double;
  spec->is_complex = type_spec->is_complex;
  if (type_kind == TK_INT && type_spec->is_unsigned) {
    spec->base_kind = TK_UNSIGNED;
  }
  spec->fp_kind = fp_kind_for_type_kind_toplevel(type_kind);
  spec->elem_size = 8;
  if (type_kind != TK_EOF) psx_ctx_get_type_info(type_kind, NULL, &spec->elem_size);
}

// 現在のトークンが #pragma pack マーカーなら対応する関数を呼んで消費し true を返す。
// プリプロセッサはマーカーを出現位置に挿入するだけなので、トップレベルだけでなく
// 関数本体のブロック内でも遭遇しうる。透過的に処理する。
bool psx_try_consume_pragma_pack_marker(void) {
  token_kind_t k = curtok()->kind;
  if (k == TK_PRAGMA_PACK_PUSH) {
    pragma_pack_push((int)((token_num_int_t *)curtok())->val);
    set_curtok(curtok()->next);
    return true;
  }
  if (k == TK_PRAGMA_PACK_POP) {
    pragma_pack_pop();
    set_curtok(curtok()->next);
    return true;
  }
  if (k == TK_PRAGMA_PACK_SET) {
    pragma_pack_set((int)((token_num_int_t *)curtok())->val);
    set_curtok(curtok()->next);
    return true;
  }
  if (k == TK_PRAGMA_PACK_RESET) {
    pragma_pack_reset();
    set_curtok(curtok()->next);
    return true;
  }
  return false;
}

// program = funcdef*
void ps_stream_begin(ps_stream_t *stream, tokenizer_context_t *tk_ctx, token_t *start) {
  if (stream) {
    stream->tk_ctx = tk_ctx;
  }
  if (tk_ctx) {
    tk_set_current_token_ctx(tk_ctx, start);
  }
  tk_set_current_token(start);
  /* 翻訳単位境界で関数名テーブルを初期化。
   * テストが同プロセスで複数プログラムを処理しても前回の登録が漏れないようにする。 */
  psx_ctx_reset_function_names();
}

node_t *ps_next_function(ps_stream_t *stream) {
  while (!tk_at_eof()) {
    if (psx_try_consume_pragma_pack_marker()) continue;
    if (psx_ctx_is_tag_keyword(curtok()->kind)) {
      if (!is_tag_return_function_signature(curtok())) {
        parse_toplevel_tag_decl();
        continue;
      }
      // struct/union Tag func(...) — 戻り値型がタグ型の関数定義: funcdef() へ fall through
    }
    if (parse_toplevel_declaration_like()) {
      continue;
    }
    node_t *fn = funcdef();
    if (!fn) continue; // 関数プロトタイプ宣言はASTへ載せない
    if (stream && stream->tk_ctx) {
      tk_set_current_token_ctx(stream->tk_ctx, tk_get_current_token());
    }
    return fn;
  }
  if (stream && stream->tk_ctx) {
    tk_set_current_token_ctx(stream->tk_ctx, tk_get_current_token());
  }
  return NULL;
}

void ps_stream_end(ps_stream_t *stream) {
  psx_ctx_emit_deferred_parser_warnings();
  if (stream) {
    stream->tk_ctx = NULL;
  }
}

void ps_free_processed_ast(void) {
  /* 直前に処理した関数 (および直前の非関数トップレベル宣言) の AST を解放する。
   * AST ノードは全て parser arena 上にあり、関数間で参照されない (永続データ —
   * 文字列ラベル・グローバル名・mangled static-local 名等 — は arena 外)。
   * codegen が IR 経由で AST の funcname を alias するため、必ず 1 関数の codegen を
   * 終えてから呼ぶこと。 */
  arena_free_all();
}

node_t **ps_program_ctx(tokenizer_context_t *tk_ctx, token_t *start) {
  ps_stream_t stream = {0};
  ps_stream_begin(&stream, tk_ctx, start);
  int cap = 16;
  node_t **codes = calloc(cap, sizeof(node_t*));
  int i = 0;
  node_t *fn;
  while ((fn = ps_next_function(&stream)) != NULL) {
    if (i >= cap - 1) { // NULL終端用
      cap = pda_next_cap(cap, i + 2);
      codes = pda_xreallocarray(codes, (size_t)cap, sizeof(node_t *));
    }
    codes[i++] = fn;
  }
  codes[i] = NULL;
  psx_semantic_analyze_program(codes);
  ps_stream_end(&stream);
  return codes;
}

node_t **ps_program_from(token_t *start) {
  /* 新しいコンパイル開始時に、前回のパースが残した診断フラグをクリアする。
   * これは「同一プロセス内で複数回 ps_program_from を呼ぶ」ユニットテスト用のリセット
   * (実コンパイルは 1 ファイル 1 プロセスなので影響なし)。これがないと前回パースの
   * `int g=1;` の has_init=1 や前回 funcdef の is_defined=1 が次回パースに漏れて、
   * 重複定義チェック等が誤って発火する。 */
  psx_global_registry_reset_diag_state();
  psx_ctx_reset_function_diag_state();
  psx_ctx_reset_tag_diag_state();
  return ps_program_ctx(NULL, start);
}

node_t **ps_program(void) {
  return ps_program_ctx(NULL, tk_get_current_token());
}

/* 型 spec (builtin / typedef 名 / タグ) の直後 t から、関数宣言子のシグネチャかを判定する。
 * `*name(` / `(*f())(...)` (関数ポインタ・配列へのポインタ戻り) / `(name)(...)` を扱う。
 * builtin/typedef/tag のどの戻り型でも同一なので共有する (tag 版に `(*...)` が無かったため
 * `struct S (*f())[3]` が変数と誤判定され E2006 になっていた)。 */
static int is_function_declarator_sig(token_t *t) {
  while (t && (t->kind == TK_MUL || t->kind == TK_CONST || t->kind == TK_VOLATILE)) t = t->next;
  if (!t) return 0;
  if (t->kind == TK_IDENT) {
    return t->next && t->next->kind == TK_LPAREN;
  }
  // function declarator returning function pointer / pointer-to-array:
  //   int (*f(void))(int)  /  int (*f(void))[3]  /  int (*(*f(void))(int))[3]
  if (t->kind == TK_LPAREN && t->next && t->next->kind == TK_MUL) {
    int depth = 0;
    int saw_name = 0;
    int saw_param = 0;
    token_t *u = t;
    while (u) {
      if (u->kind == TK_LPAREN) {
        if (depth >= 1 && saw_name && !saw_param) saw_param = 1;
        depth++;
      } else if (u->kind == TK_RPAREN) {
        depth--;
        if (depth == 0) {
          u = u->next;
          break;
        }
      } else if (depth >= 1 && !saw_name && u->kind == TK_IDENT) {
        // name must be followed by a parameter list: f(...)
        if (u->next && u->next->kind == TK_LPAREN) {
          saw_name = 1;
        }
      }
      u = u->next;
    }
    if (!saw_name || !saw_param || !u) return 0;
    return u->kind == TK_LPAREN || u->kind == TK_LBRACKET;
  }
  // parenthesized function declarator name: int (f)(...)
  if (t->kind == TK_LPAREN) {
    int depth = 0;
    while (t && t->kind == TK_LPAREN) {
      depth++;
      t = t->next;
    }
    if (!t || t->kind != TK_IDENT) return 0;
    t = t->next;
    while (depth-- > 0) {
      if (!t || t->kind != TK_RPAREN) return 0;
      t = t->next;
    }
    return t && t->kind == TK_LPAREN;
  }
  return 0;
}

/* 型指定子の後、宣言子列にトップレベル `,` があるか (`int f(int), g(int), a;` 等)。
 * 関数定義 `int main() {` は `)` の次が `{` なので偽。単一プロトタイプ `int f(int);` も偽。 */
static int toplevel_decl_has_comma_separated_declarators(token_t *tok) {
  token_t *t = skip_decl_prefix_lookahead(tok);
  if (!t) return 0;
  if (psx_ctx_is_tag_keyword(t->kind)) {
    t = t->next;
    if (t && t->kind == TK_IDENT) t = t->next;
  } else if (psx_ctx_is_type_token(t->kind)) {
    while (t && psx_ctx_is_type_token(t->kind)) t = t->next;
  } else if (psx_ctx_is_typedef_name_token(t)) {
    t = t->next;
  } else {
    return 0;
  }
  if (!t) return 0;
  int depth = 0;
  for (; t && t->kind != TK_EOF; t = t->next) {
    if (depth == 0 && t->kind == TK_SEMI) return 0;
    if (depth == 0 && t->kind == TK_LBRACE) return 0;
    if (depth == 0 && t->kind == TK_COMMA) return 1;
    if (t->kind == TK_LPAREN || t->kind == TK_LBRACKET) depth++;
    else if (t->kind == TK_RPAREN || t->kind == TK_RBRACKET) depth--;
  }
  return 0;
}

static int is_toplevel_function_signature(token_t *tok) {
  if (!tok) return 0;
  token_t *t = skip_decl_prefix_lookahead(tok);
  if (!t) return 0;
  /* タグ戻り型 (`static struct S *g(void){...}`): storage class を飛ばした後がタグ
   * キーワードなら専用判定へ委譲する。これがないと struct/union/enum はここで弾かれ、
   * `static struct S *g()` がオブジェクト宣言と誤判定され `;` 期待で E2006 になっていた。 */
  if (psx_ctx_is_tag_keyword(t->kind)) {
    return is_tag_return_function_signature(t);
  }
  if (psx_ctx_is_type_token(t->kind)) {
    // 複合型キーワード（unsigned long 等）を全てスキップ
    while (t && psx_ctx_is_type_token(t->kind)) t = t->next;
  } else if (psx_ctx_is_typedef_name_token(t)) {
    t = t->next; // typedef 名は1トークン
  } else {
    return 0;
  }
  return is_function_declarator_sig(t);
}

// struct/union Tag [*] ident ( のパターンを検出（戻り値型がタグ型の関数定義）
static int is_tag_return_function_signature(token_t *tok) {
  if (!tok || !psx_ctx_is_tag_keyword(tok->kind)) return 0;
  token_t *t = tok->next; // skip struct/union keyword
  if (!t) return 0;
  if (t->kind == TK_IDENT) t = t->next; // optional tag name
  if (!t) return 0;
  if (t->kind == TK_LBRACE) {
    int depth = 1;
    t = t->next;
    while (t && depth > 0) {
      if (t->kind == TK_LBRACE) depth++;
      else if (t->kind == TK_RBRACE) depth--;
      t = t->next;
    }
    if (!t) return 0;
  }
  /* タグ名/本体の後は builtin/typedef と同じ宣言子判定。これで `struct S (*f())[3]`
   * (配列へのポインタ戻り) や `struct S (*f())(int)` (関数ポインタ戻り) も検出できる。 */
  return is_function_declarator_sig(t);
}

void psx_skip_func_suffix_groups_ex(int *out_has_func_suffix,
                                    psx_funcptr_signature_t *sig) {
  psx_funcptr_signature_reset(sig);
  while (curtok()->kind == TK_LPAREN) {
    if (out_has_func_suffix) *out_has_func_suffix = 1;
    psx_skip_func_param_list(sig);
  }
}

static toplevel_array_suffix_t finish_toplevel_array_suffixes(
    const toplevel_decl_spec_t *spec, const toplevel_declarator_head_t *head) {
  toplevel_array_suffix_t out = {0};
  out.arr_total = 1;
  int dim_count = 0;
  if (head) {
    for (int i = 0; i < head->declarator_shape.count; i++) {
      const psx_declarator_op_t *op = &head->declarator_shape.ops[i];
      if (op->kind != PSX_DECL_OP_ARRAY) continue;
      out.is_array = 1;
      if (op->is_incomplete_array) {
        out.has_incomplete_array = 1;
      } else {
        out.arr_total *= op->array_len;
        if (dim_count == 0) out.first_dim = op->array_len;
      }
      if (dim_count < 8) out.dims[dim_count] = op->array_len;
      dim_count++;
    }
  }
  // 使用側 typedef 配列 (`typedef int M[3][4]; M g;`) では typedef dims を後ろに
  // 連結する。`M g[2];` のときは [2] が外側、typedef dims が内側で合計 [2][3][4]。
  if (!spec->is_typedef && spec->td_array_dim_count > 0) {
    for (int di = 0; di < spec->td_array_dim_count && dim_count < 8; di++) {
      int dim = spec->td_array_dims[di];
      if (dim > 0) {
        out.arr_total *= dim;
        if (dim_count == 0) out.first_dim = dim;
        out.dims[dim_count] = dim;
        dim_count++;
      }
    }
    out.is_array = 1;
  }
  if (dim_count > 8) dim_count = 8;
  out.dim_count = dim_count;
  return out;
}

static void parse_toplevel_declarator_list(const toplevel_decl_spec_t *spec) {
  parse_toplevel_declarator_list_with_apply(spec, 0, apply_toplevel_object_from_head);
}

static void parse_toplevel_declarator_list_with_apply(const toplevel_decl_spec_t *spec, int base_is_ptr,
                                                      void (*apply)(const toplevel_decl_spec_t *,
                                                                    toplevel_declarator_head_t)) {
  int declarator_count = 0;
  for (;;) {
    declarator_count++;
    guard_toplevel_declarator_count(declarator_count);
    toplevel_declarator_head_t head = parse_toplevel_declarator_head(spec, base_is_ptr, 1);
    apply(spec, head);
    if (!has_next_toplevel_declarator()) break;
  }
}

static void guard_toplevel_declarator_count(int declarator_count) {
  if (declarator_count <= PS_MAX_DECLARATOR_COUNT) return;
  psx_diag_ctx(curtok(), "decl",
               diag_message_for(DIAG_ERR_PARSER_DECLARATOR_LIST_TOO_LONG),
               PS_MAX_DECLARATOR_COUNT);
}

static void apply_toplevel_object_initializer(global_var_t *gv) {
  token_t *assign_tok = curtok();
  if (!tk_consume('=')) return;
  /* ファイルスコープの複合リテラル初期化子 `T g = (T){...};` は `T g = {...};` と
   * 等価 (C11 6.5.2.5)。先頭の `(型)` を読み飛ばして既存の brace 初期化経路に渡す。
   * `)` の直後が `{` であることを先読みして複合リテラルだけを対象にする。
   * ただし変数がポインタの場合 (`int *p = (int[]){...}`)、cast 型と変数型が違うため strip
   * してしまうと「ポインタを brace 初期化子で初期化」と解釈され先頭要素値がポインタスロット
   * に書き込まれて SIGBUS になる。集約 (配列 / struct 値 / union 値) のときだけ strip し、
   * ポインタ・スカラ変数では式経路 (psx_expr_assign) で compound literal 経路に乗せて hidden
   * gvar を作る。スカラ変数 `int g = (int){5}` は式経路の compound literal 短絡
   * (expr.c の `!is_arr && !want_addr && ND_NUM` 分岐) が ND_NUM を直接返すので動作する。 */
  if (curtok()->kind == TK_LPAREN) {
    token_t *t = curtok()->next;
    int depth = 1;
    while (t && depth > 0) {
      if (t->kind == TK_LPAREN) depth++;
      else if (t->kind == TK_RPAREN) { depth--; if (depth == 0) break; }
      t = t->next;
    }
    if (t && t->kind == TK_RPAREN && t->next && t->next->kind == TK_LBRACE) {
      /* strip 判定: 集約 (配列 / struct 値 / union 値) なら常に OK。ポインタ・スカラ var では、
       * brace が単一文字列 (`char *p = (char[6]){"hi"}` の "hi" のような形) ならポインタ初期化
       * として等価なので strip OK。複数値の `int *p = (int[]){10,20,30}` 形は strip すると先頭
       * 要素値がポインタスロットに書き込まれて SIGBUS なので skip し、式経路で compound literal
       * を hidden gvar に materialize させる。 */
      int gv_is_aggregate = ps_gvar_is_array(gv) || ps_gvar_is_tag_aggregate(gv);
      int may_strip = gv_is_aggregate;
      if (!may_strip) {
        token_t *brace_open = t->next;            /* '{' */
        token_t *first = brace_open->next;        /* 中身先頭 */
        if (first && first->kind == TK_STRING && first->next &&
            first->next->kind == TK_RBRACE) {
          may_strip = 1;  /* {"str"} 単一文字列 → ポインタ初期化と等価 */
        }
      }
      if (may_strip) {
        set_curtok(t->next);  /* `(型)` を捨てて `{` から始める */
      }
    }
  }
  token_t *initializer_tok = curtok();
  psx_decl_init_kind_t initializer_kind =
      curtok()->kind == TK_LBRACE ? PSX_DECL_INIT_LIST
                                  : PSX_DECL_INIT_EXPR;
  node_t *initializer = initializer_kind == PSX_DECL_INIT_LIST
                            ? psx_parse_initializer_syntax_list()
                            : psx_expr_assign();
  psx_type_t *type = psx_gvar_get_decl_type(gv);
  if (!lower_static_declaration_initializer(
          &(psx_static_declaration_initializer_request_t){
              .global = gv,
              .type = type,
              .initializer_kind = initializer_kind,
              .initializer = initializer,
              .diag_tok = initializer_tok ? initializer_tok : assign_tok,
          },
          NULL)) {
    psx_diag_ctx(initializer_tok, "static-init", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS));
  }
}

static void apply_toplevel_object_from_head(const toplevel_decl_spec_t *spec,
                                            toplevel_declarator_head_t head) {
  if (head.name && curtok()->kind == TK_LPAREN) {
    register_toplevel_function_prototype(spec, head);
    if (curtok()->kind == TK_ASSIGN) {
      set_curtok(curtok()->next);
      psx_expr_assign();
    }
    return;
  }
  if (spec->tag_kind != TK_EOF && !head.is_ptr &&
      ps_ctx_get_tag_member_count(spec->tag_kind, spec->tag_name,
                                   spec->tag_len) <= 0) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_INCOMPLETE_OBJECT_FORBIDDEN));
  }
  toplevel_array_suffix_t arr = finish_toplevel_array_suffixes(spec, &head);
  psx_ret_pointee_array_t direct_funcptr_ret_pointee_array = {0};
  if (head.is_ptr && head.has_func_suffix && arr.is_array &&
      arr.dim_count >= 1 && !arr.has_incomplete_array) {
    /* 関数ポインタグローバルが配列へのポインタを返す直書き宣言子:
     * `int (*(*g)(void))[N]` / `int (*(*g)(void))[N][M]`。
     * trailing `[N][M]` はグローバル自身の配列ではなく、戻り値ポインタの pointee 配列次元。 */
    psx_ret_pointee_array_absorb_suffix(&arr.is_array, &arr.arr_total,
                                        &arr.dim_count, &arr.first_dim,
                                        arr.dims, 8,
                                        spec->elem_size,
                                        &direct_funcptr_ret_pointee_array);
  }
  /* `T (*pa)[N]` (配列へのポインタ): `*` が括弧内 (ptr_in_paren_group) で、外側に `[N]`
   * 配列サフィックス (arr.is_array) が付く形。pa は 8B のスカラポインタで、`[N]` は
   * **pointee 配列**の次元。compat array viewは `[N]` を arr.is_array=1 /
   * arr_total=N にするため、`int *pa[N]` (ポインタの配列) と同一に登録され、pa が
   * 「N 要素配列」と誤って扱われて subscript が pa のポインタ値をロードせず &pa を base に
   * して隣接メモリを読んでいた (SIGSEGV / 誤値)。配列扱いを解除し、pointee の各次元を
   * subscript ストライドに記録する (ローカル decl.c の `(*p)[N]...` 分岐と同じ表現)。関数
   * ポインタ (`(*f)(args)`) は has_func_suffix で除外。 */
  int paren_pointer_to_array =
      head.ptr_in_paren_group ||
      (spec->base_is_ptr && head.paren_array_present);
  int is_ptr_to_array = (head.is_ptr && paren_pointer_to_array &&
                         !head.has_func_suffix && arr.is_array &&
                         arr.dim_count >= 1 && !arr.has_incomplete_array);
  /* pointer-to-array typedef 経由 `typedef int (*PA)[3]; PA gp;`: 直書き `int (*gp)[3]`
   * と違い宣言子に括弧も trailing `[N]` も無いので上の is_ptr_to_array では検出できない。
   * typedef に記録したポインティ dims から同じセットアップを行う (base のポインタ性は
   * head.is_ptr で既に立っている)。 */
  if (!is_ptr_to_array && head.is_ptr && head.ptr_levels == 0 && !arr.is_array &&
      spec->td_ptr_pointee_dim_count > 0) {
    is_ptr_to_array = 1;
  }
  if (is_ptr_to_array) {
    arr.is_array = 0;
    arr.arr_total = 1;
    arr.dim_count = 0;
  }
  /* 要素数 1 の括弧内配列 `(*g[1])(...)` / `(*g[1])`: paren_array_mul=1 だと
   * arr.is_array が立たず (is_array = base_mul>1)、スカラ funcptr/ポインタとして誤登録され
   * subscript `g[0]` が crash していた。括弧内に配列サフィックスがあれば要素数によらず
   * 配列として登録する。pointer-to-array (trailing `[N]`) は is_ptr_to_array で別処理済み。 */
  if (!is_ptr_to_array && head.paren_array_present && !arr.is_array) {
    arr.is_array = 1;
    arr.arr_total = (head.paren_array_mul > 0) ? head.paren_array_mul : 1;
    arr.dim_count = 1;
    arr.dims[0] = arr.arr_total;
  }
  /* 多次元の括弧内配列 `int (*t[2][2])(void)`: legacy paren_array_mul(積=4)だけでは
   * 個別次元が失われるため、捕捉した dims (head.paren_array_dims) で canonical
   * array chain を組み立てられるようにする。 */
  if (!is_ptr_to_array && head.paren_array_present &&
      head.paren_array_dim_count >= 2 && arr.dim_count < 2) {
    arr.is_array = 1;
    arr.dim_count = head.paren_array_dim_count;
    if (arr.dim_count > 8) arr.dim_count = 8;
    arr.arr_total = 1;
    for (int i = 0; i < arr.dim_count; i++) {
      arr.dims[i] = head.paren_array_dims[i];
      arr.arr_total *= arr.dims[i];
    }
  }
  validate_toplevel_object_array_suffix(spec, arr);
  global_var_t *gv = register_toplevel_object_from_declarator(spec, head, arr);
  /* _Generic 用: 先頭宣言子の型を name 抜きでトークン文字列化する。
   * 正本は finalize 後に gv->decl_type へ付ける。 */
  char *decl_type_sig = NULL;
  if (spec->typespec_start && gv && head.name) {
    decl_type_sig = psx_serialize_decl_type_tokens(spec->typespec_start, curtok(),
                                                   (token_t *)head.name);
  }
  finalize_toplevel_object_declarator(spec, gv);
  if (gv && decl_type_sig) {
    psx_decl_set_gvar_type_sig(gv, decl_type_sig);
  }
}

static toplevel_declarator_head_t parse_toplevel_declarator_head(const toplevel_decl_spec_t *spec,
                                                                 int base_is_ptr, int require_name) {
  toplevel_declarator_head_t out = new_toplevel_declarator_head(base_is_ptr);
  out.name = parse_toplevel_decl_name(spec, &out);
  for (int i = 0; i < out.declarator_shape.count; i++) {
    if (out.declarator_shape.ops[i].kind != PSX_DECL_OP_FUNCTION) continue;
    int object_pointer_levels = 0;
    for (int j = 0; j < i; j++) {
      if (out.declarator_shape.ops[j].kind == PSX_DECL_OP_POINTER)
        object_pointer_levels++;
    }
    out.funcptr_object_pointer_levels = object_pointer_levels;
    break;
  }
  if (!out.name && require_name) emit_decl_name_required_diag();
  return out;
}

static toplevel_declarator_head_t new_toplevel_declarator_head(int base_is_ptr) {
  toplevel_declarator_head_t out = {0};
  out.is_ptr = base_is_ptr;
  out.paren_array_mul = 1;
  psx_declarator_shape_init(&out.declarator_shape);
  psx_funcptr_signature_reset(&out.func_suffix_sig);
  psx_funcptr_signature_reset(&out.returned_funcptr_suffix_sig);
  return out;
}

static void validate_toplevel_object_array_suffix(const toplevel_decl_spec_t *spec,
                                                 toplevel_array_suffix_t arr) {
  if (!arr.has_incomplete_array || spec->is_extern) return;
  /* C11 6.7.6.2p1: 初期化子がある場合、配列のサイズは初期化子から推論できる。
   * 後段の apply_toplevel_object_initializer で type_size を再計算する。 */
  if (curtok() && curtok()->kind == TK_ASSIGN) return;
  psx_diag_ctx(curtok(), "decl", "%s",
               diag_message_for(DIAG_ERR_PARSER_INCOMPLETE_OBJECT_FORBIDDEN));
}

static void finalize_toplevel_object_declarator(const toplevel_decl_spec_t *spec, global_var_t *gv) {
  if (spec->is_extern) {
    consume_toplevel_extern_initializer_if_any();
    return;
  }
  gv->is_thread_local = spec->is_thread_local;
  apply_toplevel_object_initializer(gv);
}

static global_var_t *register_toplevel_object_from_declarator(const toplevel_decl_spec_t *spec,
                                                              toplevel_declarator_head_t head,
                                                              toplevel_array_suffix_t arr) {
  (void)arr;
  psx_type_t *canonical_type = build_toplevel_derived_typedef_type(
      spec, head, NULL, 0);
  psx_global_object_result_t result = {0};
  if (!lower_global_object_declaration(
          &(psx_global_object_request_t){
              .name = head.name->str,
              .name_len = head.name->len,
              .type = canonical_type,
              .is_extern_decl = current_toplevel_extern_flag(spec),
              .is_static = spec->is_static,
              .diag_tok = curtok(),
          },
          &result)) {
    psx_diag_ctx(curtok(), "decl",
                 "canonical global storage planning failed for '%.*s'",
                 head.name->len, head.name->str);
  }
  return result.global;
}

static int current_toplevel_extern_flag(const toplevel_decl_spec_t *spec) {
  return spec->is_extern ? 1 : 0;
}

static void consume_toplevel_extern_initializer_if_any(void) {
  if (tk_consume('=')) {
    psx_expr_assign(); // extern宣言では通常ないが消費する
  }
}

static void define_toplevel_typedef_from_declarator(const toplevel_decl_spec_t *spec,
                                                    toplevel_declarator_head_t head) {
  toplevel_array_suffix_t arr = finish_toplevel_array_suffixes(spec, &head);
  int typedef_is_funcptr =
      head.has_func_suffix && (head.is_ptr || head.ptr_in_paren_group);
  int effective_is_ptr = (head.is_ptr || typedef_is_funcptr) ? 1 : 0;
  int typedef_sizeof = compute_toplevel_typedef_sizeof(spec, head, effective_is_ptr, arr);
  token_kind_t stored_base_kind = resolve_toplevel_typedef_base_kind_for_store(spec);
  /* pointer-element 配列 typedef (`typedef BinOp OpArr3[3]`): base が pointer typedef + 配列
   * suffix のとき、is_array=1 として登録する (sizeof_size 経路と同じく ptr_in_paren_group=0
   * かつ declarator に `*` 追加なしを条件にして pointer-to-array typedef とは排他)。 */
  int base_ptr_elem_array = (head.is_ptr && !head.ptr_in_paren_group &&
                             head.ptr_levels == 0 &&
                             arr.is_array && arr.arr_total > 0) ? 1 : 0;
  /* `typedef int *IP[5]`: declarator の `*` (ptr_levels>=1) と `[N]` 配列 suffix の組合せは
   * 「N 個のポインタ配列」。is_array=1 として記録する。 */
  int decl_ptr_array = (head.is_ptr && !head.ptr_in_paren_group &&
                        head.ptr_levels >= 1 &&
                        arr.is_array && arr.arr_total > 0) ? 1 : 0;
  int td_is_array = ((!head.is_ptr || base_ptr_elem_array || decl_ptr_array) &&
                     (arr.is_array || arr.has_incomplete_array)) ? 1 : 0;
  int td_first_dim = td_is_array ? arr.first_dim : 0;
  int td_dim_count = td_is_array ? arr.dim_count : 0;
  const int *td_dims = arr.dims;
  /* 多次元 typedef chain: 基底 typedef が自身配列の場合 (`typedef int Row[3]; typedef Row Matrix[2]`)、
   * declarator の dims (= [2]) と base typedef の dims (= [3]) を [declarator..., base...] の順で
   * 結合し、新しい typedef の dims を [2, 3] にする。これがないと Matrix は int[2] として登録され、
   * sizeof(Matrix)=24 のはずが 8 になり、`Matrix m; m[i][j]` も誤計算する。
   * 条件は base が配列 (spec->td_array_dim_count>0) かつ declarator も配列 (td_is_array)、
   * かつ pointer-to-array typedef でない (!is_ptr, !ptr_in_paren_group)。
   * pointer-element 配列 typedef (`typedef IP IPA[3]`、base_ptr_elem_array) は base が array でなく
   * ポインタなので td_array_dim_count=0 で自然に除外される。 */
  int merged_dims[8] = {0};
  if (td_is_array && !head.is_ptr && !head.ptr_in_paren_group &&
      spec->td_array_dim_count > 0) {
    int n = 0;
    for (int i = 0; i < arr.dim_count && n < 8; i++) {
      merged_dims[n++] = arr.dims[i];
    }
    for (int i = 0; i < spec->td_array_dim_count && n < 8; i++) {
      merged_dims[n++] = spec->td_array_dims[i];
    }
    td_dims = merged_dims;
    td_dim_count = n;
    td_first_dim = (n > 0) ? merged_dims[0] : td_first_dim;
    int prod = 1;
    for (int i = 0; i < n; i++) prod *= merged_dims[i];
    typedef_sizeof = spec->elem_size * prod;
  }
  /* pointer-to-array typedef `typedef int (*PA)[3]`: is_ptr=1 で `*` が括弧内
   * (ptr_in_paren_group) のとき、括弧の後ろの `[3]` (arr に入っている) はポインティ
   * 配列の extent。is_array=0 のままその dims を typedef に記録する。これがないと
   * `PA p; p+1 / p[i]` が要素 1 個 (4B) しか進まず、直書き `int(*p)[3]` と食い違う。
   * 宣言側 (decl.c の `is_pointer && td_array_dim_count>0` 分岐) が outer_stride /
   * mid_stride をこの dims から設定する。ポインタの配列 `int *PB[3]` は `*` が括弧外
   * (ptr_in_paren_group=0) なので除外される。 */
  if (head.is_ptr && head.ptr_in_paren_group && arr.is_array && arr.dim_count > 0) {
    td_first_dim = arr.first_dim;
    td_dim_count = arr.dim_count;
    td_dims = arr.dims;
  }
  register_toplevel_typedef_name(head.name, stored_base_kind, spec, head, typedef_sizeof, td_is_array,
                                 td_first_dim, td_dims, td_dim_count);
  /* 多段ポインタ typedef (`typedef int **PP`) の段数を記録する。関数ポインタ
   * typedef では戻り値ポインタの `*` を除き、関数ポインタオブジェクトを指す段数だけを
   * 保存する (`int *(*G)(void)` は 1、`int (**PP)(int)` は 2)。 */
  int td_ptr_levels = head.has_func_suffix
                          ? head.funcptr_object_pointer_levels
                          : spec->base_pointer_levels + head.ptr_levels;
  if (effective_is_ptr && td_ptr_levels >= 2) {
  }
}

static void register_toplevel_typedef_name(token_ident_t *name, token_kind_t stored_base_kind,
                                           const toplevel_decl_spec_t *spec,
                                           toplevel_declarator_head_t head,
                                           int typedef_sizeof, int td_is_array,
                                           int td_first_dim,
                                           const int *td_dims, int td_dim_count) {
  psx_typedef_info_t _ti = {0};
  _ti.base_kind = stored_base_kind;
  _ti.elem_size = spec->elem_size;
  _ti.fp_kind = spec->fp_kind;
  _ti.tag_kind = spec->tag_kind;
  _ti.tag_name = spec->tag_name;
  _ti.tag_len = spec->tag_len;
  _ti.is_pointer = head.is_ptr || (head.has_func_suffix && (head.is_ptr || head.ptr_in_paren_group));
  _ti.sizeof_size = typedef_sizeof;
  _ti.pointee_const_qualified = spec->pointee_const;
  _ti.pointee_volatile_qualified = spec->pointee_volatile;
  _ti.is_unsigned = is_toplevel_typedef_unsigned(stored_base_kind, spec);
  _ti.is_long_double = (!_ti.is_pointer && !td_is_array && spec->is_long_double) ? 1 : 0;
  _ti.is_array = td_is_array;
  _ti.array_first_dim = td_first_dim;
  _ti.array_dim_count = td_dim_count;
  if (td_dims) for (int i = 0; i < td_dim_count && i < 8; i++) _ti.array_dims[i] = td_dims[i];
  psx_type_t *derived_type = build_toplevel_derived_typedef_type(
      spec, head, td_dims, td_dim_count);
  if (derived_type) {
    psx_ctx_typedef_set_decl_type(&_ti, derived_type);
  }
  if (head.has_func_suffix && (head.is_ptr || head.ptr_in_paren_group)) {
    _ti.is_funcptr = 1;
    _ti.fp_kind = TK_FLOAT_KIND_NONE;
  }
  if (!psx_ctx_define_typedef_name(name->str, name->len, &_ti)) {
    psx_diag_duplicate_with_name(curtok(), "typedef", name->str, name->len);
  }
}

static psx_type_t *toplevel_typedef_base_type(
    const toplevel_decl_spec_t *spec) {
  if (!spec) return NULL;
  if (spec->base_decl_type)
    return psx_type_clone(spec->base_decl_type);
  if (psx_ctx_is_tag_aggregate_kind(spec->tag_kind)) {
    int scope_depth = ps_ctx_get_tag_scope_depth(
        spec->tag_kind, spec->tag_name, spec->tag_len);
    return psx_type_new_tag(spec->tag_kind, spec->tag_name, spec->tag_len,
                            scope_depth >= 0 ? scope_depth + 1 : 0,
                            spec->elem_size);
  }
  if (spec->is_complex) {
    psx_type_t *type = psx_type_new(PSX_TYPE_COMPLEX);
    type->fp_kind = spec->fp_kind != TK_FLOAT_KIND_NONE
                        ? spec->fp_kind
                        : TK_FLOAT_KIND_DOUBLE;
    type->size = spec->elem_size;
    type->align = spec->elem_size >= 8 ? 8 : spec->elem_size;
    return type;
  }
  if (spec->fp_kind != TK_FLOAT_KIND_NONE)
    return psx_type_new_float(spec->fp_kind, spec->elem_size);
  if (spec->base_kind == TK_VOID) {
    psx_type_t *type = psx_type_new(PSX_TYPE_VOID);
    type->scalar_kind = TK_VOID;
    return type;
  }
  return psx_type_new_integer(spec->base_kind, spec->elem_size,
                              spec->is_unsigned);
}

static psx_type_t *build_toplevel_derived_typedef_type(
    const toplevel_decl_spec_t *spec, toplevel_declarator_head_t head,
    const int *decl_dims, int decl_dim_count) {
  if (!spec) return NULL;
  (void)decl_dims;
  (void)decl_dim_count;

  psx_type_t *type = toplevel_typedef_base_type(spec);
  if (!type) return NULL;
  if (spec->type_spec.is_atomic) type->is_atomic = 1;
  if (spec->type_spec.is_long_long) type->is_long_long = 1;
  if (!spec->base_decl_type)
    type->is_plain_char = spec->type_spec.is_plain_char ? 1 : 0;
  if (spec->type_spec.is_long_double || spec->is_long_double)
    type->is_long_double = 1;
  psx_type_set_decl_spec_qualifiers(
      type, type->is_const_qualified || spec->pointee_const,
      type->is_volatile_qualified || spec->pointee_volatile);
  return psx_type_apply_declarator_shape(type, &head.declarator_shape);
}

static int is_toplevel_typedef_unsigned(token_kind_t stored_base_kind, const toplevel_decl_spec_t *spec) {
  return (stored_base_kind == TK_UNSIGNED) || spec->is_unsigned;
}

static int compute_toplevel_typedef_sizeof(const toplevel_decl_spec_t *spec,
                                           toplevel_declarator_head_t head,
                                           int effective_is_ptr,
                                           toplevel_array_suffix_t arr) {
  int typedef_sizeof = effective_is_ptr ? 8 : spec->elem_size;
  if (!effective_is_ptr && arr.has_incomplete_array) return 0;
  if (!effective_is_ptr && arr.is_array && arr.arr_total > 0) typedef_sizeof *= arr.arr_total;
  /* pointer-element 配列 typedef (`typedef BinOp OpArr3[3]`): base が pointer typedef
   * かつ declarator は `*` を追加していない (= ptr_in_paren_group=0 で base のみ由来) +
   * 配列 suffix が立つケース。sizeof = 8 (pointer) * arr_total。これがないと typedef の
   * sizeof_size が 8 (base pointer サイズのまま) になり、宣言側で OpArr3 *pa の要素サイズ
   * が誤判定される。pointer-to-array typedef (`typedef int (*PA)[3]`、ptr_in_paren_group=1)
   * とは排他。 */
  if (effective_is_ptr && !head.ptr_in_paren_group &&
      head.ptr_levels == 0 &&
      arr.is_array && arr.arr_total > 0) {
    typedef_sizeof = 8 * arr.arr_total;
  }
  /* 直書きの「array of pointer typedef」(`typedef int *IP[5]`): declarator が `*` を追加
   * (ptr_levels>=1) かつ `[N]` 配列 suffix がある形は「N 個のポインタ配列」なので
   * sizeof = 8 (pointer) * arr_total。修正前は単一ポインタ扱いで 8 のまま、`IP a; a[0]=&g;
   * *a[0]` が SIGSEGV していた。pointer-to-array typedef (`int (*PA)[3]`) は
   * ptr_in_paren_group=1 で除外。 */
  if (effective_is_ptr && !head.ptr_in_paren_group &&
      head.ptr_levels >= 1 &&
      arr.is_array && arr.arr_total > 0) {
    typedef_sizeof = 8 * arr.arr_total;
  }
  return typedef_sizeof;
}

static token_kind_t resolve_toplevel_typedef_base_kind_for_store(const toplevel_decl_spec_t *spec) {
  token_kind_t stored_base_kind = spec->base_kind;
  if (stored_base_kind == TK_INT && spec->is_unsigned) return TK_UNSIGNED;
  return stored_base_kind;
}

static void apply_toplevel_typedef_from_head(const toplevel_decl_spec_t *spec,
                                             toplevel_declarator_head_t head) {
  define_toplevel_typedef_from_declarator(spec, head);
}

static int has_next_toplevel_declarator(void) {
  return tk_consume(',');
}

static token_ident_t *parse_toplevel_decl_name(const toplevel_decl_spec_t *spec,
                                               toplevel_declarator_head_t *head) {
  return parse_decl_name_recursive(spec, head, 1, 0);
}

/* curtok が `(` のとき仮引数リストだけを消費する (関数名は既に読んだ後)。 */
static void parse_func_param_list_only(int *out_is_variadic, int *out_has_unnamed_param,
                                       node_t ***out_args, int *out_nargs) {
  int arg_cap = 16;
  node_t **args = calloc(arg_cap, sizeof(node_t *));
  int nargs = 0;
  int is_variadic = 0;
  int has_unnamed_param = 0;
  tk_expect('(');
  if (!tk_consume(')')) {
    bool done = false;
    node_func_t node_tmp = {0};
    node_tmp.args = args;
    while (!done) {
      if (curtok()->kind == TK_ELLIPSIS) {
        set_curtok(curtok()->next);
        if (curtok()->kind == ',') {
          diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, curtok(), "%s",
                         diag_message_for(DIAG_ERR_PARSER_VARIADIC_NOT_LAST));
        }
        is_variadic = 1;
        done = true;
        continue;
      }
      if (parse_param_decl(&node_tmp, &nargs, &arg_cap, 1)) has_unnamed_param = 1;
      args = node_tmp.args;
      if (!tk_consume(',')) break;
      if (curtok()->kind == TK_RPAREN) {
        psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_PARAMETER));
      }
    }
    tk_expect(')');
  }
  *out_is_variadic = is_variadic;
  *out_has_unnamed_param = has_unnamed_param;
  *out_args = args;
  *out_nargs = nargs;
}

static void register_function_signature(const psx_function_signature_t *sig) {
  token_ident_t *tok = sig->name;
  if (ps_find_global_var(tok->str, tok->len)) {
    psx_diag_ctx(curtok(), sig->diag_context,
                 "'%.*s' はグローバル変数として既に宣言されています (C11 6.7p4)",
                 tok->len, tok->str);
  }
  psx_ctx_define_function_name_with_ret(tok->str, tok->len, sig->ret_struct_size);
  psx_decl_funcptr_sig_t effective_funcptr_sig = sig->funcptr_sig;
  if (sig->ret_is_funcptr) {
    psx_decl_funcptr_sig_t funcptr_sig = sig->funcptr_sig;
    int funcptr_returns_pointer =
        funcptr_sig.function.callable.return_shape.is_data_pointer ||
        psx_ret_pointee_array_has_dims(funcptr_sig.function.callable.return_shape.pointee_array);
    if (funcptr_returns_pointer &&
        funcptr_sig.function.callable.return_shape.pointee_fp_kind == TK_FLOAT_KIND_NONE) {
      funcptr_sig.function.callable.return_shape.pointee_fp_kind = sig->ret_fp_kind;
    } else if (!funcptr_returns_pointer &&
               funcptr_sig.function.callable.return_shape.fp_kind == TK_FLOAT_KIND_NONE) {
      funcptr_sig.function.callable.return_shape.fp_kind = sig->ret_fp_kind;
    }
    int funcptr_ret_int_width = funcptr_sig.function.callable.return_shape.int_width
                                    ? funcptr_sig.function.callable.return_shape.int_width
                                    : psx_funcptr_ret_int_width_from_kind(
                                          sig->ret_token_kind, funcptr_sig.function.callable.return_shape.is_data_pointer,
                                          funcptr_sig.function.callable.return_shape.fp_kind);
    funcptr_sig.function.callable.return_shape.int_width = (unsigned char)funcptr_ret_int_width;
    effective_funcptr_sig = funcptr_sig;
    if (sig->func_node) ps_node_funcdef_set_ret_funcptr_sig(sig->func_node, funcptr_sig);
  }
  psx_type_t *ret_type = function_signature_ret_type(sig, effective_funcptr_sig);
  if (ret_type) {
    psx_type_t *function_type =
        psx_type_new_function(psx_type_clone(ret_type), effective_funcptr_sig);
    psx_type_t *param_types[16] = {0};
    int tracked_param_count = sig->nargs > 16 ? 16 : sig->nargs;
    for (int i = 0; i < tracked_param_count; i++)
      param_types[i] = sig->args ? ps_node_get_type(sig->args[i]) : NULL;
    psx_type_set_function_params(function_type, param_types, sig->nargs,
                                 sig->is_variadic);
    if (!psx_ctx_track_function_type(tok->str, tok->len, function_type)) {
      psx_diag_ctx(curtok(), sig->diag_context,
                   "関数 '%.*s' の型が以前の宣言と異なります (C11 6.7p3-4)",
                   tok->len, tok->str);
    }
  }
}

/* `int f(int), g(int), a;` の f/g 等: funcdef と同様に関数テーブルへプロトタイプを登録する。 */
static void register_toplevel_function_prototype(const toplevel_decl_spec_t *spec,
                                                 toplevel_declarator_head_t head) {
  token_ident_t *tok = head.name;
  if (!tok || curtok()->kind != TK_LPAREN) return;
  psx_decl_reset_locals();
  token_kind_t ret_kind = spec->base_kind;
  tk_float_kind_t ret_fp_kind = spec->fp_kind;
  token_kind_t ret_token_kind = (ret_kind == TK_EOF) ? TK_INT : ret_kind;
  int ret_is_ptr = spec->base_is_ptr || head.is_ptr ||
                   spec->base_pointer_levels > 0;
  int ret_base_unsigned = spec->is_unsigned;
  int ret_is_complex = !ret_is_ptr && spec->is_complex;
  int ret_struct_size = 0;
  if (psx_ctx_is_tag_aggregate_kind(spec->tag_kind) &&
      !ret_is_ptr && spec->tag_name &&
      psx_ctx_has_tag_type(spec->tag_kind, spec->tag_name,
                           spec->tag_len)) {
    ret_struct_size = psx_ctx_get_tag_size(spec->tag_kind, spec->tag_name,
                                           spec->tag_len);
  }
  int is_variadic = 0;
  int has_unnamed_param = 0;
  node_t **args = NULL;
  int nargs = 0;
  parse_func_param_list_only(&is_variadic, &has_unnamed_param, &args, &nargs);
  psx_skip_gnu_attributes();
  (void)has_unnamed_param;
  psx_function_signature_t sig = {0};
  sig.name = tok;
  sig.diag_context = "decl";
  sig.ret_struct_size = ret_struct_size;
  sig.ret_fp_kind = ret_fp_kind;
  sig.ret_token_kind = ret_token_kind;
  sig.ret_is_ptr = ret_is_ptr;
  sig.ret_base_unsigned = ret_base_unsigned;
  sig.ret_pointee_const = spec->pointee_const;
  sig.ret_pointee_volatile = spec->pointee_volatile;
  sig.ret_is_complex = ret_is_complex;
  sig.ret_is_void = (ret_kind == TK_VOID);
  sig.ret_tag_kind = spec->tag_kind;
  sig.ret_tag_name = spec->tag_name;
  sig.ret_tag_len = spec->tag_len;
  sig.ret_pointer_levels = spec->base_pointer_levels + head.ptr_levels;
  sig.is_variadic = is_variadic;
  sig.nargs = nargs;
  sig.args = args;
  register_function_signature(&sig);
}

static token_ident_t *parse_decl_name_recursive(const toplevel_decl_spec_t *spec,
                                                toplevel_declarator_head_t *head,
                                                int require_name, int nesting_depth) {
  (void)spec;
  int frame_is_ptr = 0;
  int frame_pointer_levels = psx_consume_pointer_prefix_counted(&frame_is_ptr);
  head->ptr_levels += frame_pointer_levels;
  if (frame_pointer_levels > 0) {
    head->is_ptr = 1;
    if (nesting_depth > 0) head->ptr_in_paren_group = 1;
  }
  psx_skip_gnu_attributes();
  token_ident_t *name = NULL;
  int had_parens = 0;
  if (tk_consume('(')) {
    had_parens = 1;
    psx_skip_gnu_attributes();
    name = parse_decl_name_recursive(spec, head, require_name, nesting_depth + 1);
    tk_expect(')');
  } else {
    name = consume_decl_ident_or_error(require_name);
  }
  consume_toplevel_decl_suffixes(head, nesting_depth, had_parens);
  psx_declarator_shape_append_pointer_levels(
      &head->declarator_shape, frame_pointer_levels, 0, 0);
  return name;
}

static token_ident_t *consume_decl_ident_or_error(int require_name) {
  token_ident_t *name = tk_consume_ident();
  if (!name && require_name) {
    diag_emit_tokf(DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED, curtok(), "%s",
                   diag_message_for(DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED));
  }
  return name;
}

static void emit_decl_name_required_diag(void) {
  diag_emit_tokf(DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED, curtok(), "%s",
                 diag_message_for(DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED));
}

static void consume_toplevel_decl_suffixes(toplevel_declarator_head_t *head,
                                           int nesting_depth,
                                           int direct_was_parenthesized) {
  for (;;) {
    if (tk_consume('[')) {
      int has_size = 0;
      int dim = psx_parse_array_size_optional_constexpr(&has_size);
      psx_declarator_shape_append_array_ex(
          &head->declarator_shape, has_size ? dim : 0, !has_size);

      if (nesting_depth > 0) {
        head->paren_array_present = 1;
        if (has_size && dim > 0) {
          head->paren_array_mul *= dim;
          if (head->paren_array_dim_count < 8)
            head->paren_array_dims[head->paren_array_dim_count] = dim;
          head->paren_array_dim_count++;
        }
      }
      continue;
    }

    /* A plain file-scope `f(...)` is registered by the prototype path, which
     * needs to parse named parameters into AST locals. Nested and
     * parenthesized function suffixes are declarator operators and are parsed
     * here as part of the shape. */
    if (curtok()->kind == TK_LPAREN &&
        (nesting_depth > 0 || direct_was_parenthesized)) {
      psx_funcptr_signature_t parsed_suffix = {0};
      psx_skip_func_param_list(&parsed_suffix);
      if (head->func_suffix_count == 0) {
        head->func_suffix_sig = parsed_suffix;
      } else if (head->func_suffix_count == 1) {
        head->returned_funcptr_suffix_sig = parsed_suffix;
      }
      head->has_func_suffix = 1;
      head->func_suffix_count++;
      psx_decl_funcptr_sig_t op_sig = {0};
      op_sig.function.callable.signature = parsed_suffix;
      psx_declarator_shape_append_function(&head->declarator_shape, op_sig);
      continue;
    }
    break;
  }
}

static void parse_toplevel_decl_after_type(const toplevel_decl_spec_t *spec) {
  if (spec->is_typedef) {
    parse_toplevel_declarator_stmt(spec, spec->base_is_ptr, apply_toplevel_typedef_from_head);
    return;
  }
  /* ポインタ typedef を基底にしたグローバル変数 `typedef int *PI; PI gp;` では、
   * 基底のポインタ性 (spec->base_is_ptr) を宣言子へ渡す必要がある。0 固定だと
   * gp が int スカラとして登録され sizeof=4 / subscript で E3064 になっていた。直書き
   * `int *gp` は base_is_ptr=0 + 宣言子の `*` で is_ptr が立つため影響しない。 */
  parse_toplevel_declarator_stmt(spec, spec->base_is_ptr, apply_toplevel_object_from_head);
}

static void parse_toplevel_declarator_stmt(const toplevel_decl_spec_t *spec, int base_is_ptr,
                                           void (*apply)(const toplevel_decl_spec_t *,
                                                         toplevel_declarator_head_t)) {
  parse_toplevel_declarator_list_with_apply(spec, base_is_ptr, apply);
  tk_expect(';');
}

static int parse_toplevel_declaration_like(void) {
  if (curtok()->kind == TK_STATIC_ASSERT) {
    parse_static_assert_toplevel();
    return 1;
  }
  if (psx_ctx_is_tag_keyword(curtok()->kind)) {
    // struct/union/enum 開始は ps_program() 側の専用経路で処理する。
    return 0;
  }
  if (is_toplevel_decl_like_start(curtok()) &&
      (!is_toplevel_function_signature(curtok()) ||
       toplevel_decl_has_comma_separated_declarators(curtok()))) {
    /* _Generic 用: 型シグネチャ文字列化のため型開始トークンを記録 (オブジェクト宣言のみ)。 */
    token_t *typespec_start = (curtok()->kind == TK_TYPEDEF) ? NULL : curtok();
    toplevel_decl_spec_t spec;
    parse_toplevel_decl_spec(&spec);
    spec.typespec_start = typespec_start;
    parse_toplevel_decl_after_type(&spec);
    return 1;
  }
  return 0;
}

static int is_toplevel_decl_like_start(token_t *tok) {
  if (!tok) return 0;
  return tok->kind == TK_TYPEDEF ||
         psx_ctx_is_type_token(tok->kind) ||
         psx_is_decl_prefix_token(tok->kind) ||
         psx_ctx_is_typedef_name_token(tok);
}


static void install_toplevel_tag_decl_spec(toplevel_decl_spec_t *spec,
                                           token_kind_t tag_kind, char *tag_name, int tag_len) {
  spec->tag_kind = tag_kind;
  spec->tag_name = tag_name;
  spec->tag_len = tag_len;
  spec->base_kind = tag_kind;
  spec->elem_size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
}

static void parse_toplevel_tag_decl(void) {
  /* この経路は宣言が tag キーワード (`struct`/`union`/`enum`) で始まる場合のみ (storage
   * class 前置があれば dispatcher が parse_toplevel_declaration_like へ回す)。dispatcher
   * (ps_next_function) はこの経路の前に reset_toplevel_decl_spec_state を呼ばないので、
   * 前の宣言の decl-spec 状態が残る。例えば直前が `typedef double T;`
   * だと fp_kind=DOUBLE が漏れ、ここで宣言する struct object の fp_kind が
   * DOUBLE になり、グローバル brace init の fp-fold 経路が文字列/関数参照/アドレス初期化子を
   * fp 定数(0)として食べてしまう (`struct{char b[4];char*p;} g={"x","y"}` の p が NULL 化)。
   * extern/static 漏れ (`extern struct S es; struct S es={7};` の 2 行目が extern 扱いされ
   * brace を取りこぼす) も同根。宣言ごとに全状態をクリアする。tag 情報は後段の
   * install_toplevel_tag_decl_spec が再設定する。 */
  toplevel_decl_spec_t spec;
  reset_toplevel_decl_spec_state(&spec);
  spec.typespec_start = curtok();
  token_kind_t tag_kind = TK_EOF;
  char *tag_name = NULL;
  int tag_len = 0;
  parse_toplevel_tag_head(&tag_kind, &tag_name, &tag_len);

  if (tk_consume('{')) {
    int member_count = 0;
    int tag_size = 0;
    int tag_align = 0;
    member_count = psx_parse_tag_definition_body(tag_kind, tag_name, tag_len, &tag_size, &tag_align);
    psx_ctx_define_tag_type_with_layout(tag_kind, tag_name, tag_len, member_count, tag_size, tag_align);
    if (tk_consume(';')) return;
    psx_type_spec_result_t tag_type_spec;
    psx_type_spec_result_reset(&tag_type_spec);
    skip_post_type_cv_qualifiers_into(&tag_type_spec);
    install_toplevel_tag_decl_spec(&spec, tag_kind, tag_name, tag_len);
    apply_toplevel_type_result_prefix_flags(&spec, &tag_type_spec);
    parse_toplevel_declarator_list(&spec);
    tk_expect(';');
    return;
  }
  if (tk_consume(';')) {
    psx_ctx_define_tag_type(tag_kind, tag_name, tag_len);
    return;
  }
  if (!psx_ctx_has_tag_type(tag_kind, tag_name, tag_len)) {
    psx_ctx_define_tag_type(tag_kind, tag_name, tag_len);
  }
  psx_type_spec_result_t tag_type_spec;
  psx_type_spec_result_reset(&tag_type_spec);
  skip_post_type_cv_qualifiers_into(&tag_type_spec);
  install_toplevel_tag_decl_spec(&spec, tag_kind, tag_name, tag_len);
  apply_toplevel_type_result_prefix_flags(&spec, &tag_type_spec);
  parse_toplevel_declarator_list(&spec);
  tk_expect(';');
}

static void psx_type_spec_result_reset(psx_type_spec_result_t *out) {
  if (!out) return;
  memset(out, 0, sizeof(*out));
  out->kind = TK_EOF;
}

static void emit_invalid_type_spec_diag(void) {
  diag_emit_tokf(DIAG_ERR_PARSER_INVALID_TYPE_SPEC, curtok(), "%s",
                 diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC));
}

// consume_type: 型キーワードがあれば読み進め、そのトークン種別を返す（0=型なし）
/* 後置 cv/atomic 修飾子トークンを 1 つ消費する。const/volatile/restrict/atomic
 * いずれも同じ「対応 flag を立てて trailing トークンを進める」パターンなので
 * 集約する。消費したら 1、該当しなければ 0 (呼出側で loop を抜ける)。 */
static int try_consume_post_cv_qualifier(psx_type_spec_result_t *out, token_kind_t k) {
  switch (k) {
    case TK_CONST:    out->is_const_qualified = 1; break;
    case TK_VOLATILE: out->is_volatile_qualified = 1; break;
    case TK_RESTRICT: break;
    case TK_ATOMIC:   out->is_atomic = 1; break;
    default: return 0;
  }
  set_curtok(curtok()->next);
  return 1;
}

/* saw_* flag 群から最終的な型 token_kind_t を決定する。
 * 優先度: void > float > double > bool > char > short > long > int。 */
static token_kind_t resolve_type_kind_from_flags(int saw_void, int saw_float, int saw_double,
                                                  int saw_bool, int saw_char, int saw_short,
                                                  int long_count) {
  if (saw_void) return TK_VOID;
  if (saw_float) return TK_FLOAT;
  if (saw_double) return TK_DOUBLE;
  if (saw_bool) return TK_BOOL;
  if (saw_char) return TK_CHAR;
  if (saw_short) return TK_SHORT;
  if (long_count > 0) return TK_LONG;
  return TK_INT;
}

token_kind_t psx_consume_type_kind_ex(psx_type_spec_result_t *out) {
  psx_type_spec_result_t local;
  if (!out) out = &local;
  skip_cv_qualifiers_into(out);
  if (curtok()->kind == TK_ATOMIC && curtok()->next && curtok()->next->kind == TK_LPAREN) {
    out->is_atomic = 1;
    token_kind_t inner = parse_atomic_type_specifier();
    if (inner != TK_EOF) {
      out->kind = inner;
      return inner;
    }
  }
  // qualifier-form: _Atomic int x;
  if (curtok()->kind == TK_ATOMIC) {
    out->is_atomic = 1;
    set_curtok(curtok()->next);
  }
  token_t *start = curtok();
  int saw_signed = 0;
  int saw_unsigned = 0;
  int long_count = 0;
  int saw_short = 0;
  int saw_int = 0;
  int saw_char = 0;
  int saw_void = 0;
  int saw_float = 0;
  int saw_double = 0;
  int saw_bool = 0;
  int saw_complex = 0;
  int saw_imaginary = 0;

  while (true) {
    token_kind_t k = curtok()->kind;
    if (k == TK_COMPLEX) {
      if (saw_complex || saw_imaginary || saw_void || saw_char || saw_short || saw_int || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_complex = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_IMAGINARY) {
      if (saw_complex || saw_imaginary || saw_void || saw_char || saw_short || saw_int || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_imaginary = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_SIGNED) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_void || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_signed = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_UNSIGNED) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_void || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_unsigned = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_LONG) {
      if (saw_char || saw_short || saw_void || saw_float || saw_bool || long_count >= 2) {
        emit_invalid_type_spec_diag();
      }
      long_count++;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_SHORT) {
      if (saw_char || saw_short || long_count || saw_void || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_short = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_INT) {
      if (saw_int || saw_char || saw_void || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_int = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_CHAR) {
      if (saw_char || saw_short || long_count || saw_int || saw_void || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_char = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_VOID) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_float || saw_double || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_void = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_FLOAT) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_void || saw_double || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_float = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_DOUBLE) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || saw_int || saw_void || saw_float || saw_bool) {
        emit_invalid_type_spec_diag();
      }
      saw_double = 1;
      set_curtok(curtok()->next);
      continue;
    }
    if (k == TK_BOOL) {
      if (saw_signed || saw_unsigned || saw_char || saw_short || long_count || saw_int || saw_void || saw_float || saw_double) {
        emit_invalid_type_spec_diag();
      }
      saw_bool = 1;
      set_curtok(curtok()->next);
      continue;
    }
    // 後置 cv 修飾子（int const, volatile int const など）は同じ形なので集約。
    if (try_consume_post_cv_qualifier(out, k)) continue;
    /* C11 6.7p1: declaration-specifiers の順序は任意。型指定子の後ろに storage class
     * (static / extern / auto / register / inline / _Noreturn / _Thread_local / _Alignas) が
     * 来てもよい (`int static x = 5;` 等)。ここで遭遇したら skip_cv_qualifiers と同じ要領で
     * 1 つ消費して flag を立てループ継続。skip_cv_qualifiers を直接呼ぶと先頭で reset され
     * 既に立っている qualifier 情報 (const/volatile/atomic) を失うため、ここでは OR 的に
     * 1 トークンずつ処理する。 */
    if (psx_is_decl_prefix_token(k)) {
      /* storage class の重複・併用検出 (C11 6.7.1p2): static / extern / auto / register は
       * 高々 1 個。型指定子の前 (skip_cv_qualifiers) ですでに 1 つ立っていたら 2 つ目で error。 */
      int is_new_storage = (k == TK_STATIC || k == TK_EXTERN ||
                            k == TK_AUTO || k == TK_REGISTER);
      if (is_new_storage && (out->is_static || out->is_extern)) {
        psx_diag_ctx(curtok(), "decl",
                     "storage class 指定子は1つまでです (C11 6.7.1p2)");
      }
      if (k == TK_CONST)        out->is_const_qualified = 1;
      else if (k == TK_VOLATILE) out->is_volatile_qualified = 1;
      else if (k == TK_STATIC)   out->is_static = 1;
      else if (k == TK_EXTERN)   out->is_extern = 1;
      else if (k == TK_THREAD_LOCAL) out->is_thread_local = 1;
      else if (k == TK_ATOMIC) {
        /* `int _Atomic(int) x` 形式は ATOMIC 後に `(` が来る (型指定子)。型指定子の後の
         * 単独 `_Atomic` は qualifier 形 (`int _Atomic x`)。 */
        if (curtok()->next && curtok()->next->kind == TK_LPAREN) break;
        out->is_atomic = 1;
      }
      /* TK_AUTO / TK_REGISTER / TK_INLINE / TK_NORETURN / TK_ALIGNAS(...) は flag を立てずに
       * 単純消費。TK_ALIGNAS は `(value)` 形のため複雑だが、型指定子の後の出現は稀 (実例は
       * `int _Alignas(8) x` で C11 では基本的に typespec の前)。ここでは省略 — 必要ならば
       * 既存の skip_cv_qualifiers の TK_ALIGNAS 分岐を引用する。 */
      set_curtok(curtok()->next);
      continue;
    }
    break;
  }

  if (curtok() == start) return TK_EOF;
  out->is_unsigned = saw_unsigned;
  out->is_complex = saw_complex;
  out->is_long_long = (long_count >= 2) ? 1 : 0;
  out->is_plain_char = (saw_char && !saw_signed && !saw_unsigned) ? 1 : 0;
  out->is_long_double = (saw_double && long_count >= 1) ? 1 : 0;
  if ((saw_complex || saw_imaginary) && !(saw_float || saw_double)) {
    diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, start,
                   "%s",
                   diag_message_for(DIAG_ERR_PARSER_COMPLEX_IMAGINARY_TYPE_REQUIRES_FLOAT));
  }
  out->kind = resolve_type_kind_from_flags(saw_void, saw_float, saw_double, saw_bool,
                                           saw_char, saw_short, long_count);
  return out->kind;
}


// _Alignas( constant-expression | type-name )
static void skip_balanced_group(token_kind_t lkind, token_kind_t rkind) {
  if (curtok()->kind != lkind) return;
  int depth = 0;
  while (curtok() && curtok()->kind != TK_EOF) {
    if (curtok()->kind == lkind) depth++;
    else if (curtok()->kind == rkind) {
      depth--;
      if (depth == 0) {
        set_curtok(curtok()->next);
        return;
      }
    }
    set_curtok(curtok()->next);
  }
  psx_diag_ctx(curtok(), "param", "%s",
               diag_message_for(DIAG_ERR_PARSER_MISSING_CLOSING_PAREN));
}

static token_ident_t *parse_param_declarator_name(param_declarator_state_t *decl_state,
                                                  int *out_is_array_declarator, int *out_is_pointer_declarator,
                                                  int *out_pointer_levels,
                                                  int *out_inner_first_dim, int *out_inner_second_dim,
                                                  token_ident_t **out_inner_first_dim_ident,
                                                  token_ident_t **out_inner_second_dim_ident,
                                                  int *out_has_func_suffix) {
  if (out_is_array_declarator) *out_is_array_declarator = 0;
  if (out_is_pointer_declarator) *out_is_pointer_declarator = 0;
  if (out_pointer_levels) *out_pointer_levels = 0;
  if (out_inner_first_dim) *out_inner_first_dim = 0;
  if (out_inner_second_dim) *out_inner_second_dim = 0;
  if (out_inner_first_dim_ident) *out_inner_first_dim_ident = NULL;
  if (out_inner_second_dim_ident) *out_inner_second_dim_ident = NULL;
  if (out_has_func_suffix) *out_has_func_suffix = 0;
  /* N-D VLA 仮引数 (`int t[n][m][k][l]` 等) の inner dim は
   * 仮引数1個ごとの明示 state として登録側へ渡す。 */
  if (decl_state) {
    memset(decl_state, 0, sizeof(*decl_state));
    psx_declarator_shape_init(&decl_state->declarator_shape);
  }
  token_ident_t *param = parse_param_declarator_name_recursive(decl_state,
                                                               out_is_array_declarator,
                                                               out_is_pointer_declarator,
                                                               out_pointer_levels,
                                                               out_inner_first_dim,
                                                               out_inner_second_dim,
                                                               out_inner_first_dim_ident,
                                                               out_inner_second_dim_ident,
                                                               out_has_func_suffix);
  return param;
}

/* `int (int x)` のように declarator の `(` 直後が型指定子なら、関数型の仮引数リスト。 */
static int is_param_decl_spec_start(void) {
  token_t *t = curtok();
  if (!t) return 0;
  if (psx_ctx_is_tag_keyword(t->kind)) return 1;
  if (psx_ctx_is_type_token(t->kind)) return 1;
  if (psx_ctx_is_typedef_name_token(t)) return 1;
  if (t->kind == TK_CONST || t->kind == TK_VOLATILE) return 1;
  return 0;
}

static void psx_skip_param_func_suffix_groups(param_declarator_state_t *decl_state,
                                              int *out_has_func_suffix) {
  while (curtok()->kind == TK_LPAREN) {
    psx_funcptr_signature_t parsed_suffix = {0};
    psx_skip_func_param_list(&parsed_suffix);
    if (decl_state) {
      if (decl_state->func_suffix_count == 0) {
        decl_state->func_suffix_sig = parsed_suffix;
      } else if (decl_state->func_suffix_count == 1) {
        decl_state->returned_funcptr_suffix_sig = parsed_suffix;
      }
      decl_state->func_suffix_count++;
      psx_decl_funcptr_sig_t op_sig = {0};
      op_sig.function.callable.signature = parsed_suffix;
      psx_declarator_shape_append_function(
          &decl_state->declarator_shape, op_sig);
    }
    if (out_has_func_suffix) *out_has_func_suffix = 1;
  }
}

static token_ident_t *parse_param_declarator_name_recursive(param_declarator_state_t *decl_state,
                                                            int *out_is_array_declarator,
                                                            int *out_is_pointer_declarator,
                                                            int *out_pointer_levels,
                                                            int *out_inner_first_dim,
                                                            int *out_inner_second_dim,
                                                            token_ident_t **out_inner_first_dim_ident,
                                                            token_ident_t **out_inner_second_dim_ident,
                                                            int *out_has_func_suffix) {
  int frame_level_start = out_pointer_levels ? *out_pointer_levels : 0;
  while (tk_consume('*')) {
    if (out_is_pointer_declarator) *out_is_pointer_declarator = 1;
    if (out_pointer_levels) (*out_pointer_levels)++;
    skip_ptr_qualifiers();
  }
  int frame_pointer_prefix_levels = out_pointer_levels ? *out_pointer_levels : 0;
  token_ident_t *name = NULL;
  // 括弧内に *p があるか (= 「ポインタを括弧で覆って配列にする」`(*p)[N]` 形式) を
  // 判定する。recursive 呼び出し前後で pointer level の変化を見れば判別できる。
  int levels_before_paren = out_pointer_levels ? *out_pointer_levels : 0;
  bool paren_made_pointer = false;
  if (tk_consume('(')) {
    if (curtok()->kind == TK_RPAREN) {
      /* Abstract function declarator with an old-style empty parameter list:
       * `int ()` in a parameter position denotes a function type and adjusts
       * to a function pointer, just like `int (int)`. */
      if (out_has_func_suffix) *out_has_func_suffix = 1;
      if (out_is_pointer_declarator) *out_is_pointer_declarator = 1;
      if (out_pointer_levels && *out_pointer_levels == 0) (*out_pointer_levels)++;
      tk_expect(')');
      if (decl_state) {
        psx_declarator_shape_append_function(
            &decl_state->declarator_shape, (psx_decl_funcptr_sig_t){0});
      }
    } else if (is_param_decl_spec_start() || curtok()->kind == TK_VOID) {
      /* 関数型 declarator: `int (int x)` / `int (int())` / `int (void)` 等。
       * 仮引数位置では関数型は関数ポインタへ decay する。 */
      if (out_has_func_suffix) *out_has_func_suffix = 1;
      if (out_is_pointer_declarator) *out_is_pointer_declarator = 1;
      if (out_pointer_levels && *out_pointer_levels == 0) (*out_pointer_levels)++;
      node_func_t discard = {0};
      int discard_nargs = 0;
      int discard_cap = 0;
      while (curtok()->kind != TK_RPAREN) {
        parse_param_decl(&discard, &discard_nargs, &discard_cap, 0);
        if (!tk_consume(',')) break;
      }
      tk_expect(')');
      if (decl_state) {
        psx_declarator_shape_append_function(
            &decl_state->declarator_shape, (psx_decl_funcptr_sig_t){0});
      }
    } else {
    name = parse_param_declarator_name_recursive(decl_state,
                                                 out_is_array_declarator, out_is_pointer_declarator,
                                                 out_pointer_levels,
                                                 out_inner_first_dim, out_inner_second_dim,
                                                 out_inner_first_dim_ident,
                                                 out_inner_second_dim_ident,
                                                 out_has_func_suffix);
    tk_expect(')');
    if (out_pointer_levels && *out_pointer_levels > levels_before_paren) {
      paren_made_pointer = true;
    }
    }
  } else {
    name = tk_consume_ident();
  }
  int bracket_count = 0;
  while (curtok()->kind == TK_LPAREN || curtok()->kind == TK_LBRACKET) {
    if (curtok()->kind == TK_LPAREN) {
      /* 関数 suffix `(...)`: `int (*ops[])(int)` の最後の `(int)` 等を skip。
       * 仮引数登録経路で「関数ポインタ配列」を識別するためフラグを立てる。 */
      int has_suffix = 0;
      psx_skip_param_func_suffix_groups(decl_state, &has_suffix);
      if (out_has_func_suffix && has_suffix) *out_has_func_suffix = 1;
      if (has_suffix && decl_state && decl_state->funcptr_object_pointer_levels == 0 &&
          out_pointer_levels) {
        int object_levels = *out_pointer_levels - frame_pointer_prefix_levels;
        if (object_levels > 0) decl_state->funcptr_object_pointer_levels = object_levels;
      }
    } else {
      if (out_is_array_declarator) *out_is_array_declarator = 1;
      // C11 6.7.6.3p7: 通常の仮引数 `int a[N][M]` では最も外側の `[N]` が
      // pointer 調整によりサイズが無関係になる。一方 `int (*a)[N][M]` は
      // ポインタが既に括弧内で適用されており、続く `[N][M]` は pointee の
      // dim を表すため最初の bracket も捕捉する。
      bool skip_first = (bracket_count == 0) && !paren_made_pointer;
      if (skip_first) {
        if (out_is_pointer_declarator && *out_is_pointer_declarator) {
          tk_consume('[');
          int dim = 0;
          int has_size = curtok() && curtok()->kind != TK_RBRACKET;
          if (curtok() && curtok()->kind != TK_RBRACKET) {
            dim = psx_parse_array_size_constexpr();
          }
          tk_expect(']');
          if (decl_state) {
            psx_declarator_shape_append_array_ex(
                &decl_state->declarator_shape, dim, !has_size);
          }
          if (dim > 0 && decl_state) decl_state->pointer_array_outer_dim = dim;
        } else {
          int is_incomplete = curtok()->next && curtok()->next->kind == TK_RBRACKET;
          skip_balanced_group(TK_LBRACKET, TK_RBRACKET);
          if (decl_state) {
            if (is_incomplete)
              psx_declarator_shape_append_array_ex(
                  &decl_state->declarator_shape, 0, 1);
            else
              psx_declarator_shape_append_vla_array(
                  &decl_state->declarator_shape);
          }
        }
      } else {
        tk_consume('[');
        int dim = 0;
        token_ident_t *dim_ident = NULL;
        int has_size = curtok() && curtok()->kind != TK_RBRACKET;
        if (curtok() && curtok()->kind != TK_RBRACKET) {
          /* C99 6.7.6.3p7 VLA-as-param: `int g[n][m]` の内側 dim が単純な
           * パラメータ識別子のとき、constexpr 評価を試みずに識別子トークンを
           * 捕捉する。それ以外は従来の定数式評価へ。 */
          if (curtok()->kind == TK_IDENT &&
              curtok()->next && curtok()->next->kind == TK_RBRACKET) {
            dim_ident = (token_ident_t *)curtok();
            set_curtok(curtok()->next);
          } else {
            dim = psx_parse_array_size_constexpr();
          }
        }
        tk_expect(']');
        if (decl_state) {
          if (!has_size)
            psx_declarator_shape_append_array_ex(
                &decl_state->declarator_shape, 0, 1);
          else if (dim_ident)
            psx_declarator_shape_append_vla_array(
                &decl_state->declarator_shape);
          else
            psx_declarator_shape_append_array(
                &decl_state->declarator_shape, dim);
        }
        // paren_made_pointer 時は bracket 0/1/... が全て pointee dim。
        // 通常時は bracket 1/2/... が pointee dim。
        int dim_pos = paren_made_pointer ? bracket_count : (bracket_count - 1);
        if (dim_pos == 0) {
          if (out_is_pointer_declarator && *out_is_pointer_declarator &&
              out_inner_first_dim && *out_inner_first_dim > 0 &&
              decl_state && decl_state->pointer_array_outer_dim == 0) {
            decl_state->pointer_array_outer_dim = *out_inner_first_dim;
          }
          if (out_inner_first_dim) *out_inner_first_dim = dim;
          if (out_inner_first_dim_ident) *out_inner_first_dim_ident = dim_ident;
        } else if (dim_pos == 1) {
          if (out_inner_second_dim) *out_inner_second_dim = dim;
          if (out_inner_second_dim_ident) *out_inner_second_dim_ident = dim_ident;
        }
        /* N-D 用 inner dim 配列にも記録 (最大 7 個まで)。 */
        if (decl_state && dim_pos >= 0 && dim_pos < 7) {
          decl_state->inner_dim_consts[dim_pos] = dim;
          decl_state->inner_dim_idents[dim_pos] = dim_ident;
          if (dim_pos + 1 > decl_state->inner_dim_count) {
            decl_state->inner_dim_count = dim_pos + 1;
          }
        }
      }
      bracket_count++;
    }
  }
  psx_declarator_shape_append_pointer_levels(
      decl_state ? &decl_state->declarator_shape : NULL,
      frame_pointer_prefix_levels - frame_level_start, 0, 0);
  return name;
}

static psx_parameter_vla_lowering_result_t lower_parameter_vla_from_state(
    token_ident_t *param, int element_size,
    const param_declarator_state_t *decl_state,
    token_ident_t *fallback_inner_dimension,
    const psx_type_t *canonical_type) {
  psx_parameter_vla_lowering_request_t request = {
      .name = param->str,
      .name_len = param->len,
      .element_size = element_size,
      .type = canonical_type,
      .diag_tok = curtok(),
  };
  request.inner_dimension_count =
      decl_state ? decl_state->inner_dim_count : 0;
  for (int i = 0; i < request.inner_dimension_count; i++) {
    request.inner_dimensions[i].constant =
        decl_state->inner_dim_consts[i];
    token_ident_t *source = decl_state->inner_dim_idents[i];
    if (source) {
      request.inner_dimensions[i].source_name = source->str;
      request.inner_dimensions[i].source_name_len = source->len;
    }
  }
  if (request.inner_dimension_count == 0 && fallback_inner_dimension) {
    request.inner_dimension_count = 1;
    request.inner_dimensions[0].source_name =
        fallback_inner_dimension->str;
    request.inner_dimensions[0].source_name_len =
        fallback_inner_dimension->len;
  }
  return lower_parameter_vla_declaration(&request);
}

static int parameter_state_has_runtime_inner_dimension(
    const param_declarator_state_t *state) {
  if (!state) return 0;
  for (int i = 0; i < state->inner_dim_count; i++) {
    if (state->inner_dim_consts[i] <= 0 && state->inner_dim_idents[i])
      return 1;
  }
  return 0;
}

static lvar_t *register_param_lvar(
    token_ident_t *param, const param_decl_spec_t *ds,
    const param_declarator_state_t *decl_state,
    int param_is_ptr, int param_is_array_declarator,
    int param_has_func_suffix,
    token_ident_t *param_inner_first_dim_ident,
    const psx_type_t *canonical_type, int *out_type_attached) {
  if (out_type_attached) *out_type_attached = 0;
  if (param_is_array_declarator && ds->tag_kind == TK_EOF && !param_is_ptr) {
    psx_parameter_vla_lowering_result_t result =
        lower_parameter_vla_from_state(
            param, ds->elem_size, decl_state,
            param_inner_first_dim_ident, canonical_type);
    if (out_type_attached) *out_type_attached = result.type_attached;
    return result.var;
  }
  if (param_is_ptr && param_is_array_declarator &&
      ds->tag_kind == TK_EOF && !param_has_func_suffix &&
      (param_inner_first_dim_ident != NULL ||
       parameter_state_has_runtime_inner_dimension(decl_state))) {
    psx_parameter_vla_lowering_result_t result =
        lower_parameter_vla_from_state(
            param, ds->elem_size, decl_state,
            param_inner_first_dim_ident, canonical_type);
    if (out_type_attached) *out_type_attached = result.type_attached;
    return result.var;
  }

  psx_parameter_lowering_result_t result = {0};
  if (!lower_parameter_declaration(
          &(psx_parameter_lowering_request_t){
              .name = param->str,
              .name_len = param->len,
              .type = canonical_type,
          },
          &result)) {
    psx_diag_ctx(curtok(), "param",
                 "canonical parameter storage planning failed for '%.*s'",
                 param->len, param->str);
  }
  if (out_type_attached) *out_type_attached = result.type_attached;
  return result.var;
}

static psx_type_t *param_decl_base_type(const param_decl_spec_t *ds) {
  if (!ds) return NULL;
  if (ds->base_decl_type) return psx_type_clone(ds->base_decl_type);
  if (psx_ctx_is_tag_aggregate_kind(ds->tag_kind)) {
    int scope_depth = ps_ctx_get_tag_scope_depth(
        ds->tag_kind, ds->tag_name, ds->tag_len);
    return psx_type_new_tag(ds->tag_kind, ds->tag_name, ds->tag_len,
                            scope_depth >= 0 ? scope_depth + 1 : 0,
                            ds->struct_size);
  }
  if (ds->is_complex) {
    psx_type_t *type = psx_type_new(PSX_TYPE_COMPLEX);
    type->fp_kind = ds->fp_kind != TK_FLOAT_KIND_NONE
                        ? ds->fp_kind
                        : TK_FLOAT_KIND_DOUBLE;
    type->size = ds->elem_size;
    type->align = ds->elem_size >= 8 ? 8 : ds->elem_size;
    return type;
  }
  if (ds->fp_kind != TK_FLOAT_KIND_NONE)
    return psx_type_new_float(ds->fp_kind, ds->elem_size);
  if (ds->base_type_kind == TK_VOID) {
    psx_type_t *type = psx_type_new(PSX_TYPE_VOID);
    type->scalar_kind = TK_VOID;
    return type;
  }
  return psx_type_new_integer(ds->base_type_kind, ds->elem_size,
                              ds->is_unsigned);
}

static psx_type_t *param_canonical_decl_type(
    const param_decl_spec_t *ds, const param_declarator_state_t *decl_state) {
  psx_type_t *type = param_decl_base_type(ds);
  if (type) {
    if (ds->type_spec.is_atomic) type->is_atomic = 1;
    if (ds->type_spec.is_long_long) type->is_long_long = 1;
    if (!ds->base_decl_type)
      type->is_plain_char = ds->type_spec.is_plain_char ? 1 : 0;
    if (ds->type_spec.is_long_double || ds->is_long_double)
      type->is_long_double = 1;
    psx_type_set_decl_spec_qualifiers(
        type,
        type->is_const_qualified || ds->type_spec.is_const_qualified,
        type->is_volatile_qualified || ds->type_spec.is_volatile_qualified);
  }
  if (decl_state)
    type = psx_type_apply_declarator_shape(
        type, &decl_state->declarator_shape);
  return psx_type_adjust_parameter_type(type);
}

static int parse_param_decl(node_func_t *node, int *nargs, int *arg_cap, int count_unnamed) {
  param_decl_spec_t ds = {0};
  parse_param_decl_spec(&ds);
  // ポインタ修飾子を確認してから parse_param_declarator_name へ
  int param_is_ptr = 0;
  int param_is_array_declarator = 0;
  int param_ptr_levels = 0;
  int param_inner_first_dim = 0;
  int param_inner_second_dim = 0;
  token_ident_t *param_inner_first_dim_ident = NULL;
  token_ident_t *param_inner_second_dim_ident = NULL;
  int param_has_func_suffix = 0;
  param_declarator_state_t param_decl_state = {0};
  token_ident_t *param = parse_param_declarator_name(&param_decl_state,
                                                     &param_is_array_declarator, &param_is_ptr,
                                                     &param_ptr_levels,
                                                     &param_inner_first_dim,
                                                     &param_inner_second_dim,
                                                     &param_inner_first_dim_ident,
                                                     &param_inner_second_dim_ident,
                                                     &param_has_func_suffix);
  /* ポインタ typedef 基底 (`typedef char* Str; f(Str s)`) を実効ポインタ性へ合成する。
   * 宣言子に `*` が無く (param_is_ptr=0) 配列宣言子でもないときのみ、基底の段数を採用する
   * (`Str *p` のように宣言子側にも `*` がある場合は param_ptr_levels に基底段数を足す)。
   * 配列宣言子 `Str a[]` は C11 6.7.6.3p7 の adjust が別経路で効くので触らない。 */
  if (ds.base_is_pointer && !param_is_array_declarator) {
    param_is_ptr = 1;
    param_ptr_levels += ds.base_pointer_levels;
  }
  if (!param) {
    // int f(void) の "void" は仮引数0件として扱う（C11 6.7.6.3）。
    if (ds.base_type_kind == TK_VOID && ds.tag_kind == TK_EOF && !ds.saw_typedef_name &&
        !param_is_ptr && !param_is_array_declarator) {
      return 0;
    }
    // decl-specifier はあるが識別子が無い仮引数（例: int f(int);）は
    // プロトタイプでは許容し、関数定義時のみ呼び出し元で診断する。
    if (ds.base_type_kind != TK_EOF || ds.tag_kind != TK_EOF || ds.saw_typedef_name) {
      /* count_unnamed のとき、無名でも固定引数として nargs に数える。これがないと
       * 可変長プロトタイプ (`int printf(const char*, ...)` のように引数名を省く一般的な
       * 書き方) で固定引数数が 0 と誤算され、可変長呼び出し ABI で format 等までスタックに
       * 積まれ x0 が未設定になって crash していた (Apple ARM64)。args[] には fp_kind を持つ
       * プレースホルダを置き、固定 fp 引数の ABI 情報も index 整合させて保つ。プロトタイプ
       * 専用 (定義で無名引数は下流で診断) なので args[] のプレースホルダは codegen に出ない。
       * count_unnamed=0 は入れ子宣言子 (`int (*(*f(void))(int))[3]` の内側 `(int)` 等) で、
       * これらは関数 f 自身の引数ではないため f の nargs に数えてはならない。 */
      if (count_unnamed) {
        if (*nargs >= *arg_cap) {
          *arg_cap = pda_next_cap(*arg_cap, *nargs + 1);
          node->args = pda_xreallocarray(node->args, (size_t)(*arg_cap), sizeof(node_t *));
        }
        psx_type_t *placeholder_type =
            param_canonical_decl_type(&ds, &param_decl_state);
        node_t *ph = psx_node_new_param_placeholder(placeholder_type);
        node->args[(*nargs)++] = ph;
      }
      return 1;
    }
    psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_PARAMETER));
  }

  if (*nargs >= *arg_cap) {
    *arg_cap = pda_next_cap(*arg_cap, *nargs + 1);
    node->args = pda_xreallocarray(node->args, (size_t)(*arg_cap), sizeof(node_t *));
  }
  psx_type_t *canonical_param_type =
      param_canonical_decl_type(&ds, &param_decl_state);
  int type_attached = 0;
  lvar_t *var = register_param_lvar(param, &ds, &param_decl_state,
                                     param_is_ptr, param_is_array_declarator,
                                     param_has_func_suffix,
                                     param_inner_first_dim_ident,
                                     canonical_param_type, &type_attached);
  var->is_param = 1;
  if (canonical_param_type && !type_attached) {
    psx_type_copy_vla_runtime_metadata(
        canonical_param_type, psx_lvar_get_decl_type(var));
    psx_decl_set_lvar_decl_type(var, canonical_param_type);
  }
  // args[] には宣言型を正本にした ND_LVAR を格納する。
  node_t *param_node = psx_node_new_param_lvar_for(var);
  node->args[(*nargs)++] = param_node;
  return 0;
}

static void parse_param_decl_spec(param_decl_spec_t *out) {
  out->base_type_kind = TK_EOF;
  out->saw_typedef_name = 0;
  out->tag_kind = TK_EOF;
  out->tag_name = NULL;
  out->tag_len = 0;
  out->struct_size = 0;
  out->elem_size = 8;

  // 仮引数の型解析（struct/union の値渡し/ポインタ渡しを含む）
  if (psx_is_decl_prefix_token(curtok()->kind)) {
    token_t *after_prefix = skip_decl_prefix_tokens_for_lookahead(curtok());
    if (after_prefix && psx_ctx_is_tag_keyword(after_prefix->kind)) {
      psx_type_spec_result_t prefix_spec;
      skip_cv_qualifiers_into(&prefix_spec);
      out->type_spec = prefix_spec;
    }
  }
  if (parse_param_tag_decl_spec(out)) {
    return;
  }

  // スカラー型: 仮引数配列宣言子のelemサイズ取得のため型を明示消費
  parse_param_scalar_decl_spec(out);
}

static int parse_param_tag_decl_spec(param_decl_spec_t *out) {
  if (!psx_ctx_is_tag_keyword(curtok()->kind)) return 0;
  out->tag_kind = curtok()->kind;
  set_curtok(curtok()->next);
  token_ident_t *tag_ident = tk_consume_ident();
  if (tag_ident) {
    out->tag_name = tag_ident->str;
    out->tag_len = tag_ident->len;
    if (psx_ctx_has_tag_type(out->tag_kind, out->tag_name, out->tag_len)) {
      out->struct_size = psx_ctx_get_tag_size(out->tag_kind, out->tag_name, out->tag_len);
    }
  }
  if (out->tag_kind == TK_ENUM) {
    out->tag_kind = TK_EOF;
    out->base_type_kind = TK_INT;
    out->elem_size = 4;
    out->struct_size = 0;
  }
  return 1;
}

static void parse_param_scalar_decl_spec(param_decl_spec_t *out) {
  psx_type_spec_result_t type_spec;
  token_kind_t param_type_kind = psx_consume_type_kind_ex(&type_spec);
  out->type_spec = type_spec;
  if (param_type_kind != TK_EOF) {
    out->base_type_kind = param_type_kind;
    out->is_unsigned = (param_type_kind == TK_UNSIGNED) || type_spec.is_unsigned;
    out->is_long_double = type_spec.is_long_double;
    psx_ctx_get_type_info(param_type_kind, NULL, &out->elem_size);
    if (param_type_kind == TK_FLOAT) out->fp_kind = TK_FLOAT_KIND_FLOAT;
    else if (param_type_kind == TK_DOUBLE) out->fp_kind = TK_FLOAT_KIND_DOUBLE;
    /* `double _Complex z` 等: psx_consume_type_kind_ex が _Complex も消費する。
     * HFA 受け取りのため記録する。 */
    out->is_complex = type_spec.is_complex;
  } else if (psx_ctx_is_typedef_name_token(curtok())) {
    out->saw_typedef_name = 1;
    // typedef 名の情報を仮引数解析に伝える。特に「配列型 typedef」が
    // `typedef_name *a` の形でポインタ仮引数になるとき、配列の総バイト数を
    // outer_stride として使うため。
    token_ident_t *id = (token_ident_t *)curtok();
    int td_elem_size = 0;
    int td_is_array = 0;
    int td_sizeof_size = 0;
    int td_first_dim = 0;
    tk_float_kind_t td_fp_kind = TK_FLOAT_KIND_NONE;
    int td_dim_count = 0;
    token_kind_t td_tag_kind = TK_EOF;
    char *td_tag_name = NULL;
    int td_tag_len = 0;
    psx_typedef_info_t _ti;
    if (psx_ctx_find_typedef_name(id->str, id->len, &_ti)) {
      out->base_decl_type = psx_ctx_typedef_decl_type(&_ti);
      td_elem_size = _ti.elem_size;
      td_fp_kind = _ti.fp_kind;
      td_tag_kind = _ti.tag_kind;
      td_tag_name = _ti.tag_name;
      td_tag_len = _ti.tag_len;
      td_is_array = _ti.is_array;
      td_sizeof_size = _ti.sizeof_size;
      td_first_dim = _ti.array_first_dim;
      td_dim_count = _ti.array_dim_count;
      out->is_long_double = _ti.is_long_double ? 1 : 0;
      for (int i = 0; i < td_dim_count && i < 8; i++) out->typedef_array_dims[i] = _ti.array_dims[i];
      if (td_elem_size > 0) out->elem_size = td_elem_size;
      out->typedef_is_array = td_is_array;
      out->typedef_sizeof_size = td_sizeof_size;
      out->typedef_array_first_dim = td_first_dim;
      out->typedef_array_dim_count = td_dim_count;
      if (td_fp_kind != TK_FLOAT_KIND_NONE) out->fp_kind = td_fp_kind;
      /* struct/union typedef (`typedef struct {...} T; T *t`) のタグを伝播し、
       * `t->m` のメンバアクセスと subscript スケーリングを解決できるようにする。 */
      if (psx_ctx_is_tag_aggregate_kind(td_tag_kind)) {
        out->tag_kind = td_tag_kind;
        out->tag_name = td_tag_name;
        out->tag_len = td_tag_len;
        int ts = psx_ctx_get_tag_size(td_tag_kind, td_tag_name, td_tag_len);
        if (ts > 0) out->struct_size = ts;
      }
      /* ポインタ typedef (`typedef char* Str; f(Str s)`): 基底のポインタ性を捕捉し、
       * 仮引数を非配列・宣言子に `*` が無くてもポインタとして登録できるようにする。
       * 未捕捉だと `s` がスカラ登録され `s[i]` が E3064 (subscript 不可) になっていた。
       * elem_size は typedef 解決で pointee サイズ (char=1 等) に設定済み。 */
      if (_ti.is_pointer) {
        out->base_is_pointer = 1;
        int lv = psx_ctx_get_typedef_pointer_levels(id->str, id->len);
        out->base_pointer_levels = (lv > 0) ? lv : 1;
      }
      psx_decl_funcptr_sig_t td_funcptr_sig = psx_ctx_typedef_funcptr_sig(&_ti);
      if (_ti.is_funcptr || ps_decl_funcptr_sig_has_payload(td_funcptr_sig)) {
        out->funcptr_sig = td_funcptr_sig;
      }
      if (_ti.is_unsigned) out->is_unsigned = 1;
    }
    set_curtok(curtok()->next);
  }
}

static void parse_func_decl_spec(func_ret_parse_state_t *ret_state,
                                 token_kind_t *ret_kind, tk_float_kind_t *ret_fp_kind,
                                 token_ident_t **ret_tag, int *ret_is_ptr,
                                 int *ret_is_unsigned) {
  *ret_kind = TK_EOF;
  *ret_fp_kind = TK_FLOAT_KIND_NONE;
  *ret_tag = NULL;
  *ret_is_ptr = 0;
  if (ret_is_unsigned) *ret_is_unsigned = 0;
  psx_type_spec_result_reset(&ret_state->type_spec);
  /* storage class (static/extern) の直後がタグキーワードなら、先に storage class を消費
   * してフラグを立ててからタグ経路へ。psx_consume_type_kind は `static` の後の `struct` を
   * 型と認識できず implicit int に落ちるため (`static struct S *g(){}` が壊れていた)。
   * 後ろがタグでないとき (builtin/typedef) は従来どおり psx_consume_type_kind に任せる。 */
  {
    token_t *t = curtok();
    while (t && (t->kind == TK_STATIC || t->kind == TK_EXTERN ||
                 t->kind == TK_CONST || t->kind == TK_VOLATILE)) t = t->next;
    if (t && t != curtok() && psx_ctx_is_tag_keyword(t->kind)) {
      while (curtok()->kind == TK_STATIC || curtok()->kind == TK_EXTERN ||
             curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE) {
        if (curtok()->kind == TK_STATIC) {
          ret_state->type_spec.is_static = 1;
        }
        if (curtok()->kind == TK_EXTERN) {
          ret_state->type_spec.is_extern = 1;
        }
        if (curtok()->kind == TK_CONST) {
          ret_state->type_spec.is_const_qualified = 1;
        }
        if (curtok()->kind == TK_VOLATILE) {
          ret_state->type_spec.is_volatile_qualified = 1;
        }
        set_curtok(curtok()->next);
      }
    }
  }
  if (psx_ctx_is_tag_keyword(curtok()->kind)) {
    resolve_func_ret_tag_spec(ret_kind, ret_tag);
    parse_pointer_suffix_flags(ret_state, ret_is_ptr); // skip optional pointer(s)
    return;
  }

  *ret_kind = psx_consume_type_kind_ex(&ret_state->type_spec); // 通常の戻り値型（省略可）
  if (*ret_kind == TK_EOF && psx_ctx_is_typedef_name_token(curtok())) {
    resolve_func_ret_typedef(ret_state, ret_kind, ret_fp_kind, ret_tag,
                             ret_is_ptr, ret_is_unsigned);
  }
  *ret_fp_kind = fp_kind_for_type_kind_toplevel(*ret_kind);
  parse_pointer_suffix_flags(ret_state, ret_is_ptr);
}

static void resolve_func_ret_tag_spec(token_kind_t *ret_kind, token_ident_t **ret_tag) {
  *ret_kind = curtok()->kind;
  set_curtok(curtok()->next);
  psx_skip_gnu_attributes();
  token_ident_t *tag = tk_consume_ident();
  if (!tag && curtok()->kind != TK_LBRACE) {
    psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_TAG_NAME));
  }
  if (!tag) {
    char *anon_name = NULL;
    int anon_len = 0;
    psx_make_anonymous_tag_name(&anon_name, &anon_len);
    tag = calloc(1, sizeof(token_ident_t));
    tag->str = anon_name;
    tag->len = anon_len;
  }
  *ret_tag = tag;
  if (tk_consume('{')) {
    int member_count = 0;
    int tag_size = 0;
    int tag_align = 0;
    member_count = psx_parse_tag_definition_body(*ret_kind, tag->str, tag->len, &tag_size, &tag_align);
    psx_ctx_define_tag_type_with_layout(*ret_kind, tag->str, tag->len, member_count, tag_size, tag_align);
  } else if (!psx_ctx_has_tag_type(*ret_kind, tag->str, tag->len)) {
    psx_ctx_define_tag_type(*ret_kind, tag->str, tag->len);
  }
}

static void parse_pointer_suffix_flags(func_ret_parse_state_t *ret_state, int *out_is_ptr) {
  while (curtok()->kind == TK_MUL) {
    set_curtok(curtok()->next);
    if (out_is_ptr) *out_is_ptr = 1;
    if (ret_state) ret_state->ret_pointer_levels++;
    /* ポインタ修飾子 `int *const f()` / `int *volatile f()` の const/volatile を読み飛ばす。
     * これがないと戻り型の `*` の後の const で declarator が止まり E2006 になっていた
     * (ag_c は値の正しさのみ対象なので修飾子はパースして捨てる)。 */
    while (curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE) {
      set_curtok(curtok()->next);
    }
  }
}

static void resolve_func_ret_typedef(func_ret_parse_state_t *ret_state,
                                     token_kind_t *ret_kind, tk_float_kind_t *ret_fp_kind,
                                     token_ident_t **ret_tag, int *ret_is_ptr,
                                     int *ret_is_unsigned) {
  token_ident_t *td_id = (token_ident_t *)curtok();
  token_kind_t td_base = TK_EOF;
  tk_float_kind_t td_fp = TK_FLOAT_KIND_NONE;
  token_kind_t td_tag = TK_EOF;
  char *td_tag_name = NULL;
  int td_tag_len = 0;
  int td_is_ptr = 0;
  int td_is_unsigned = 0;
  /* typedef の unsigned 性を捕捉する。`typedef unsigned char u8` の戻り型 `u8 f()` は
   * td_base=TK_CHAR だが unsigned。捨てると sub-int 戻り値が符号拡張され
   * `u8 f(){return 200;}` が -56 に化ける (uint8_t ローカルと同根の戻り型版)。 */
  psx_typedef_info_t _ti = {0};
  if (psx_ctx_find_typedef_name(td_id->str, td_id->len, &_ti)) {
    td_base = _ti.base_kind; td_fp = _ti.fp_kind;
    td_tag = _ti.tag_kind; td_tag_name = _ti.tag_name; td_tag_len = _ti.tag_len;
    td_is_ptr = _ti.is_pointer; td_is_unsigned = _ti.is_unsigned;
    if (ret_state) {
      ret_state->is_funcptr = _ti.is_funcptr;
      ret_state->funcptr_sig = psx_ctx_typedef_funcptr_sig(&_ti);
    }
  }
  set_curtok(curtok()->next);
  *ret_kind = td_base;
  *ret_fp_kind = td_fp;
  if (td_is_ptr) *ret_is_ptr = 1;
  if (ret_is_unsigned && td_is_unsigned) *ret_is_unsigned = 1;
  if (td_tag != TK_EOF) {
    *ret_tag = calloc(1, sizeof(token_ident_t));
    (*ret_tag)->str = td_tag_name;
    (*ret_tag)->len = td_tag_len;
    *ret_kind = td_tag;
  }
}

static token_ident_t *parse_func_name_declarator_recursive(void) {
  psx_skip_gnu_attributes();
  while (tk_consume('*')) {
    skip_ptr_qualifiers();
  }
  if (tk_consume('(')) {
    while (tk_consume('*')) {
      skip_ptr_qualifiers();
    }
    token_ident_t *name = parse_func_name_declarator_recursive();
    tk_expect(')');
    return name;
  }
  return tk_consume_ident();
}

static token_ident_t *parse_func_declarator(func_ret_parse_state_t *ret_state,
                                            int *out_is_variadic, int *out_has_unnamed_param,
                                            node_t ***out_args, int *out_nargs) {
  int arg_cap = 16;
  node_t **args = calloc(arg_cap, sizeof(node_t *));
  int nargs = 0;
  int is_variadic = 0;
  int has_unnamed_param = 0;
  int parsed_nested_inner_params = 0;

  psx_skip_gnu_attributes();
  token_ident_t *tok = NULL;
  // function declarator returning function pointer:
  //   int (*f(void))(int) { ... }
  if (curtok()->kind == TK_LPAREN && curtok()->next && curtok()->next->kind == TK_MUL) {
    tk_expect('(');
    while (tk_consume('*')) {
      /* `int (*choose(...))(int)` のように外側 declarator が `(*` を含むとき、
       * 戻り値型は宣言子としてはポインタ (関数ポインタ) になる。
       * funcdef 側に戻り値ポインタを伝える。 */
      if (ret_state) {
        ret_state->outer_declarator_is_ptr = 1;
        ret_state->funcptr_object_pointer_levels++;
      }
    }
    if (curtok()->kind == TK_LPAREN && curtok()->next && curtok()->next->kind == TK_MUL) {
      // nested pointer declarator: (*(*f(void))(int))
      tk_expect('(');
      while (tk_consume('*')) {
        if (ret_state) {
          ret_state->outer_declarator_is_ptr = 1;
          ret_state->funcptr_object_pointer_levels++;
        }
      }
      tok = tk_consume_ident();
      if (!tok) {
        psx_diag_ctx(curtok(), "funcdef", "%s",
                     diag_message_for(DIAG_ERR_PARSER_FUNCTION_DEF_EXPECTED));
      }
      tk_expect('(');
      if (!tk_consume(')')) {
        bool done = false;
        node_func_t node_tmp = {0};
        node_tmp.args = args;
        while (!done) {
          if (curtok()->kind == TK_ELLIPSIS) {
            set_curtok(curtok()->next);
            if (curtok()->kind == ',') {
              diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, curtok(),
                             "%s",
                             diag_message_for(DIAG_ERR_PARSER_VARIADIC_NOT_LAST));
            }
            is_variadic = 1;
            done = true;
            continue;
          }
          if (parse_param_decl(&node_tmp, &nargs, &arg_cap, 0)) has_unnamed_param = 1;
          args = node_tmp.args;
          if (!tk_consume(',')) break;
          if (curtok()->kind == TK_RPAREN) {
            psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_PARAMETER));
          }
        }
        tk_expect(')');
      }
      tk_expect(')');
      parsed_nested_inner_params = 1;
    } else {
      tok = parse_func_name_declarator_recursive();
      if (!tok) {
        psx_diag_ctx(curtok(), "funcdef", "%s",
                     diag_message_for(DIAG_ERR_PARSER_FUNCTION_DEF_EXPECTED));
      }
    }
    tk_expect('(');
    if (!tk_consume(')')) {
      bool done = false;
      node_func_t node_tmp = {0};
      node_tmp.args = args;
      while (!done) {
        if (curtok()->kind == TK_ELLIPSIS) {
          set_curtok(curtok()->next);
          if (curtok()->kind == ',') {
            diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, curtok(),
                           "%s",
                           diag_message_for(DIAG_ERR_PARSER_VARIADIC_NOT_LAST));
          }
          is_variadic = 1;
          done = true;
          continue;
        }
        if (parse_param_decl(&node_tmp, &nargs, &arg_cap, 0) && !parsed_nested_inner_params) {
          has_unnamed_param = 1;
        }
        args = node_tmp.args;
        if (!tk_consume(',')) break;
        if (curtok()->kind == TK_RPAREN) {
          psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_PARAMETER));
        }
      }
      tk_expect(')');
    }
    tk_expect(')');
    // consume trailing direct-declarator suffixes for returned function pointer type.
    while (curtok()->kind == TK_LPAREN || curtok()->kind == TK_LBRACKET) {
      if (curtok()->kind == TK_LPAREN) {
        psx_funcptr_signature_t returned_func_sig = {0};
        psx_skip_func_param_list(&returned_func_sig);
        if (ret_state) {
          ret_state->is_funcptr = 1;
          ret_state->funcptr_sig.function.callable.signature.param_fp_mask =
              returned_func_sig.param_fp_mask;
          ret_state->funcptr_sig.function.callable.signature.param_int_mask =
              returned_func_sig.param_int_mask;
          ret_state->funcptr_sig.function.callable.signature.is_variadic =
              returned_func_sig.is_variadic ? 1 : 0;
          ret_state->funcptr_sig.function.callable.signature.nargs_fixed =
              (short)returned_func_sig.nargs_fixed;
        }
        continue;
      }
      if (tk_consume('[')) {
        /* pointee 配列次元 `int (*f())[N]` / `int (*f())[N][M]` を捕捉する。これを記録しないと
         * 呼び出し結果 `f()[i]` の行ストライドが分からず base 要素サイズで誤スケール→SIGSEGV。 */
        if (curtok()->kind != TK_RBRACKET) {
          int n = psx_parse_array_size_constexpr();
          if (ret_state && ret_state->pointee_dim_count == 0) ret_state->pointee_first_dim = n;
          else if (ret_state && ret_state->pointee_dim_count == 1) ret_state->pointee_second_dim = n;
        }
        if (ret_state) ret_state->pointee_dim_count++;
        tk_expect(']');
      }
    }
  } else {
    tok = parse_func_name_declarator_recursive();
    if (!tok) {
      psx_diag_ctx(curtok(), "funcdef", "%s",
                   diag_message_for(DIAG_ERR_PARSER_FUNCTION_DEF_EXPECTED));
    }
    tk_expect('(');
    if (!tk_consume(')')) {
      bool done = false;
      node_func_t node_tmp = {0};
      node_tmp.args = args;
      while (!done) {
        if (curtok()->kind == TK_ELLIPSIS) {
          set_curtok(curtok()->next);
          if (curtok()->kind == ',') {
            diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, curtok(),
                           "%s",
                           diag_message_for(DIAG_ERR_PARSER_VARIADIC_NOT_LAST));
          }
          is_variadic = 1;
          done = true;
          continue;
        }
        if (parse_param_decl(&node_tmp, &nargs, &arg_cap, 1)) has_unnamed_param = 1;
        args = node_tmp.args;
        if (!tk_consume(',')) break;
        if (curtok()->kind == TK_RPAREN) {
          psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_PARAMETER));
        }
      }
      tk_expect(')');
    }
  }

  psx_skip_gnu_attributes();
  *out_is_variadic = is_variadic;
  *out_has_unnamed_param = has_unnamed_param;
  *out_args = args;
  *out_nargs = nargs;
  return tok;
}

// funcdef = "int"? ident "(" params? ")" (";" | "{" stmt* "}")
// params  = "int"? ident ("," "int"? ident)*
/* 関数本体の `{ ... }` を 1 つの node_block_t にパースする。
 * 既に opening `{` は呼出側が consume 済みの前提。block scope を enter / leave し、
 * 後段 semantic pass 用に各 statement の診断 token / usage region を保持する。
 * pragma pack マーカーは透過に消費する。 */
static node_block_t *parse_funcdef_body_block(void) {
  psx_ctx_enter_block_scope();
  node_block_t *body = arena_alloc(sizeof(node_block_t));
  body->base.kind = ND_BLOCK;
  int i = 0;
  int body_cap = 16;
  body->body = calloc(body_cap, sizeof(node_t *));
  while (!tk_consume('}')) {
    // #pragma pack マーカーは関数本体冒頭・任意の位置で出現しうる。透過処理。
    if (psx_try_consume_pragma_pack_marker()) continue;
    if (i >= body_cap - 1) {
      body_cap = pda_next_cap(body_cap, i + 2);
      body->body = pda_xreallocarray(body->body, (size_t)body_cap, sizeof(node_t *));
    }
    token_t *stmt_tok = curtok();
    psx_lvar_usage_region_t *region = psx_decl_begin_lvar_usage_region();
    body->body[i] = psx_stmt_stmt();
    psx_decl_end_lvar_usage_region(region);
    if (body->body[i]) {
      body->body[i]->tok = stmt_tok;
      body->body[i]->usage_region = region;
    }
    i++;
  }
  body->body[i] = NULL;
  psx_ctx_leave_block_scope();
  return body;
}

static node_t *funcdef(void) {
  token_kind_t ret_kind;
  tk_float_kind_t ret_fp_kind = TK_FLOAT_KIND_NONE;
  token_ident_t *ret_tag = NULL;
  int ret_is_ptr = 0;
  int ret_td_unsigned = 0;
  func_ret_parse_state_t ret_state = {0};
  parse_func_decl_spec(&ret_state, &ret_kind, &ret_fp_kind, &ret_tag,
                       &ret_is_ptr, &ret_td_unsigned);
  /* static 関数 (内部リンケージ) かを捕捉する。parse_func_decl_spec が返した
   * return type-spec result から読む。 */
  int fn_is_static = ret_state.type_spec.is_static ? 1 : 0;
  int saw_implicit_int_return = (ret_kind == TK_EOF);
  token_kind_t ret_token_kind = (ret_kind == TK_EOF) ? TK_INT : ret_kind;
  /* 戻り値型の unsigned 性を捕捉する (`unsigned` は ret_kind が TK_INT に潰れ
   * 符号性が落ちるため別管理)。後段で関数名判明後に記録する。 */
  /* 基底型の unsigned 性。ポインタ返しでも pointee 符号として保持し、
   * `unsigned char *g(); g()[i]` の subscript zero-extend に使う。 */
  int ret_base_unsigned = ret_state.type_spec.is_unsigned || ret_td_unsigned;
  int ret_pointee_const = ret_state.type_spec.is_const_qualified;
  int ret_pointee_volatile = ret_state.type_spec.is_volatile_qualified;
  /* 戻り型が _Complex か。
   * IR builder が複素数戻り値 (HFA: re→d0, im→d1) を組むために node に記録する。 */
  int ret_is_complex = !ret_is_ptr && ret_state.type_spec.is_complex;
  // 構造体戻り値の場合、サイズを記録（ポインタ戻り値は除く）
  int ret_struct_size = 0;
  if (psx_ctx_is_tag_aggregate_kind(ret_kind) && !ret_is_ptr) {
    if (ret_tag && psx_ctx_has_tag_type(ret_kind, ret_tag->str, ret_tag->len)) {
      ret_struct_size = psx_ctx_get_tag_size(ret_kind, ret_tag->str, ret_tag->len);
    }
  }
  // 関数ごとにローカル変数テーブルをリセット
  psx_decl_reset_locals();
  psx_ctx_reset_function_scope();

  int is_variadic = 0;
  int has_unnamed_param = 0;
  node_t **args = NULL;
  int nargs = 0;
  token_ident_t *tok = parse_func_declarator(&ret_state, &is_variadic,
                                             &has_unnamed_param, &args, &nargs);
  /* declarator が `(*` を含めば、戻り値型はポインタ (`int (*f())[N]` も含む)。
   * function pointer かどうかは後続の関数 suffix を実際に見た ret_state.is_funcptr で判定する。 */
  int returned_funcptr_returns_data_pointer = ret_is_ptr;
  if (ret_state.outer_declarator_is_ptr) {
    ret_is_ptr = 1;
  }
  node_func_t *node = arena_alloc(sizeof(node_func_t));
  node->base.kind = ND_FUNCDEF;
  node->base.tok = (token_t *)tok;
  node->base.is_implicit_int_return = saw_implicit_int_return ? 1 : 0;
  /* 戻り型の fp_kind をノードへ記録。IR builder の ir_type_from_node が
   * 関数の戻り型 (IR_TY_F32/F64) を決定し、callee が fp レジスタで返すために必要。
   * ただし `double *g()` のようにポインタを返す関数は戻り値が x0 のポインタ値なので
   * fp_kind を立ててはいけない (立てると funcall が d0 から読み SIGSEGV)。pointee が
   * fp であることは別途 ret_token_kind 経由 (psx_node_pointee_fp_kind) で扱う。 */
  node->base.fp_kind = ret_is_ptr ? TK_FLOAT_KIND_NONE : ret_fp_kind;
  node->base.is_complex = ret_is_complex;
  node->funcname = tok->str;
  node->funcname_len = tok->len;
  int ret_is_funcptr = ret_state.is_funcptr;
  psx_decl_funcptr_sig_t funcptr_sig = ret_state.funcptr_sig;
  int function_ret_pointer_levels = ret_state.ret_pointer_levels;
  if (ret_state.outer_declarator_is_ptr) {
    function_ret_pointer_levels = ret_state.funcptr_object_pointer_levels > 0
                                      ? ret_state.funcptr_object_pointer_levels
                                      : 1;
    if (ret_is_funcptr) {
      funcptr_sig.function.callable.return_shape.is_data_pointer =
          returned_funcptr_returns_data_pointer ? 1 : 0;
      funcptr_sig.function.callable.return_shape.is_void =
          (ret_kind == TK_VOID &&
           !funcptr_sig.function.callable.return_shape.is_data_pointer) ? 1 : 0;
      funcptr_sig.function.callable.return_shape.is_complex =
          (ret_is_complex &&
           !funcptr_sig.function.callable.return_shape.is_data_pointer) ? 1 : 0;
    }
  }
  psx_function_signature_t sig = {0};
  sig.name = tok;
  sig.diag_context = "funcdef";
  sig.ret_struct_size = ret_struct_size;
  sig.ret_fp_kind = ret_fp_kind;
  sig.ret_token_kind = ret_token_kind;
  sig.ret_is_ptr = ret_is_ptr;
  sig.ret_base_unsigned = ret_base_unsigned;
  sig.ret_pointee_const = ret_pointee_const;
  sig.ret_pointee_volatile = ret_pointee_volatile;
  sig.ret_is_complex = ret_is_complex;
  sig.ret_is_void = (ret_kind == TK_VOID);
  sig.ret_tag_kind = ret_kind;
  sig.ret_tag_name = ret_tag ? ret_tag->str : NULL;
  sig.ret_tag_len = ret_tag ? ret_tag->len : 0;
  sig.ret_pointer_levels = function_ret_pointer_levels;
  sig.ret_pointee_first_dim = ret_state.pointee_first_dim;
  sig.ret_pointee_second_dim = ret_state.pointee_second_dim;
  sig.ret_pointee_dim_count = ret_state.pointee_dim_count;
  sig.is_variadic = is_variadic;
  sig.nargs = nargs;
  sig.args = args;
  sig.ret_is_funcptr = ret_is_funcptr;
  sig.funcptr_sig = funcptr_sig;
  sig.func_node = node;
  register_function_signature(&sig);
  psx_decl_set_current_funcname(tok->str, tok->len); // __func__ / static local mangle 用
  node->is_static = fn_is_static;
  node->args = args;
  node->is_variadic = is_variadic;
  node->nargs = nargs;
  // 可変長引数関数: ローカル変数スペースを引数レジスタ保存領域の後ろに移動する
  if (node->is_variadic) {
    psx_decl_reserve_variadic_regs();
  }

  // 関数プロトタイプ宣言（本体なし）
  if (tk_consume(';')) {
    /* __func__ 用に立てた現在関数名を NULL に戻す。プロトタイプの後はファイルスコープ
     * なので、ここを残すと後続のファイルスコープ複合リテラル `&(int){5}` 等が「関数内」と
     * 誤判定されローカル lvar 経路に乗ってしまう (assert.h の宣言後に顕在化)。 */
    psx_decl_set_current_funcname(NULL, 0);
    return NULL;
  }
  if (has_unnamed_param) {
    // 関数定義の仮引数では識別子必須。
    psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_PARAMETER));
  }
  /* C11 6.9p3: 同一名の関数を 2 度定義することはできない。プロトタイプ宣言は何度でも OK
   * だが、本体 `{...}` を伴う定義は 1 度のみ。`;` (プロトタイプ) を弾いた後にチェックする。 */
  if (!psx_ctx_track_function_defined(tok->str, tok->len)) {
    psx_diag_ctx(curtok(), "funcdef",
                 "関数 '%.*s' の重複定義 (C11 6.9p3)",
                 tok->len, tok->str);
  }

  // 関数本体 (ブロック)
  tk_expect('{');
  node_block_t *body = parse_funcdef_body_block();
  node->base.rhs = (node_t *)body;
  psx_ctx_validate_goto_refs();

  /* IR builder (Phase 4d-1〜) が関数ごとの lvar リストを必要とするため、
   * 関数解析完了時点の all_locals 先頭を node に保存しておく。
   * psx_decl_reset_locals は次の関数開始時に呼ばれるが、それは静的変数を
   * NULL に戻すだけで、既存 lvar_t は arena/calloc されたまま残る。 */
  node->lvars = psx_decl_get_locals();

  /* 関数本体を抜けたらファイルスコープに戻る。現在関数名を NULL に戻し、関数間の
   * ファイルスコープ宣言が「関数内」と誤判定されないようにする。 */
  psx_decl_set_current_funcname(NULL, 0);

  psx_semantic_analyze_function((node_t *)node, curtok());
  return (node_t *)node;
}

// expr = assign ("," assign)*
node_t *ps_expr_ctx(tokenizer_context_t *tk_ctx, token_t *start) {
  if (tk_ctx) {
    tk_set_current_token_ctx(tk_ctx, start);
  }
  tk_set_current_token(start);
  node_t *node = psx_expr_expr();
  if (tk_ctx) {
    tk_set_current_token_ctx(tk_ctx, tk_get_current_token());
  }
  return node;
}

node_t *ps_expr_from(token_t *start) {
  return ps_expr_ctx(NULL, start);
}

node_t *ps_expr(void) {
  return ps_expr_ctx(NULL, tk_get_current_token());
}
