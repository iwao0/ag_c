#include "struct_layout.h"
#include "aggregate_member_declaration.h"
#include "alignas_value.h"
#include "anon_tag.h"
#include "array_suffixes.h"
#include "core.h"
#include "decl.h"
#include "declarator_syntax.h"
#include "diag.h"
#include "enum_const.h"
#include "expr.h"
#include "config_runtime.h"
#include "ret_pointee_array.h"
#include "semantic_ctx.h"
#include "tag_declaration.h"
#include "type.h"
#include "../diag/diag.h"
#include "../semantic/constant_expression.h"
#include "../tokenizer/tokenizer.h"
#include "../pragma_pack.h"

static inline token_t *curtok(void) { return tk_get_current_token(); }
static inline void set_curtok(token_t *tok) { tk_set_current_token(tok); }

static void diagnose_member_declarator_too_complex(
    void *context, token_t *tok) {
  (void)context;
  psx_diag_ctx(tok, "member", "member declarator is too complex");
}

static int append_member_declarator_pointer(
    void *context, int is_const, int is_volatile, int nesting_depth) {
  member_decl_head_t *head = context;
  if (!head) return 0;
  if (nesting_depth > 0) head->ptr_in_paren = 1;
  return psx_declarator_shape_append_pointer(
      &head->declarator_shape, is_const, is_volatile);
}

static int consume_member_declarator_suffix(
    void *context, int nesting_depth, int direct_was_parenthesized,
    int direct_pointer_count, int frame_pointer_count) {
  (void)direct_was_parenthesized;
  (void)frame_pointer_count;
  member_decl_head_t *head = context;
  if (!head) return 0;
  if (curtok()->kind == TK_LBRACKET) {
    tk_expect('[');
    int has_size = 0;
    int dim = psx_parse_array_size_optional_constexpr(&has_size);
    if (!psx_declarator_shape_append_array_ex(
            &head->declarator_shape, has_size ? dim : 0, !has_size))
      diagnose_member_declarator_too_complex(context, curtok());
    if (nesting_depth > 0 && dim > 0) head->paren_array_mul *= dim;
    return 1;
  }
  if (curtok()->kind != TK_LPAREN) return 0;
  psx_funcptr_signature_t suffix = {0};
  psx_skip_func_param_list(&suffix);
  if (!head->has_func_suffix) head->func_suffix_sig = suffix;
  head->has_func_suffix = 1;
  if (head->funcptr_object_pointer_levels == 0 &&
      direct_pointer_count > 0) {
    head->funcptr_object_pointer_levels = direct_pointer_count;
  }
  psx_decl_funcptr_sig_t op_sig = {0};
  op_sig.function.callable.signature = suffix;
  if (!psx_declarator_shape_append_function(
          &head->declarator_shape, op_sig))
    diagnose_member_declarator_too_complex(context, curtok());
  return 1;
}

member_decl_head_t psx_parse_member_decl_head(void) {
  member_decl_head_t out = {0};
  out.paren_array_mul = 1;
  psx_declarator_shape_init(&out.declarator_shape);
  out.member = psx_parse_declarator_syntax(
      &(psx_declarator_syntax_t){
          .context = &out,
          .consume_suffix = consume_member_declarator_suffix,
          .append_pointer = append_member_declarator_pointer,
          .diagnose_too_complex = diagnose_member_declarator_too_complex,
      },
      &out.ptr_levels);
  out.is_ptr = out.ptr_levels > 0;
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
  psx_aggregate_layout_state_t layout;
  psx_aggregate_layout_init(&layout, tag_kind);
  while (!tk_consume('}')) {
    /* C11 6.7.2.1: struct/union のメンバ位置にも static_assert-declaration を書ける。
     * `_Static_assert(expr, "msg");` の expr を畳み込んで真なら受理、偽なら診断。
     * これがないと `struct S { _Static_assert(...); int x; };` が「メンバ型が必要」E3064。 */
    if (curtok()->kind == TK_STATIC_ASSERT) {
      set_curtok(curtok()->next);
      tk_expect('(');
      int const_ok = 1;
      long long cond_val = psx_eval_const_int(psx_expr_assign(), &const_ok);
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
        psx_apply_parsed_tag_declaration(
            member_tag_kind, member_tag_name, member_tag_len,
            PSX_TAG_DECLARATION_DEFINITION, nested_n, nested_sz,
            nested_align, curtok());
      } else if (!psx_ctx_has_tag_type(member_tag_kind, member_tag_name, member_tag_len)) {
        psx_apply_parsed_tag_declaration(
            member_tag_kind, member_tag_name, member_tag_len,
            PSX_TAG_DECLARATION_REFERENCE, 0, 0, 0, curtok());
      }
      if (psx_ctx_has_tag_type(member_tag_kind, member_tag_name, member_tag_len)) {
        elem_size = psx_ctx_get_tag_size(member_tag_kind, member_tag_name, member_tag_len);
      }
    } else if (psx_ctx_is_typedef_name_token(curtok())) {
      /* `typedef struct Node Node; struct Node { ... Node *next; };` のように
       * typedef 名が struct メンバ型として現れるケース。typedef を解決して
       * 基底型 / tag 情報を取り出し、後続の `*` や宣言子と一緒に処理する。 */
      token_ident_t *td = (token_ident_t *)curtok();
      psx_ctx_find_typedef_decl_type(
          td->str, td->len, &member_typedef_decl_type);
      set_curtok(curtok()->next);
    } else {
      psx_diag_ctx(curtok(), "decl", "%s",
                   diag_message_for(DIAG_ERR_PARSER_MEMBER_TYPE_REQUIRED));
    }

    psx_aggregate_member_base_resolution_t member_base_resolution;
    psx_resolve_aggregate_member_base_type(
        &(psx_aggregate_member_base_resolution_request_t){
            .declaration = {
                .base_kind = member_base_kind,
                .elem_size = elem_size,
                .fp_kind = member_fp_kind,
                .tag_kind = member_tag_kind,
                .tag_name = member_tag_name,
                .tag_len = member_tag_len,
                .is_unsigned = member_is_unsigned,
                .is_complex = member_is_complex,
                .is_atomic = member_is_atomic,
                .base_decl_type = member_typedef_decl_type,
            },
        },
        &member_base_resolution);
    if (member_base_resolution.status != PSX_AGGREGATE_MEMBER_OK) {
      psx_diag_ctx(curtok(), "member",
                   "canonical aggregate member base type resolution failed");
    }
    const psx_aggregate_member_storage_plan_t *member_base_plan =
        &member_base_resolution.storage;
    if (member_base_plan->scalar_size > 0)
      elem_size = member_base_plan->scalar_size;
    if (member_base_plan->scalar_kind != TK_EOF)
      member_base_kind = member_base_plan->scalar_kind;
    member_fp_kind = member_base_plan->fp_kind;
    member_is_bool = member_base_plan->is_bool;
    member_is_unsigned = member_base_plan->is_unsigned;
    member_is_complex = member_base_plan->is_complex;
    member_is_atomic = member_base_plan->is_atomic;
    member_is_ptr_typedef = member_typedef_decl_type &&
                            member_base_plan->is_pointer_object;
    member_typedef_funcptr_sig = ps_type_funcptr_signature(
        member_base_resolution.type);
    is_signed_type =
        (member_is_bool || member_is_unsigned || member_tag_kind == TK_ENUM)
            ? 0
            : 1;

    for (;;) {
      member_decl_head_t head = psx_parse_member_decl_head();
      int has_member_name = head.member != NULL;
      int member_is_tag_aggregate = psx_ctx_is_tag_aggregate_kind(member_tag_kind);
      if (!has_member_name && !member_is_tag_aggregate && curtok()->kind != TK_COLON) {
        psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
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
        psx_aggregate_bitfield_resolution_t bitfield_placement =
            psx_apply_aggregate_bitfield_placement(
                &layout, storage_size, bit_width, curtok());
        bit_field_offset_in_storage = bitfield_placement.bit_offset;
        if (bit_width == 0 && !has_member_name) {
          if (!tk_consume(',')) break;
          continue;
        }
        if (has_member_name) {
          int storage_type_size = bitfield_placement.storage_size;
          tag_member_info_t _mi = {0};
          _mi.name = head.member->str;
          _mi.len = head.member->len;
          _mi.offset = bitfield_placement.offset;
          _mi.bit_width = bit_width;
          _mi.bit_offset = bit_field_offset_in_storage;
          _mi.bit_is_signed = is_signed_type;
          _mi.decl_type = psx_resolve_aggregate_member_type(
              &(psx_aggregate_member_type_request_t){
                  .declaration = {
                      .base_kind = member_base_kind,
                      .elem_size = storage_type_size,
                      .fp_kind = member_fp_kind,
                      .is_unsigned = member_is_unsigned,
                      .is_complex = member_is_complex,
                      .is_atomic = member_is_atomic,
                      .base_decl_type = member_base_resolution.type,
                  },
                  .layout_metadata = &_mi,
              });
          psx_validate_resolved_aggregate_member_type(
              _mi.decl_type, (token_t *)head.member);
          psx_apply_resolved_aggregate_member(
              tag_kind, tag_name, tag_len, &_mi, (token_t *)head.member);
          member_count++;
        }
        if (!tk_consume(',')) break;
        continue;
      }

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
      int pointee_arr_elem_storage_size = elem_size;
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
          pointee_arr_elem_storage_size =
              member_is_ptr_typedef ? ptr_size : elem_size;
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
      int member_is_ptr_hint = head.is_ptr || member_is_ptr_typedef;
      psx_decl_funcptr_sig_t member_funcptr_sig = member_typedef_funcptr_sig;
      if (member_is_ptr_hint) {
        if (head.has_func_suffix) {
          int ret_is_data_pointer =
              member_funcptr_direct_ret_is_data_pointer(
                  &head, member_is_ptr_typedef);
          member_funcptr_sig = psx_decl_make_funcptr_sig_from_kind(
              &head.func_suffix_sig, member_base_kind, member_fp_kind,
              ret_is_data_pointer, 0, member_is_complex,
              member_typedef_funcptr_sig.function.callable.return_shape.pointee_array);
        }
        psx_ret_pointee_array_t ret_pointee_array = {0};
        PSX_RET_POINTEE_ARRAY_SELECT_INTO(
            &ret_pointee_array,
            &member_typedef_funcptr_sig.function.callable.return_shape.pointee_array,
            &direct_funcptr_ret_pointee_array);
        if (!psx_ret_pointee_array_has_dims(
                member_funcptr_sig.function.callable.return_shape.pointee_array)) {
          member_funcptr_sig.function.callable.return_shape.pointee_array =
              ret_pointee_array;
        }
      }
      psx_type_t *canonical_member_type = psx_resolve_aggregate_member_type(
          &(psx_aggregate_member_type_request_t){
              .declaration = {
                  .base_kind = member_base_kind,
                  .elem_size = elem_size,
                  .fp_kind = member_fp_kind,
                  .tag_kind = member_tag_kind,
                  .tag_name = member_tag_name,
                  .tag_len = member_tag_len,
                  .is_unsigned = member_is_unsigned,
                  .is_complex = member_is_complex,
                  .is_atomic = member_is_atomic,
                  .base_decl_type = member_base_resolution.type,
                  .declarator_shape = &head.declarator_shape,
              },
              .funcptr_signature = member_funcptr_sig,
              .has_funcptr_signature =
                  ps_decl_funcptr_sig_has_payload(member_funcptr_sig),
          });
      psx_validate_resolved_aggregate_member_type(
          canonical_member_type,
          has_member_name ? (token_t *)head.member : curtok());
      psx_aggregate_member_storage_plan_t storage_plan;
      psx_plan_aggregate_member_storage(canonical_member_type, &storage_plan);
      if (storage_plan.status != PSX_AGGREGATE_MEMBER_OK) {
        psx_diag_ctx(curtok(), "member",
                     "canonical aggregate member storage planning failed");
      }
      int member_is_ptr = storage_plan.is_pointer_object;
      int member_elem_size = storage_plan.value_size;
      if (storage_plan.array_dim_count > 0) {
        arr_dim_count = storage_plan.array_dim_count > 8
                            ? 8
                            : storage_plan.array_dim_count;
        arr_size = storage_plan.array_element_count;
        arr_first_dim = storage_plan.array_dims[0];
        for (int i = 0; i < arr_dim_count; i++)
          arr_dims_buf[i] = storage_plan.array_dims[i];
      } else if (!is_flex_array) {
        arr_dim_count = 0;
        arr_size = 1;
        arr_first_dim = 0;
        for (int i = 0; i < 8; i++) arr_dims_buf[i] = 0;
      }
      int pack_align = pragma_pack_current_alignment();
      psx_aggregate_object_placement_t object_placement =
          psx_apply_aggregate_object_placement(
              &layout, storage_plan.storage_size, storage_plan.alignment,
              pack_align,
              member_alignas, curtok());
      int off = object_placement.offset;
      char *member_name = has_member_name ? head.member->str : "";
      int member_len = has_member_name ? head.member->len : 0;
      if (has_member_name || member_is_tag_aggregate) {
        tag_member_info_t _mi = {0};
        _mi.name = member_name;
        _mi.len = member_len;
        _mi.offset = off;
        _mi.deref_size = storage_plan.deref_size;
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
        }
        /* array-of-pointer-to-array メンバ (`int (*p[M])[N]`): 各要素ポインタが指す配列の
         * 全バイト数 (= N * elem) を保存する。`s.p[i]` の subscript 結果 deref に carry し、
         * `(*s.p[i])[j]` の build_unary_deref_node 経路で要素ストライドに再設定する。 */
        if (has_member_name && ptr_array_pointee_bytes > 0) {
          _mi.ptr_array_pointee_bytes = ptr_array_pointee_bytes;
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
           * (1) char (`char c[2][2][3]`): brace init `{{{"ab","cd"},...}}` を
           *     canonical type の各次元に沿って再帰展開するのに使う。
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
        _mi.decl_type = canonical_member_type;
        psx_apply_aggregate_member_layout_metadata(_mi.decl_type, &_mi);
        psx_apply_resolved_aggregate_member(
            tag_kind, tag_name, tag_len, &_mi,
            has_member_name ? (token_t *)head.member : curtok());
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
            psx_apply_resolved_aggregate_member(
                tag_kind, tag_name, tag_len, &_mi, curtok());
            member_count++;
          }
        }
      }
      if (!has_member_name && tk_consume(',')) psx_diag_missing(curtok(), diag_text_for(DIAG_TEXT_MEMBER_NAME));
      if (!tk_consume(',')) break;
    }
    tk_expect(';');
  }
  *out_size = psx_aggregate_layout_size(&layout);
  if (out_align) *out_align = psx_aggregate_layout_alignment(&layout);
  return member_count;
}
