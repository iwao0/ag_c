#include "internal/struct_layout.h"
#include "internal/alignas_value.h"
#include "internal/anon_tag.h"
#include "internal/array_suffixes.h"
#include "internal/core.h"
#include "internal/diag.h"
#include "internal/enum_const.h"
#include "internal/semantic_ctx.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include "../pragma_pack.h"

static inline token_t *curtok(void) { return tk_get_current_token(); }
static inline void set_curtok(token_t *tok) { tk_set_current_token(tok); }

#define ALIGN_UP(v, a) (((v) + ((a) - 1)) / (a) * (a))

static token_ident_t *parse_member_decl_name_recursive(int *is_ptr, int *out_has_func_suffix,
                                                       int *out_paren_array_mul) {
  psx_consume_pointer_prefix(is_ptr);
  token_ident_t *name = NULL;
  int paren_array_mul = 1;
  if (tk_consume('(')) {
    name = parse_member_decl_name_recursive(is_ptr, out_has_func_suffix, &paren_array_mul);
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
  out.member = parse_member_decl_name_recursive(&out.is_ptr, &out.has_func_suffix, &out.paren_array_mul);
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
    if (psx_ctx_is_type_token(curtok()->kind)) {
      is_signed_type = (curtok()->kind != TK_UNSIGNED);
      psx_ctx_get_type_info(curtok()->kind, NULL, &elem_size);
      if (curtok()->kind == TK_FLOAT) member_fp_kind = TK_FLOAT_KIND_FLOAT;
      else if (curtok()->kind == TK_DOUBLE) member_fp_kind = TK_FLOAT_KIND_DOUBLE;
      else if (curtok()->kind == TK_BOOL) member_is_bool = 1;
      set_curtok(curtok()->next);
      while (psx_ctx_is_type_token(curtok()->kind)) {
        if (curtok()->kind != TK_UNSIGNED && curtok()->kind != TK_SIGNED) {
          psx_ctx_get_type_info(curtok()->kind, NULL, &elem_size);
        }
        if (curtok()->kind == TK_FLOAT) member_fp_kind = TK_FLOAT_KIND_FLOAT;
        else if (curtok()->kind == TK_DOUBLE) member_fp_kind = TK_FLOAT_KIND_DOUBLE;
        else if (curtok()->kind == TK_BOOL) member_is_bool = 1;
        set_curtok(curtok()->next);
      }
    } else if (psx_ctx_is_tag_keyword(curtok()->kind)) {
      member_tag_kind = curtok()->kind;
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
      token_kind_t td_base = TK_EOF;
      int td_elem = 0;
      tk_float_kind_t td_fp = TK_FLOAT_KIND_NONE;
      token_kind_t td_tag = TK_EOF;
      char *td_tn = NULL;
      int td_tl = 0;
      int td_isptr = 0, td_pcq = 0, td_pvq = 0, td_isu = 0;
      psx_ctx_find_typedef_name(td->str, td->len, &td_base, &td_elem, &td_fp,
                                 &td_tag, &td_tn, &td_tl, &td_isptr,
                                 &td_pcq, &td_pvq, &td_isu);
      if (td_tag != TK_EOF) {
        member_tag_kind = td_tag;
        member_tag_name = td_tn;
        member_tag_len = td_tl;
      }
      if (td_elem > 0) elem_size = td_elem;
      else if (td_tag != TK_EOF && psx_ctx_has_tag_type(td_tag, td_tn, td_tl)) {
        elem_size = psx_ctx_get_tag_size(td_tag, td_tn, td_tl);
      }
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
            current_off = ALIGN_UP(current_off, storage_size);
            bf_storage_offset = current_off;
            bf_storage_type_size = storage_size;
            bf_bits_used = 0;
            current_off += storage_size;
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
          psx_ctx_add_tag_member_bf(tag_kind, tag_name, tag_len,
                                    head.member->str, head.member->len,
                                    tag_kind == TK_UNION ? 0 : bf_storage_offset,
                                    storage_type_size, 0, 0,
                                    TK_EOF, NULL, 0, 0,
                                    bit_width, bit_field_offset_in_storage, is_signed_type);
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
      int arr_size = psx_parse_member_array_suffixes(&is_flex_array,
                                                     &arr_dim_count, &arr_first_dim);
      if (head.paren_array_mul > 1) arr_size *= head.paren_array_mul;
      int member_elem_size = head.is_ptr ? 8 : elem_size;
      int total_size = is_flex_array ? 0 : (member_elem_size * arr_size);
      int deref_size = head.is_ptr ? elem_size : 0;
      int member_align = head.is_ptr ? 8 : elem_size;
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
      int member_array_len = (arr_size <= 1) ? 0 : arr_size;
      if (has_member_name || (member_tag_kind == TK_STRUCT || member_tag_kind == TK_UNION)) {
        psx_ctx_add_tag_member(tag_kind, tag_name, tag_len,
                               member_name, member_len, off, head.is_ptr ? 8 : elem_size, deref_size,
                               member_array_len,
                               member_tag_kind, member_tag_name, member_tag_len, head.is_ptr ? 1 : 0);
        if (has_member_name && !head.is_ptr && member_fp_kind != TK_FLOAT_KIND_NONE) {
          psx_ctx_set_tag_member_fp_kind(tag_kind, tag_name, tag_len,
                                          member_name, member_len, member_fp_kind);
        }
        if (has_member_name && !head.is_ptr && member_is_bool) {
          psx_ctx_set_tag_member_is_bool(tag_kind, tag_name, tag_len,
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
            psx_ctx_add_tag_member(tag_kind, tag_name, tag_len,
                                   im.name, im.len, off + im.offset,
                                   im.type_size, im.deref_size, im.array_len,
                                   im.tag_kind, im.tag_name, im.tag_len, im.is_tag_pointer);
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
  return member_count;
}

#undef ALIGN_UP
