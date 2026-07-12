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
#include "declarator_syntax.h"
#include "dynarray.h"
#include "enum_const.h"
#include "expr.h"
#include "global_registry.h"
#include "initializer_syntax.h"
#include "ret_pointee_array.h"
#include "stmt.h"
#include "struct_layout.h"
#include "type.h"
#include "type_name.h"
#include "../diag/diag.h"
#include "../lowering/global_object_lowering.h"
#include "../lowering/parameter_lowering.h"
#include "../lowering/static_data_initializer.h"
#include "../lowering/vla_lowering.h"
#include "../semantic/function_declaration_resolution.h"
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
  psx_type_spec_result_t type_spec;
  psx_type_name_t type_name;
  /* Identifier-outward operators from the function declarator come before
   * pointer operators lexically attached to the declaration specifier. */
  psx_declarator_shape_t declarator_shape;
  psx_declarator_shape_t base_pointer_shape;
  int saw_implicit_int;
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
  int is_complex;
  /* typedef 参照の正本。派生 typedef はこの型木へdeclarator operatorを適用する。 */
  const psx_type_t *base_decl_type;
  int pointee_const;
  int pointee_volatile;
} toplevel_decl_spec_t;

static node_t *funcdef(void);
static void parse_toplevel_decl_after_type(const toplevel_decl_spec_t *spec);
static int has_next_toplevel_declarator(void);
typedef struct {
  token_ident_t *name;
  psx_declarator_shape_t declarator_shape;
} toplevel_declarator_head_t;
static toplevel_declarator_head_t new_toplevel_declarator_head(void);
static toplevel_declarator_head_t parse_toplevel_declarator_head(const toplevel_decl_spec_t *spec,
                                                                 int require_name);
static void parse_toplevel_declarator_stmt(const toplevel_decl_spec_t *spec,
                                           void (*apply)(const toplevel_decl_spec_t *,
                                                         toplevel_declarator_head_t));
static void parse_toplevel_declarator_list_with_apply(const toplevel_decl_spec_t *spec,
                                                      void (*apply)(const toplevel_decl_spec_t *,
                                                                    toplevel_declarator_head_t));
static void apply_toplevel_typedef_from_head(const toplevel_decl_spec_t *spec,
                                             toplevel_declarator_head_t head);
static void define_toplevel_typedef_from_declarator(const toplevel_decl_spec_t *spec,
                                                    toplevel_declarator_head_t head);
static void register_toplevel_typedef_name(
    token_ident_t *name, psx_type_t *derived_type);
static psx_type_t *build_toplevel_derived_typedef_type(
    const toplevel_decl_spec_t *spec, toplevel_declarator_head_t head);
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
                                             int td_is_unsigned, int td_is_long_double);
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
static void emit_decl_name_required_diag(void);
static int consume_toplevel_decl_suffix(void *context, int nesting_depth,
                                        int direct_was_parenthesized,
                                        int direct_pointer_count,
                                        int frame_pointer_count);
static int append_toplevel_decl_pointer(void *context, int is_const,
                                        int is_volatile, int nesting_depth);
static void diagnose_toplevel_decl_too_complex(void *context, token_t *tok);
static int is_toplevel_function_signature(token_t *tok);
static int is_tag_return_function_signature(token_t *tok);
static void skip_balanced_group(token_kind_t lkind, token_kind_t rkind);
typedef struct {
  psx_declarator_shape_t declarator_shape;
  int inner_dim_consts[7];
  token_ident_t *inner_dim_idents[7];
  int inner_dim_count;
} param_declarator_state_t;
typedef struct {
  param_declarator_state_t *decl_state;
  int bracket_count[24];
} param_declarator_syntax_ctx_t;
static token_ident_t *parse_param_declarator_name(
    param_declarator_state_t *decl_state);
static int parse_param_decl(node_func_t *node, int *nargs, int *arg_cap, int count_unnamed);
static int param_declarator_is_grouping(void *context, int nesting_depth);
static int consume_param_declarator_suffix(
    void *context, int nesting_depth, int direct_was_parenthesized,
    int direct_pointer_count, int frame_pointer_count);
static int append_param_declarator_pointer(
    void *context, int is_const, int is_volatile, int nesting_depth);
typedef struct {
  psx_type_spec_result_t type_spec;
  token_kind_t base_type_kind;
  int saw_typedef_name;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int struct_size;
  int elem_size;
  // float/double 仮引数を ABI に従い d0..d7 で受け取るための種別。
  tk_float_kind_t fp_kind;
  // `double _Complex` / `float _Complex` 仮引数。HFA として 2 FP レジスタで受ける。
  int is_complex;
  const psx_type_t *base_decl_type;
  // 基底型が unsigned か (`unsigned char* p` の pointee zero-extend に使う)。
  int is_unsigned;
} param_decl_spec_t;
static int parse_param_tag_decl_spec(param_decl_spec_t *out);
static void parse_param_scalar_decl_spec(param_decl_spec_t *out);
static void parse_param_decl_spec(param_decl_spec_t *out);
static void parse_func_decl_spec(func_ret_parse_state_t *ret_state);
static void parse_pointer_suffix_flags(func_ret_parse_state_t *ret_state);
static void resolve_func_ret_typedef(func_ret_parse_state_t *ret_state);
static void resolve_func_ret_tag_spec(token_kind_t *ret_kind, token_ident_t **ret_tag);
typedef struct {
  node_t **args;
  int arg_cap;
  int nargs;
  int is_variadic;
  int has_unnamed_param;
} parsed_function_params_t;
static void parsed_function_params_init(parsed_function_params_t *params);
static void parse_function_param_list(parsed_function_params_t *params,
                                      int count_unnamed);
static token_ident_t *parse_func_declarator(func_ret_parse_state_t *ret_state,
                                            int *out_is_variadic, int *out_has_unnamed_param,
                                            node_t ***out_args, int *out_nargs);
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
  psx_type_t *return_type;
  int is_variadic;
  int is_definition;
  int nargs;
  node_t **args;
  node_func_t *func_node;
} psx_function_signature_t;

static void validate_toplevel_object_type(
    const toplevel_decl_spec_t *spec, const psx_type_t *type);
static void register_toplevel_function_prototype(const toplevel_decl_spec_t *spec,
                                                 toplevel_declarator_head_t head);
static void register_function_signature(const psx_function_signature_t *sig);
static global_var_t *register_toplevel_object_from_declarator(
    const toplevel_decl_spec_t *spec, toplevel_declarator_head_t head,
    psx_type_t *canonical_type);
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
  int td_is_unsigned = 0;
  int td_is_long_double = 0;
  psx_typedef_info_t _ti;
  if (psx_ctx_find_typedef_name(id->str, id->len, &_ti)) {
    td_base = _ti.base_kind; td_elem = _ti.elem_size; td_fp = _ti.fp_kind;
    td_tag = _ti.tag_kind; td_tag_name = _ti.tag_name; td_tag_len = _ti.tag_len;
    td_is_unsigned = _ti.is_unsigned;
    td_is_long_double = _ti.is_long_double;
    spec->base_decl_type = psx_ctx_typedef_decl_type(&_ti);
  }
  set_curtok(curtok()->next);
  apply_toplevel_typedef_decl_spec(spec, td_base, td_elem, td_fp, td_tag, td_tag_name, td_tag_len,
                                   td_is_unsigned, td_is_long_double);
}

static void apply_toplevel_typedef_decl_spec(toplevel_decl_spec_t *spec,
                                             token_kind_t td_base, int td_elem, tk_float_kind_t td_fp,
                                             token_kind_t td_tag, char *td_tag_name, int td_tag_len,
                                             int td_is_unsigned, int td_is_long_double) {
  spec->base_kind = td_base;
  spec->is_unsigned = td_is_unsigned ? 1 : 0;
  spec->is_long_double = td_is_long_double ? 1 : 0;
  spec->fp_kind = td_fp;
  spec->tag_kind = td_tag;
  spec->tag_name = td_tag_name;
  spec->tag_len = td_tag_len;
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

static void parse_toplevel_declarator_list(const toplevel_decl_spec_t *spec) {
  parse_toplevel_declarator_list_with_apply(
      spec, apply_toplevel_object_from_head);
}

static void parse_toplevel_declarator_list_with_apply(const toplevel_decl_spec_t *spec,
                                                      void (*apply)(const toplevel_decl_spec_t *,
                                                                    toplevel_declarator_head_t)) {
  int declarator_count = 0;
  for (;;) {
    declarator_count++;
    guard_toplevel_declarator_count(declarator_count);
    toplevel_declarator_head_t head =
        parse_toplevel_declarator_head(spec, 1);
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
  psx_type_t *canonical_type = build_toplevel_derived_typedef_type(spec, head);
  const psx_type_t *value_type = canonical_type;
  while (value_type && value_type->kind == PSX_TYPE_ARRAY)
    value_type = value_type->base;
  if (spec->tag_kind != TK_EOF && value_type &&
      value_type->kind != PSX_TYPE_POINTER &&
      ps_ctx_get_tag_member_count(spec->tag_kind, spec->tag_name,
                                  spec->tag_len) <= 0) {
    psx_diag_ctx(curtok(), "decl", "%s",
                 diag_message_for(DIAG_ERR_PARSER_INCOMPLETE_OBJECT_FORBIDDEN));
  }
  validate_toplevel_object_type(spec, canonical_type);
  global_var_t *gv = register_toplevel_object_from_declarator(
      spec, head, canonical_type);
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

static toplevel_declarator_head_t parse_toplevel_declarator_head(
    const toplevel_decl_spec_t *spec, int require_name) {
  toplevel_declarator_head_t out = new_toplevel_declarator_head();
  out.name = parse_toplevel_decl_name(spec, &out);
  if (!out.name && require_name) emit_decl_name_required_diag();
  return out;
}

static toplevel_declarator_head_t new_toplevel_declarator_head(void) {
  toplevel_declarator_head_t out = {0};
  psx_declarator_shape_init(&out.declarator_shape);
  return out;
}

static void validate_toplevel_object_type(
    const toplevel_decl_spec_t *spec, const psx_type_t *type) {
  if (!type || type->kind != PSX_TYPE_ARRAY || type->array_len > 0 ||
      spec->is_extern) return;
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

static global_var_t *register_toplevel_object_from_declarator(
    const toplevel_decl_spec_t *spec, toplevel_declarator_head_t head,
    psx_type_t *canonical_type) {
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
  register_toplevel_typedef_name(
      head.name, build_toplevel_derived_typedef_type(spec, head));
}

static void register_toplevel_typedef_name(
    token_ident_t *name, psx_type_t *derived_type) {
  psx_typedef_info_t _ti = {0};
  psx_ctx_typedef_set_decl_type(&_ti, derived_type);
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
    const toplevel_decl_spec_t *spec, toplevel_declarator_head_t head) {
  if (!spec) return NULL;

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

static void apply_toplevel_typedef_from_head(const toplevel_decl_spec_t *spec,
                                             toplevel_declarator_head_t head) {
  define_toplevel_typedef_from_declarator(spec, head);
}

static int has_next_toplevel_declarator(void) {
  return tk_consume(',');
}

static token_ident_t *parse_toplevel_decl_name(const toplevel_decl_spec_t *spec,
                                               toplevel_declarator_head_t *head) {
  (void)spec;
  return psx_parse_declarator_syntax(
      &(psx_declarator_syntax_t){
          .context = head,
          .consume_suffix = consume_toplevel_decl_suffix,
          .append_pointer = append_toplevel_decl_pointer,
          .diagnose_too_complex = diagnose_toplevel_decl_too_complex,
      }, NULL);
}

static void parsed_function_params_init(parsed_function_params_t *params) {
  if (!params) return;
  *params = (parsed_function_params_t){0};
  params->arg_cap = 16;
  params->args = calloc(params->arg_cap, sizeof(node_t *));
}

static void parse_function_param_list(parsed_function_params_t *params,
                                      int count_unnamed) {
  if (!params) return;
  tk_expect('(');
  if (!tk_consume(')')) {
    bool done = false;
    node_func_t node_tmp = {0};
    node_tmp.args = params->args;
    while (!done) {
      if (curtok()->kind == TK_ELLIPSIS) {
        set_curtok(curtok()->next);
        if (curtok()->kind == ',') {
          diag_emit_tokf(DIAG_ERR_PARSER_INVALID_CONTEXT, curtok(), "%s",
                         diag_message_for(DIAG_ERR_PARSER_VARIADIC_NOT_LAST));
        }
        params->is_variadic = 1;
        done = true;
        continue;
      }
      if (parse_param_decl(&node_tmp, &params->nargs, &params->arg_cap,
                           count_unnamed))
        params->has_unnamed_param = 1;
      params->args = node_tmp.args;
      if (!tk_consume(',')) break;
      if (curtok()->kind == TK_RPAREN) {
        psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_PARAMETER));
      }
    }
    tk_expect(')');
  }
}

/* curtok が `(` のとき仮引数リストだけを消費する (関数名は既に読んだ後)。 */
static void parse_func_param_list_only(int *out_is_variadic, int *out_has_unnamed_param,
                                       node_t ***out_args, int *out_nargs) {
  parsed_function_params_t params;
  parsed_function_params_init(&params);
  parse_function_param_list(&params, 1);
  *out_is_variadic = params.is_variadic;
  *out_has_unnamed_param = params.has_unnamed_param;
  *out_args = params.args;
  *out_nargs = params.nargs;
}

static void register_function_signature(const psx_function_signature_t *sig) {
  token_ident_t *tok = sig->name;
  psx_type_t *param_types[16] = {0};
  int tracked_param_count = sig->nargs > 16 ? 16 : sig->nargs;
  for (int i = 0; i < tracked_param_count; i++)
    param_types[i] = sig->args ? ps_node_get_type(sig->args[i]) : NULL;
  psx_function_declaration_resolution_t resolution;
  psx_resolve_function_declaration(
      &(psx_function_declaration_resolution_request_t){
          .name = tok->str,
          .name_len = tok->len,
          .return_type = sig->return_type,
          .parameter_types = param_types,
          .parameter_count = sig->nargs,
          .is_variadic = sig->is_variadic,
          .is_definition = sig->is_definition,
      },
      &resolution);
  if (resolution.status == PSX_FUNCTION_DECLARATION_INVALID) {
    psx_diag_ctx(curtok(), sig->diag_context,
                 "canonical function declaration resolution failed for '%.*s'",
                 tok->len, tok->str);
  }
  if (resolution.status == PSX_FUNCTION_DECLARATION_OBJECT_NAME_CONFLICT) {
    psx_diag_ctx(curtok(), sig->diag_context,
                 "'%.*s' はグローバル変数として既に宣言されています (C11 6.7p4)",
                 tok->len, tok->str);
  }
  if (resolution.status == PSX_FUNCTION_DECLARATION_TYPE_CONFLICT) {
    psx_diag_ctx(curtok(), sig->diag_context,
                 "関数 '%.*s' の型が以前の宣言と異なります (C11 6.7p3-4)",
                 tok->len, tok->str);
  }
  if (resolution.status == PSX_FUNCTION_DECLARATION_DUPLICATE_DEFINITION) {
    psx_diag_ctx(curtok(), sig->diag_context,
                 "関数 '%.*s' の重複定義 (C11 6.9p3)",
                 tok->len, tok->str);
  }
  if (sig->func_node &&
      resolution.declaration_plan.returns_function_pointer) {
    ps_node_funcdef_set_ret_funcptr_sig(
        sig->func_node,
        resolution.declaration_plan.returned_funcptr_signature);
  }
}

/* `int f(int), g(int), a;` の f/g 等: funcdef と同様に関数テーブルへプロトタイプを登録する。 */
static void register_toplevel_function_prototype(const toplevel_decl_spec_t *spec,
                                                 toplevel_declarator_head_t head) {
  token_ident_t *tok = head.name;
  if (!tok || curtok()->kind != TK_LPAREN) return;
  psx_decl_reset_locals();
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
  sig.return_type = build_toplevel_derived_typedef_type(spec, head);
  sig.is_variadic = is_variadic;
  sig.nargs = nargs;
  sig.args = args;
  register_function_signature(&sig);
}

static void emit_decl_name_required_diag(void) {
  diag_emit_tokf(DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED, curtok(), "%s",
                 diag_message_for(DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED));
}

static void diagnose_toplevel_decl_too_complex(void *context, token_t *tok) {
  (void)context;
  psx_diag_ctx(tok, "decl", "%s", "declarator is too complex");
}

static int append_toplevel_decl_pointer(void *context, int is_const,
                                        int is_volatile, int nesting_depth) {
  (void)nesting_depth;
  toplevel_declarator_head_t *head = context;
  if (!head) return 0;
  return psx_declarator_shape_append_pointer(
      &head->declarator_shape, is_const, is_volatile);
}

static int consume_toplevel_decl_suffix(void *context, int nesting_depth,
                                        int direct_was_parenthesized,
                                        int direct_pointer_count,
                                        int frame_pointer_count) {
  (void)direct_pointer_count;
  (void)frame_pointer_count;
  toplevel_declarator_head_t *head = context;
  if (!head) return 0;
  if (tk_consume('[')) {
    int has_size = 0;
    int dim = psx_parse_array_size_optional_constexpr(&has_size);
    if (!psx_declarator_shape_append_array_ex(
            &head->declarator_shape, has_size ? dim : 0, !has_size))
      diagnose_toplevel_decl_too_complex(context, curtok());
    return 1;
  }

  /* A plain file-scope `f(...)` is registered by the prototype path, which
   * needs to parse named parameters into AST locals. Nested and
   * parenthesized function suffixes are declarator operators here. */
  if (curtok()->kind == TK_LPAREN &&
      (nesting_depth > 0 || direct_was_parenthesized)) {
    psx_funcptr_signature_t parsed_suffix = {0};
    psx_skip_func_param_list(&parsed_suffix);
    psx_decl_funcptr_sig_t op_sig = {0};
    op_sig.function.callable.signature = parsed_suffix;
    if (!psx_declarator_shape_append_function(
            &head->declarator_shape, op_sig))
      diagnose_toplevel_decl_too_complex(context, curtok());
    return 1;
  }
  return 0;
}

static void parse_toplevel_decl_after_type(const toplevel_decl_spec_t *spec) {
  if (spec->is_typedef) {
    parse_toplevel_declarator_stmt(spec, apply_toplevel_typedef_from_head);
    return;
  }
  /* typedefのpointer/array/function構造はbase_decl_typeに保持される。 */
  parse_toplevel_declarator_stmt(spec, apply_toplevel_object_from_head);
}

static void parse_toplevel_declarator_stmt(const toplevel_decl_spec_t *spec,
                                           void (*apply)(const toplevel_decl_spec_t *,
                                                         toplevel_declarator_head_t)) {
  parse_toplevel_declarator_list_with_apply(spec, apply);
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

static token_ident_t *parse_param_declarator_name(
    param_declarator_state_t *decl_state) {
  /* N-D VLA 仮引数 (`int t[n][m][k][l]` 等) の inner dim は
   * 仮引数1個ごとの明示 state として登録側へ渡す。 */
  if (decl_state) {
    memset(decl_state, 0, sizeof(*decl_state));
    psx_declarator_shape_init(&decl_state->declarator_shape);
  }
  param_declarator_syntax_ctx_t context = {
      .decl_state = decl_state,
  };
  return psx_parse_declarator_syntax(
      &(psx_declarator_syntax_t){
          .context = &context,
          .consume_suffix = consume_param_declarator_suffix,
          .is_grouping_parenthesis = param_declarator_is_grouping,
          .append_pointer = append_param_declarator_pointer,
      },
      NULL);
}

static int psx_skip_param_func_suffix_groups(
    param_declarator_state_t *decl_state) {
  int consumed = 0;
  while (curtok()->kind == TK_LPAREN) {
    psx_funcptr_signature_t parsed_suffix = {0};
    psx_skip_func_param_list(&parsed_suffix);
    consumed = 1;
    if (decl_state) {
      psx_decl_funcptr_sig_t op_sig = {0};
      op_sig.function.callable.signature = parsed_suffix;
      psx_declarator_shape_append_function(
          &decl_state->declarator_shape, op_sig);
    }
  }
  return consumed;
}

static int param_declarator_is_grouping(void *context, int nesting_depth) {
  (void)context;
  (void)nesting_depth;
  token_t *next = curtok()->next;
  if (!next || next->kind == TK_RPAREN) return 0;
  if (psx_ctx_is_tag_keyword(next->kind) ||
      psx_ctx_is_type_token(next->kind) ||
      psx_ctx_is_typedef_name_token(next) ||
      next->kind == TK_CONST || next->kind == TK_VOLATILE) {
    return 0;
  }
  return 1;
}

static int append_param_declarator_pointer(
    void *context, int is_const, int is_volatile, int nesting_depth) {
  (void)nesting_depth;
  param_declarator_syntax_ctx_t *ctx = context;
  return ctx && ctx->decl_state &&
         psx_declarator_shape_append_pointer(
             &ctx->decl_state->declarator_shape, is_const, is_volatile);
}

static int consume_param_declarator_suffix(
    void *context, int nesting_depth, int direct_was_parenthesized,
    int direct_pointer_count, int frame_pointer_count) {
  param_declarator_syntax_ctx_t *ctx = context;
  if (!ctx || nesting_depth < 0 || nesting_depth >= 24) return 0;
  if (curtok()->kind == TK_LPAREN) {
    return psx_skip_param_func_suffix_groups(ctx->decl_state);
  }
  if (curtok()->kind != TK_LBRACKET) return 0;

  int bracket_count = ctx->bracket_count[nesting_depth]++;
  int paren_made_pointer =
      direct_was_parenthesized && direct_pointer_count > 0;
  int has_pointer = direct_pointer_count + frame_pointer_count > 0;
  int skip_first = bracket_count == 0 && !paren_made_pointer;
  if (skip_first) {
    if (has_pointer) {
      tk_expect('[');
      int dim = 0;
      int has_size = curtok()->kind != TK_RBRACKET;
      if (has_size) dim = psx_parse_array_size_constexpr();
      tk_expect(']');
      if (ctx->decl_state &&
          !psx_declarator_shape_append_array_ex(
              &ctx->decl_state->declarator_shape, dim, !has_size))
        return 0;
    } else {
      int is_incomplete =
          curtok()->next && curtok()->next->kind == TK_RBRACKET;
      skip_balanced_group(TK_LBRACKET, TK_RBRACKET);
      if (ctx->decl_state) {
        if (is_incomplete)
          psx_declarator_shape_append_array_ex(
              &ctx->decl_state->declarator_shape, 0, 1);
        else
          psx_declarator_shape_append_vla_array(
              &ctx->decl_state->declarator_shape);
      }
    }
    return 1;
  }

  tk_expect('[');
  int dim = 0;
  token_ident_t *dim_ident = NULL;
  int has_size = curtok()->kind != TK_RBRACKET;
  if (has_size) {
    if (curtok()->kind == TK_IDENT && curtok()->next &&
        curtok()->next->kind == TK_RBRACKET) {
      dim_ident = (token_ident_t *)curtok();
      set_curtok(curtok()->next);
    } else {
      dim = psx_parse_array_size_constexpr();
    }
  }
  tk_expect(']');
  if (ctx->decl_state) {
    if (!has_size)
      psx_declarator_shape_append_array_ex(
          &ctx->decl_state->declarator_shape, 0, 1);
    else if (dim_ident)
      psx_declarator_shape_append_vla_array(
          &ctx->decl_state->declarator_shape);
    else
      psx_declarator_shape_append_array(
          &ctx->decl_state->declarator_shape, dim);
  }
  int dim_pos = paren_made_pointer ? bracket_count : bracket_count - 1;
  if (ctx->decl_state && dim_pos >= 0 && dim_pos < 7) {
    ctx->decl_state->inner_dim_consts[dim_pos] = dim;
    ctx->decl_state->inner_dim_idents[dim_pos] = dim_ident;
    if (dim_pos + 1 > ctx->decl_state->inner_dim_count)
      ctx->decl_state->inner_dim_count = dim_pos + 1;
  }
  return 1;
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

static const psx_type_t *parameter_decl_leaf_type(
    const psx_type_t *type) {
  while (type && (type->kind == PSX_TYPE_POINTER ||
                  type->kind == PSX_TYPE_ARRAY)) {
    type = type->base;
  }
  return type;
}

static lvar_t *register_param_lvar(
    token_ident_t *param,
    const param_declarator_state_t *decl_state,
    int param_is_ptr, int param_is_array_declarator,
    int param_has_func_suffix,
    token_ident_t *param_inner_first_dim_ident,
    const psx_type_t *canonical_type, int *out_type_attached) {
  if (out_type_attached) *out_type_attached = 0;
  const psx_type_t *leaf = parameter_decl_leaf_type(canonical_type);
  int leaf_is_aggregate = leaf && ps_type_is_tag_aggregate(leaf);
  int element_size = ps_type_sizeof(leaf);
  if (element_size <= 0) element_size = 8;
  if (param_is_array_declarator && !leaf_is_aggregate && !param_is_ptr) {
    psx_parameter_vla_lowering_result_t result =
        lower_parameter_vla_from_state(
            param, element_size, decl_state,
            param_inner_first_dim_ident, canonical_type);
    if (out_type_attached) *out_type_attached = result.type_attached;
    return result.var;
  }
  if (param_is_ptr && param_is_array_declarator &&
      !leaf_is_aggregate && !param_has_func_suffix &&
      (param_inner_first_dim_ident != NULL ||
       parameter_state_has_runtime_inner_dimension(decl_state))) {
    psx_parameter_vla_lowering_result_t result =
        lower_parameter_vla_from_state(
            param, element_size, decl_state,
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
    if (ds->type_spec.is_long_double)
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
  param_declarator_state_t param_decl_state = {0};
  token_ident_t *param =
      parse_param_declarator_name(&param_decl_state);
  int param_is_array_declarator = psx_declarator_shape_count_ops(
      &param_decl_state.declarator_shape, PSX_DECL_OP_ARRAY) > 0;
  int param_has_func_suffix = psx_declarator_shape_count_ops(
      &param_decl_state.declarator_shape, PSX_DECL_OP_FUNCTION) > 0;
  int param_is_ptr =
      psx_declarator_shape_count_ops(
          &param_decl_state.declarator_shape, PSX_DECL_OP_POINTER) > 0 ||
      param_has_func_suffix;
  token_ident_t *param_inner_first_dim_ident =
      param_decl_state.inner_dim_count > 0
          ? param_decl_state.inner_dim_idents[0]
          : NULL;
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
  lvar_t *var = register_param_lvar(param, &param_decl_state,
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
    psx_ctx_get_type_info(param_type_kind, NULL, &out->elem_size);
    if (param_type_kind == TK_FLOAT) out->fp_kind = TK_FLOAT_KIND_FLOAT;
    else if (param_type_kind == TK_DOUBLE) out->fp_kind = TK_FLOAT_KIND_DOUBLE;
    /* `double _Complex z` 等: psx_consume_type_kind_ex が _Complex も消費する。
     * HFA 受け取りのため記録する。 */
    out->is_complex = type_spec.is_complex;
  } else if (psx_ctx_is_typedef_name_token(curtok())) {
    out->saw_typedef_name = 1;
    token_ident_t *id = (token_ident_t *)curtok();
    psx_typedef_info_t _ti;
    if (psx_ctx_find_typedef_name(id->str, id->len, &_ti)) {
      out->base_decl_type = psx_ctx_typedef_decl_type(&_ti);
    }
    set_curtok(curtok()->next);
  }
}

static void parse_func_decl_spec(func_ret_parse_state_t *ret_state) {
  token_kind_t ret_kind = TK_EOF;
  tk_float_kind_t ret_fp_kind = TK_FLOAT_KIND_NONE;
  token_ident_t *ret_tag = NULL;
  psx_type_spec_result_reset(&ret_state->type_spec);
  psx_type_name_init(&ret_state->type_name);
  psx_declarator_shape_init(&ret_state->declarator_shape);
  psx_declarator_shape_init(&ret_state->base_pointer_shape);
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
    resolve_func_ret_tag_spec(&ret_kind, &ret_tag);
    parse_pointer_suffix_flags(ret_state);
  } else {
    ret_kind = psx_consume_type_kind_ex(&ret_state->type_spec);
    if (ret_kind == TK_EOF && psx_ctx_is_typedef_name_token(curtok())) {
      resolve_func_ret_typedef(ret_state);
    } else {
      ret_state->saw_implicit_int = ret_kind == TK_EOF;
    }
    ret_fp_kind = fp_kind_for_type_kind_toplevel(ret_kind);
    parse_pointer_suffix_flags(ret_state);
  }

  if (!ret_state->type_name.canonical_base) {
    ret_state->type_name.base_kind =
        ret_kind == TK_EOF ? TK_INT : ret_kind;
    ret_state->type_name.fp_kind = ret_fp_kind;
    ret_state->type_name.is_unsigned = ret_state->type_spec.is_unsigned;
    ret_state->type_name.is_complex = ret_state->type_spec.is_complex;
    ret_state->type_name.is_long_long = ret_state->type_spec.is_long_long;
    ret_state->type_name.is_plain_char = ret_state->type_spec.is_plain_char;
    ret_state->type_name.is_long_double = ret_state->type_spec.is_long_double;
    ret_state->type_name.pointee_const =
        ret_state->type_spec.is_const_qualified;
    ret_state->type_name.pointee_volatile =
        ret_state->type_spec.is_volatile_qualified;
    if (ret_tag) {
      ret_state->type_name.tag_kind = ret_kind;
      ret_state->type_name.tag_name = ret_tag->str;
      ret_state->type_name.tag_len = ret_tag->len;
      int scope_depth = ps_ctx_get_tag_scope_depth(
          ret_kind, ret_tag->str, ret_tag->len);
      ret_state->type_name.tag_scope_depth_p1 =
          scope_depth >= 0 ? scope_depth + 1 : 0;
      if (psx_ctx_has_tag_type(ret_kind, ret_tag->str,
                               ret_tag->len)) {
        ret_state->type_name.base_size = psx_ctx_get_tag_size(
            ret_kind, ret_tag->str, ret_tag->len);
      }
    }
  }
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

static void parse_pointer_suffix_flags(func_ret_parse_state_t *ret_state) {
  while (curtok()->kind == TK_MUL) {
    set_curtok(curtok()->next);
    int is_const = 0;
    int is_volatile = 0;
    while (curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE) {
      if (curtok()->kind == TK_CONST) is_const = 1;
      if (curtok()->kind == TK_VOLATILE) is_volatile = 1;
      set_curtok(curtok()->next);
    }
    if (ret_state && !psx_declarator_shape_append_pointer(
                         &ret_state->base_pointer_shape,
                         is_const, is_volatile)) {
      psx_diag_ctx(curtok(), "funcdef", "%s",
                   "function return declarator is too complex");
    }
  }
}

static void resolve_func_ret_typedef(func_ret_parse_state_t *ret_state) {
  token_ident_t *td_id = (token_ident_t *)curtok();
  psx_typedef_info_t _ti = {0};
  if (psx_ctx_find_typedef_name(td_id->str, td_id->len, &_ti)) {
    ret_state->type_name.canonical_base = psx_ctx_typedef_decl_type(&_ti);
  }
  set_curtok(curtok()->next);
}

typedef struct {
  func_ret_parse_state_t *ret_state;
  parsed_function_params_t params;
  int saw_function_suffix;
} func_declarator_parse_ctx_t;

static void diagnose_func_declarator_missing_name(void *context, token_t *tok) {
  (void)context;
  psx_diag_ctx(tok, "funcdef", "%s",
               diag_message_for(DIAG_ERR_PARSER_FUNCTION_DEF_EXPECTED));
}

static void diagnose_func_declarator_too_complex(void *context, token_t *tok) {
  (void)context;
  psx_diag_ctx(tok, "funcdef", "%s",
               "function return declarator is too complex");
}

static int append_func_return_pointer(void *context, int is_const,
                                      int is_volatile, int nesting_depth) {
  (void)nesting_depth;
  func_declarator_parse_ctx_t *ctx = context;
  return ctx && ctx->ret_state &&
         psx_declarator_shape_append_pointer(
             &ctx->ret_state->declarator_shape, is_const, is_volatile);
}

static int consume_func_declarator_suffix(void *context, int nesting_depth,
                                          int direct_was_parenthesized,
                                          int direct_pointer_count,
                                          int frame_pointer_count) {
  (void)nesting_depth;
  (void)direct_was_parenthesized;
  (void)direct_pointer_count;
  (void)frame_pointer_count;
  func_declarator_parse_ctx_t *ctx = context;
  if (curtok()->kind == TK_LPAREN) {
    if (!ctx->saw_function_suffix) {
      parse_function_param_list(&ctx->params, 1);
      ctx->saw_function_suffix = 1;
    } else {
      psx_funcptr_signature_t returned_func_sig = {0};
      psx_skip_func_param_list(&returned_func_sig);
      psx_decl_funcptr_sig_t op_sig = {0};
      op_sig.function.callable.signature = returned_func_sig;
      if (!psx_declarator_shape_append_function(
              &ctx->ret_state->declarator_shape, op_sig))
        diagnose_func_declarator_too_complex(context, curtok());
    }
    return 1;
  }
  if (curtok()->kind == TK_LBRACKET) {
    tk_expect('[');
    int has_size = 0;
    int n = psx_parse_array_size_optional_constexpr(&has_size);
    if (!psx_declarator_shape_append_array_ex(
            &ctx->ret_state->declarator_shape,
            has_size ? n : 0, !has_size))
      diagnose_func_declarator_too_complex(context, curtok());
    return 1;
  }
  return 0;
}

static token_ident_t *parse_func_declarator(func_ret_parse_state_t *ret_state,
                                            int *out_is_variadic, int *out_has_unnamed_param,
                                            node_t ***out_args, int *out_nargs) {
  func_declarator_parse_ctx_t ctx = {.ret_state = ret_state};
  parsed_function_params_init(&ctx.params);
  token_ident_t *name = psx_parse_declarator_syntax(
      &(psx_declarator_syntax_t){
          .context = &ctx,
          .require_identifier = 1,
          .consume_suffix = consume_func_declarator_suffix,
          .append_pointer = append_func_return_pointer,
          .diagnose_missing_identifier = diagnose_func_declarator_missing_name,
          .diagnose_too_complex = diagnose_func_declarator_too_complex,
      }, NULL);
  if (!ctx.saw_function_suffix)
    psx_diag_ctx(curtok(), "funcdef", "%s",
                 diag_message_for(DIAG_ERR_PARSER_FUNCTION_DEF_EXPECTED));
  psx_skip_gnu_attributes();
  *out_is_variadic = ctx.params.is_variadic;
  *out_has_unnamed_param = ctx.params.has_unnamed_param;
  *out_args = ctx.params.args;
  *out_nargs = ctx.params.nargs;
  return name;
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
  func_ret_parse_state_t ret_state = {0};
  parse_func_decl_spec(&ret_state);
  /* static 関数 (内部リンケージ) かを捕捉する。parse_func_decl_spec が返した
   * return type-spec result から読む。 */
  int fn_is_static = ret_state.type_spec.is_static ? 1 : 0;
  int saw_implicit_int_return = ret_state.saw_implicit_int;
  // 関数ごとにローカル変数テーブルをリセット
  psx_decl_reset_locals();
  psx_ctx_reset_function_scope();

  int is_variadic = 0;
  int has_unnamed_param = 0;
  node_t **args = NULL;
  int nargs = 0;
  token_ident_t *tok = parse_func_declarator(&ret_state, &is_variadic,
                                             &has_unnamed_param, &args, &nargs);
  psx_declarator_shape_init(&ret_state.type_name.declarator_shape);
  if (!psx_declarator_shape_append_shape(
          &ret_state.type_name.declarator_shape,
          &ret_state.declarator_shape) ||
      !psx_declarator_shape_append_shape(
          &ret_state.type_name.declarator_shape,
          &ret_state.base_pointer_shape)) {
    psx_diag_ctx(curtok(), "funcdef", "%s",
                 "function return declarator is too complex");
  }
  psx_type_t *return_type = psx_type_name_build(&ret_state.type_name);
  if (!return_type) {
    psx_diag_ctx(curtok(), "funcdef", "%s",
                 "canonical function return type construction failed");
  }
  int ret_is_ptr = return_type->kind == PSX_TYPE_POINTER;
  tk_float_kind_t ret_fp_kind =
      return_type->kind == PSX_TYPE_FLOAT ||
              return_type->kind == PSX_TYPE_COMPLEX
          ? return_type->fp_kind
          : TK_FLOAT_KIND_NONE;
  int ret_is_complex = return_type->kind == PSX_TYPE_COMPLEX;
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
  psx_function_signature_t sig = {0};
  sig.name = tok;
  sig.diag_context = "funcdef";
  sig.is_variadic = is_variadic;
  sig.nargs = nargs;
  sig.args = args;
  sig.func_node = node;
  sig.return_type = return_type;
  sig.is_definition = curtok()->kind != TK_SEMI;
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
