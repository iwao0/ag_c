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
  int base_ptr_levels;
  int base_array_dims[8];
  int base_array_dim_count;
  const psx_type_t *base_decl_type;
} stmt_decl_type_state_t;
typedef struct {
  int ptr_in_paren;
  int has_func_suffix;
  int ptr_levels;
  int funcptr_object_pointer_levels;
  psx_funcptr_signature_t func_suffix_sig;
} stmt_typedef_declarator_state_t;
static int parse_decl_type_spec(int *elem_size, tk_float_kind_t *fp_kind,
                                token_kind_t *tag_kind, char **tag_name, int *tag_len,
                                int *is_pointer_base, token_kind_t *base_kind,
                                stmt_decl_type_state_t *type_state);
static token_ident_t *parse_typedef_name_decl(stmt_typedef_declarator_state_t *decl_state,
                                              int *is_ptr);
static token_ident_t *parse_typedef_name_decl_recursive(stmt_typedef_declarator_state_t *decl_state,
                                                        int *is_ptr);
typedef struct {
  int arr_total;
  int is_array;
  int has_incomplete_array;
  // pointer-to-array typedef `typedef int (*PA)[3][4]` のポインティ各次元 (dims[0] が最外)。
  int dims[8];
  int dim_count;
  int first_dim;
} stmt_array_suffix_t;
static stmt_array_suffix_t parse_stmt_array_suffixes(int base_mul);
static node_t *stmt_internal(void);
static node_t *parse_stmt_label(void);
static node_t *block_item(void);
static int is_decl_like_start_stmt(void);
static node_t *parse_decl_like_stmt(void);

static token_ident_t *parse_typedef_name_decl_recursive(stmt_typedef_declarator_state_t *decl_state,
                                                        int *is_ptr) {
  int stars = psx_consume_pointer_prefix_counted(is_ptr);
  if (decl_state) decl_state->ptr_levels += stars;
  int frame_pointer_prefix_levels = decl_state ? decl_state->ptr_levels : 0;
  token_ident_t *name = NULL;
  if (tk_consume('(')) {
    int ptr_before = *is_ptr;
    name = parse_typedef_name_decl_recursive(decl_state, is_ptr);
    /* 括弧内で初めて `*` が立った (`(*PA)`): pointer-to-array / 関数ポインタの指標。 */
    if (decl_state && *is_ptr && !ptr_before) decl_state->ptr_in_paren = 1;
    tk_expect(')');
  } else {
    name = tk_consume_ident();
  }
  if (decl_state) {
    int had_suffix_before = decl_state->has_func_suffix;
    psx_skip_func_suffix_groups_ex(&decl_state->has_func_suffix,
                                   &decl_state->func_suffix_sig);
    if (!had_suffix_before && decl_state->has_func_suffix) {
      int object_levels = decl_state->ptr_levels - frame_pointer_prefix_levels;
      if (object_levels > 0) decl_state->funcptr_object_pointer_levels = object_levels;
    }
  }
  else {
    int discard_func_suffix = 0;
    psx_funcptr_signature_t discard_sig = {0};
    psx_skip_func_suffix_groups_ex(&discard_func_suffix, &discard_sig);
  }
  return name;
}

static int stmt_funcptr_direct_ret_is_data_pointer(const stmt_typedef_declarator_state_t *decl_state,
                                                   int base_is_pointer) {
  if (!decl_state || !decl_state->has_func_suffix) return 0;
  int object_pointer_levels = decl_state->funcptr_object_pointer_levels;
  int ret_pointer_levels = decl_state->ptr_levels - object_pointer_levels;
  return (ret_pointer_levels > 0 || base_is_pointer) ? 1 : 0;
}

static token_ident_t *parse_typedef_name_decl(stmt_typedef_declarator_state_t *decl_state,
                                              int *is_ptr) {
  int initial_ptr_levels = decl_state ? decl_state->ptr_levels : 0;
  if (decl_state) {
    memset(decl_state, 0, sizeof(*decl_state));
    decl_state->ptr_levels = initial_ptr_levels;
  }
  token_ident_t *name = parse_typedef_name_decl_recursive(decl_state, is_ptr);
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
      psx_ctx_define_tag_type_with_layout(*tag_kind, *tag_name, *tag_len, member_count, tag_size, tag_align);
    } else if (!psx_ctx_has_tag_type(*tag_kind, *tag_name, *tag_len)) {
      psx_ctx_define_tag_type(*tag_kind, *tag_name, *tag_len);
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
    /* 基底がポインタ typedef なら段数を捕捉 (合成 typedef の段数加算用)。 */
    if (type_state) type_state->base_ptr_levels = psx_ctx_get_typedef_pointer_levels(id->str, id->len);
    /* 基底が配列 typedef なら dims を捕捉 (typedef chain `typedef Row Matrix[2]` の合成用)。
     * pointer typedef (is_pointer=1) は対象外: そちらの dims は pointer-to-array typedef
     * のポインティ extent を表しており、ここでの array typedef chain とは別経路。 */
    if (!_ti.is_pointer && _ti.is_array && _ti.array_dim_count > 0) {
      if (type_state) type_state->base_array_dim_count = _ti.array_dim_count;
      for (int i = 0; i < _ti.array_dim_count && i < 8; i++) {
        if (type_state) type_state->base_array_dims[i] = _ti.array_dims[i];
      }
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

  int base_ptr_levels = type_state.base_ptr_levels;
  for (;;) {
    int is_ptr = is_pointer_base;
    int decl_stars = psx_consume_pointer_prefix_counted(&is_ptr);
    stmt_typedef_declarator_state_t decl_state = {0};
    decl_state.ptr_levels = decl_stars;
    token_ident_t *name = parse_typedef_name_decl(&decl_state, &is_ptr);
    /* pointer-element 配列 typedef (`typedef BinOp OpArr3[3]` / `typedef ScorePtr SPA[3]`):
     * base が pointer typedef かつ declarator に prefix `*` 追加なし (decl_stars==0) かつ
     * 括弧内 `*` も無し (!decl_state.ptr_in_paren) で配列 suffix があるケース。
     * sizeof_size = 8*N、is_array=1 として登録し、宣言側で配列扱いにする。
     * pointer-to-array typedef (`typedef int (*PA)[3]`) は base=int だが declarator 内の
     * 括弧内で `*` を取り is_ptr=1 / decl_state.ptr_in_paren=1 になる。decl_stars=0
     * (prefix `*` 無し) と区別できないので decl_state.ptr_in_paren で除外する必要がある。 */
    int base_is_ptr_only = (is_ptr && decl_stars == 0 && !decl_state.ptr_in_paren);
    int typedef_sizeof = is_ptr ? 8 : elem_size;
    stmt_array_suffix_t arr = parse_stmt_array_suffixes(0);
    int declarator_dims[8] = {0};
    int declarator_dim_count = arr.is_array ? arr.dim_count : 0;
    for (int i = 0; i < declarator_dim_count && i < 8; i++)
      declarator_dims[i] = arr.dims[i];
    if (arr.is_array && declarator_dim_count <= 0) {
      declarator_dims[0] = arr.arr_total;
      declarator_dim_count = 1;
    }
    if (!is_ptr && arr.has_incomplete_array) typedef_sizeof = 0;
    else if (!is_ptr && arr.is_array && arr.arr_total > 0) typedef_sizeof *= arr.arr_total;
    else if (base_is_ptr_only && arr.is_array && arr.arr_total > 0) typedef_sizeof = 8 * arr.arr_total;
    token_kind_t stored_base_kind = (td_is_unsigned && base_kind == TK_INT) ? TK_UNSIGNED : base_kind;
    /* pointer-to-array typedef `typedef int (*PA)[3]` (is_ptr=1 かつ `*` が括弧内) のみ、
     * 括弧の後ろの `[3]` をポインティ配列の extent として dims に記録する (is_array=0 の
     * まま)。これがないと `PA p; p+1 / p[i]` が要素 1 個 (4B) しか進まず直書き `int(*p)[3]`
     * と食い違う。その他 (スカラ / 配列 typedef) は従来の psx_ctx_define_typedef_name
     * 相当 (is_array=0, dims なし) を維持して退行を避ける。 */
    int is_pta = (is_ptr && decl_state.ptr_in_paren && arr.is_array && arr.dim_count > 0);
    int is_base_ptr_arr = (base_is_ptr_only && arr.is_array && arr.arr_total > 0);
    /* 通常の配列 typedef (`typedef int Row[3]`): is_pointer でなく array suffix がある場合
     * is_array=1 + dims を立てる (トップレベル版 parser.c と対称)。これがないと関数内
     * typedef で `Row r = {1,2,3}` が「スカラに brace 初期化」E3064 になっていた。 */
    int is_plain_array = (!is_ptr && arr.is_array && arr.dim_count > 0);
    int td_first_dim = is_pta ? arr.first_dim
                      : (is_base_ptr_arr ? arr.first_dim
                      : (is_plain_array ? arr.first_dim : 0));
    int td_dim_count = is_pta ? arr.dim_count
                      : (is_base_ptr_arr ? arr.dim_count
                      : (is_plain_array ? arr.dim_count : 0));
    const int *td_dims = (is_pta || is_base_ptr_arr || is_plain_array) ? arr.dims : NULL;
    /* 多次元 typedef chain: 基底 typedef が自身配列の場合 (`typedef int Row[3]; typedef Row Matrix[2]`)、
     * declarator の dims と base typedef の dims を [declarator..., base...] の順で結合し、
     * 新しい typedef の dims/sizeof を更新する。トップレベル版 (parser.c) と同じロジック。 */
    int merged_dims[8] = {0};
    int is_array_chain = (!is_ptr && !decl_state.ptr_in_paren &&
                          arr.is_array && type_state.base_array_dim_count > 0) ? 1 : 0;
    if (is_array_chain) {
      int n = 0;
      for (int i = 0; i < arr.dim_count && n < 8; i++) merged_dims[n++] = arr.dims[i];
      for (int i = 0; i < type_state.base_array_dim_count && n < 8; i++) {
        merged_dims[n++] = type_state.base_array_dims[i];
      }
      td_dims = merged_dims;
      td_dim_count = n;
      td_first_dim = (n > 0) ? merged_dims[0] : td_first_dim;
      int prod = 1;
      for (int i = 0; i < n; i++) prod *= merged_dims[i];
      typedef_sizeof = elem_size * prod;
    }
    psx_typedef_info_t _ti = {0};
    _ti.base_kind = stored_base_kind;
    _ti.elem_size = elem_size;
    _ti.fp_kind = fp_kind;
    _ti.tag_kind = tag_kind;
    _ti.tag_name = tag_name;
    _ti.tag_len = tag_len;
    _ti.is_pointer = is_ptr;
    _ti.sizeof_size = typedef_sizeof;
    _ti.pointee_const_qualified = td_pointee_const;
    _ti.pointee_volatile_qualified = td_pointee_volatile;
    _ti.is_unsigned = td_is_unsigned;
    _ti.is_array = (is_base_ptr_arr || is_array_chain || is_plain_array) ? 1 : 0;
    _ti.array_first_dim = td_first_dim;
    _ti.array_dim_count = td_dim_count;
    if (td_dims) for (int i = 0; i < td_dim_count && i < 8; i++) _ti.array_dims[i] = td_dims[i];
    psx_type_t *canonical_type = stmt_typedef_base_type(
        base_kind, elem_size, fp_kind, tag_kind, tag_name, tag_len,
        td_is_unsigned, type_state.type_spec.is_complex,
        type_state.base_decl_type);
    if (canonical_type) {
      if (td_pointee_const) canonical_type->is_const_qualified = 1;
      if (td_pointee_volatile) canonical_type->is_volatile_qualified = 1;
      psx_declarator_shape_t shape;
      psx_declarator_shape_init(&shape);
      if (decl_state.ptr_in_paren)
        psx_declarator_shape_append_pointer_levels(
            &shape, decl_state.ptr_levels, 0, 0);
      psx_declarator_shape_append_array_dims(
          &shape, declarator_dims, declarator_dim_count);
      if (!decl_state.ptr_in_paren)
        psx_declarator_shape_append_pointer_levels(
            &shape, decl_state.ptr_levels, 0, 0);
      canonical_type = psx_type_apply_declarator_shape(
          canonical_type, &shape);
      psx_ctx_typedef_set_decl_type(&_ti, canonical_type);
    }
    if (decl_state.has_func_suffix && (is_ptr || decl_state.ptr_in_paren)) {
      _ti.is_funcptr = 1;
      _ti.fp_kind = TK_FLOAT_KIND_NONE;
      psx_decl_funcptr_sig_t sig = psx_decl_make_funcptr_sig_from_kind(
          &decl_state.func_suffix_sig, base_kind, fp_kind,
          stmt_funcptr_direct_ret_is_data_pointer(&decl_state, is_pointer_base), 0,
          type_state.type_spec.is_complex, (psx_ret_pointee_array_t){0});
      int object_pointer_levels = decl_state.funcptr_object_pointer_levels > 0
                                      ? decl_state.funcptr_object_pointer_levels
                                      : 1;
      psx_declarator_shape_t shape;
      psx_declarator_shape_init(&shape);
      if (_ti.is_array)
        psx_declarator_shape_append_array_dims(
            &shape, declarator_dims, declarator_dim_count);
      psx_declarator_shape_append_pointer_levels(
          &shape, object_pointer_levels, 0, 0);
      psx_declarator_shape_append_function(&shape, sig);
      psx_type_t *funcptr_type = psx_type_apply_declarator_shape(
          psx_type_new_funcptr_return_type(sig), &shape);
      psx_ctx_typedef_set_decl_type(&_ti, funcptr_type);
      psx_ctx_typedef_set_funcptr_sig(&_ti, sig);
    }
    if (!psx_ctx_define_typedef_name(name->str, name->len, &_ti)) {
      psx_diag_duplicate_with_name(curtok(), "typedef", name->str, name->len);
    }
    /* 多段ポインタ typedef (`typedef int **PP`) の段数を記録する。関数ポインタ
     * typedef では戻り値ポインタの `*` を除き、関数ポインタオブジェクトを指す段数だけを
     * 保存する (`int *(*G)(void)` は 1、`int (**PP)(int)` は 2)。 */
    int td_ptr_levels = decl_state.has_func_suffix
                            ? decl_state.funcptr_object_pointer_levels
                            : base_ptr_levels + decl_state.ptr_levels;
    if (is_ptr && td_ptr_levels >= 2) {
      psx_ctx_set_typedef_pointer_levels(name->str, name->len, td_ptr_levels);
    }
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
      psx_ctx_define_tag_type_with_layout(tag_kind, tag_name, tag_len, member_count, tag_size, tag_align);
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
                                                      &tag_type_spec, NULL, 0,
                                                      0, 0, 0, 0,
                                                      (psx_decl_funcptr_sig_t){0},
                                                      NULL,
                                                      NULL,
                                                      0, 0);
    }
    if (tk_consume(';')) {
      psx_ctx_define_tag_type(tag_kind, tag_name, tag_len);
      return psx_node_new_num(0);
    }
    if (!psx_ctx_has_tag_type(tag_kind, tag_name, tag_len)) {
      psx_ctx_define_tag_type(tag_kind, tag_name, tag_len);
    }
    while (curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE) {
      if (curtok()->kind == TK_CONST) tag_path_saw_const = 1;
      if (curtok()->kind == TK_VOLATILE) tag_path_saw_volatile = 1;
      set_curtok(curtok()->next);
    }
    int tag_size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
    int tag_members = psx_ctx_get_tag_member_count(tag_kind, tag_name, tag_len);
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
                                                    &tag_type_spec, NULL, 0,
                                                    0, 0, 0, 0,
                                                    (psx_decl_funcptr_sig_t){0},
                                                    NULL,
                                                    NULL,
                                                    0, 0);
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
