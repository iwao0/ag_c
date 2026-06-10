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
#include "../parser/parser.h"
#include "../parser/internal/semantic_ctx.h"
#include "../tokenizer/escape.h"
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

void gen_string_literals(void) {
  if (!string_literals) return;
  /* narrow char 文字列のみ __TEXT,__cstring に置く。
   * u"..." / U"..." / L"..." は内部にゼロバイトを含み得るため __DATA,__const へ。 */
  int has_narrow = 0;
  int has_wide = 0;
  for (string_lit_t *lit = string_literals; lit; lit = lit->next) {
    if (lit->char_width == TK_CHAR_WIDTH_CHAR) has_narrow = 1;
    else has_wide = 1;
  }
  if (has_narrow) cg_emitf(".section __TEXT,__cstring\n");
  for (string_lit_t *lit = string_literals; lit; lit = lit->next) {
    if (lit->char_width != TK_CHAR_WIDTH_CHAR) continue;
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
  /* u"..." (UTF-16) / U"..." (UTF-32) / L"..." リテラル */
  if (has_wide) {
    cg_emitf(".section __DATA,__const\n");
    cg_emitf(".align 2\n");
  }
  for (string_lit_t *lit = string_literals; lit; lit = lit->next) {
    if (lit->char_width == TK_CHAR_WIDTH_CHAR) continue;
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
      if (lit->char_width == TK_CHAR_WIDTH_CHAR16) {
        if (v < 0x10000) {
          cg_emitf("  .hword %u\n", (unsigned)v);
        } else {
          uint32_t u = v - 0x10000;
          unsigned hi = 0xD800u | ((u >> 10) & 0x3FFu);
          unsigned lo = 0xDC00u | (u & 0x3FFu);
          cg_emitf("  .hword %u\n", hi);
          cg_emitf("  .hword %u\n", lo);
        }
      } else {
        cg_emitf("  .word %u\n", (unsigned)v);
      }
    }
    if (lit->char_width == TK_CHAR_WIDTH_CHAR16) cg_emitf("  .hword 0\n");
    else cg_emitf("  .word 0\n");
  }
  cg_emitf(".text\n");
}

void gen_float_literals(void) {
  if (!float_literals) return;
  cg_emitf(".section __DATA,__data\n");
  cg_emitf(".align 3\n");
  for (float_lit_t *lit = float_literals; lit; lit = lit->next) {
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
  cg_emitf(".text\n");
}

void gen_global_vars(void) {
  for (global_var_t *gv = global_vars; gv; gv = gv->next) {
    if (gv->is_extern_decl) continue;
    if (gv->is_thread_local) {
      /* _Thread_local: TLV descriptor + thread data/bss */
      if (gv->has_init) {
        cg_emitf(".section __DATA,__thread_data\n");
        cg_emitf("_%.*s$tlv$init:\n", gv->name_len, gv->name);
        if (gv->type_size == 1) cg_emitf("  .byte %lld\n", gv->init_val);
        else if (gv->type_size == 2) cg_emitf("  .short %lld\n", gv->init_val);
        else if (gv->type_size == 4) cg_emitf("  .long %lld\n", gv->init_val);
        else cg_emitf("  .quad %lld\n", gv->init_val);
      } else {
        cg_emitf(".section __DATA,__thread_bss\n");
        cg_emitf("_%.*s$tlv$init:\n", gv->name_len, gv->name);
        cg_emitf("  .space %d\n", gv->type_size);
      }
      cg_emitf(".section __DATA,__thread_vars,thread_local_variables\n");
      cg_emitf(".global _%.*s\n", gv->name_len, gv->name);
      cg_emitf("_%.*s:\n", gv->name_len, gv->name);
      cg_emitf("  .quad __tlv_bootstrap\n");
      cg_emitf("  .quad 0\n");
      cg_emitf("  .quad _%.*s$tlv$init\n", gv->name_len, gv->name);
    } else if (gv->has_init) {
      cg_emitf(".section __DATA,__data\n");
      cg_emitf(".global _%.*s\n", gv->name_len, gv->name);
      int align_size = (gv->init_count > 0 && gv->deref_size > 0)
                          ? gv->deref_size : (int)gv->type_size;
      int align = (align_size >= 8) ? 3 : (align_size >= 4) ? 2 : (align_size >= 2) ? 1 : 0;
      cg_emitf(".align %d\n", align);
      cg_emitf("_%.*s:\n", gv->name_len, gv->name);
      if (gv->init_count > 0 && gv->tag_kind != TK_EOF && !gv->is_array) {
        /* struct / union global with brace init: 各メンバの型サイズに合わせて
         * init_values[i] を出力。メンバ間の padding は .space で埋める。
         * `struct { int x; int y; } p = {10, 32}` → .long 10; .long 32。 */
        int n_members = psx_ctx_get_tag_member_count(gv->tag_kind, gv->tag_name, gv->tag_len);
        int prev_end = 0;
        for (int i = 0; i < n_members && i < gv->init_count; i++) {
          char *mn = NULL; int ml = 0;
          int off = 0, ts = 0, ds = 0, alen = 0;
          token_kind_t mtk = TK_EOF; char *mtn = NULL; int mtl = 0; int mtp = 0;
          if (!psx_ctx_get_tag_member_at(gv->tag_kind, gv->tag_name, gv->tag_len, i,
                                         &mn, &ml, &off, &ts, &ds, &alen,
                                         &mtk, &mtn, &mtl, &mtp)) break;
          if (off > prev_end) cg_emitf("  .space %d\n", off - prev_end);
          /* メンバが関数ポインタ等 (init_value_symbols[i] が設定済み) のときは
           * `.quad _<sym>` を出力。 */
          char *sym_i = gv->init_value_symbols ? gv->init_value_symbols[i] : NULL;
          int sym_i_len = gv->init_value_symbol_lens ? gv->init_value_symbol_lens[i] : 0;
          if (sym_i && sym_i_len > 0) {
            cg_emitf("  .quad _%.*s\n", sym_i_len, sym_i);
          } else {
            long long v = gv->init_values[i];
            if (ts == 1) cg_emitf("  .byte %lld\n", v);
            else if (ts == 2) cg_emitf("  .short %lld\n", v);
            else if (ts == 4) cg_emitf("  .long %lld\n", v);
            else cg_emitf("  .quad %lld\n", v);
          }
          prev_end = off + ts;
        }
        if (prev_end < gv->type_size) cg_emitf("  .space %d\n", gv->type_size - prev_end);
      } else if (gv->init_count > 0 && gv->is_array && gv->tag_kind != TK_EOF) {
        /* struct/union 配列のグローバル brace init: 各要素を member 毎に
         * その型サイズで出力する。`struct {int x; int y;} a[3] = {{1,2},...}`
         * は .long 1; .long 2; .long 3; ... と展開する。 */
        int n_members = psx_ctx_get_tag_member_count(gv->tag_kind, gv->tag_name, gv->tag_len);
        int elem_size = gv->deref_size;
        int total_elems = elem_size > 0 ? gv->type_size / elem_size : 0;
        int val_idx = 0;
        for (int e = 0; e < total_elems; e++) {
          int prev_end = 0;
          for (int i = 0; i < n_members; i++) {
            char *mn = NULL; int ml = 0;
            int off = 0, ts = 0, ds = 0, alen = 0;
            token_kind_t mtk = TK_EOF; char *mtn = NULL; int mtl = 0; int mtp = 0;
            if (!psx_ctx_get_tag_member_at(gv->tag_kind, gv->tag_name, gv->tag_len, i,
                                           &mn, &ml, &off, &ts, &ds, &alen,
                                           &mtk, &mtn, &mtl, &mtp)) break;
            if (off > prev_end) cg_emitf("  .space %d\n", off - prev_end);
            long long v = (val_idx < gv->init_count) ? gv->init_values[val_idx] : 0;
            val_idx++;
            if (ts == 1) cg_emitf("  .byte %lld\n", v);
            else if (ts == 2) cg_emitf("  .short %lld\n", v);
            else if (ts == 4) cg_emitf("  .long %lld\n", v);
            else cg_emitf("  .quad %lld\n", v);
            prev_end = off + ts;
          }
          if (prev_end < elem_size) cg_emitf("  .space %d\n", elem_size - prev_end);
        }
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
            cg_emitf("  .quad _%.*s\n", sym_i_len, sym_i);
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
          if (elem == 1) cg_emitf("  .byte %lld\n", v);
          else if (elem == 2) cg_emitf("  .short %lld\n", v);
          else if (elem == 4) cg_emitf("  .long %lld\n", v);
          else cg_emitf("  .quad %lld\n", v);
        }
        int remain = total_elems - gv->init_count;
        if (remain > 0) cg_emitf("  .space %d\n", remain * elem);
      } else if (gv->init_symbol) {
        if (gv->init_symbol_len < 0) {
          /* sentinel: 文字列リテラル `.LCn` のラベル — `_` プレフィックスなしで出力。 */
          cg_emitf("  .quad %s\n", gv->init_symbol);
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
      }
      else if (gv->type_size == 1) cg_emitf("  .byte %lld\n", gv->init_val);
      else if (gv->type_size == 2) cg_emitf("  .short %lld\n", gv->init_val);
      else if (gv->type_size == 4) cg_emitf("  .long %lld\n", gv->init_val);
      else cg_emitf("  .quad %lld\n", gv->init_val);
    } else {
      /* 暫定定義: .comm _name,size,log2align */
      int log2align = (gv->type_size >= 8) ? 3 : (gv->type_size >= 4) ? 2 : (gv->type_size >= 2) ? 1 : 0;
      cg_emitf(".comm _%.*s,%d,%d\n", gv->name_len, gv->name, gv->type_size, log2align);
    }
  }
}
