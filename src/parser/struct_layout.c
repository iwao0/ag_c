#include "struct_layout.h"
#include "alignas_value.h"
#include "anon_tag.h"
#include "array_suffixes.h"
#include "core.h"
#include "decl.h"
#include "diag.h"
#include "enum_const.h"
#include "expr.h"
#include "ret_pointee_array.h"
#include "semantic_ctx.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include "../pragma_pack.h"

static inline token_t *curtok(void) { return tk_get_current_token(); }
static inline void set_curtok(token_t *tok) { tk_set_current_token(tok); }

#define ALIGN_UP(v, a) (((v) + ((a) - 1)) / (a) * (a))

static token_ident_t *parse_member_decl_name_recursive(int *is_ptr, int *out_has_func_suffix,
                                                       int *out_paren_array_mul,
                                                       int *out_ptr_in_paren) {
  psx_consume_pointer_prefix(is_ptr);
  token_ident_t *name = NULL;
  int paren_array_mul = 1;
  if (tk_consume('(')) {
    int ptr_before = *is_ptr;
    name = parse_member_decl_name_recursive(is_ptr, out_has_func_suffix, &paren_array_mul,
                                            out_ptr_in_paren);
    /* `(` 通過直後に `*` を消費したか? `int (*p)[N]` 等を `int *p[N]` と区別するためのフラグ。
     * 内側で更に `(` を踏んで設定された結果は維持する。 */
    if (!ptr_before && *is_ptr && out_ptr_in_paren) *out_ptr_in_paren = 1;
    paren_array_mul = psx_parse_array_suffixes_constexpr_required(paren_array_mul);
    tk_expect(')');
  } else {
    name = tk_consume_ident();
  }
  psx_skip_func_suffix_groups(out_has_func_suffix);
  if (out_paren_array_mul) *out_paren_array_mul = paren_array_mul;
  return name;
}

member_decl_head_t psx_parse_member_decl_head(void) {
  member_decl_head_t out = {0};
  out.paren_array_mul = 1;
  out.member = parse_member_decl_name_recursive(&out.is_ptr, &out.has_func_suffix,
                                                &out.paren_array_mul, &out.ptr_in_paren);
  return out;
}

int psx_parse_tag_definition_body(token_kind_t tag_kind, char *tag_name, int tag_len,
                                  int *out_size) {
  if (tag_kind == TK_ENUM) {
    if (out_size) *out_size = 4;
    return psx_parse_enum_members();
  }
  return psx_parse_struct_or_union_members_layout(tag_kind, tag_name, tag_len, out_size);
}

int psx_parse_struct_or_union_members_layout(token_kind_t tag_kind, char *tag_name, int tag_len,
                                             int *out_size) {
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
    int member_is_bool = 0;
    int member_is_unsigned = 0;
    int member_base_is_void = 0;
    int member_is_ptr_typedef = 0;
    unsigned short member_typedef_funcptr_param_fp_mask = 0;
    unsigned short member_typedef_funcptr_param_int_mask = 0;
    int member_typedef_funcptr_ret_is_void = 0;
    psx_ret_pointee_array_t member_typedef_funcptr_ret_pointee_array = {0};
    int member_typedef_array_dim_count = 0;
    int member_typedef_array_dims[8] = {0};
    if (psx_ctx_is_type_token(curtok()->kind)) {
      is_signed_type = (curtok()->kind != TK_UNSIGNED);
      psx_ctx_get_type_info(curtok()->kind, NULL, &elem_size);
      if (curtok()->kind == TK_FLOAT) member_fp_kind = TK_FLOAT_KIND_FLOAT;
      else if (curtok()->kind == TK_DOUBLE) member_fp_kind = TK_FLOAT_KIND_DOUBLE;
      else if (curtok()->kind == TK_BOOL) member_is_bool = 1;
      else if (curtok()->kind == TK_UNSIGNED) member_is_unsigned = 1;
      else if (curtok()->kind == TK_VOID) member_base_is_void = 1;
      set_curtok(curtok()->next);
      while (psx_ctx_is_type_token(curtok()->kind)) {
        if (curtok()->kind != TK_UNSIGNED && curtok()->kind != TK_SIGNED) {
          psx_ctx_get_type_info(curtok()->kind, NULL, &elem_size);
        }
        if (curtok()->kind == TK_FLOAT) member_fp_kind = TK_FLOAT_KIND_FLOAT;
        else if (curtok()->kind == TK_DOUBLE) member_fp_kind = TK_FLOAT_KIND_DOUBLE;
        else if (curtok()->kind == TK_BOOL) member_is_bool = 1;
        else if (curtok()->kind == TK_UNSIGNED) member_is_unsigned = 1;
        else if (curtok()->kind == TK_VOID) member_base_is_void = 1;
        set_curtok(curtok()->next);
      }
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
        nested_n = psx_parse_tag_definition_body(member_tag_kind, member_tag_name, member_tag_len, &nested_sz);
        psx_ctx_define_tag_type_with_layout(member_tag_kind, member_tag_name, member_tag_len, nested_n, nested_sz);
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
        if (_ti.is_pointer && _ti.funcptr_param_fp_mask) {
          member_typedef_funcptr_param_fp_mask = _ti.funcptr_param_fp_mask;
        }
        if (_ti.is_pointer && _ti.funcptr_param_int_mask) {
          member_typedef_funcptr_param_int_mask = _ti.funcptr_param_int_mask;
        }
        if (_ti.is_pointer && _ti.funcptr_ret_is_void) {
          member_typedef_funcptr_ret_is_void = 1;
        }
        if (_ti.is_pointer && psx_ret_pointee_array_has_dims(_ti.funcptr_ret_pointee_array)) {
          member_typedef_funcptr_ret_pointee_array = _ti.funcptr_ret_pointee_array;
        }
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
      if (!has_member_name && !(member_tag_kind == TK_STRUCT || member_tag_kind == TK_UNION)
          && curtok()->kind != TK_COLON) {
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
        int storage_size = head.is_ptr ? 8 : (elem_size > 0 ? elem_size : 4);
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
          _mi.type_size = storage_type_size;
          _mi.tag_kind = TK_EOF;
          _mi.bit_width = bit_width;
          _mi.bit_offset = bit_field_offset_in_storage;
          _mi.bit_is_signed = is_signed_type;
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
      int arr_size = psx_parse_member_array_suffixes_ex(&is_flex_array,
                                                        &arr_dim_count, &arr_first_dim,
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
      int ptr_array_pointee_bytes = 0;
      psx_ret_pointee_array_t direct_funcptr_ret_pointee_array = {0};
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
      int member_is_ptr = head.is_ptr || member_is_ptr_typedef;
      int member_elem_size = member_is_ptr ? 8 : elem_size;
      int total_size = is_flex_array ? 0 : (member_elem_size * arr_size);
      int deref_size = member_is_ptr ? elem_size : 0;
      int member_align = member_is_ptr ? 8 : elem_size;
      /* struct/union メンバ (非ポインタ) のアラインメントは「メンバの最大スカラ
       * アラインメント (psx_ctx_get_tag_align)」であり、sizeof ではない。
       * 修正前は elem_size (= sizeof) をそのまま採用しており、
       * `struct Inner {int a, b;}` (sizeof=8, align=4) を含む `struct Outer
       * {struct Inner i; int trail;}` で agg_align=8 となり sizeof(Outer)=16 と 4 バイト
       * 過剰パディング (clang は 12)。 */
      if (!member_is_ptr && (member_tag_kind == TK_STRUCT || member_tag_kind == TK_UNION) &&
          member_tag_name) {
        int tag_align = psx_ctx_get_tag_align(member_tag_kind, member_tag_name, member_tag_len);
        if (tag_align > 0) member_align = tag_align;
      }
      if (member_align <= 0) member_align = 1;
      if (member_align > 8) member_align = 8;
      int pack_align = pragma_pack_current;
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
      if (has_member_name || (member_tag_kind == TK_STRUCT || member_tag_kind == TK_UNION)) {
        tag_member_info_t _mi = {0};
        _mi.name = member_name;
        _mi.len = member_len;
        _mi.offset = off;
        _mi.type_size = member_is_ptr ? 8 : elem_size;
        _mi.deref_size = deref_size;
        _mi.array_len = member_array_len;
        _mi.tag_kind = member_tag_kind;
        _mi.tag_name = member_tag_name;
        _mi.tag_len = member_tag_len;
        _mi.is_tag_pointer = member_is_ptr ? 1 : 0;
        if (member_is_ptr) {
          _mi.funcptr_param_fp_mask = head.has_func_suffix
                                          ? psx_last_funcptr_param_fp_mask()
                                          : member_typedef_funcptr_param_fp_mask;
          _mi.funcptr_param_int_mask = head.has_func_suffix
                                           ? psx_last_funcptr_param_int_mask()
                                           : member_typedef_funcptr_param_int_mask;
          _mi.funcptr_ret_is_void = head.has_func_suffix
                                        ? (member_base_is_void ? 1 : 0)
                                        : member_typedef_funcptr_ret_is_void;
          psx_ret_pointee_array_t ret_pointee_array = psx_ret_pointee_array_select(
              member_typedef_funcptr_ret_pointee_array,
              direct_funcptr_ret_pointee_array);
          _mi.funcptr_ret_pointee_array = ret_pointee_array;
        }
        psx_ctx_add_tag_member(tag_kind, tag_name, tag_len, &_mi);
        /* pointer-to-array メンバ (`int (*p)[N]` / `int (*p)[M][N]`): pointee 全バイトサイズを
         * outer_stride に保存。多次元 pointee の場合は 1 段目 subscript stride も mid_stride に
         * 保存し、build_member_deref_node が deref を multi-dim 配列形に組めるようにする。 */
        if (has_member_name && pointee_arr_size > 0) {
          psx_ctx_set_tag_member_outer_stride(tag_kind, tag_name, tag_len,
                                              member_name, member_len,
                                              pointee_arr_size * elem_size);
          if (pointee_arr_dim_count >= 2 && pointee_arr_first_dim > 0) {
            /* 2D pointee (`int (*p)[M][N]`): 1 段目 subscript stride = (M*N*elem)/M = N*elem */
            int mid = (pointee_arr_size / pointee_arr_first_dim) * elem_size;
            psx_ctx_set_tag_member_mid_stride(tag_kind, tag_name, tag_len,
                                              member_name, member_len, mid);
          }
        }
        /* array-of-pointer-to-array メンバ (`int (*p[M])[N]`): 各要素ポインタが指す配列の
         * 全バイト数 (= N * elem) を保存する。`s.p[i]` の subscript 結果 deref に carry し、
         * `(*s.p[i])[j]` の build_unary_deref_node 経路で要素ストライドに再設定する。 */
        if (has_member_name && ptr_array_pointee_bytes > 0) {
          psx_ctx_set_tag_member_ptr_array_pointee_bytes(tag_kind, tag_name, tag_len,
                                                          member_name, member_len,
                                                          ptr_array_pointee_bytes);
        }
        if (has_member_name && !head.is_ptr && member_fp_kind != TK_FLOAT_KIND_NONE) {
          psx_ctx_set_tag_member_fp_kind(tag_kind, tag_name, tag_len,
                                          member_name, member_len, member_fp_kind);
        }
        /* 関数ポインタメンバ `double (*f)(double)`: 戻り型の fp_kind を保存する。
         * メンバ自体はポインタ (fp_kind は通常 !is_ptr のみ保存) なので、戻り fp を
         * member fp_kind フィールドに載せておき、build_member_deref_node が
         * pointee_fp_kind (= 戻り fp) として deref に伝播 → `s.f(x)` の funcall が
         * 戻り値を d0 で読めるようにする。これがないと戻り値を x0 で読み値が化けた。
         * データポインタメンバ (`double *dp`) は対象外 (has_func_suffix で限定)。 */
        if (has_member_name && head.is_ptr && head.has_func_suffix &&
            member_fp_kind != TK_FLOAT_KIND_NONE) {
          psx_ctx_set_tag_member_fp_kind(tag_kind, tag_name, tag_len,
                                          member_name, member_len, member_fp_kind);
        }
        if (has_member_name && member_is_ptr && member_typedef_funcptr_param_fp_mask &&
            member_fp_kind != TK_FLOAT_KIND_NONE) {
          psx_ctx_set_tag_member_fp_kind(tag_kind, tag_name, tag_len,
                                          member_name, member_len, member_fp_kind);
        }
        if (has_member_name && member_is_ptr) {
          unsigned short fp_mask = head.has_func_suffix
                                       ? psx_last_funcptr_param_fp_mask()
                                       : member_typedef_funcptr_param_fp_mask;
          unsigned short int_mask = head.has_func_suffix
                                        ? psx_last_funcptr_param_int_mask()
                                        : member_typedef_funcptr_param_int_mask;
          if (fp_mask) {
            psx_ctx_set_tag_member_funcptr_param_fp_mask(tag_kind, tag_name, tag_len,
                                                         member_name, member_len, fp_mask);
          }
          if (int_mask) {
            psx_ctx_set_tag_member_funcptr_param_int_mask(tag_kind, tag_name, tag_len,
                                                          member_name, member_len, int_mask);
          }
        }
        if (has_member_name && !head.is_ptr && member_is_bool) {
          psx_ctx_set_tag_member_is_bool(tag_kind, tag_name, tag_len,
                                          member_name, member_len, 1);
        }
        if (has_member_name && !head.is_ptr && member_is_unsigned) {
          psx_ctx_set_tag_member_is_unsigned(tag_kind, tag_name, tag_len,
                                             member_name, member_len, 1);
        }
        /* 多次元配列メンバ (例 int a[2][2]) は最外次元のバイトストライドを保存し、
         * メンバアクセス時に多段 subscript を正しくスケールできるようにする。 */
        if (has_member_name && !head.is_ptr && !is_flex_array &&
            arr_dim_count >= 2 && arr_first_dim > 0) {
          int inner_count = arr_size / arr_first_dim;   /* 第1次元を除く要素数 */
          psx_ctx_set_tag_member_outer_stride(tag_kind, tag_name, tag_len,
                                              member_name, member_len,
                                              inner_count * member_elem_size);
          /* 3 次元以上は中間段ストライド (1 段 subscript 後の要素サイズ) も保存。
           * `char c[2][2][3]` なら arr_dims=[2,2,3]、mid_stride = 3*1 = 3。
           * これがないと build_member_deref_node の inner_deref_size が elem_size の
           * ままで 3 段目 subscript が誤スケール (or SIGSEGV) になっていた。 */
          if (arr_dim_count >= 3 && arr_dims_buf[1] > 0) {
            int inner2 = inner_count / arr_dims_buf[1];  /* 第1+第2次元を除く要素数 */
            psx_ctx_set_tag_member_mid_stride(tag_kind, tag_name, tag_len,
                                              member_name, member_len,
                                              inner2 * member_elem_size);
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
            psx_ctx_set_tag_member_arr_dims(tag_kind, tag_name, tag_len,
                                            member_name, member_len,
                                            arr_dims_buf, arr_dim_count);
          }
        }
        member_count++;
      }
      /* C11 6.7.2.1p13: 匿名 struct/union (タグ名なし、メンバ名なし) の場合、
       * 内側のメンバを外側 tag に直接見えるように昇格 (promote) する。
       * 外側からは `outer.inner_member` の形でアクセスできる。 */
      if (!has_member_name && !head.is_ptr &&
          (member_tag_kind == TK_STRUCT || member_tag_kind == TK_UNION) &&
          member_tag_name) {
        int inner_count = psx_ctx_get_tag_member_count(member_tag_kind, member_tag_name, member_tag_len);
        for (int i = 0; i < inner_count; i++) {
          tag_member_info_t im = {0};
          if (psx_ctx_get_tag_member_info(member_tag_kind, member_tag_name, member_tag_len, i, &im)) {
            if (im.len == 0) continue; /* 匿名同士の連鎖は今回対象外 */
            tag_member_info_t _mi = im;
            _mi.offset = off + im.offset;
            /* C11 6.7.2.1p13: 匿名 struct/union 内 bitfield も昇格メンバとして外側からの
             * アクセスで正しく動くよう、bit_width/bit_offset/bit_is_signed を保持する。
             * 修正前: 旧 non-bf 版の名残でこれらを 0 にクリアしており、`s.lo` (匿名 struct
             * 内 `unsigned lo:16`) が bitfield 抽出を経ず full-width load されて値が化けていた。 */
            psx_ctx_add_tag_member(tag_kind, tag_name, tag_len, &_mi);
            /* 昇格メンバの fp_kind / is_bool / is_unsigned / outer_stride も伝播する。
             * add_tag_member は基本属性のみ渡すため、これがないと匿名 union/struct の
             * float/double メンバ (`struct { union { int n; float f; }; };` の f) が
             * fp_kind を失い整数 load/store され値が化けていた。 */
            if (im.fp_kind != TK_FLOAT_KIND_NONE)
              psx_ctx_set_tag_member_fp_kind(tag_kind, tag_name, tag_len, im.name, im.len, im.fp_kind);
            if (im.is_bool)
              psx_ctx_set_tag_member_is_bool(tag_kind, tag_name, tag_len, im.name, im.len, 1);
            if (im.is_unsigned)
              psx_ctx_set_tag_member_is_unsigned(tag_kind, tag_name, tag_len, im.name, im.len, 1);
            if (im.outer_stride > 0)
              psx_ctx_set_tag_member_outer_stride(tag_kind, tag_name, tag_len, im.name, im.len, im.outer_stride);
            if (im.mid_stride > 0)
              psx_ctx_set_tag_member_mid_stride(tag_kind, tag_name, tag_len, im.name, im.len, im.mid_stride);
            if (im.arr_ndim > 0)
              psx_ctx_set_tag_member_arr_dims(tag_kind, tag_name, tag_len, im.name, im.len,
                                              im.arr_dims, im.arr_ndim);
            if (im.ptr_array_pointee_bytes > 0)
              psx_ctx_set_tag_member_ptr_array_pointee_bytes(tag_kind, tag_name, tag_len,
                                                              im.name, im.len,
                                                              im.ptr_array_pointee_bytes);
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
  /* `_Alignof(struct T)` 用にアラインメント (agg_align) を pending に預ける。
   * 直後の呼び出し元 psx_ctx_define_tag_type_with_layout が tag に書き込む
   * (size とは異なる: 例 `{char;double}` は size 16 / align 8)。 */
  psx_ctx_set_pending_tag_align(agg_align);
  return member_count;
}

#undef ALIGN_UP
