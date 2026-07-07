/*
 * arm64 Apple ABI: 共有インフラとデータセクション出力。
 *
 * ag_c は AST → IR → ASM の経路で関数本体を生成する (arm64_apple_ir.c)。
 * このファイルは IR バックエンドと main.c が共有する以下を提供する:
 *   - cg_emit_mov_imm: ARM64 即値ロードヘルパ
 *   - gen_string_literals / gen_float_literals / gen_global_vars: parser が登録
 *     した文字列 / 浮動小数点定数 / グローバル変数のデータセクション emit
 *
 * 旧 AST 直 codegen (gen / gen_stmt / gen_expr ...) はここから削除済み。
 * Phase 7o で IR 経路が fixture 100% を通過したため、AST 経路は不要になった。
 */

#include "arm64_apple_emit.h"
#include "../codegen_backend.h"
#include "../diag/diag.h"
/* arm64_apple.c は AST node 型を使わない。
 * Phase C2-3: tag_member_info_t / psx_ctx_* は parser_public.h 経由。 */
#include "../parser/parser_public.h"
#include "../tokenizer/literals.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* AArch64 即値ロード: 16bit に収まらない値は movz/movk シーケンス。
 * IR バックエンドからも共有するため非 static。 */
void cg_emit_mov_imm(const char *reg, long long val) {
  uint64_t uval = (uint64_t)val;
  if (val >= 0 && val <= 0xFFFF) {
    cg_emitf("  mov %s, #%lld\n", reg, val);
    return;
  }
  if (val < 0 && val >= -0x10000) {
    cg_emitf("  mov %s, #%lld\n", reg, val);
    return;
  }
  int first = 1;
  for (int shift = 0; shift < 64; shift += 16) {
    uint64_t chunk = (uval >> shift) & 0xFFFF;
    if (chunk == 0 && !first) continue;
    if (first) {
      cg_emitf("  movz %s, #%llu, lsl #%d\n", reg, (unsigned long long)chunk, shift);
      first = 0;
    } else {
      cg_emitf("  movk %s, #%llu, lsl #%d\n", reg, (unsigned long long)chunk, shift);
    }
  }
}

/* ------------------------------------------------------------------ */
/* データセクション (parser が tokenize/parse 中に登録したテーブルを emit) */
/* ------------------------------------------------------------------ */

/* string_literals walk 用の状態 (visitor 経由で渡す)。 */
typedef struct {
  int has_narrow;
  int has_wide;
} string_lit_kind_scan_t;

static void scan_string_lit_kinds(string_lit_t *lit, void *user) {
  string_lit_kind_scan_t *s = user;
  psx_string_lit_view_t view = psx_string_lit_view(lit);
  if (view.char_width == TK_CHAR_WIDTH_CHAR) s->has_narrow = 1;
  else s->has_wide = 1;
}

static void emit_string_literal_asm_byte(unsigned char byte, void *user) {
  (void)user;
  cg_emitf("  .byte %u\n", (unsigned)byte);
}

static void emit_narrow_string_literal(string_lit_t *lit, void *user) {
  (void)user;
  psx_string_lit_view_t view = psx_string_lit_view(lit);
  if (view.char_width != TK_CHAR_WIDTH_CHAR) return;
  cg_emitf("%s:\n", view.label);
  tk_emit_string_literal_bytes(view.str, view.len, (int)view.char_width, true,
                               emit_string_literal_asm_byte, NULL);
}

static void emit_wide_string_literal(string_lit_t *lit, void *user) {
  (void)user;
  psx_string_lit_view_t view = psx_string_lit_view(lit);
  if (view.char_width == TK_CHAR_WIDTH_CHAR) return;
  cg_emitf("%s:\n", view.label);
  tk_emit_string_literal_bytes(view.str, view.len, (int)view.char_width, true,
                               emit_string_literal_asm_byte, NULL);
}

void gen_string_literals(void) {
  /* narrow char 文字列のみ __TEXT,__cstring に置く。
   * u"..." / U"..." / L"..." は内部にゼロバイトを含み得るため __DATA,__const へ。 */
  string_lit_kind_scan_t scan = {0};
  if (!ps_iter_string_literals(scan_string_lit_kinds, &scan)) return;
  if (scan.has_narrow) cg_emitf(".section __TEXT,__cstring\n");
  ps_iter_string_literals(emit_narrow_string_literal, NULL);
  if (scan.has_wide) {
    cg_emitf(".section __DATA,__const\n");
    cg_emitf(".align 2\n");
    ps_iter_string_literals(emit_wide_string_literal, NULL);
  }
  cg_emitf(".text\n");
}

static void emit_one_float_literal(float_lit_t *lit, void *user) {
  (void)user;
  psx_float_lit_view_t view = psx_float_lit_view(lit);
  cg_emitf(".LCF%d:\n", view.id);
  if (view.fp_kind == TK_FLOAT_KIND_FLOAT) {
    union { float f; uint32_t i; } u = { .f = (float)view.fval };
    cg_emitf("  .word %u\n", u.i);
  } else {
    /* note: long double is currently lowered to double. */
    union { double d; uint64_t i; } u = { .d = view.fval };
    cg_emitf("  .quad %llu\n", (unsigned long long)u.i);
  }
}

void gen_float_literals(void) {
  if (!ps_has_float_literals()) return;
  cg_emitf(".section __DATA,__data\n");
  cg_emitf(".align 3\n");
  ps_iter_float_literals(emit_one_float_literal, NULL);
  cg_emitf(".text\n");
}

/* 整数値を指定サイズの assembly directive で出力する。
 *   size=1 → .byte / size=2 → .short / size=4 → .long / その他 → .quad
 * gen_global_vars 系で同形ブロックが 6 箇所重複していたのを集約。 */
static void cg_emit_int_directive(int size, long long value) {
  if (size == 1)      cg_emitf("  .byte %lld\n", value);
  else if (size == 2) cg_emitf("  .short %lld\n", value);
  else if (size == 4) cg_emitf("  .long %lld\n", value);
  else                cg_emitf("  .quad %lld\n", value);
}

/* struct / union global with brace init: 各メンバの型サイズに合わせて
 * init_values[] を出力。メンバ間の padding は .space で埋める。
 * `struct { int x; int y; } p = {10, 32}` → .long 10; .long 32。
 * 配列メンバは alen 個連続出力する。 */
/* グローバル struct/union メンバ 1 個分のスカラ初期値を出力する。
 *   sym_len < 0 : 文字列リテラルラベル (`.quad .LCn`、`_` なし)
 *   sym_len > 0 : グローバル変数/関数シンボル (`.quad _sym`)
 *   float/double: fv を IEEE-754 ビットパターンで出力
 *   それ以外    : 数値 (メンバ型サイズ ts で出力)。 */
static void emit_global_init_member_scalar(char *sym, int sym_len, tk_float_kind_t fp_kind,
                                           int ts, long long v, double fv) {
  if (sym && sym_len < 0) {
    /* 文字列リテラル要素: `.LC<n>` ラベル参照。`"abc" + 2` 由来の +offset があれば併記。 */
    if (v != 0) cg_emitf("  .quad %s+%lld\n", sym, v);
    else        cg_emitf("  .quad %s\n", sym);
  } else if (sym && sym_len > 0) {
    /* `&data[n]` 由来は v にバイトオフセットを持つ (`_data+off`)。 */
    if (v != 0) cg_emitf("  .quad _%.*s+%lld\n", sym_len, sym, v);
    else        cg_emitf("  .quad _%.*s\n", sym_len, sym);
  } else if (fp_kind == TK_FLOAT_KIND_FLOAT) {
    float f = (float)fv;
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    cg_emitf("  .long %u\n", (unsigned)bits);
  } else if (fp_kind >= TK_FLOAT_KIND_DOUBLE) {
    uint64_t bits;
    memcpy(&bits, &fv, sizeof(bits));
    cg_emitf("  .quad %llu\n", (unsigned long long)bits);
  } else {
    cg_emit_int_directive(ts, v);
  }
}

typedef struct {
  global_var_t *gv;
} arm64_global_aggregate_emit_ctx_t;

static void emit_global_walk_padding(void *user, long long offset, int size) {
  (void)user;
  (void)offset;
  if (size > 0) cg_emitf("  .space %d\n", size);
}

static void emit_global_walk_scalar(void *user, const tag_member_info_t *mi,
                                    int slot_idx, long long offset) {
  (void)offset;
  arm64_global_aggregate_emit_ctx_t *ctx = user;
  psx_gvar_init_slot_t slot = psx_gvar_init_slot_view(ctx->gv, slot_idx);
  long long value = slot.value;
  if (mi->is_bool) value = value != 0;
  tk_float_kind_t fp_kind = mi->fp_kind;
  int size = mi->type_size;
  int sym_len = slot.symbol_len;
  if (!slot.symbol && slot.fp_sentinel_kind != TK_FLOAT_KIND_NONE) {
    fp_kind = slot.fp_sentinel_kind;
    size = fp_kind >= TK_FLOAT_KIND_DOUBLE ? 8 : 4;
    sym_len = 0;
  }
  emit_global_init_member_scalar(slot.symbol, sym_len, fp_kind,
                                 size, value, slot.fvalue);
}

static void emit_global_walk_bitfield_unit(void *user,
                                           const psx_gvar_bitfield_unit_t *unit,
                                           long long base_offset) {
  (void)user;
  (void)base_offset;
  cg_emit_int_directive(unit->size, (long long)unit->packed);
}

static void emit_global_walk_bitfield_member(void *user, const tag_member_info_t *mi,
                                             int slot_idx, long long base_offset) {
  (void)base_offset;
  arm64_global_aggregate_emit_ctx_t *ctx = user;
  unsigned long long packed = psx_gvar_init_slot_bitfield_bits(ctx->gv, slot_idx,
                                                               mi->bit_width,
                                                               mi->bit_offset);
  cg_emit_int_directive(mi->type_size, (long long)packed);
}

static const psx_gvar_aggregate_walk_ops_t arm64_global_aggregate_walk_ops = {
    .scalar = emit_global_walk_scalar,
    .bitfield_unit = emit_global_walk_bitfield_unit,
    .bitfield_member = emit_global_walk_bitfield_member,
    .padding = emit_global_walk_padding,
};

static void emit_global_aggregate_init(global_var_t *gv) {
  arm64_global_aggregate_emit_ctx_t ctx = {.gv = gv};
  psx_gvar_walk_aggregate_initializer(gv, 0, &arm64_global_aggregate_walk_ops, &ctx);
}

/* gen_global_vars の本体: 1 つの global_var_t を assembly directive に
 * 落とす visitor 関数 (Phase C3-2 で ps_iter_globals に切替)。 */
static void emit_one_global_var(global_var_t *gv, void *user) {
  (void)user;
  psx_gvar_view_t view = psx_gvar_view(gv);
  if (view.is_extern_decl) return;
  int storage_size = psx_gvar_storage_size(gv, 4);
  if (view.is_thread_local) {
    /* _Thread_local: TLV descriptor + thread data/bss */
    if (view.has_init) {
      cg_emitf(".section __DATA,__thread_data\n");
      cg_emitf("_%.*s$tlv$init:\n", view.name_len, view.name);
      cg_emit_int_directive(storage_size, view.init_val);
    } else {
      cg_emitf(".section __DATA,__thread_bss\n");
      cg_emitf("_%.*s$tlv$init:\n", view.name_len, view.name);
      cg_emitf("  .space %d\n", storage_size);
    }
    cg_emitf(".section __DATA,__thread_vars,thread_local_variables\n");
    if (!view.is_static) cg_emitf(".global _%.*s\n", view.name_len, view.name);
    cg_emitf("_%.*s:\n", view.name_len, view.name);
    cg_emitf("  .quad __tlv_bootstrap\n");
    cg_emitf("  .quad 0\n");
    cg_emitf("  .quad _%.*s$tlv$init\n", view.name_len, view.name);
    return;
  }
  if (view.has_init) {
    cg_emitf(".section __DATA,__data\n");
    /* static (内部リンケージ) は .global を出さない (C11 6.2.2p3)。 */
    if (!view.is_static) cg_emitf(".global _%.*s\n", view.name_len, view.name);
    int align_size = psx_gvar_initializer_element_size(gv, storage_size);
    int align = (align_size >= 8) ? 3 : (align_size >= 4) ? 2 : (align_size >= 2) ? 1 : 0;
    cg_emitf(".align %d\n", align);
    cg_emitf("_%.*s:\n", view.name_len, view.name);
    if (view.init_count > 0 && psx_gvar_is_tag_aggregate(gv)) {
      emit_global_aggregate_init(gv);
    } else if (view.init_count > 0) {
      int elem = psx_gvar_initializer_element_size(gv, 4);
      int total_elems = psx_gvar_initializer_element_count(gv, 4);
      int is_fp_arr = view.has_init_fvalues &&
                      (view.fp_kind == TK_FLOAT_KIND_FLOAT ||
                       view.fp_kind == TK_FLOAT_KIND_DOUBLE ||
                       view.fp_kind == TK_FLOAT_KIND_LONG_DOUBLE);
      for (int i = 0; i < view.init_count && i < total_elems; i++) {
        psx_gvar_init_slot_t slot = psx_gvar_init_slot_view(gv, i);
        if (slot.symbol && slot.symbol_len < 0) {
          /* 文字列リテラル要素: `.LC<n>` ラベルをそのまま参照 (アンダースコアなし)。
           * `const char *arr[] = {"abc" + 2, ...}` のような +offset は init_values[i] に
           * バイトオフセットが入っているので併記する。 */
          if (slot.value != 0) cg_emitf("  .quad %s+%lld\n", slot.symbol, slot.value);
          else                 cg_emitf("  .quad %s\n", slot.symbol);
          continue;
        }
        if (slot.symbol && slot.symbol_len > 0) {
          /* `&data[n]` 由来のシンボル+オフセット。init_values[i] にバイトオフセット。 */
          if (slot.value != 0) {
            cg_emitf("  .quad _%.*s+%lld\n", slot.symbol_len, slot.symbol, slot.value);
          } else {
            cg_emitf("  .quad _%.*s\n", slot.symbol_len, slot.symbol);
          }
          continue;
        }
        if (is_fp_arr) {
          /* 浮動小数配列要素: fvalues[i] を IEEE-754 ビットパターンで出力。 */
          double d = slot.fvalue;
          if (view.fp_kind == TK_FLOAT_KIND_FLOAT) {
            float f = (float)d;
            uint32_t bits;
            memcpy(&bits, &f, sizeof(bits));
            cg_emitf("  .long %u\n", (unsigned)bits);
          } else {
            uint64_t bits;
            memcpy(&bits, &d, sizeof(bits));
            cg_emitf("  .quad %llu\n", (unsigned long long)bits);
          }
          continue;
        }
        cg_emit_int_directive(elem, slot.value);
      }
      int remain = total_elems - view.init_count;
      if (remain > 0) cg_emitf("  .space %d\n", remain * elem);
    } else if (view.init_symbol) {
      if (view.init_symbol_len < 0) {
        /* sentinel: 文字列リテラル `.LCn` のラベル — `_` プレフィックスなしで出力。
         * `const char *p = "abc" + 2;` のように +offset がある場合はラベル + offset を出す。 */
        if (view.init_symbol_offset != 0) {
          cg_emitf("  .quad %s + %lld\n", view.init_symbol, view.init_symbol_offset);
        } else {
          cg_emitf("  .quad %s\n", view.init_symbol);
        }
      } else if (view.init_symbol_offset != 0) {
        /* `&a[1]` / `a+1`: シンボル + バイトオフセット。 */
        cg_emitf("  .quad _%.*s + %lld\n", view.init_symbol_len, view.init_symbol,
                 view.init_symbol_offset);
      } else {
        cg_emitf("  .quad _%.*s\n", view.init_symbol_len, view.init_symbol);
      }
    } else if (view.fp_kind == TK_FLOAT_KIND_FLOAT) {
      /* float スカラ: fval を 32bit IEEE-754 ビットパターンで出力する。 */
      float f = (float)view.fval;
      uint32_t bits;
      memcpy(&bits, &f, sizeof(bits));
      cg_emitf("  .long %u\n", (unsigned)bits);
    } else if (view.fp_kind == TK_FLOAT_KIND_DOUBLE ||
               view.fp_kind == TK_FLOAT_KIND_LONG_DOUBLE) {
      /* double スカラ: fval を 64bit IEEE-754 ビットパターンで出力する。 */
      double d = view.fval;
      uint64_t bits;
      memcpy(&bits, &d, sizeof(bits));
      cg_emitf("  .quad %llu\n", (unsigned long long)bits);
    } else {
      cg_emit_int_directive(storage_size, view.init_val);
    }
    return;
  }
  /* 暫定定義: .comm _name,size,log2align。
   * ただし static (内部リンケージ) は .comm (= common/外部シンボル) にすると別 TU の
   * 同名 static と共有/衝突するため、ローカルな .zerofill (__bss) に出す。 */
  int log2align = (storage_size >= 8) ? 3 : (storage_size >= 4) ? 2 : (storage_size >= 2) ? 1 : 0;
  if (view.is_static) {
    cg_emitf(".zerofill __DATA,__bss,_%.*s,%d,%d\n", view.name_len, view.name, storage_size,
             log2align);
  } else {
    cg_emitf(".comm _%.*s,%d,%d\n", view.name_len, view.name, storage_size, log2align);
  }
}

void gen_global_vars(void) {
  ps_iter_globals(emit_one_global_var, NULL);
}
