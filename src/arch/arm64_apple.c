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

/* ネスト union の 1 slot を出力する。union は active メンバだけをその型で emit し、
 * 残りは union 全体サイズまで 0 padding する必要がある。 */
static void emit_global_struct_members_rec(token_kind_t tk, char *tn, int tl,
                                           int struct_size, global_var_t *gv,
                                           psx_gvar_init_cursor_t *cur);

static void emit_global_union_slot(token_kind_t tk, char *tn, int tl, int union_size,
                                   global_var_t *gv, psx_gvar_init_cursor_t *cur) {
  if (!psx_gvar_init_cursor_has(cur)) {
    cg_emitf("  .space %d\n", union_size);
    return;
  }
  int start_idx = psx_gvar_init_cursor_index(cur);
  tag_member_info_t mi = {0};
  if (!psx_tag_union_init_member_for_slot(tk, tn, tl, gv,
                                          psx_gvar_init_cursor_index(cur), &mi)) {
    cg_emitf("  .space %d\n", union_size);
    return;
  }
  if (psx_tag_member_is_tag_aggregate(&mi)) {
    if (mi.offset > 0) cg_emitf("  .space %d\n", mi.offset);
    if (mi.array_len > 0) {
      for (int k = 0; k < mi.array_len && psx_gvar_init_cursor_has(cur); k++) {
        if (psx_tag_member_is_struct_aggregate(&mi)) {
          emit_global_struct_members_rec(mi.tag_kind, mi.tag_name, mi.tag_len, mi.type_size,
                                         gv, cur);
        } else {
          emit_global_union_slot(mi.tag_kind, mi.tag_name, mi.tag_len, mi.type_size, gv, cur);
        }
      }
    } else if (psx_tag_member_is_struct_aggregate(&mi)) {
      emit_global_struct_members_rec(mi.tag_kind, mi.tag_name, mi.tag_len, mi.type_size,
                                     gv, cur);
    } else {
      emit_global_union_slot(mi.tag_kind, mi.tag_name, mi.tag_len, mi.type_size, gv, cur);
    }
    int emitted = mi.offset + (mi.array_len > 0 ? mi.type_size * mi.array_len : mi.type_size);
    if (emitted < union_size) cg_emitf("  .space %d\n", union_size - emitted);
    if (psx_gvar_init_cursor_index(cur) == start_idx) psx_gvar_init_cursor_advance(cur);
    return;
  }
  int init_idx = psx_gvar_init_cursor_index(cur);
  psx_gvar_init_slot_t slot = psx_gvar_init_slot_view(gv, init_idx);
  char *sym = slot.symbol;
  int sym_len = slot.symbol_len;
  double fv = slot.fvalue;
  long long iv = slot.value;
  psx_gvar_init_cursor_advance(cur);
  tk_float_kind_t use_fp = TK_FLOAT_KIND_NONE;
  int use_size = union_size;
  /* sentinel チェック (sym==NULL のときのみ意味を持つ; sym!=NULL は文字列/関数ポインタ) */
  use_fp = slot.fp_sentinel_kind;
  if (sym == NULL && use_fp != TK_FLOAT_KIND_NONE) {
    use_size = use_fp >= TK_FLOAT_KIND_DOUBLE ? 8 : 4;
    sym_len = 0;  /* emit_global_init_member_scalar には通常の 0 を渡す */
  } else if (fv != 0.0 && iv == 0) {
    int inner_n = psx_ctx_get_tag_member_count(tk, tn, tl);
    for (int j = 0; j < inner_n; j++) {
      tag_member_info_t imi = {0};
      if (psx_ctx_get_tag_member_info(tk, tn, tl, j, &imi) &&
          imi.fp_kind != TK_FLOAT_KIND_NONE) {
        use_fp = imi.fp_kind;
        use_size = imi.type_size;
        break;
      }
    }
  }
  tag_member_info_t active = {0};
  if (use_fp == TK_FLOAT_KIND_NONE &&
      psx_ctx_get_tag_member_info(tk, tn, tl, 0, &active) &&
      active.array_len > 0 &&
      !psx_tag_member_is_tag_aggregate(&active)) {
    int emitted = 0;
    int elem_size = active.type_size;
    emit_global_init_member_scalar(sym, sym_len, active.fp_kind, elem_size, iv, fv);
    emitted += elem_size;
    for (int k = 1; k < active.array_len && emitted + elem_size <= union_size; k++) {
      psx_gvar_init_slot_t elem_slot = psx_gvar_init_cursor_slot(cur);
      if (elem_slot.in_range) psx_gvar_init_cursor_advance(cur);
      emit_global_init_member_scalar(elem_slot.symbol, elem_slot.symbol_len,
                                     active.fp_kind, elem_size,
                                     elem_slot.value, elem_slot.fvalue);
      emitted += elem_size;
    }
    if (emitted < union_size) cg_emitf("  .space %d\n", union_size - emitted);
    return;
  }
  emit_global_init_member_scalar(sym, sym_len, use_fp, use_size, iv, fv);
  int emitted = sym ? 8
                    : (use_fp == TK_FLOAT_KIND_FLOAT) ? 4
                    : (use_fp >= TK_FLOAT_KIND_DOUBLE) ? 8
                    : (use_size <= 8 ? use_size : 8);
  if (emitted < union_size) cg_emitf("  .space %d\n", union_size - emitted);
}

/* struct のメンバを宣言順にフラット出力する (init_values を cursor から消費)。
 * 入れ子 struct メンバ (`struct Out{struct In i; int z;}`) は再帰して内側メンバを
 * 展開する。これをしないと内側 struct を 1 つの .quad として出力し、フラット値が
 * ずれていた (`{1,2,3}` が i.p=1,i.q=0,z=2 になる)。
 * struct_size まで末尾を 0 padding する。 */
static void emit_global_struct_members_rec(token_kind_t tk, char *tn, int tl,
                                           int struct_size, global_var_t *gv,
                                           psx_gvar_init_cursor_t *cur) {
  int n_members = psx_ctx_get_tag_member_count(tk, tn, tl);
  int prev_end = 0;
  psx_tag_flat_cover_state_t cover_state;
  psx_tag_flat_cover_state_init(&cover_state);
  for (int i = 0; i < n_members && psx_gvar_init_cursor_has(cur); i++) {
    tag_member_info_t mi = {0};
    if (!psx_ctx_get_tag_member_info(tk, tn, tl, i, &mi)) break;
    int off = mi.offset, ts = mi.type_size, alen = mi.array_len;
    if (psx_tag_member_is_unnamed_struct(&mi)) continue;
    if (psx_tag_flat_cover_state_covers(&cover_state, &mi)) continue;
    if (off > prev_end) cg_emitf("  .space %d\n", off - prev_end);
    /* 配列メンバ (`int values[3]`): alen 個の要素を連続出力。 */
    if (alen > 0) {
      if (psx_tag_member_is_struct_aggregate(&mi)) {
        /* struct 配列メンバ (`struct P pts[2]`): 各要素を再帰してメンバ単位で出力する。
         * これをしないと要素 1 つを ts バイトのスカラとして出力し、
         * `{{10,20},{30,40}}` が .quad 10/.quad 20 と化けていた。 */
        for (int k = 0; k < alen; k++) {
          emit_global_struct_members_rec(mi.tag_kind, mi.tag_name, mi.tag_len, ts, gv, cur);
        }
      } else if (psx_tag_member_is_union_aggregate(&mi)) {
        for (int k = 0; k < alen; k++) {
          emit_global_union_slot(mi.tag_kind, mi.tag_name, mi.tag_len, ts, gv, cur);
        }
      } else {
        for (int k = 0; k < alen && psx_gvar_init_cursor_has(cur); k++) {
          psx_gvar_init_slot_t slot = psx_gvar_init_cursor_slot(cur);
          long long ev = slot.value;
          psx_gvar_init_cursor_advance(cur);
          if (mi.is_bool) ev = (ev != 0);  /* C11 6.3.1.2: _Bool 配列メンバは 0/1 に正規化 */
          /* ポインタ配列メンバ (シンボル+オフセット要素や文字列/関数ポインタ要素) も
           * fp 配列メンバ (`struct R{double m[2][2];}`) も emit_global_init_member_scalar
           * 経由で統一処理する。直接 cg_emit_int_directive を呼ぶと fp_kind を見落として
           * double 値が `.quad 0` として出力されていた (efv を捨てて ev=0 を整数で書く)。 */
          emit_global_init_member_scalar(slot.symbol, slot.symbol_len, mi.fp_kind,
                                         ts, ev, slot.fvalue);
        }
      }
      prev_end = off + ts * alen;
      psx_tag_flat_cover_state_note(&cover_state, tk, tn, tl, &mi);
      continue;
    }
    /* 入れ子 struct メンバ: 再帰してフラット展開する。 */
    if (psx_tag_member_is_struct_aggregate(&mi)) {
      emit_global_struct_members_rec(mi.tag_kind, mi.tag_name, mi.tag_len, ts, gv, cur);
      prev_end = off + ts;
      psx_tag_flat_cover_state_note(&cover_state, tk, tn, tl, &mi);
      continue;
    }
    /* ネスト union メンバ: トップレベル global_var_t.union_init_ordinal はネスト union 用では
     * ないため、active メンバの fp_kind/type_size をここで判定する。
     * 最優先: parser が立てた sentinel (sym==NULL && sym_len==-2 (float) / -3 (double))。
     *   `.f = 0.0f` でも明示通知されるため、ヒューリスティックの曖昧さがない。
     * フォールバック (sentinel 無し): 旧ヒューリスティック (fv!=0 && iv==0) で内側 fp メンバ
     *   を探す。これは「sentinel が立たない経路」(parser がまだ対応していない形) 用の保険。 */
    if (psx_tag_member_is_union_aggregate(&mi)) {
      emit_global_union_slot(mi.tag_kind, mi.tag_name, mi.tag_len, ts, gv, cur);
      psx_tag_flat_cover_state_note(&cover_state, tk, tn, tl, &mi);
      prev_end = off + ts;
      continue;
    }
    /* ビットフィールド: 同じ storage ユニット (同一 offset) に属する連続ビット
     * フィールドを 1 つの整数に詰めて出力する。各メンバを別々の .long で出すと
     * `{3,5}` が `.long 3 / .long 5` (8B) になり値が壊れていた (正しくは 1B 0x53)。 */
    if (mi.bit_width > 0) {
      psx_gvar_bitfield_unit_t unit = {0};
      if (!psx_gvar_init_cursor_pack_bitfield_unit(tk, tn, tl, i, cur, &unit)) break;
      i = unit.last_member_index;
      cg_emit_int_directive(unit.size, (long long)unit.packed);
      prev_end = unit.offset + unit.size;
      continue;
    }
    /* スカラ / ポインタ / 関数ポインタメンバ。 */
    psx_gvar_init_slot_t slot = psx_gvar_init_cursor_slot(cur);
    long long mv = slot.value;
    if (mi.is_bool) mv = (mv != 0);  /* C11 6.3.1.2: _Bool スカラメンバは 0/1 に正規化 */
    emit_global_init_member_scalar(slot.symbol, slot.symbol_len, mi.fp_kind,
                                   ts, mv, slot.fvalue);
    psx_gvar_init_cursor_advance(cur);
    prev_end = off + ts;
    psx_tag_flat_cover_state_note(&cover_state, tk, tn, tl, &mi);
  }
  if (prev_end < struct_size) cg_emitf("  .space %d\n", struct_size - prev_end);
}

static void emit_global_struct_init(global_var_t *gv) {
  psx_gvar_view_t view = psx_gvar_view(gv);
  psx_gvar_aggregate_layout_t layout = psx_gvar_aggregate_layout(gv);
  /* union: 活性メンバ (union_init_ordinal, 既定 0=先頭) だけをその型で出力し、
   * 残りを type_size まで 0 で埋める。`{.f=1.5f}` 等の designated 初期化に対応。 */
  if (layout.is_union) {
    tag_member_info_t mi = {0};
    if (view.init_count > 0 &&
        psx_tag_union_init_member_for_slot(layout.tag_kind, layout.tag_name,
                                           layout.tag_len, gv, 0, &mi)) {
      psx_gvar_init_slot_t slot = psx_gvar_init_slot_view(gv, 0);
      emit_global_init_member_scalar(slot.symbol, slot.symbol_len, mi.fp_kind,
                                     mi.type_size, slot.value, slot.fvalue);
      int emitted = slot.symbol ? 8
                        : (mi.fp_kind == TK_FLOAT_KIND_FLOAT) ? 4
                        : (mi.fp_kind >= TK_FLOAT_KIND_DOUBLE) ? 8
                        : mi.type_size;
      if (emitted < layout.type_size) {
        cg_emitf("  .space %d\n", layout.type_size - emitted);
      }
    } else {
      cg_emitf("  .space %d\n", layout.type_size);
    }
    return;
  }
  psx_gvar_init_cursor_t cur = psx_gvar_init_cursor(gv);
  emit_global_struct_members_rec(layout.tag_kind, layout.tag_name, layout.tag_len,
                                 layout.type_size, gv, &cur);
}

/* struct/union 配列のグローバル brace init: 各要素を member 毎に
 * その型サイズで出力する。`struct {int x; int y;} a[3] = {{1,2},...}`
 * は .long 1; .long 2; .long 3; ... と展開する。 */
static void emit_global_struct_array_init(global_var_t *gv) {
  psx_gvar_aggregate_layout_t layout = psx_gvar_aggregate_layout(gv);
  psx_gvar_init_cursor_t cur = psx_gvar_init_cursor(gv);
  /* 各要素を emit_global_struct_members_rec でメンバ単位に展開する。以前はメンバごとに
   * フラット slot を 1 個だけ消費する単純ループだったため、配列メンバ (`char tag[4]`)・
   * char 配列の文字列展開・入れ子 struct メンバ・bitfield を扱えず、`struct{char tag[4];
   * int n;} g[2]={{"aa",1},...}` の tag が 1 バイトしか出ず後続メンバがずれていた。
   * 非配列 struct の出力 (emit_global_struct_init) と同じ機構を要素ごとに適用して統一する。 */
  for (int e = 0; e < layout.elem_count; e++) {
    emit_global_struct_members_rec(layout.tag_kind, layout.tag_name, layout.tag_len,
                                   layout.elem_size, gv, &cur);
  }
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
    if (view.init_count > 0 && psx_gvar_is_tag_aggregate(gv) && !view.is_array) {
      emit_global_struct_init(gv);
    } else if (view.init_count > 0 && view.is_array && psx_gvar_is_tag_aggregate(gv)) {
      emit_global_struct_array_init(gv);
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
