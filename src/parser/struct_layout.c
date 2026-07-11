#include "struct_layout.h"
#include "alignas_value.h"
#include "anon_tag.h"
#include "array_suffixes.h"
#include "core.h"
#include "decl.h"
#include "diag.h"
#include "enum_const.h"
#include "expr.h"
#include "config_runtime.h"
#include "ret_pointee_array.h"
#include "semantic_ctx.h"
#include "type.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include "../pragma_pack.h"

static inline token_t *curtok(void) { return tk_get_current_token(); }
static inline void set_curtok(token_t *tok) { tk_set_current_token(tok); }

#define ALIGN_UP(v, a) (((v) + ((a) - 1)) / (a) * (a))

static psx_type_t *member_scalar_type(token_kind_t base_kind, tk_float_kind_t fp_kind,
                                      token_kind_t tag_kind, char *tag_name, int tag_len,
                                      int elem_size, int is_unsigned, int is_bool,
                                      int is_complex, int is_atomic) {
  psx_type_t *type = NULL;
  if (psx_ctx_is_tag_aggregate_kind(tag_kind)) {
    int tag_size = psx_ctx_get_tag_size(tag_kind, tag_name, tag_len);
    type = psx_type_new_tag(tag_kind, tag_name, tag_len, 0,
                            tag_size > 0 ? tag_size : elem_size);
    type->is_atomic = is_atomic ? 1 : 0;
    return type;
  }
  if (is_complex) {
    type = psx_type_new(PSX_TYPE_COMPLEX);
    type->fp_kind = fp_kind;
    type->size = elem_size > 0 ? elem_size : 16;
    type->align = type->size >= 8 ? 8 : 4;
    type->is_atomic = is_atomic ? 1 : 0;
    return type;
  }
  if (fp_kind != TK_FLOAT_KIND_NONE) {
    type = psx_type_new_float(fp_kind, elem_size > 0 ? elem_size : 8);
    type->is_atomic = is_atomic ? 1 : 0;
    return type;
  }
  if (base_kind == TK_VOID) {
    type = psx_type_new(PSX_TYPE_VOID);
    type->scalar_kind = TK_VOID;
    return type;
  }
  token_kind_t scalar_kind = is_bool ? TK_BOOL : (base_kind != TK_EOF ? base_kind : TK_INT);
  type = psx_type_new_integer(scalar_kind, elem_size > 0 ? elem_size : 4, is_unsigned);
  type->is_atomic = is_atomic ? 1 : 0;
  return type;
}

static psx_type_t *member_decl_type_from_layout(token_kind_t base_kind,
                                                tk_float_kind_t fp_kind,
                                                token_kind_t tag_kind,
                                                char *tag_name, int tag_len,
                                                int elem_size, int is_unsigned,
                                                int is_bool, int is_complex,
                                                int is_atomic,
                                                int is_pointer, int pointer_levels,
                                                int array_len, int total_size,
                                                int elem_storage_size,
                                                psx_decl_funcptr_sig_t funcptr_sig) {
  psx_type_t *type = member_scalar_type(base_kind, fp_kind, tag_kind, tag_name,
                                        tag_len, elem_size, is_unsigned,
                                        is_bool, is_complex, is_atomic);
  if (is_pointer) {
    int levels = pointer_levels > 0 ? pointer_levels : 1;
    int top_deref_size = levels >= 2 ? 8 : elem_size;
    type = psx_type_wrap_pointer_levels(type, levels, top_deref_size,
                                        elem_size, 0, 0);
    type = psx_type_attach_funcptr_signature(type, funcptr_sig);
  }
  if (array_len > 0) {
    int elem_size_for_array = elem_storage_size > 0 ? elem_storage_size : ps_type_sizeof(type);
    int array_size = total_size > 0 ? total_size : elem_size_for_array * array_len;
    psx_type_t *array_type = psx_type_new_array(type, array_len, array_size,
                                                elem_size_for_array, 0);
    return array_type;
  }
  return type;
}

static psx_type_t *member_decl_type_pointer_to_array_from_layout(
    token_kind_t base_kind, tk_float_kind_t fp_kind,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int elem_size, int is_unsigned, int is_bool, int is_complex,
    int is_atomic, int element_is_pointer,
    const int *dims, int dim_count, int fallback_count,
    int elem_storage_size) {
  psx_type_t *elem_type = member_scalar_type(base_kind, fp_kind, tag_kind,
                                             tag_name, tag_len, elem_size,
                                             is_unsigned, is_bool, is_complex,
                                             is_atomic);
  if (element_is_pointer) {
    int scalar_deref = elem_size > 0 ? elem_size : ps_type_sizeof(elem_type);
    if (scalar_deref <= 0) scalar_deref = 8;
    psx_type_t *ptr = psx_type_new_pointer(elem_type, scalar_deref);
    ptr->base_deref_size = scalar_deref;
    ptr->pointer_qual_levels = 1;
    elem_type = ptr;
  }

  int normalized_dims[8] = {0};
  int n = dim_count;
  if (n < 0) n = 0;
  if (n > 8) n = 8;
  for (int i = 0; i < n; i++) normalized_dims[i] = dims ? dims[i] : 0;
  if (n <= 0 && fallback_count > 0) {
    normalized_dims[0] = fallback_count;
    n = 1;
  }
  if (n <= 0) return psx_type_new_pointer(elem_type, ps_type_sizeof(elem_type));

  int leaf_storage = elem_storage_size > 0 ? elem_storage_size
                                           : ps_type_sizeof(elem_type);
  if (leaf_storage <= 0) leaf_storage = element_is_pointer ? 8 : elem_size;
  if (leaf_storage <= 0) leaf_storage = 1;

  psx_type_t *array = elem_type;
  int suffix_size = leaf_storage;
  for (int i = n - 1; i >= 0; i--) {
    int len = normalized_dims[i];
    if (len <= 0) len = 1;
    int array_size = suffix_size * len;
    psx_type_t *next = psx_type_new_array(array, len, array_size,
                                          suffix_size, 0);
    next->base_deref_size = elem_size > 0 ? elem_size : leaf_storage;
    array = next;
    suffix_size = array_size;
  }

  psx_type_t *ptr = psx_type_new_pointer(array, suffix_size);
  ptr->base_deref_size = elem_size > 0 ? elem_size : leaf_storage;
  ptr->pointer_qual_levels = 1;
  ptr->ptr_array_pointee_bytes = suffix_size;
  ptr->outer_stride = suffix_size;
  if (n >= 2 && normalized_dims[0] > 0)
    ptr->mid_stride = suffix_size / normalized_dims[0];
  return ptr;
}

static void tag_member_info_init_layout_cache(tag_member_info_t *mi,
                                              int type_size, int deref_size,
                                              int array_len,
                                              token_kind_t tag_kind,
                                              char *tag_name, int tag_len,
                                              tk_float_kind_t fp_kind,
                                              int is_bool, int is_unsigned,
                                              int is_tag_pointer,
                                              int pointer_qual_levels) {
  if (!mi) return;
  mi->type_size = type_size;
  mi->deref_size = deref_size;
  mi->array_len = array_len;
  mi->tag_kind = tag_kind;
  mi->tag_name = tag_name;
  mi->tag_len = tag_len;
  mi->fp_kind = fp_kind;
  mi->is_bool = is_bool ? 1 : 0;
  mi->is_unsigned = is_unsigned ? 1 : 0;
  mi->is_tag_pointer = is_tag_pointer ? 1 : 0;
  mi->pointer_qual_levels = pointer_qual_levels;
}

static void member_decl_type_apply_shape_cache(psx_type_t *type,
                                               const tag_member_info_t *mi) {
  if (!type || !mi) return;
  if (mi->outer_stride > 0 && type->outer_stride <= 0)
    type->outer_stride = mi->outer_stride;
  if (mi->mid_stride > 0 && type->mid_stride <= 0)
    type->mid_stride = mi->mid_stride;
  if (mi->ptr_array_pointee_bytes > 0) {
    type->ptr_array_pointee_bytes = mi->ptr_array_pointee_bytes;
    if (type->outer_stride <= 0)
      type->outer_stride = mi->ptr_array_pointee_bytes;
    if (type->kind == PSX_TYPE_POINTER) {
      type->deref_size = mi->ptr_array_pointee_bytes;
      if (type->base_deref_size <= 0 && mi->deref_size > 0)
        type->base_deref_size = mi->deref_size;
    }
    if (type->kind == PSX_TYPE_ARRAY && type->base)
      member_decl_type_apply_shape_cache(type->base, mi);
  }
}

static token_ident_t *parse_member_decl_name_recursive(
    member_decl_head_t *head, int nesting_depth) {
  int level_start = head->ptr_levels;
  int stars = psx_consume_pointer_prefix_counted(&head->is_ptr);
  head->ptr_levels += stars;
  int frame_pointer_prefix_levels = head->ptr_levels;
  token_ident_t *name = NULL;
  if (tk_consume('(')) {
    int levels_before = head->ptr_levels;
    name = parse_member_decl_name_recursive(head, nesting_depth + 1);
    if (head->ptr_levels > levels_before) head->ptr_in_paren = 1;
    tk_expect(')');
  } else {
    name = tk_consume_ident();
  }

  for (;;) {
    if (tk_consume('[')) {
      int has_size = 0;
      int dim = psx_parse_array_size_optional_constexpr(&has_size);
      psx_declarator_shape_append_array_ex(
          &head->declarator_shape, has_size ? dim : 0, !has_size);
      if (nesting_depth > 0 && dim > 0) head->paren_array_mul *= dim;
      continue;
    }
    if (curtok()->kind == TK_LPAREN) {
      psx_funcptr_signature_t suffix = {0};
      psx_skip_func_param_list(&suffix);
      if (!head->has_func_suffix) head->func_suffix_sig = suffix;
      head->has_func_suffix = 1;
      if (head->funcptr_object_pointer_levels == 0) {
        int object_levels = head->ptr_levels - frame_pointer_prefix_levels;
        if (object_levels > 0)
          head->funcptr_object_pointer_levels = object_levels;
      }
      psx_decl_funcptr_sig_t op_sig = {0};
      op_sig.function.callable.signature = suffix;
      psx_declarator_shape_append_function(
          &head->declarator_shape, op_sig);
      continue;
    }
    break;
  }

  psx_declarator_shape_append_pointer_levels(
      &head->declarator_shape, frame_pointer_prefix_levels - level_start,
      0, 0);
  return name;
}

member_decl_head_t psx_parse_member_decl_head(void) {
  member_decl_head_t out = {0};
  out.paren_array_mul = 1;
  psx_declarator_shape_init(&out.declarator_shape);
  out.member = parse_member_decl_name_recursive(&out, 0);
  return out;
}

static int member_funcptr_direct_ret_is_data_pointer(const member_decl_head_t *head,
                                                     int base_is_pointer) {
  if (!head || !head->has_func_suffix) return 0;
  int object_pointer_levels = head->funcptr_object_pointer_levels;
  if (object_pointer_levels <= 0 && head->ptr_in_paren) object_pointer_levels = 1;
  int ret_pointer_levels = head->ptr_levels - object_pointer_levels;
  return (ret_pointer_levels > 0 || base_is_pointer) ? 1 : 0;
}

static int member_array_layout_from_shape(
    const member_decl_head_t *head, int *out_is_flex_array,
    int *out_dim_count, int *out_first_dim, int *out_dims, int max_dims) {
  int total = 1;
  int dim_count = 0;
  int first_dim = 0;
  int is_flex = 0;
  int saw_pointer = 0;
  if (head) {
    for (int i = 0; i < head->declarator_shape.count; i++) {
      const psx_declarator_op_t *op = &head->declarator_shape.ops[i];
      if (op->kind == PSX_DECL_OP_POINTER) {
        saw_pointer = 1;
        continue;
      }
      if (op->kind != PSX_DECL_OP_ARRAY) continue;
      if (head->ptr_in_paren && !saw_pointer) continue;
      int dim = op->array_len;
      if (op->is_incomplete_array) {
        is_flex = 1;
        total = 0;
      } else if (total > 0) {
        total *= dim;
      }
      if (dim_count == 0) first_dim = dim;
      if (out_dims && dim_count < max_dims) out_dims[dim_count] = dim;
      dim_count++;
    }
  }
  if (out_is_flex_array) *out_is_flex_array = is_flex;
  if (out_dim_count) *out_dim_count = dim_count;
  if (out_first_dim) *out_first_dim = first_dim;
  return total;
}

int psx_parse_tag_definition_body(token_kind_t tag_kind, char *tag_name, int tag_len,
                                  int *out_size, int *out_align) {
  if (tag_kind == TK_ENUM) {
    if (out_size) *out_size = 4;
    if (out_align) *out_align = 4;
    return psx_parse_enum_members();
  }
  return psx_parse_struct_or_union_members_layout(tag_kind, tag_name, tag_len, out_size, out_align);
}

int psx_parse_struct_or_union_members_layout(token_kind_t tag_kind, char *tag_name, int tag_len,
                                             int *out_size, int *out_align) {
  int member_count = 0;
  int current_off = 0;
  int union_size = 0;
  int agg_align = 1;
  int bf_storage_offset = -1;
  int bf_storage_type_size = 0;
  int bf_bits_used = 0;
  while (!tk_consume('}')) {
    /* C11 6.7.2.1: struct/union のメンバ位置にも static_assert-declaration を書ける。
     * `_Static_assert(expr, "msg");` の expr を畳み込んで真なら受理、偽なら診断。
     * これがないと `struct S { _Static_assert(...); int x; };` が「メンバ型が必要」E3064。 */
    if (curtok()->kind == TK_STATIC_ASSERT) {
      set_curtok(curtok()->next);
      tk_expect('(');
      int const_ok = 1;
      long long cond_val = psx_decl_eval_const_int(psx_expr_assign(), &const_ok);
      tk_expect(',');
      if (curtok()->kind != TK_STRING) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_MSG_NOT_STRING));
      }
      set_curtok(curtok()->next);
      tk_expect(')');
      tk_expect(';');
      if (!const_ok) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_COND_NOT_CONST));
      }
      if (cond_val == 0) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_FAILED));
      }
      continue;
    }
    int elem_size = 8;
    int is_signed_type = 1;
    token_kind_t member_tag_kind = TK_EOF;
    char *member_tag_name = NULL;
    int member_tag_len = 0;
    int member_alignas = 0;
    while (curtok()->kind == TK_CONST || curtok()->kind == TK_VOLATILE || curtok()->kind == TK_ALIGNAS) {
      if (curtok()->kind == TK_ALIGNAS) {
        set_curtok(curtok()->next);
        int av = psx_parse_alignas_value();
        if (av > member_alignas) member_alignas = av;
      } else {
        set_curtok(curtok()->next);
      }
    }
    tk_float_kind_t member_fp_kind = TK_FLOAT_KIND_NONE;
    token_kind_t member_base_kind = TK_EOF;
    int member_is_bool = 0;
    int member_is_unsigned = 0;
    int member_is_complex = 0;
    int member_is_atomic = 0;
    int member_is_ptr_typedef = 0;
    const psx_type_t *member_typedef_decl_type = NULL;
    psx_decl_funcptr_sig_t member_typedef_funcptr_sig = {0};
    int member_typedef_array_dim_count = 0;
    int member_typedef_array_dims[8] = {0};
    int member_typedef_ptr_array_pointee_bytes = 0;
    int member_typedef_ptr_array_dim_count = 0;
    int member_typedef_ptr_array_dims[8] = {0};
    psx_type_spec_result_t member_type_spec;
    token_kind_t builtin_member_kind = psx_consume_type_kind_ex(&member_type_spec);
    if (builtin_member_kind != TK_EOF) {
      is_signed_type = member_type_spec.is_unsigned ? 0 : 1;
      member_base_kind = builtin_member_kind;
      psx_ctx_get_type_info(builtin_member_kind, NULL, &elem_size);
      if (builtin_member_kind == TK_FLOAT) member_fp_kind = TK_FLOAT_KIND_FLOAT;
      else if (builtin_member_kind == TK_DOUBLE) member_fp_kind = TK_FLOAT_KIND_DOUBLE;
      else if (builtin_member_kind == TK_BOOL) member_is_bool = 1;
      member_is_unsigned = member_type_spec.is_unsigned ? 1 : 0;
      member_is_complex = member_type_spec.is_complex ? 1 : 0;
      member_is_atomic = member_type_spec.is_atomic ? 1 : 0;
      if (member_is_complex) elem_size *= 2;
      /* `_Bool b : 1;` の bitfield 抽出は符号拡張せず 0/1 として扱う必要がある
       * (C11 6.7.2.1)。is_signed_type は line 108 で TK_BOOL を見て signed と
       * 判定してしまうため、ここで _Bool / unsigned を明示的に反映し直す。
       * 修正前: `_Bool b:1 = 1` を読み出すと 1bit signed extension で -1 に化け
       * `(int)s.b == -1` になっていた。 */
      if (member_is_bool || member_is_unsigned) is_signed_type = 0;
    } else if (psx_ctx_is_tag_keyword(curtok()->kind)) {
      member_tag_kind = curtok()->kind;
      /* enum 型ビットフィールド (`enum E e:2`) は非負列挙なら unsigned 扱い
       * (clang と同じ)。これがないと is_signed_type=1 のままで読み出しが符号拡張され、
       * 最上位ビットを使う値が負に化ける。 */
      if (member_tag_kind == TK_ENUM) is_signed_type = 0;
      set_curtok(curtok()->next);
      token_ident_t *nested_tag = tk_consume_ident();
      if (nested_tag) {
        member_tag_name = nested_tag->str;
        member_tag_len = nested_tag->len;
      } else if (curtok()->kind == TK_LBRACE) {
        psx_make_anonymous_tag_name(&member_tag_name, &member_tag_len);
      } else {
        psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_TAG_NAME));
      }
      if (tk_consume('{')) {
        int nested_n = 0;
        int nested_sz = 0;
        int nested_align = 0;
        nested_n = psx_parse_tag_definition_body(member_tag_kind, member_tag_name, member_tag_len,
                                                 &nested_sz, &nested_align);
        psx_ctx_define_tag_type_with_layout(member_tag_kind, member_tag_name, member_tag_len,
                                            nested_n, nested_sz, nested_align);
      } else if (!psx_ctx_has_tag_type(member_tag_kind, member_tag_name, member_tag_len)) {
        if (curtok()->kind != TK_MUL) {
          psx_diag_undefined_with_name(curtok(), diag_text_for(DIAG_TEXT_TAG_TYPE_SUFFIX), member_tag_name, member_tag_len);
        }
      }
      if (psx_ctx_has_tag_type(member_tag_kind, member_tag_name, member_tag_len)) {
        elem_size = psx_ctx_get_tag_size(member_tag_kind, member_tag_name, member_tag_len);
      }
      if (elem_size <= 0 && curtok()->kind != TK_MUL) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_INCOMPLETE_MEMBER_FORBIDDEN));
      }
    } else if (psx_ctx_is_typedef_name_token(curtok())) {
      /* `typedef struct Node Node; struct Node { ... Node *next; };` のように
       * typedef 名が struct メンバ型として現れるケース。typedef を解決して
       * 基底型 / tag 情報を取り出し、後続の `*` や宣言子と一緒に処理する。 */
      token_ident_t *td = (token_ident_t *)curtok();
      int td_elem = 0;
      token_kind_t td_tag = TK_EOF;
      char *td_tn = NULL;
      int td_tl = 0;
      int td_isu = 0;
      psx_typedef_info_t _ti;
      if (psx_ctx_find_typedef_name(td->str, td->len, &_ti)) {
        member_typedef_decl_type = psx_ctx_typedef_decl_type(&_ti);
        td_elem = _ti.elem_size;
        td_tag = _ti.tag_kind; td_tn = _ti.tag_name; td_tl = _ti.tag_len;
        td_isu = _ti.is_unsigned;
        /* ポインタ typedef (`typedef int (*FnPtr)(int)`, `typedef T *PT`) は struct メンバとして
         * 使うと 8 バイト幅で扱う必要がある。head.is_ptr (宣言子に `*` がついた) を立てて
         * 下流の type_size / deref_size / member_align 計算を再利用する。
         * elem_size は td_elem (指す先のサイズ) のまま保持して deref_size が正しく決まる
         * ようにする。修正前は ptr-typedef で elem_size のままにしていたため type_size=4
         * (関数戻り型 int) で 32bit `str w` に潰れ呼び出し時 SIGSEGV していた。 */
        if (_ti.is_pointer) member_is_ptr_typedef = 1;
        if (_ti.is_pointer) member_typedef_funcptr_sig = psx_ctx_typedef_funcptr_sig(&_ti);
        if (_ti.is_pointer && _ti.fp_kind != TK_FLOAT_KIND_NONE) {
          member_fp_kind = _ti.fp_kind;
        }
        /* 配列 typedef (`typedef int Row[4]; struct S { Row r; }`): typedef が array なら
         * 後段の psx_parse_member_array_suffixes_ex は inline [N] が無いため 1 を返す。
         * 多次元 (`typedef int Row[3][2]; struct { Row m; }`) も dims[] 経由で復元する。 */
        if (_ti.is_array && _ti.array_dim_count > 0) {
          member_typedef_array_dim_count = _ti.array_dim_count;
          for (int i = 0; i < _ti.array_dim_count && i < 8; i++) {
            member_typedef_array_dims[i] = _ti.array_dims[i];
          }
        }
        if (_ti.is_pointer && _ti.array_dim_count > 0) {
          int td_pointee_count = 1;
          member_typedef_ptr_array_dim_count = _ti.array_dim_count;
          for (int i = 0; i < _ti.array_dim_count && i < 8; i++) {
            member_typedef_ptr_array_dims[i] = _ti.array_dims[i];
            if (_ti.array_dims[i] > 0) td_pointee_count *= _ti.array_dims[i];
          }
          member_typedef_ptr_array_pointee_bytes = td_pointee_count * (_ti.elem_size > 0 ? _ti.elem_size : elem_size);
        }
      }
      if (td_tag != TK_EOF) {
        member_tag_kind = td_tag;
        member_tag_name = td_tn;
        member_tag_len = td_tl;
      }
      if (td_elem > 0) elem_size = td_elem;
      else if (td_tag != TK_EOF && psx_ctx_has_tag_type(td_tag, td_tn, td_tl)) {
        elem_size = psx_ctx_get_tag_size(td_tag, td_tn, td_tl);
      }
      /* typedef の unsigned 性をメンバへ伝播する。`typedef unsigned char u8;
       * struct S { u8 x; }` で u8 は IDENT トークンなので上の TK_UNSIGNED 検出に
       * かからず、捨てると sub-int メンバのロードが ldrsb (符号拡張) になり
       * s.x=200 が -56 に化ける。 */
      if (td_isu) member_is_unsigned = 1;
      set_curtok(curtok()->next);
    } else {
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_MEMBER_TYPE_REQUIRED));
    }

    for (;;) {
      member_decl_head_t head = psx_parse_member_decl_head();
      int has_member_name = head.member != NULL;
      int member_is_tag_aggregate = psx_ctx_is_tag_aggregate_kind(member_tag_kind);
      if (!has_member_name && !member_is_tag_aggregate && curtok()->kind != TK_COLON) {
        psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
      }
      if (head.has_func_suffix && !head.is_ptr) {
        psx_diag_ctx(curtok(), "decl", "%s",
                     diag_message_for(DIAG_ERR_PARSER_FUNCTION_MEMBER_FORBIDDEN));
      }

      int bit_width = 0;
      int bit_field_offset_in_storage = 0;
      if (curtok()->kind == TK_COLON) {
        set_curtok(curtok()->next);
        long long bw = psx_parse_enum_const_expr();
        if (bw < 0) bw = 0;
        bit_width = (int)bw;
        int storage_size = head.is_ptr ? ps_get_target_pointer_size()
                                       : (elem_size > 0 ? elem_size : 4);
        /* long / long long bitfield は 8 バイトのストレージユニットを使う。
         * 以前は 4 にクランプしており `unsigned long a:40` が 32bit ユニットに
         * 収まらず、後続フィールドの bit_offset 計算が破綻して重複配置していた。
         * codegen 側 (emit_bitfield_load/store) は offset+width>32 で 64bit 単位を
         * 扱えるようにしてある。 */
        if (storage_size > 8) storage_size = 8;
        int storage_bits = storage_size * 8;
        if (bit_width == 0) {
          bf_storage_offset = -1;
          bf_bits_used = 0;
          if (tag_kind != TK_UNION)
            current_off = ALIGN_UP(current_off, storage_size);
          if (!has_member_name) { if (!tk_consume(',')) break; continue; }
        }
        if (tag_kind != TK_UNION) {
          if (bf_storage_offset < 0
              || bf_storage_type_size != storage_size
              || bf_bits_used + bit_width > storage_bits) {
            /* 新しい bitfield run の開始 (直前が非 bitfield メンバ等で bf_storage_offset<0)。
             * AAPCS: 直前メンバに続く現在位置を含む storage ユニット内に、sizeof(T) 境界を
             * 跨がず収まるなら、そのユニットへ詰める (`char c; int x:20;` の x は c と同じ
             * 4B ユニットの bit 8 以降に入り sizeof=4)。跨ぐ場合・run のオーバーフロー時・
             * 型が変わる時は次の sizeof(T) 境界へ整列する。 */
            int container_start = current_off - (current_off % storage_size);
            int bits_before = (current_off - container_start) * 8;
            if (bf_storage_offset < 0 && bits_before + bit_width <= storage_bits) {
              bf_storage_offset = container_start;
              bf_storage_type_size = storage_size;
              bf_bits_used = bits_before;
              current_off = container_start + storage_size;
            } else {
              current_off = ALIGN_UP(current_off, storage_size);
              bf_storage_offset = current_off;
              bf_storage_type_size = storage_size;
              bf_bits_used = 0;
              current_off += storage_size;
            }
            if (storage_size > agg_align) agg_align = storage_size;
          }
          bit_field_offset_in_storage = bf_bits_used;
          bf_bits_used += bit_width;
        } else {
          bit_field_offset_in_storage = 0;
          bf_storage_offset = 0;
          bf_storage_type_size = storage_size;
          if (storage_size > union_size) union_size = storage_size;
          if (storage_size > agg_align) agg_align = storage_size;
        }
        if (has_member_name) {
          int storage_type_size = bf_storage_type_size > 0 ? bf_storage_type_size : 4;
          tag_member_info_t _mi = {0};
          _mi.name = head.member->str;
          _mi.len = head.member->len;
          _mi.offset = (tag_kind == TK_UNION) ? 0 : bf_storage_offset;
          tag_member_info_init_layout_cache(
              &_mi, storage_type_size, 0, 0, TK_EOF, NULL, 0,
              member_fp_kind, member_is_bool, member_is_unsigned, 0, 0);
          _mi.bit_width = bit_width;
          _mi.bit_offset = bit_field_offset_in_storage;
          _mi.bit_is_signed = is_signed_type;
          _mi.decl_type = member_decl_type_from_layout(
              member_base_kind, member_fp_kind, TK_EOF, NULL, 0,
              storage_type_size, member_is_unsigned, member_is_bool,
              member_is_complex, member_is_atomic, 0, 0, 0, 0,
              storage_type_size, (psx_decl_funcptr_sig_t){0});
          psx_ctx_add_tag_member(tag_kind, tag_name, tag_len, &_mi);
          member_count++;
        }
        if (!tk_consume(',')) break;
        continue;
      }

      bf_storage_offset = -1;
      bf_bits_used = 0;

      int is_flex_array = 0;
      if (curtok()->kind == TK_LBRACKET && !has_member_name) {
        psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
      }
      int arr_dim_count = 0, arr_first_dim = 0;
      int arr_dims_buf[8] = {0};
      int arr_size = member_array_layout_from_shape(
          &head, &is_flex_array, &arr_dim_count, &arr_first_dim,
          arr_dims_buf, 8);
      /* `int (*p)[N]` (struct メンバ版): パレン内 `*` + パレン外 trailing `[N]`。trailing は
       * pointee の配列次元であり、メンバ自身は単一ポインタ (8B 1 slot)。pointee dims は
       * outer_stride に reflect して downstream の `(*s.p)[i]` で参照する。
       * `int (*p[M])[N]` (parens 内に `[M]` あり) は array-of-pointer-to-array: 各要素が
       * pointer-to-array で、メンバ自身は M 個のポインタ配列 (8B*M)。trailing `[N]` は
       * pointee の配列次元として ptr_array_pointee_bytes に保存し、downstream の subscript
       * 結果に carry する。 */
      int pointee_arr_size = 0;
      int pointee_arr_dim_count = 0;
      int pointee_arr_first_dim = 0;
      int pointee_arr_dims[8] = {0};
      int pointee_arr_elem_storage_size = elem_size;
      int pointee_arr_element_is_pointer = 0;
      int ptr_array_pointee_bytes = 0;
      psx_ret_pointee_array_t direct_funcptr_ret_pointee_array = {0};
      /* The frontend currently models C pointer objects as 8 bytes even when the Wasm
       * backend lowers addresses to i32. Keep aggregate layout consistent with sizeof
       * and the rest of parser metadata until the whole type model is moved to ILP32. */
      int ptr_size = 8;
      if (head.is_ptr && head.has_func_suffix && arr_size > 1 && !is_flex_array) {
        /* 関数ポインタメンバが配列へのポインタを返す直書き宣言子:
         * `int (*(*f)(void))[N]` / `int (*(*f)(void))[N][M]`。
         * trailing `[N][M]` はメンバ自身の配列ではなく、戻り値ポインタの pointee 配列次元。
         * メンバは 8B の関数ポインタ 1 個として layout し、次元は呼び出し側 subscript 用に保存する。 */
        psx_ret_pointee_array_absorb_member_suffix(
            &arr_size, &arr_dim_count, &arr_first_dim, arr_dims_buf, 8,
            elem_size, &direct_funcptr_ret_pointee_array);
      }
      if (head.ptr_in_paren && head.is_ptr && arr_size > 1 && !is_flex_array
          && !head.has_func_suffix) {
        if (head.paren_array_mul == 1) {
          /* `int (*p)[N]` / `int (*p)[M][N]`: メンバは単一ポインタ。pointee dims (1D / 多次元)
           * を保存し、outer_stride / mid_stride に反映して downstream の `(*s.p)[i][j]` が
           * 行ストライドで添字できるようにする。 */
          pointee_arr_size = arr_size;
          pointee_arr_dim_count = arr_dim_count;
          pointee_arr_first_dim = arr_first_dim;
          for (int i = 0; i < arr_dim_count && i < 8; i++)
            pointee_arr_dims[i] = arr_dims_buf[i];
          pointee_arr_element_is_pointer = member_is_ptr_typedef ? 1 : 0;
          pointee_arr_elem_storage_size =
              pointee_arr_element_is_pointer ? ptr_size : elem_size;
          arr_size = 1;
          arr_dim_count = 0;
          arr_first_dim = 0;
          for (int i = 0; i < 8; i++) arr_dims_buf[i] = 0;
        } else {
          /* `int (*p[M])[N]`: メンバは M 個のポインタ配列。pointee 1 個ぶんのバイト数 (N*elem)
           * を保存しておき、subscript 結果 deref に carry する。arr_size は M に置き換える。 */
          ptr_array_pointee_bytes = arr_size * elem_size;
          arr_size = head.paren_array_mul;
          arr_first_dim = head.paren_array_mul;
          arr_dim_count = 1;
          arr_dims_buf[0] = head.paren_array_mul;
          for (int i = 1; i < 8; i++) arr_dims_buf[i] = 0;
        }
      }
      if (head.paren_array_mul > 1 && ptr_array_pointee_bytes == 0) {
        /* 既存挙動: ptr_in_paren でない通常の `(*name)` 経路 (関数ポインタ配列等) は
         * これまでどおり paren_array_mul で arr_size を倍化する。 */
        arr_size *= head.paren_array_mul;
      }
      /* 配列 typedef + 宣言子に追加 `[N]` なし → typedef の次元情報を取り込む。
       * 宣言子にも追加 `[N]` がある場合 (`typedef int R[3]; struct {R r[2];}`) は
       * 宣言子側 dims を outer に、typedef 側 dims を inner に連結する。
       * 結果 r は [2][3] の 2D 配列で、6 要素 (24 バイト)。 */
      if (member_typedef_array_dim_count > 0 && !is_flex_array) {
        int combined_dims[8] = {0};
        int combined_count = 0;
        /* outer (declarator side) */
        for (int i = 0; i < arr_dim_count && combined_count < 8; i++) {
          combined_dims[combined_count++] = arr_dims_buf[i];
        }
        /* inner (typedef side) */
        for (int i = 0; i < member_typedef_array_dim_count && combined_count < 8; i++) {
          combined_dims[combined_count++] = member_typedef_array_dims[i];
        }
        arr_dim_count = combined_count;
        arr_first_dim = (combined_count > 0) ? combined_dims[0] : 0;
        for (int i = 0; i < arr_dim_count && i < 8; i++) {
          arr_dims_buf[i] = combined_dims[i];
        }
        int total_count = 1;
        for (int i = 0; i < arr_dim_count; i++) total_count *= arr_dims_buf[i];
        arr_size = total_count;
      }
      /* 宣言子に `*` がついた場合 (head.is_ptr) と、typedef 自体がポインタ型の場合
       * (member_is_ptr_typedef) の両方を「メンバはポインタ」として扱う。typedef ポインタは
       * 宣言子に `*` が現れないため head.is_ptr を立てておくと扱いが揃う。 */
      int total_pointer_levels = head.ptr_levels + (member_is_ptr_typedef ? 1 : 0);
      int member_is_ptr = head.is_ptr || member_is_ptr_typedef;
      int layout_pointer_levels = total_pointer_levels;
      if (pointee_arr_size > 0 && head.is_ptr)
        layout_pointer_levels = head.ptr_levels > 0 ? head.ptr_levels : 1;
      int member_elem_size = member_is_ptr ? ptr_size : elem_size;
      int total_size = is_flex_array ? 0 : (member_elem_size * arr_size);
      int deref_size = member_is_ptr ? ((layout_pointer_levels >= 2) ? ptr_size : elem_size) : 0;
      int member_align = member_is_ptr ? ptr_size : elem_size;
      /* struct/union メンバ (非ポインタ) のアラインメントは「メンバの最大スカラ
       * アラインメント (psx_ctx_get_tag_align)」であり、sizeof ではない。
       * 修正前は elem_size (= sizeof) をそのまま採用しており、
       * `struct Inner {int a, b;}` (sizeof=8, align=4) を含む `struct Outer
       * {struct Inner i; int trail;}` で agg_align=8 となり sizeof(Outer)=16 と 4 バイト
       * 過剰パディング (clang は 12)。 */
      if (!member_is_ptr && member_is_tag_aggregate && member_tag_name) {
        int tag_align = psx_ctx_get_tag_align(member_tag_kind, member_tag_name, member_tag_len);
        if (tag_align > 0) member_align = tag_align;
      }
      if (member_align <= 0) member_align = 1;
      if (member_align > 8) member_align = 8;
      int pack_align = pragma_pack_current_alignment();
      if (pack_align > 0 && pack_align < member_align) member_align = pack_align;
      if (member_alignas > member_align) member_align = member_alignas;
      if (member_align > agg_align) agg_align = member_align;
      int off = 0;
      if (tag_kind == TK_UNION) {
        off = 0;
      } else {
        current_off = ALIGN_UP(current_off, member_align);
        off = current_off;
      }
      char *member_name = has_member_name ? head.member->str : "";
      int member_len = has_member_name ? head.member->len : 0;
      int member_array_len = (arr_dim_count > 0 || head.paren_array_mul > 1) ? arr_size : 0;
      if (has_member_name || member_is_tag_aggregate) {
        tag_member_info_t _mi = {0};
        _mi.name = member_name;
        _mi.len = member_len;
        _mi.offset = off;
        tag_member_info_init_layout_cache(
            &_mi, member_is_ptr ? ptr_size : elem_size, deref_size,
            member_array_len, member_tag_kind, member_tag_name, member_tag_len,
            member_fp_kind, member_is_bool, member_is_unsigned,
            member_is_ptr, member_is_ptr ? layout_pointer_levels : 0);
        psx_decl_funcptr_sig_t member_funcptr_sig = member_typedef_funcptr_sig;
        if (member_is_ptr) {
          if (head.has_func_suffix) {
            int ret_is_data_pointer =
                member_funcptr_direct_ret_is_data_pointer(&head, member_is_ptr_typedef);
            member_funcptr_sig = psx_decl_make_funcptr_sig_from_kind(
                &head.func_suffix_sig, member_base_kind, member_fp_kind,
                ret_is_data_pointer, 0, member_is_complex,
                member_typedef_funcptr_sig.function.callable.return_shape.pointee_array);
          }
          psx_ret_pointee_array_t ret_pointee_array = {0};
          PSX_RET_POINTEE_ARRAY_SELECT_INTO(&ret_pointee_array,
                                            &member_typedef_funcptr_sig.function.callable.return_shape.pointee_array,
                                            &direct_funcptr_ret_pointee_array);
          if (!psx_ret_pointee_array_has_dims(member_funcptr_sig.function.callable.return_shape.pointee_array)) {
            member_funcptr_sig.function.callable.return_shape.pointee_array = ret_pointee_array;
          }
        }
        _mi.decl_type = member_decl_type_from_layout(
            member_base_kind, member_fp_kind, member_tag_kind, member_tag_name,
            member_tag_len, elem_size, member_is_unsigned, member_is_bool,
            member_is_complex, member_is_atomic, member_is_ptr, layout_pointer_levels,
            member_array_len, total_size, member_elem_size, member_funcptr_sig);
        /* pointer-to-array メンバ (`int (*p)[N]` / `int (*p)[M][N]`): pointee 全バイトサイズを
         * outer_stride に保存。多次元 pointee の場合は 1 段目 subscript stride も mid_stride に
         * 保存し、build_member_deref_node が deref を multi-dim 配列形に組めるようにする。 */
        if (has_member_name && pointee_arr_size > 0) {
          _mi.outer_stride = pointee_arr_size * pointee_arr_elem_storage_size;
          if (pointee_arr_dim_count >= 2 && pointee_arr_first_dim > 0) {
            /* 2D pointee (`int (*p)[M][N]`): 1 段目 subscript stride = (M*N*elem)/M = N*elem */
            _mi.mid_stride = (pointee_arr_size / pointee_arr_first_dim) *
                             pointee_arr_elem_storage_size;
          }
          _mi.ptr_array_pointee_bytes = _mi.outer_stride;
          _mi.decl_type = member_decl_type_pointer_to_array_from_layout(
              member_base_kind, member_fp_kind, member_tag_kind,
              member_tag_name, member_tag_len, elem_size, member_is_unsigned,
              member_is_bool, member_is_complex, member_is_atomic,
              pointee_arr_element_is_pointer, pointee_arr_dims,
              pointee_arr_dim_count, pointee_arr_size,
              pointee_arr_elem_storage_size);
        }
        /* array-of-pointer-to-array メンバ (`int (*p[M])[N]`): 各要素ポインタが指す配列の
         * 全バイト数 (= N * elem) を保存する。`s.p[i]` の subscript 結果 deref に carry し、
         * `(*s.p[i])[j]` の build_unary_deref_node 経路で要素ストライドに再設定する。 */
        if (has_member_name && ptr_array_pointee_bytes > 0) {
          _mi.ptr_array_pointee_bytes = ptr_array_pointee_bytes;
          int pointee_count = elem_size > 0
                                  ? ptr_array_pointee_bytes / elem_size
                                  : 0;
          if (pointee_count > 0) {
            _mi.decl_type = psx_type_wrap_pointer_base_array(
                _mi.decl_type, pointee_count);
            if (_mi.decl_type) {
              _mi.decl_type->ptr_array_pointee_bytes =
                  ptr_array_pointee_bytes;
            }
          }
        }
        if (has_member_name && member_typedef_ptr_array_pointee_bytes > 0) {
          if (head.is_ptr || member_array_len > 0) {
            _mi.ptr_array_pointee_bytes = member_typedef_ptr_array_pointee_bytes;
            if (member_typedef_ptr_array_dim_count > 0) {
              _mi.arr_ndim = member_typedef_ptr_array_dim_count > 8
                                  ? 8
                                  : member_typedef_ptr_array_dim_count;
              for (int i = 0; i < _mi.arr_ndim; i++)
                _mi.arr_dims[i] = member_typedef_ptr_array_dims[i];
            }
          } else if (member_is_ptr_typedef) {
            _mi.ptr_array_pointee_bytes = member_typedef_ptr_array_pointee_bytes;
            if (member_typedef_ptr_array_dim_count > 0) {
              _mi.arr_ndim = member_typedef_ptr_array_dim_count > 8
                                  ? 8
                                  : member_typedef_ptr_array_dim_count;
              for (int i = 0; i < _mi.arr_ndim; i++)
                _mi.arr_dims[i] = member_typedef_ptr_array_dims[i];
            }
            _mi.outer_stride = member_typedef_ptr_array_pointee_bytes;
            if (member_typedef_ptr_array_dim_count >= 2 &&
                member_typedef_ptr_array_dims[0] > 0) {
              _mi.mid_stride =
                  member_typedef_ptr_array_pointee_bytes / member_typedef_ptr_array_dims[0];
            }
          }
        }
        /* 多次元配列メンバ (例 int a[2][2]) は最外次元のバイトストライドを保存し、
         * メンバアクセス時に多段 subscript を正しくスケールできるようにする。 */
        if (has_member_name && !head.is_ptr && !is_flex_array &&
            arr_dim_count >= 2 && arr_first_dim > 0) {
          int inner_count = arr_size / arr_first_dim;   /* 第1次元を除く要素数 */
          _mi.outer_stride = inner_count * member_elem_size;
          /* 3 次元以上は中間段ストライド (1 段 subscript 後の要素サイズ) も保存。
           * `char c[2][2][3]` なら arr_dims=[2,2,3]、mid_stride = 3*1 = 3。
           * これがないと build_member_deref_node の inner_deref_size が elem_size の
           * ままで 3 段目 subscript が誤スケール (or SIGSEGV) になっていた。 */
          if (arr_dim_count >= 3 && arr_dims_buf[1] > 0) {
            int inner2 = inner_count / arr_dims_buf[1];  /* 第1+第2次元を除く要素数 */
            _mi.mid_stride = inner2 * member_elem_size;
          }
          /* 多次元配列メンバの各次元サイズを保存する。
           * (1) char (`char c[2][2][3]`): グローバル brace init `{{{"ab","cd"},...}}` を
           *     再帰展開する (gbrace_ctx_t.sub_dims 経由) のに使う。
           * (2) 非 char (`int x[3][3]`): `[N]={...}` designator の elem_slots を
           *     「内側次元の総スカラ数」として算出するのに使う。これがないと多次元配列で
           *     `[N]=` のジャンプ幅が 1 slot ぶんしか進まず `[2]=` が slot 6 でなく
           *     slot 2 にジャンプし他要素を上書きしていた (designator nested バグ)。
           * (3) struct タグ多次元配列メンバ (`struct C rows[3][2]`): 外側 `[N]=` の
           *     elem_slots を `struct slot * 内側次元の積` で計算するため sub_dims が必要。
           *     これがないと外側 designator が内側次元を無視して誤ジャンプ。
           * tag ポインタメンバはスカラ単位の slot 計算で対象外 (member_is_ptr で除外)。 */
          if (!member_is_ptr) {
            _mi.arr_ndim = arr_dim_count > 8 ? 8 : arr_dim_count;
            for (int i = 0; i < _mi.arr_ndim; i++) _mi.arr_dims[i] = arr_dims_buf[i];
          }
        }
        psx_type_t *canonical_member = member_typedef_decl_type
                                           ? psx_type_clone(member_typedef_decl_type)
                                           : member_scalar_type(
                                                 member_base_kind, member_fp_kind,
                                                 member_tag_kind, member_tag_name,
                                                 member_tag_len, elem_size,
                                                 member_is_unsigned, member_is_bool,
                                                 member_is_complex, member_is_atomic);
        canonical_member = psx_type_apply_declarator_shape(
            canonical_member, &head.declarator_shape);
        if (canonical_member) {
          _mi.decl_type = canonical_member;
        }
        member_decl_type_apply_shape_cache(_mi.decl_type, &_mi);
        psx_ctx_add_tag_member(tag_kind, tag_name, tag_len, &_mi);
        member_count++;
      }
      /* C11 6.7.2.1p13: 匿名 struct/union (タグ名なし、メンバ名なし) の場合、
       * 内側のメンバを外側 tag に直接見えるように昇格 (promote) する。
       * 外側からは `outer.inner_member` の形でアクセスできる。 */
      if (!has_member_name && !head.is_ptr &&
          member_is_tag_aggregate && member_tag_name) {
        int inner_count = ps_ctx_get_tag_member_count(member_tag_kind, member_tag_name, member_tag_len);
        for (int i = 0; i < inner_count; i++) {
          tag_member_info_t im = {0};
          if (ps_ctx_get_tag_member_info(member_tag_kind, member_tag_name, member_tag_len, i, &im)) {
            if (im.len == 0) continue; /* 匿名同士の連鎖は今回対象外 */
            tag_member_info_t _mi = im;
            _mi.offset = off + im.offset;
            /* C11 6.7.2.1p13: 匿名 struct/union 内 bitfield も昇格メンバとして外側からの
             * アクセスで正しく動くよう、bit_width/bit_offset/bit_is_signed を保持する。
             * 修正前: 旧 non-bf 版の名残でこれらを 0 にクリアしており、`s.lo` (匿名 struct
             * 内 `unsigned lo:16`) が bitfield 抽出を経ず full-width load されて値が化けていた。 */
            psx_ctx_add_tag_member(tag_kind, tag_name, tag_len, &_mi);
            member_count++;
          }
        }
      }
      if (tag_kind == TK_UNION) {
        if (total_size > union_size) union_size = total_size;
      } else {
        current_off += total_size;
      }
      if (!has_member_name && tk_consume(',')) psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
      if (!tk_consume(',')) break;
    }
    tk_expect(';');
  }
  *out_size = (tag_kind == TK_UNION) ? ALIGN_UP(union_size, agg_align) : ALIGN_UP(current_off, agg_align);
  if (out_align) *out_align = agg_align;
  return member_count;
}

#undef ALIGN_UP
