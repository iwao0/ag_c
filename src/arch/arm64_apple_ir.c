/*
 * ARM64 Apple ABI: IR → ASM 出力 (Phase 5: regalloc 対応版)。
 *
 * フレームレイアウト:
 *   [x29 + 0 .. 16]      saved x29, x30
 *   [x29 + 16 .. 16+N*8] vreg slots (8B 単位、N = func->next_vreg_id)
 *   [x29 + 16+N*8 ..]    ALLOCA 領域
 *
 * vreg は ir_regalloc_function が以下に分類:
 *   - vreg_phys_reg[v] >= 0 (物理レジスタ x9..x15) → frame slot を経由しない
 *   - vreg_phys_reg[v] == -1 (spill) → frame slot をロード/ストア
 *
 * 対応命令: 全 IR 命令 (Phase 1〜4d までで導入したもの全部)。
 */

#include "arm64_apple_ir.h"
#include "arm64_apple_emit.h"
#include "../ir/ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  ir_func_t *f;
  int *vreg_off;
  int alloca_base;
  int total_size;
  int *alloca_region_off;
  int alloca_next;
  /* x19..x28 (10 個 callee-saved) のうち、この関数が使った reg のフラグ */
  int reg_used[10];
  int saved_count;
  int saved_area_size;  /* prologue で確保する saved area のバイト数 */
} gen_ctx_t;

static int round_up(int v, int a) {
  return (v + a - 1) / a * a;
}

static int scan_alloca_total(ir_func_t *f) {
  int total = 0;
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      if (i->op == IR_ALLOCA) {
        int sz = i->alloca_size > 0 ? i->alloca_size : 8;
        int al = i->alloca_align > 0 ? i->alloca_align : 8;
        total = round_up(total, al);
        total += sz;
      }
    }
  }
  return total;
}

static void scan_used_regs(gen_ctx_t *ctx) {
  ir_func_t *f = ctx->f;
  if (!f->vreg_phys_reg) return;
  for (int v = 0; v < f->next_vreg_id; v++) {
    int r = f->vreg_phys_reg[v];
    if (r >= 19 && r <= 28) {
      ctx->reg_used[r - 19] = 1;
    }
  }
  for (int i = 0; i < 10; i++) {
    if (ctx->reg_used[i]) ctx->saved_count++;
  }
  /* 16-byte align するため偶数に切り上げ */
  int count = ctx->saved_count;
  if (count & 1) count++;
  ctx->saved_area_size = count * 8;
}

static void layout_frame(gen_ctx_t *ctx) {
  scan_used_regs(ctx);
  int nvregs = ctx->f->next_vreg_id;
  ctx->vreg_off = calloc((size_t)(nvregs > 0 ? nvregs : 1), sizeof(int));
  /* layout: [x29 +0..16] saved x29/x30, [x29 +16..+16+S] saved x19..x28,
   * [x29 +16+S..] vreg slots, それ以降 alloca */
  int vreg_base = 16 + ctx->saved_area_size;
  for (int i = 0; i < nvregs; i++) {
    ctx->vreg_off[i] = vreg_base + i * 8;
  }
  ctx->alloca_base = vreg_base + nvregs * 8;
  ctx->alloca_next = ctx->alloca_base;
  ctx->alloca_region_off = calloc((size_t)(nvregs > 0 ? nvregs : 1), sizeof(int));
  int alloca_total = scan_alloca_total(ctx->f);
  int raw = ctx->alloca_base + alloca_total;
  ctx->total_size = round_up(raw, 16);
  if (ctx->total_size < 32) ctx->total_size = 32;
}

/* prologue: x29/x30 のあと、使われた callee-saved reg を保存する。 */
static void emit_save_regs(gen_ctx_t *ctx) {
  int off = 16;
  for (int i = 0; i < 10; i++) {
    if (!ctx->reg_used[i]) continue;
    cg_emitf("  str x%d, [x29, #%d]\n", 19 + i, off);
    off += 8;
  }
}

/* epilogue: 保存した reg を復元する。 */
static void emit_restore_regs(gen_ctx_t *ctx) {
  int off = 16;
  for (int i = 0; i < 10; i++) {
    if (!ctx->reg_used[i]) continue;
    cg_emitf("  ldr x%d, [x29, #%d]\n", 19 + i, off);
    off += 8;
  }
}

/* ------------------------------------------------------------------ */
/* レジスタ名解決ヘルパ                                                */
/* ------------------------------------------------------------------ */

/* val を「使える x レジスタ」にロードして、そのレジスタ名 (`x9` 等) を返す。
 * out_buf に書く。
 *   - imm: scratch に mov を出して scratch を返す
 *   - vreg with phys: そのレジスタ名を返す (新規 emit なし)
 *   - vreg spilled: scratch に ldr を出して scratch を返す
 *   - VAL_NONE: scratch に 0 をロードして返す */
static const char *ensure_val_in(gen_ctx_t *ctx, ir_val_t v, const char *scratch,
                                  char *out_buf, size_t out_size) {
  int is_fp = (v.type == IR_TY_F32 || v.type == IR_TY_F64);
  if (v.id == IR_VAL_IMM) {
    snprintf(out_buf, out_size, "%s", scratch);
    cg_emit_mov_imm(scratch, v.imm);
    return out_buf;
  }
  if (v.id == IR_VAL_NONE) {
    snprintf(out_buf, out_size, "%s", scratch);
    cg_emitf("  mov %s, #0\n", scratch);
    return out_buf;
  }
  int vv = v.id;
  if (!is_fp && ctx->f->vreg_phys_reg && vv >= 0 && vv < ctx->f->next_vreg_id &&
      ctx->f->vreg_phys_reg[vv] >= 0) {
    snprintf(out_buf, out_size, "x%d", ctx->f->vreg_phys_reg[vv]);
    return out_buf;
  }
  if (vv >= 0 && vv < ctx->f->next_vreg_id) {
    snprintf(out_buf, out_size, "%s", scratch);
    cg_emitf("  ldr %s, [x29, #%d]\n", scratch, ctx->vreg_off[vv]);
    return out_buf;
  }
  snprintf(out_buf, out_size, "%s", scratch);
  cg_emitf("  mov %s, #0\n", scratch);
  return out_buf;
}

/* dst の書き先レジスタ名を返す。
 *   - vreg with phys: そのレジスタ名 (release_dst で spill しない)
 *   - vreg spilled or 不正: scratch (release_dst で frame に store する) */
static const char *acquire_dst(gen_ctx_t *ctx, ir_val_t dst, const char *scratch,
                                char *out_buf, size_t out_size, int *out_needs_spill) {
  *out_needs_spill = 0;
  if (dst.id < 0 || dst.id >= ctx->f->next_vreg_id) {
    snprintf(out_buf, out_size, "%s", scratch);
    return out_buf;
  }
  if (ctx->f->vreg_phys_reg && ctx->f->vreg_phys_reg[dst.id] >= 0) {
    snprintf(out_buf, out_size, "x%d", ctx->f->vreg_phys_reg[dst.id]);
    return out_buf;
  }
  *out_needs_spill = 1;
  snprintf(out_buf, out_size, "%s", scratch);
  return out_buf;
}

static void release_dst(gen_ctx_t *ctx, ir_val_t dst, const char *reg, int needs_spill) {
  if (!needs_spill) return;
  if (dst.id < 0 || dst.id >= ctx->f->next_vreg_id) return;
  cg_emitf("  str %s, [x29, #%d]\n", reg, ctx->vreg_off[dst.id]);
}

/* "x9" → "w9" の変換。phys reg / scratch 共通。out 長 8 想定。 */
static void to_w_name(const char *xname, char *out, size_t sz) {
  if (xname[0] == 'x') {
    snprintf(out, sz, "w%s", xname + 1);
  } else {
    snprintf(out, sz, "%s", xname);
  }
}

/* ------------------------------------------------------------------ */
/* 命令生成                                                            */
/* ------------------------------------------------------------------ */

static void gen_inst(gen_ctx_t *ctx, ir_inst_t *inst) {
  switch (inst->op) {
    case IR_NOP:
      return;
    case IR_LOAD_STR: {
      /* 文字列リテラルのラベルアドレス (.LC<id>) を vreg に */
      char bd[8];
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
      cg_emitf("  adrp %s, %.*s@PAGE\n", d, inst->sym_len, inst->sym ? inst->sym : "");
      cg_emitf("  add %s, %s, %.*s@PAGEOFF\n", d, d, inst->sym_len, inst->sym ? inst->sym : "");
      release_dst(ctx, inst->dst, d, spill);
      return;
    }
    case IR_LOAD_SYM: {
      /* グローバル変数のアドレス (_<name>@PAGE/PAGEOFF) を vreg に */
      char bd[8];
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
      cg_emitf("  adrp %s, _%.*s@PAGE\n", d, inst->sym_len, inst->sym ? inst->sym : "");
      cg_emitf("  add %s, %s, _%.*s@PAGEOFF\n", d, d, inst->sym_len, inst->sym ? inst->sym : "");
      release_dst(ctx, inst->dst, d, spill);
      return;
    }
    case IR_LOAD_TLV_ADDR: {
      /* Apple ARM64 thread-local 解決:
       *   adrp x0, _<sym>@TLVPPAGE
       *   ldr  x0, [x0, _<sym>@TLVPPAGEOFF]   ; descriptor pointer
       *   ldr  x8, [x0]                       ; bootstrap function pointer
       *   blr  x8                             ; returns TLS address in x0
       * 戻り値 (TLS のアドレス) を dst slot へ。CALL 同様 caller-saved を
       * 全 clobber する想定 (regalloc/DCE は callee 相当として扱う)。 */
      cg_emitf("  adrp x0, _%.*s@TLVPPAGE\n", inst->sym_len, inst->sym ? inst->sym : "");
      cg_emitf("  ldr x0, [x0, _%.*s@TLVPPAGEOFF]\n", inst->sym_len, inst->sym ? inst->sym : "");
      cg_emitf("  ldr x8, [x0]\n");
      cg_emitf("  blr x8\n");
      if (inst->dst.id >= 0 && inst->dst.id < ctx->f->next_vreg_id) {
        cg_emitf("  str x0, [x29, #%d]\n", ctx->vreg_off[inst->dst.id]);
      }
      return;
    }
    case IR_ZEXT:
    case IR_SEXT:
    case IR_TRUNC: {
      /* 整数の幅変換。
       *   ZEXT i32→i64 : uxtw x_dst, w_src    (高 32bit ゼロ)
       *   SEXT i32→i64 : sxtw x_dst, w_src
       *   TRUNC i64→i32: mov  w_dst, w_src    (高 32bit は捨てる)
       * 現状 i32 ↔ i64 の双方向のみ。 */
      char b1[8], bd[8];
      const char *src = ensure_val_in(ctx, inst->src1, "x9", b1, sizeof(b1));
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x10", bd, sizeof(bd), &spill);
      char w_src[8], w_dst[8];
      to_w_name(src, w_src, sizeof(w_src));
      to_w_name(d, w_dst, sizeof(w_dst));
      if (inst->op == IR_ZEXT) {
        cg_emitf("  uxtw %s, %s\n", d, w_src);
      } else if (inst->op == IR_SEXT) {
        cg_emitf("  sxtw %s, %s\n", d, w_src);
      } else {
        cg_emitf("  mov %s, %s\n", w_dst, w_src);
      }
      release_dst(ctx, inst->dst, d, spill);
      return;
    }
    case IR_VLA_ALLOC: {
      /* VLA 動的スタック確保: src1 = バイトサイズ (i32 のことが多い)。
       *   ldr/mov 経由で x9 にロード
       *   x9 = (x9 + 15) & ~15   ; 16-byte align
       *   sub sp, sp, x9
       *   dst (frame slot) = sp  */
      char b1[8];
      const char *src = ensure_val_in(ctx, inst->src1, "x9", b1, sizeof(b1));
      if (strcmp(src, "x9") != 0) cg_emitf("  mov x9, %s\n", src);
      cg_emitf("  add x9, x9, #15\n");
      cg_emitf("  and x9, x9, #-16\n");
      cg_emitf("  sub sp, sp, x9\n");
      cg_emitf("  mov x9, sp\n");
      if (inst->dst.id >= 0 && inst->dst.id < ctx->f->next_vreg_id) {
        cg_emitf("  str x9, [x29, #%d]\n", ctx->vreg_off[inst->dst.id]);
      }
      return;
    }
    case IR_VA_ARG_AREA: {
      /* stack 上の variadic 引数領域の先頭 = x29 + total_size。
       * dst は spill (frame slot に書く)。 */
      char bd[8];
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
      cg_emitf("  add %s, x29, #%d\n", d, ctx->total_size);
      release_dst(ctx, inst->dst, d, spill);
      return;
    }
    case IR_LABEL:
      cg_emitf(".L%.*s_%d:\n", ctx->f->name_len, ctx->f->name, inst->label_id);
      return;
    case IR_LOAD_IMM: {
      char bd[8];
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
      cg_emit_mov_imm(d, inst->src1.imm);
      release_dst(ctx, inst->dst, d, spill);
      return;
    }
    case IR_LOAD_FP_IMM: {
      /* float/double 即値: int として一旦 x9 にロードし、fmov で fp reg に転送、
       * frame に str する。dst は spill 扱い。 */
      if (inst->dst.id < 0 || inst->dst.id >= ctx->f->next_vreg_id) return;
      if (inst->dst.type == IR_TY_F32) {
        union { float f; uint32_t i; } u = { .f = (float)inst->src1.fp_imm };
        cg_emit_mov_imm("x9", (long long)u.i);
        cg_emitf("  fmov s0, w9\n");
        cg_emitf("  str s0, [x29, #%d]\n", ctx->vreg_off[inst->dst.id]);
      } else {
        union { double d; uint64_t i; } u = { .d = inst->src1.fp_imm };
        cg_emit_mov_imm("x9", (long long)u.i);
        cg_emitf("  fmov d0, x9\n");
        cg_emitf("  str d0, [x29, #%d]\n", ctx->vreg_off[inst->dst.id]);
      }
      return;
    }
    case IR_FADD:
    case IR_FSUB:
    case IR_FMUL:
    case IR_FDIV: {
      /* float vreg は frame 経由。d0/d1 (or s0/s1) にロード、演算、frame に str。 */
      int is_double = (inst->dst.type == IR_TY_F64);
      const char *suf = is_double ? "d" : "s";
      cg_emitf("  ldr %s0, [x29, #%d]\n", suf, ctx->vreg_off[inst->src1.id]);
      cg_emitf("  ldr %s1, [x29, #%d]\n", suf, ctx->vreg_off[inst->src2.id]);
      const char *op = "fadd";
      switch (inst->op) {
        case IR_FADD: op = "fadd"; break;
        case IR_FSUB: op = "fsub"; break;
        case IR_FMUL: op = "fmul"; break;
        case IR_FDIV: op = "fdiv"; break;
        default: break;
      }
      cg_emitf("  %s %s0, %s0, %s1\n", op, suf, suf, suf);
      cg_emitf("  str %s0, [x29, #%d]\n", suf, ctx->vreg_off[inst->dst.id]);
      return;
    }
    case IR_FEQ:
    case IR_FNE:
    case IR_FLT:
    case IR_FLE: {
      int src_double = (inst->src1.type == IR_TY_F64) || (inst->src2.type == IR_TY_F64);
      const char *suf = src_double ? "d" : "s";
      cg_emitf("  ldr %s0, [x29, #%d]\n", suf, ctx->vreg_off[inst->src1.id]);
      cg_emitf("  ldr %s1, [x29, #%d]\n", suf, ctx->vreg_off[inst->src2.id]);
      cg_emitf("  fcmp %s0, %s1\n", suf, suf);
      const char *cond = "eq";
      switch (inst->op) {
        case IR_FEQ: cond = "eq"; break;
        case IR_FNE: cond = "ne"; break;
        case IR_FLT: cond = "mi"; break;
        case IR_FLE: cond = "ls"; break;
        default: break;
      }
      char bd[8];
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
      cg_emitf("  cset %s, %s\n", d, cond);
      release_dst(ctx, inst->dst, d, spill);
      return;
    }
    case IR_F2I: {
      int src_double = (inst->src1.type == IR_TY_F64);
      const char *suf = src_double ? "d" : "s";
      cg_emitf("  ldr %s0, [x29, #%d]\n", suf, ctx->vreg_off[inst->src1.id]);
      char bd[8];
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
      cg_emitf("  fcvtzs %s, %s0\n", d, suf);
      release_dst(ctx, inst->dst, d, spill);
      return;
    }
    case IR_I2F: {
      char b1[8];
      const char *s1 = ensure_val_in(ctx, inst->src1, "x9", b1, sizeof(b1));
      int is_double = (inst->dst.type == IR_TY_F64);
      const char *suf = is_double ? "d" : "s";
      cg_emitf("  scvtf %s0, %s\n", suf, s1);
      cg_emitf("  str %s0, [x29, #%d]\n", suf, ctx->vreg_off[inst->dst.id]);
      return;
    }
    case IR_F2F: {
      /* float ↔ double 変換: fcvt */
      int src_double = (inst->src1.type == IR_TY_F64);
      int dst_double = (inst->dst.type == IR_TY_F64);
      const char *src_suf = src_double ? "d" : "s";
      const char *dst_suf = dst_double ? "d" : "s";
      cg_emitf("  ldr %s0, [x29, #%d]\n", src_suf, ctx->vreg_off[inst->src1.id]);
      cg_emitf("  fcvt %s1, %s0\n", dst_suf, src_suf);
      cg_emitf("  str %s1, [x29, #%d]\n", dst_suf, ctx->vreg_off[inst->dst.id]);
      return;
    }
    case IR_ALLOCA: {
      int sz = inst->alloca_size > 0 ? inst->alloca_size : 8;
      int al = inst->alloca_align > 0 ? inst->alloca_align : 8;
      ctx->alloca_next = round_up(ctx->alloca_next, al);
      int off = ctx->alloca_next;
      ctx->alloca_next += sz;
      if (inst->dst.id >= 0 && inst->dst.id < ctx->f->next_vreg_id) {
        ctx->alloca_region_off[inst->dst.id] = off;
      }
      char bd[8];
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
      cg_emitf("  add %s, x29, #%d\n", d, off);
      release_dst(ctx, inst->dst, d, spill);
      return;
    }
    case IR_LOAD: {
      char bp[8];
      const char *ptr = ensure_val_in(ctx, inst->src1, "x9", bp, sizeof(bp));
      /* float/double: frame に書く (spill 経路で統一) */
      if (inst->dst.type == IR_TY_F32) {
        cg_emitf("  ldr s0, [%s]\n", ptr);
        cg_emitf("  str s0, [x29, #%d]\n", ctx->vreg_off[inst->dst.id]);
        return;
      }
      if (inst->dst.type == IR_TY_F64) {
        cg_emitf("  ldr d0, [%s]\n", ptr);
        cg_emitf("  str d0, [x29, #%d]\n", ctx->vreg_off[inst->dst.id]);
        return;
      }
      char bd[8];
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x10", bd, sizeof(bd), &spill);
      char w_d[8];
      to_w_name(d, w_d, sizeof(w_d));
      if (inst->is_unsigned_load) {
        switch (inst->dst.type) {
          /* unsigned: ldrb/ldrh/ldr w は自動で zero-extend する */
          case IR_TY_I8:  cg_emitf("  ldrb %s, [%s]\n", w_d, ptr); break;
          case IR_TY_I16: cg_emitf("  ldrh %s, [%s]\n", w_d, ptr); break;
          case IR_TY_I32: cg_emitf("  ldr %s, [%s]\n", w_d, ptr); break;
          default:        cg_emitf("  ldr %s, [%s]\n", d, ptr); break;
        }
      } else {
        switch (inst->dst.type) {
          case IR_TY_I8:  cg_emitf("  ldrsb %s, [%s]\n", d, ptr); break;
          case IR_TY_I16: cg_emitf("  ldrsh %s, [%s]\n", d, ptr); break;
          case IR_TY_I32: cg_emitf("  ldrsw %s, [%s]\n", d, ptr); break;
          default:        cg_emitf("  ldr %s, [%s]\n", d, ptr); break;
        }
      }
      release_dst(ctx, inst->dst, d, spill);
      return;
    }
    case IR_STORE: {
      char bp[8];
      const char *ptr = ensure_val_in(ctx, inst->src1, "x9", bp, sizeof(bp));
      /* float/double store: src2 vreg は frame 上にある (spill 経路)。 */
      if (inst->src2.type == IR_TY_F32) {
        if (inst->src2.id >= 0 && inst->src2.id < ctx->f->next_vreg_id) {
          cg_emitf("  ldr s0, [x29, #%d]\n", ctx->vreg_off[inst->src2.id]);
        }
        cg_emitf("  str s0, [%s]\n", ptr);
        return;
      }
      if (inst->src2.type == IR_TY_F64) {
        if (inst->src2.id >= 0 && inst->src2.id < ctx->f->next_vreg_id) {
          cg_emitf("  ldr d0, [x29, #%d]\n", ctx->vreg_off[inst->src2.id]);
        }
        cg_emitf("  str d0, [%s]\n", ptr);
        return;
      }
      char bv[8];
      const char *val = ensure_val_in(ctx, inst->src2, "x10", bv, sizeof(bv));
      char wval[8];
      to_w_name(val, wval, sizeof(wval));
      switch (inst->src2.type) {
        case IR_TY_I8:  cg_emitf("  strb %s, [%s]\n", wval, ptr); break;
        case IR_TY_I16: cg_emitf("  strh %s, [%s]\n", wval, ptr); break;
        case IR_TY_I32: cg_emitf("  str %s, [%s]\n", wval, ptr); break;
        default:        cg_emitf("  str %s, [%s]\n", val, ptr); break;
      }
      return;
    }
    case IR_NEG: {
      char b1[8], bd[8];
      const char *s1 = ensure_val_in(ctx, inst->src1, "x9", b1, sizeof(b1));
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
      cg_emitf("  neg %s, %s\n", d, s1);
      release_dst(ctx, inst->dst, d, spill);
      return;
    }
    case IR_NOT: {
      char b1[8], bd[8];
      const char *s1 = ensure_val_in(ctx, inst->src1, "x9", b1, sizeof(b1));
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
      cg_emitf("  mvn %s, %s\n", d, s1);
      release_dst(ctx, inst->dst, d, spill);
      return;
    }
    case IR_FNEG: {
      int is_double = (inst->dst.type == IR_TY_F64);
      const char *suf = is_double ? "d" : "s";
      cg_emitf("  ldr %s0, [x29, #%d]\n", suf, ctx->vreg_off[inst->src1.id]);
      cg_emitf("  fneg %s0, %s0\n", suf, suf);
      cg_emitf("  str %s0, [x29, #%d]\n", suf, ctx->vreg_off[inst->dst.id]);
      return;
    }
    case IR_LEA: {
      char b1[8], b2[8], bd[8];
      const char *s1 = ensure_val_in(ctx, inst->src1, "x9", b1, sizeof(b1));
      const char *s2 = ensure_val_in(ctx, inst->src2, "x10", b2, sizeof(b2));
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
      cg_emitf("  add %s, %s, %s\n", d, s1, s2);
      release_dst(ctx, inst->dst, d, spill);
      return;
    }
    case IR_MEMCPY: {
      char bp1[8], bp2[8];
      const char *dst_ptr = ensure_val_in(ctx, inst->src1, "x9", bp1, sizeof(bp1));
      const char *src_ptr = ensure_val_in(ctx, inst->src2, "x10", bp2, sizeof(bp2));
      int n = inst->alloca_size;
      int off = 0;
      for (; off + 8 <= n; off += 8) {
        cg_emitf("  ldr x11, [%s, #%d]\n", src_ptr, off);
        cg_emitf("  str x11, [%s, #%d]\n", dst_ptr, off);
      }
      for (; off + 4 <= n; off += 4) {
        cg_emitf("  ldr w11, [%s, #%d]\n", src_ptr, off);
        cg_emitf("  str w11, [%s, #%d]\n", dst_ptr, off);
      }
      for (; off + 2 <= n; off += 2) {
        cg_emitf("  ldrh w11, [%s, #%d]\n", src_ptr, off);
        cg_emitf("  strh w11, [%s, #%d]\n", dst_ptr, off);
      }
      for (; off < n; off++) {
        cg_emitf("  ldrb w11, [%s, #%d]\n", src_ptr, off);
        cg_emitf("  strb w11, [%s, #%d]\n", dst_ptr, off);
      }
      return;
    }
    case IR_ADD:
    case IR_SUB:
    case IR_MUL:
    case IR_DIV:
    case IR_UDIV:
    case IR_MOD:
    case IR_UMOD:
    case IR_AND:
    case IR_OR:
    case IR_XOR:
    case IR_SHL:
    case IR_SHR:
    case IR_LSR: {
      char b1[8], b2[8], bd[8];
      const char *s1 = ensure_val_in(ctx, inst->src1, "x9", b1, sizeof(b1));
      const char *s2 = ensure_val_in(ctx, inst->src2, "x10", b2, sizeof(b2));
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
      switch (inst->op) {
        case IR_ADD: cg_emitf("  add %s, %s, %s\n", d, s1, s2); break;
        case IR_SUB: cg_emitf("  sub %s, %s, %s\n", d, s1, s2); break;
        case IR_MUL: cg_emitf("  mul %s, %s, %s\n", d, s1, s2); break;
        case IR_DIV: cg_emitf("  sdiv %s, %s, %s\n", d, s1, s2); break;
        case IR_UDIV: cg_emitf("  udiv %s, %s, %s\n", d, s1, s2); break;
        case IR_MOD:
          cg_emitf("  sdiv x11, %s, %s\n", s1, s2);
          cg_emitf("  msub %s, x11, %s, %s\n", d, s2, s1);
          break;
        case IR_UMOD:
          cg_emitf("  udiv x11, %s, %s\n", s1, s2);
          cg_emitf("  msub %s, x11, %s, %s\n", d, s2, s1);
          break;
        case IR_AND: cg_emitf("  and %s, %s, %s\n", d, s1, s2); break;
        case IR_OR:  cg_emitf("  orr %s, %s, %s\n", d, s1, s2); break;
        case IR_XOR: cg_emitf("  eor %s, %s, %s\n", d, s1, s2); break;
        case IR_SHL: cg_emitf("  lsl %s, %s, %s\n", d, s1, s2); break;
        case IR_SHR: cg_emitf("  asr %s, %s, %s\n", d, s1, s2); break;
        case IR_LSR: cg_emitf("  lsr %s, %s, %s\n", d, s1, s2); break;
        default: break;
      }
      release_dst(ctx, inst->dst, d, spill);
      return;
    }
    case IR_LT:
    case IR_LE:
    case IR_ULT:
    case IR_ULE:
    case IR_EQ:
    case IR_NE: {
      char b1[8], b2[8], bd[8];
      const char *s1 = ensure_val_in(ctx, inst->src1, "x9", b1, sizeof(b1));
      const char *s2 = ensure_val_in(ctx, inst->src2, "x10", b2, sizeof(b2));
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
      cg_emitf("  cmp %s, %s\n", s1, s2);
      const char *cond = "eq";
      switch (inst->op) {
        case IR_LT:  cond = "lt"; break;
        case IR_LE:  cond = "le"; break;
        case IR_ULT: cond = "lo"; break;   /* unsigned <  : C clear */
        case IR_ULE: cond = "ls"; break;   /* unsigned <= : C clear || Z set */
        case IR_EQ:  cond = "eq"; break;
        case IR_NE:  cond = "ne"; break;
        default: break;
      }
      cg_emitf("  cset %s, %s\n", d, cond);
      release_dst(ctx, inst->dst, d, spill);
      return;
    }
    case IR_PARAM: {
      int idx = (int)inst->src1.imm;
      /* idx == -1 → x8 (struct return area)。
       * float/double → s{idx}/d{idx}、整数 → x{idx}。
       * 整数 idx >= 8 → 呼び出し側が stack に積んだ 9 個目以降の引数を
       *   [x29 + total_size + (idx-8)*8] から load する (Apple ARM64 ABI)。 */
      int is_fp = (inst->dst.type == IR_TY_F32 || inst->dst.type == IR_TY_F64);
      char src_buf[8];
      const char *src_reg;
      if (idx == -1) {
        src_reg = "x8";
      } else if (!is_fp && idx >= 8) {
        /* stack-passed integer arg: ldr to a scratch reg then write to slot. */
        int stack_off = ctx->total_size + (idx - 8) * 8;
        const char *tmp = "x9";
        cg_emitf("  ldr %s, [x29, #%d]\n", tmp, stack_off);
        if (inst->dst.id >= 0 && inst->dst.id < ctx->f->next_vreg_id) {
          if (ctx->f->vreg_phys_reg && ctx->f->vreg_phys_reg[inst->dst.id] >= 0) {
            int r = ctx->f->vreg_phys_reg[inst->dst.id];
            cg_emitf("  mov x%d, %s\n", r, tmp);
          } else {
            cg_emitf("  str %s, [x29, #%d]\n", tmp, ctx->vreg_off[inst->dst.id]);
          }
        }
        return;
      } else if (is_fp) {
        const char *suf = (inst->dst.type == IR_TY_F64) ? "d" : "s";
        snprintf(src_buf, sizeof(src_buf), "%s%d", suf, idx);
        src_reg = src_buf;
      } else {
        snprintf(src_buf, sizeof(src_buf), "x%d", idx);
        src_reg = src_buf;
      }
      if (inst->dst.id >= 0 && inst->dst.id < ctx->f->next_vreg_id) {
        if (!is_fp && ctx->f->vreg_phys_reg && ctx->f->vreg_phys_reg[inst->dst.id] >= 0) {
          int r = ctx->f->vreg_phys_reg[inst->dst.id];
          if (r != idx && !(idx == -1 && r == 8)) {
            cg_emitf("  mov x%d, %s\n", r, src_reg);
          }
        } else {
          cg_emitf("  str %s, [x29, #%d]\n", src_reg, ctx->vreg_off[inst->dst.id]);
        }
      }
      return;
    }
    case IR_CALL: {
      /* Apple ARM64 ABI:
       *   - 通常: 整数引数 x0..x7、float/double 引数 s0..d7 (独立カウンタ)。
       *   - variadic: 固定引数のみ通常レジスタ、可変引数は全て stack に置く。
       *     stack は 16-byte align、各 variadic arg は 8B スロット。
       *     float は引数渡しで double に促進。 */
      int var_stack_bytes = 0;
      int fixed_limit = inst->is_variadic_call ? inst->nargs_fixed : inst->nargs;
      /* 非 variadic で 9 個以降の int 引数: stack 渡し (Apple ARM64 ABI)。
       * 一旦カウントして fixed_limit を 8 に絞り、register に乗らない分を後で
       * stack に積む。float/double が境界を跨ぐ複雑な場合は未対応。 */
      int extra_stack_args = 0;
      if (!inst->is_variadic_call && inst->nargs > 8) {
        extra_stack_args = inst->nargs - 8;
        var_stack_bytes = ((extra_stack_args + 1) / 2) * 16;
        if (var_stack_bytes > 0) {
          cg_emitf("  sub sp, sp, #%d\n", var_stack_bytes);
        }
        for (int i = 8; i < inst->nargs; i++) {
          ir_val_t arg = inst->args[i];
          int slot_off = (i - 8) * 8;
          char buf[8];
          const char *src = ensure_val_in(ctx, arg, "x9", buf, sizeof(buf));
          cg_emitf("  str %s, [sp, #%d]\n", src, slot_off);
        }
        fixed_limit = 8;
      }
      if (inst->is_variadic_call) {
        int nargs_var = inst->nargs - inst->nargs_fixed;
        var_stack_bytes = ((nargs_var + 1) / 2) * 16;
        if (var_stack_bytes > 0) {
          cg_emitf("  sub sp, sp, #%d\n", var_stack_bytes);
        }
        /* 可変引数を stack slot に書く */
        for (int i = inst->nargs_fixed; i < inst->nargs; i++) {
          ir_val_t arg = inst->args[i];
          int slot_off = (i - inst->nargs_fixed) * 8;
          if (arg.type == IR_TY_F32) {
            /* float → double に昇格して書く */
            char buf[8];
            const char *src = ensure_val_in(ctx, arg, "s0", buf, sizeof(buf));
            if (strcmp(src, "s0") != 0) cg_emitf("  fmov s0, %s\n", src);
            cg_emitf("  fcvt d0, s0\n");
            cg_emitf("  str d0, [sp, #%d]\n", slot_off);
          } else if (arg.type == IR_TY_F64) {
            char buf[8];
            const char *src = ensure_val_in(ctx, arg, "d0", buf, sizeof(buf));
            if (strcmp(src, "d0") != 0) cg_emitf("  fmov d0, %s\n", src);
            cg_emitf("  str d0, [sp, #%d]\n", slot_off);
          } else {
            char buf[8];
            const char *src = ensure_val_in(ctx, arg, "x9", buf, sizeof(buf));
            cg_emitf("  str %s, [sp, #%d]\n", src, slot_off);
          }
        }
      }
      /* 固定引数を通常レジスタに積む */
      int int_idx = 0;
      int fp_idx = 0;
      for (int i = 0; i < fixed_limit && (int_idx < 8 || fp_idx < 8); i++) {
        ir_val_t arg = inst->args[i];
        int is_fp = (arg.type == IR_TY_F32 || arg.type == IR_TY_F64);
        char regname[8];
        if (is_fp) {
          const char *suf = (arg.type == IR_TY_F64) ? "d" : "s";
          snprintf(regname, sizeof(regname), "%s%d", suf, fp_idx++);
        } else {
          snprintf(regname, sizeof(regname), "x%d", int_idx++);
        }
        char buf[8];
        const char *src = ensure_val_in(ctx, arg, regname, buf, sizeof(buf));
        if (strcmp(src, regname) != 0) {
          if (is_fp) {
            cg_emitf("  fmov %s, %s\n", regname, src);
          } else {
            cg_emitf("  mov %s, %s\n", regname, src);
          }
        }
      }
      /* struct return: ret_area を x8 にロード */
      if (inst->ret_struct_size > 0 && inst->ret_struct_area.id != IR_VAL_NONE) {
        char buf[8];
        const char *src = ensure_val_in(ctx, inst->ret_struct_area, "x8", buf, sizeof(buf));
        if (strcmp(src, "x8") != 0) {
          cg_emitf("  mov x8, %s\n", src);
        }
      }
      if (inst->callee.id != IR_VAL_NONE) {
        /* 間接呼び出し: callee を x16 (= ip0、ABI 上 caller-saved の scratch)
         * にロードして blr。引数 reg (x0..x7) とは衝突しない。 */
        char buf[8];
        const char *src = ensure_val_in(ctx, inst->callee, "x16", buf, sizeof(buf));
        if (strcmp(src, "x16") != 0) cg_emitf("  mov x16, %s\n", src);
        cg_emitf("  blr x16\n");
      } else {
        cg_emitf("  bl _%.*s\n", inst->sym_len, inst->sym ? inst->sym : "");
      }
      /* variadic で stack を使った分を戻す */
      if (var_stack_bytes > 0) {
        cg_emitf("  add sp, sp, #%d\n", var_stack_bytes);
      }
      /* 戻り値: float なら s0/d0、それ以外 x0 を dst slot へ。 */
      if (inst->dst.id >= 0 && inst->dst.id < ctx->f->next_vreg_id) {
        if (inst->dst.type == IR_TY_F32) {
          cg_emitf("  str s0, [x29, #%d]\n", ctx->vreg_off[inst->dst.id]);
        } else if (inst->dst.type == IR_TY_F64) {
          cg_emitf("  str d0, [x29, #%d]\n", ctx->vreg_off[inst->dst.id]);
        } else {
          cg_emitf("  str x0, [x29, #%d]\n", ctx->vreg_off[inst->dst.id]);
        }
      }
      return;
    }
    case IR_BR:
      cg_emitf("  b .L%.*s_%d\n", ctx->f->name_len, ctx->f->name, inst->label_id);
      return;
    case IR_BR_COND: {
      char b1[8];
      const char *s1 = ensure_val_in(ctx, inst->src1, "x9", b1, sizeof(b1));
      cg_emitf("  cbnz %s, .L%.*s_%d\n", s1,
                ctx->f->name_len, ctx->f->name, inst->label_id);
      cg_emitf("  b .L%.*s_%d\n",
                ctx->f->name_len, ctx->f->name, inst->else_label_id);
      return;
    }
    case IR_RET: {
      if (inst->src1.id != IR_VAL_NONE) {
        if (inst->src1.type == IR_TY_F32) {
          char buf[8];
          const char *src = ensure_val_in(ctx, inst->src1, "s0", buf, sizeof(buf));
          if (strcmp(src, "s0") != 0) cg_emitf("  fmov s0, %s\n", src);
        } else if (inst->src1.type == IR_TY_F64) {
          char buf[8];
          const char *src = ensure_val_in(ctx, inst->src1, "d0", buf, sizeof(buf));
          if (strcmp(src, "d0") != 0) cg_emitf("  fmov d0, %s\n", src);
        } else {
          char buf[8];
          const char *src = ensure_val_in(ctx, inst->src1, "x0", buf, sizeof(buf));
          if (strcmp(src, "x0") != 0) cg_emitf("  mov x0, %s\n", src);
        }
      } else {
        cg_emitf("  mov x0, #0\n");
      }
      emit_restore_regs(ctx);
      cg_emitf("  mov sp, x29\n");
      cg_emitf("  ldp x29, x30, [sp]\n");
      cg_emitf("  add sp, sp, #%d\n", ctx->total_size);
      cg_emitf("  ret\n");
      return;
    }
    default:
      fprintf(stderr, "gen_ir_inst: unsupported op %s\n", ir_op_name(inst->op));
      return;
  }
}

static void gen_func(ir_func_t *f) {
  /* レジスタ割り付け */
  ir_regalloc_function(f);

  gen_ctx_t ctx = {0};
  ctx.f = f;
  layout_frame(&ctx);

  cg_emitf(".global _%.*s\n", f->name_len, f->name);
  cg_emitf(".align 2\n");
  cg_emitf("_%.*s:\n", f->name_len, f->name);
  cg_emitf("  sub sp, sp, #%d\n", ctx.total_size);
  cg_emitf("  stp x29, x30, [sp]\n");
  cg_emitf("  mov x29, sp\n");
  emit_save_regs(&ctx);

  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      gen_inst(&ctx, i);
    }
  }

  free(ctx.vreg_off);
  free(ctx.alloca_region_off);
}

void gen_ir_module(ir_module_t *m) {
  if (!m) return;
  /* Phase 6: 最適化パス。const fold で即値伝播と算術畳み込みを行い、
   * DCE で使われない命令 (LOAD_IMM だけ残ったものなど) を IR_NOP に置換。 */
  ir_opt_const_fold(m);
  ir_opt_dce(m);
  for (ir_func_t *f = m->funcs; f; f = f->next) {
    gen_func(f);
  }
}
