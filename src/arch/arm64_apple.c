/*
 * arm64 Apple ABI: 共有インフラとデータセクション出力。
 *
 * ag_c は AST → IR → ASM の経路で関数本体を生成する (arm64_apple_ir.c)。
 * このファイルは IR バックエンドと main.c が共有する以下を提供する:
 *   - cg_emitf / cg_emit_mov_imm: 低レベル emit ヘルパ (str/ldr 連を peephole で
 *     潰すバッファ付き)
 *   - gen_set_output_callback: stdout 以外への出力切り替え
 *   - gen_string_literals / gen_float_literals / gen_global_vars: parser が登録
 *     した文字列 / 浮動小数点定数 / グローバル変数のデータセクション emit
 *
 * 旧 AST 直 codegen (gen / gen_stmt / gen_expr ...) はここから削除済み。
 * Phase 7o で IR 経路が fixture 100% を通過したため、AST 経路は不要になった。
 */

#include "../codegen_backend.h"
#include "../diag/diag.h"
/* arm64_apple.c は AST node 型を使わない。
 * Phase C1-3: parser.h ではなくシンボルテーブル (symtab.h) を直接 include。
 * Phase C2-3: tag_member_info_t / psx_ctx_* は parser_public.h 経由に切替。 */
#include "../parser/symtab.h"
#include "../parser/parser_public.h"
#include "../tokenizer/escape.h"
#include "../tokenizer/literals.h"
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static gen_output_line_fn gen_output_cb;
static void *gen_output_user_data;

/* ピープホール最適化: 直前の出力行をバッファして、
 *   str x0, [sp, #-16]!  +  ldr x0, [sp], #16  → 両方除去
 *   str x0, [sp, #-16]!  +  ldr x1, [sp], #16  → mov x1, x0
 * のような自明な push/pop ペアを潰す。IR 経路でもこの 2 パターンは出るので
 * 残しておく価値がある。 */
#define PEEPHOLE_BUF_SIZE 128
static char peephole_buf[PEEPHOLE_BUF_SIZE];
static size_t peephole_len = 0;
static int peephole_has_line = 0;

static const char STR_X0_PUSH[] = "  str x0, [sp, #-16]!\n";
static const char LDR_X0_POP[]  = "  ldr x0, [sp], #16\n";
static const char LDR_X1_POP[]  = "  ldr x1, [sp], #16\n";
static const char MOV_X1_X0[]   = "  mov x1, x0\n";

static void cg_raw_emit(const char *line, size_t len) {
  if (gen_output_cb) {
    gen_output_cb(line, len, gen_output_user_data);
  } else {
    fwrite(line, 1, len, stdout);
  }
}

static void cg_flush_peephole(void) {
  if (!peephole_has_line) return;
  cg_raw_emit(peephole_buf, peephole_len);
  peephole_has_line = 0;
  peephole_len = 0;
}

static void cg_emit_line(const char *line, size_t len) {
  if (peephole_has_line
      && peephole_len == sizeof(STR_X0_PUSH) - 1
      && memcmp(peephole_buf, STR_X0_PUSH, peephole_len) == 0) {
    if (len == sizeof(LDR_X0_POP) - 1
        && memcmp(line, LDR_X0_POP, len) == 0) {
      peephole_has_line = 0;
      peephole_len = 0;
      return;
    }
    if (len == sizeof(LDR_X1_POP) - 1
        && memcmp(line, LDR_X1_POP, len) == 0) {
      peephole_has_line = 0;
      peephole_len = 0;
      cg_raw_emit(MOV_X1_X0, sizeof(MOV_X1_X0) - 1);
      return;
    }
  }
  cg_flush_peephole();
  if (len < PEEPHOLE_BUF_SIZE) {
    memcpy(peephole_buf, line, len);
    peephole_len = len;
    peephole_has_line = 1;
  } else {
    cg_raw_emit(line, len);
  }
}

/* IR バックエンド (arm64_apple_ir.c) からも呼ばれるため非 static。 */
void cg_emitf(const char *fmt, ...) {
  char stack_buf[256];
  va_list ap;
  va_start(ap, fmt);
  va_list ap2;
  va_copy(ap2, ap);
  int need_i = vsnprintf(stack_buf, sizeof(stack_buf), fmt, ap2);
  va_end(ap2);
  if (need_i < 0) {
    va_end(ap);
    diag_emit_internalf(DIAG_ERR_CODEGEN_OUTPUT_FAILED, "%s",
                        diag_message_for(DIAG_ERR_CODEGEN_OUTPUT_FAILED));
  }
  size_t need = (size_t)need_i;
  char *buf = stack_buf;
  char *heap_buf = NULL;
  if (need >= sizeof(stack_buf)) {
    heap_buf = malloc(need + 1);
    if (!heap_buf) {
      va_end(ap);
      diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
    }
    vsnprintf(heap_buf, need + 1, fmt, ap);
    buf = heap_buf;
  }
  va_end(ap);
  size_t start = 0;
  for (size_t i = 0; i < need; i++) {
    if (buf[i] == '\n') {
      cg_emit_line(buf + start, i - start + 1);
      start = i + 1;
    }
  }
  if (start < need) {
    cg_emit_line(buf + start, need - start);
  }
  free(heap_buf);
}

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

void gen_set_output_callback(gen_output_line_fn cb, void *user_data) {
  cg_flush_peephole();
  gen_output_cb = cb;
  gen_output_user_data = user_data;
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
  if (lit->char_width == TK_CHAR_WIDTH_CHAR) s->has_narrow = 1;
  else s->has_wide = 1;
}

static void emit_narrow_string_literal(string_lit_t *lit, void *user) {
  (void)user;
  if (lit->char_width != TK_CHAR_WIDTH_CHAR) return;
  cg_emitf("%s:\n", lit->label);
  int i = 0;
  while (i < lit->len) {
    uint32_t v = 0;
    if (lit->str[i] == '\\') {
      tk_parse_escape_value(lit->str, lit->len, &i, &v);
    } else {
      v = (unsigned char)lit->str[i];
      i++;
    }
    /* codepoint を UTF-8 エンコード。 */
    if (v < 0x80) {
      cg_emitf("  .byte %u\n", (unsigned)v);
    } else if (v < 0x800) {
      cg_emitf("  .byte %u\n", (unsigned)(0xC0 | (v >> 6)));
      cg_emitf("  .byte %u\n", (unsigned)(0x80 | (v & 0x3F)));
    } else if (v < 0x10000) {
      cg_emitf("  .byte %u\n", (unsigned)(0xE0 | (v >> 12)));
      cg_emitf("  .byte %u\n", (unsigned)(0x80 | ((v >> 6) & 0x3F)));
      cg_emitf("  .byte %u\n", (unsigned)(0x80 | (v & 0x3F)));
    } else {
      cg_emitf("  .byte %u\n", (unsigned)(0xF0 | (v >> 18)));
      cg_emitf("  .byte %u\n", (unsigned)(0x80 | ((v >> 12) & 0x3F)));
      cg_emitf("  .byte %u\n", (unsigned)(0x80 | ((v >> 6) & 0x3F)));
      cg_emitf("  .byte %u\n", (unsigned)(0x80 | (v & 0x3F)));
    }
  }
  cg_emitf("  .byte 0\n");
}

static void emit_wide_string_literal(string_lit_t *lit, void *user) {
  (void)user;
  if (lit->char_width == TK_CHAR_WIDTH_CHAR) return;
  cg_emitf("%s:\n", lit->label);
  int i = 0;
  int cw = (int)lit->char_width;
  while (i < lit->len) {
    uint32_t units[2];
    int nu = tk_next_string_code_units(lit->str, lit->len, &i, cw, units);
    for (int k = 0; k < nu; k++) {
      if (cw == TK_CHAR_WIDTH_CHAR16) cg_emitf("  .hword %u\n", (unsigned)units[k]);
      else cg_emitf("  .word %u\n", (unsigned)units[k]);
    }
  }
  if (cw == TK_CHAR_WIDTH_CHAR16) cg_emitf("  .hword 0\n");
  else cg_emitf("  .word 0\n");
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
  cg_emitf(".LCF%d:\n", lit->id);
  if (lit->fp_kind == TK_FLOAT_KIND_FLOAT) {
    union { float f; uint32_t i; } u = { .f = (float)lit->fval };
    cg_emitf("  .word %u\n", u.i);
  } else {
    /* note: long double is currently lowered to double. */
    union { double d; uint64_t i; } u = { .d = lit->fval };
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
    cg_emitf("  .quad %s\n", sym);
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

/* struct のメンバを宣言順にフラット出力する (init_values を val_idx から消費)。
 * 入れ子 struct メンバ (`struct Out{struct In i; int z;}`) は再帰して内側メンバを
 * 展開する。これをしないと内側 struct を 1 つの .quad として出力し、フラット値が
 * ずれていた (`{1,2,3}` が i.p=1,i.q=0,z=2 になる)。
 * struct_size まで末尾を 0 padding する。 */
static void emit_global_struct_members_rec(token_kind_t tk, char *tn, int tl,
                                           int struct_size, global_var_t *gv, int *val_idx) {
  int n_members = psx_ctx_get_tag_member_count(tk, tn, tl);
  int prev_end = 0;
  for (int i = 0; i < n_members && *val_idx < gv->init_count; i++) {
    tag_member_info_t mi = {0};
    if (!psx_ctx_get_tag_member_info(tk, tn, tl, i, &mi)) break;
    int off = mi.offset, ts = mi.type_size, alen = mi.array_len;
    if (off > prev_end) cg_emitf("  .space %d\n", off - prev_end);
    /* 配列メンバ (`int values[3]`): alen 個の要素を連続出力。 */
    if (alen > 0) {
      if ((mi.tag_kind == TK_STRUCT || mi.tag_kind == TK_UNION) && !mi.is_tag_pointer) {
        /* struct/union 配列メンバ (`struct P pts[2]`): 各要素を再帰してメンバ単位で
         * 出力する。これをしないと要素 1 つを ts バイトのスカラとして出力し、
         * `{{10,20},{30,40}}` が .quad 10/.quad 20 と化けていた。 */
        for (int k = 0; k < alen; k++) {
          emit_global_struct_members_rec(mi.tag_kind, mi.tag_name, mi.tag_len, ts, gv, val_idx);
        }
      } else {
        for (int k = 0; k < alen && *val_idx < gv->init_count; k++) {
          char *esym = gv->init_value_symbols ? gv->init_value_symbols[*val_idx] : NULL;
          int esym_len = gv->init_value_symbol_lens ? gv->init_value_symbol_lens[*val_idx] : 0;
          long long ev = gv->init_values[*val_idx];
          double efv = gv->init_fvalues ? gv->init_fvalues[*val_idx] : 0.0;
          (*val_idx)++;
          if (mi.is_bool) ev = (ev != 0);  /* C11 6.3.1.2: _Bool 配列メンバは 0/1 に正規化 */
          /* ポインタ配列メンバ (シンボル+オフセット要素や文字列/関数ポインタ要素) も
           * fp 配列メンバ (`struct R{double m[2][2];}`) も emit_global_init_member_scalar
           * 経由で統一処理する。直接 cg_emit_int_directive を呼ぶと fp_kind を見落として
           * double 値が `.quad 0` として出力されていた (efv を捨てて ev=0 を整数で書く)。 */
          emit_global_init_member_scalar(esym, esym_len, mi.fp_kind, ts, ev, efv);
        }
      }
      prev_end = off + ts * alen;
      continue;
    }
    /* 入れ子 struct メンバ: 再帰してフラット展開する。 */
    if (mi.tag_kind == TK_STRUCT && !mi.is_tag_pointer) {
      emit_global_struct_members_rec(mi.tag_kind, mi.tag_name, mi.tag_len, ts, gv, val_idx);
      prev_end = off + ts;
      continue;
    }
    /* ビットフィールド: 同じ storage ユニット (同一 offset) に属する連続ビット
     * フィールドを 1 つの整数に詰めて出力する。各メンバを別々の .long で出すと
     * `{3,5}` が `.long 3 / .long 5` (8B) になり値が壊れていた (正しくは 1B 0x53)。 */
    if (mi.bit_width > 0) {
      int unit_off = off, unit_ts = ts;
      unsigned long long packed = 0;
      while (i < n_members) {
        tag_member_info_t bmi = {0};
        if (!psx_ctx_get_tag_member_info(tk, tn, tl, i, &bmi)) break;
        if (bmi.bit_width == 0 || bmi.offset != unit_off) break;
        long long v = (*val_idx < gv->init_count) ? gv->init_values[*val_idx] : 0;
        if (*val_idx < gv->init_count) (*val_idx)++;
        unsigned long long mask = (bmi.bit_width >= 64)
                                    ? ~0ULL : ((1ULL << bmi.bit_width) - 1);
        packed |= ((unsigned long long)v & mask) << bmi.bit_offset;
        i++;
      }
      i--;  /* for ループの i++ と相殺 */
      cg_emit_int_directive(unit_ts, (long long)packed);
      prev_end = unit_off + unit_ts;
      continue;
    }
    /* スカラ / ポインタ / 関数ポインタメンバ。 */
    char *sym_i = gv->init_value_symbols ? gv->init_value_symbols[*val_idx] : NULL;
    int sym_i_len = gv->init_value_symbol_lens ? gv->init_value_symbol_lens[*val_idx] : 0;
    double fv_i = gv->init_fvalues ? gv->init_fvalues[*val_idx] : 0.0;
    long long mv = gv->init_values[*val_idx];
    if (mi.is_bool) mv = (mv != 0);  /* C11 6.3.1.2: _Bool スカラメンバは 0/1 に正規化 */
    emit_global_init_member_scalar(sym_i, sym_i_len, mi.fp_kind, ts, mv, fv_i);
    (*val_idx)++;
    prev_end = off + ts;
  }
  if (prev_end < struct_size) cg_emitf("  .space %d\n", struct_size - prev_end);
}

static void emit_global_struct_init(global_var_t *gv) {
  /* union: 活性メンバ (union_init_ordinal, 既定 0=先頭) だけをその型で出力し、
   * 残りを type_size まで 0 で埋める。`{.f=1.5f}` 等の designated 初期化に対応。 */
  if (gv->tag_kind == TK_UNION) {
    tag_member_info_t mi = {0};
    int ord = gv->union_init_ordinal;
    if (gv->init_count > 0 &&
        psx_ctx_get_tag_member_info(gv->tag_kind, gv->tag_name, gv->tag_len, ord, &mi)) {
      char *sym = gv->init_value_symbols ? gv->init_value_symbols[0] : NULL;
      int sym_len = gv->init_value_symbol_lens ? gv->init_value_symbol_lens[0] : 0;
      double fv = gv->init_fvalues ? gv->init_fvalues[0] : 0.0;
      emit_global_init_member_scalar(sym, sym_len, mi.fp_kind, mi.type_size,
                                     gv->init_values[0], fv);
      int emitted = sym ? 8
                        : (mi.fp_kind == TK_FLOAT_KIND_FLOAT) ? 4
                        : (mi.fp_kind >= TK_FLOAT_KIND_DOUBLE) ? 8
                        : mi.type_size;
      if (emitted < (int)gv->type_size) {
        cg_emitf("  .space %d\n", (int)gv->type_size - emitted);
      }
    } else {
      cg_emitf("  .space %d\n", (int)gv->type_size);
    }
    return;
  }
  int val_idx = 0;
  emit_global_struct_members_rec(gv->tag_kind, gv->tag_name, gv->tag_len,
                                 (int)gv->type_size, gv, &val_idx);
}

/* struct/union 配列のグローバル brace init: 各要素を member 毎に
 * その型サイズで出力する。`struct {int x; int y;} a[3] = {{1,2},...}`
 * は .long 1; .long 2; .long 3; ... と展開する。 */
static void emit_global_struct_array_init(global_var_t *gv) {
  int elem_size = gv->deref_size;
  int total_elems = elem_size > 0 ? gv->type_size / elem_size : 0;
  int val_idx = 0;
  /* 各要素を emit_global_struct_members_rec でメンバ単位に展開する。以前はメンバごとに
   * フラット slot を 1 個だけ消費する単純ループだったため、配列メンバ (`char tag[4]`)・
   * char 配列の文字列展開・入れ子 struct メンバ・bitfield を扱えず、`struct{char tag[4];
   * int n;} g[2]={{"aa",1},...}` の tag が 1 バイトしか出ず後続メンバがずれていた。
   * 非配列 struct の出力 (emit_global_struct_init) と同じ機構を要素ごとに適用して統一する。 */
  for (int e = 0; e < total_elems; e++) {
    emit_global_struct_members_rec(gv->tag_kind, gv->tag_name, gv->tag_len,
                                   elem_size, gv, &val_idx);
  }
}

/* gen_global_vars の本体: 1 つの global_var_t を assembly directive に
 * 落とす visitor 関数 (Phase C3-2 で ps_iter_globals に切替)。 */
static void emit_one_global_var(global_var_t *gv, void *user) {
  (void)user;
  if (gv->is_extern_decl) return;
  if (gv->is_thread_local) {
    /* _Thread_local: TLV descriptor + thread data/bss */
    if (gv->has_init) {
      cg_emitf(".section __DATA,__thread_data\n");
      cg_emitf("_%.*s$tlv$init:\n", gv->name_len, gv->name);
      cg_emit_int_directive(gv->type_size, gv->init_val);
    } else {
      cg_emitf(".section __DATA,__thread_bss\n");
      cg_emitf("_%.*s$tlv$init:\n", gv->name_len, gv->name);
      cg_emitf("  .space %d\n", gv->type_size);
    }
    cg_emitf(".section __DATA,__thread_vars,thread_local_variables\n");
    if (!gv->is_static) cg_emitf(".global _%.*s\n", gv->name_len, gv->name);
    cg_emitf("_%.*s:\n", gv->name_len, gv->name);
    cg_emitf("  .quad __tlv_bootstrap\n");
    cg_emitf("  .quad 0\n");
    cg_emitf("  .quad _%.*s$tlv$init\n", gv->name_len, gv->name);
    return;
  }
  if (gv->has_init) {
    cg_emitf(".section __DATA,__data\n");
    /* static (内部リンケージ) は .global を出さない (C11 6.2.2p3)。 */
    if (!gv->is_static) cg_emitf(".global _%.*s\n", gv->name_len, gv->name);
    int align_size = (gv->init_count > 0 && gv->deref_size > 0)
                        ? gv->deref_size : (int)gv->type_size;
    int align = (align_size >= 8) ? 3 : (align_size >= 4) ? 2 : (align_size >= 2) ? 1 : 0;
    cg_emitf(".align %d\n", align);
    cg_emitf("_%.*s:\n", gv->name_len, gv->name);
    if (gv->init_count > 0 && gv->tag_kind != TK_EOF && !gv->is_array) {
      emit_global_struct_init(gv);
    } else if (gv->init_count > 0 && gv->is_array && gv->tag_kind != TK_EOF) {
      emit_global_struct_array_init(gv);
    } else if (gv->init_count > 0) {
      int elem = gv->deref_size > 0 ? gv->deref_size : 4;
      int total_elems = gv->type_size / elem;
      int is_fp_arr = (gv->init_fvalues != NULL) &&
                      (gv->fp_kind == TK_FLOAT_KIND_FLOAT ||
                       gv->fp_kind == TK_FLOAT_KIND_DOUBLE ||
                       gv->fp_kind == TK_FLOAT_KIND_LONG_DOUBLE);
      for (int i = 0; i < gv->init_count && i < total_elems; i++) {
        char *sym_i = gv->init_value_symbols ? gv->init_value_symbols[i] : NULL;
        int sym_i_len = gv->init_value_symbol_lens ? gv->init_value_symbol_lens[i] : 0;
        if (sym_i && sym_i_len < 0) {
          /* 文字列リテラル要素: `.LC<n>` ラベルをそのまま参照 (アンダースコアなし)。 */
          cg_emitf("  .quad %s\n", sym_i);
          continue;
        }
        if (sym_i && sym_i_len > 0) {
          /* `&data[n]` 由来のシンボル+オフセット。init_values[i] にバイトオフセット。 */
          long long soff = gv->init_values ? gv->init_values[i] : 0;
          if (soff != 0) cg_emitf("  .quad _%.*s+%lld\n", sym_i_len, sym_i, soff);
          else           cg_emitf("  .quad _%.*s\n", sym_i_len, sym_i);
          continue;
        }
        if (is_fp_arr) {
          /* 浮動小数配列要素: fvalues[i] を IEEE-754 ビットパターンで出力。 */
          double d = gv->init_fvalues[i];
          if (gv->fp_kind == TK_FLOAT_KIND_FLOAT) {
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
        long long v = gv->init_values[i];
        cg_emit_int_directive(elem, v);
      }
      int remain = total_elems - gv->init_count;
      if (remain > 0) cg_emitf("  .space %d\n", remain * elem);
    } else if (gv->init_symbol) {
      if (gv->init_symbol_len < 0) {
        /* sentinel: 文字列リテラル `.LCn` のラベル — `_` プレフィックスなしで出力。 */
        cg_emitf("  .quad %s\n", gv->init_symbol);
      } else if (gv->init_symbol_offset != 0) {
        /* `&a[1]` / `a+1`: シンボル + バイトオフセット。 */
        cg_emitf("  .quad _%.*s + %lld\n", gv->init_symbol_len, gv->init_symbol,
                 gv->init_symbol_offset);
      } else {
        cg_emitf("  .quad _%.*s\n", gv->init_symbol_len, gv->init_symbol);
      }
    } else if (gv->fp_kind == TK_FLOAT_KIND_FLOAT) {
      /* float スカラ: fval を 32bit IEEE-754 ビットパターンで出力する。 */
      float f = (float)gv->fval;
      uint32_t bits;
      memcpy(&bits, &f, sizeof(bits));
      cg_emitf("  .long %u\n", (unsigned)bits);
    } else if (gv->fp_kind == TK_FLOAT_KIND_DOUBLE ||
               gv->fp_kind == TK_FLOAT_KIND_LONG_DOUBLE) {
      /* double スカラ: fval を 64bit IEEE-754 ビットパターンで出力する。 */
      double d = gv->fval;
      uint64_t bits;
      memcpy(&bits, &d, sizeof(bits));
      cg_emitf("  .quad %llu\n", (unsigned long long)bits);
    } else {
      cg_emit_int_directive(gv->type_size, gv->init_val);
    }
    return;
  }
  /* 暫定定義: .comm _name,size,log2align。
   * ただし static (内部リンケージ) は .comm (= common/外部シンボル) にすると別 TU の
   * 同名 static と共有/衝突するため、ローカルな .zerofill (__bss) に出す。 */
  int log2align = (gv->type_size >= 8) ? 3 : (gv->type_size >= 4) ? 2 : (gv->type_size >= 2) ? 1 : 0;
  if (gv->is_static) {
    cg_emitf(".zerofill __DATA,__bss,_%.*s,%d,%d\n", gv->name_len, gv->name, gv->type_size, log2align);
  } else {
    cg_emitf(".comm _%.*s,%d,%d\n", gv->name_len, gv->name, gv->type_size, log2align);
  }
}

void gen_global_vars(void) {
  ps_iter_globals(emit_one_global_var, NULL);
}
