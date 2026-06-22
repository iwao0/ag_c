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

/* `add/sub dst, src, #imm` を emit する。ARM64 の add/sub 即値は imm12 (0..4095) または
 * imm12<<12 (4096 の倍数) しか符号化できず、`sub sp, sp, #4112` や `add x19, x29, #8576`
 * のような 4095 超の即値は無効命令になる (大きいスタックフレームで発生)。4095 を超える場合は
 * 4096 の倍数部 (lsl #12) と端数の 2 命令に分割する (clang と同じ)。imm は非負・16MB 未満を想定。 */
static void emit_addsub_imm(const char *op, const char *dst, const char *src, int imm) {
  if (imm <= 4095) {
    cg_emitf("  %s %s, %s, #%d\n", op, dst, src, imm);
    return;
  }
  cg_emitf("  %s %s, %s, #%d, lsl #12\n", op, dst, src, (imm >> 12) & 0xfff);
  if (imm & 0xfff) cg_emitf("  %s %s, %s, #%d\n", op, dst, dst, imm & 0xfff);
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

/* gen_inst の op 別ヘルパ群 (build_expr 分割と同パターン)。
 * dispatch 本体は switch で各 helper を呼ぶだけにする。 */
static void gen_inst_load_str(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_load_sym(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_load_tlv_addr(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_int_cast(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_vla_alloc(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_va_arg_area(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_load_imm(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_load_fp_imm(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_fp_binop(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_fp_cmp(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_f2i(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_i2f(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_f2f(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_alloca(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_load(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_store(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_neg(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_not(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_fneg(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_lea(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_memcpy(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_int_binop(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_int_cmp(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_param(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_call(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_br_cond(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_ret(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_atomic(gen_ctx_t *ctx, ir_inst_t *inst);
static void gen_inst_align_ptr(gen_ctx_t *ctx, ir_inst_t *inst);

static void gen_inst(gen_ctx_t *ctx, ir_inst_t *inst) {
  switch (inst->op) {
    case IR_NOP:           return;
    case IR_LOAD_STR:      gen_inst_load_str(ctx, inst); return;
    case IR_LOAD_SYM:      gen_inst_load_sym(ctx, inst); return;
    case IR_LOAD_TLV_ADDR: gen_inst_load_tlv_addr(ctx, inst); return;
    case IR_ZEXT:
    case IR_SEXT:
    case IR_TRUNC:         gen_inst_int_cast(ctx, inst); return;
    case IR_VLA_ALLOC:     gen_inst_vla_alloc(ctx, inst); return;
    case IR_VA_ARG_AREA:   gen_inst_va_arg_area(ctx, inst); return;
    case IR_LABEL:
      cg_emitf(".L%.*s_%d:\n", ctx->f->name_len, ctx->f->name, inst->label_id);
      return;
    case IR_LOAD_IMM:      gen_inst_load_imm(ctx, inst); return;
    case IR_LOAD_FP_IMM:   gen_inst_load_fp_imm(ctx, inst); return;
    case IR_FADD:
    case IR_FSUB:
    case IR_FMUL:
    case IR_FDIV:          gen_inst_fp_binop(ctx, inst); return;
    case IR_FEQ:
    case IR_FNE:
    case IR_FLT:
    case IR_FLE:           gen_inst_fp_cmp(ctx, inst); return;
    case IR_F2I:           gen_inst_f2i(ctx, inst); return;
    case IR_I2F:           gen_inst_i2f(ctx, inst); return;
    case IR_F2F:           gen_inst_f2f(ctx, inst); return;
    case IR_ALLOCA:        gen_inst_alloca(ctx, inst); return;
    case IR_LOAD:          gen_inst_load(ctx, inst); return;
    case IR_STORE:         gen_inst_store(ctx, inst); return;
    case IR_NEG:           gen_inst_neg(ctx, inst); return;
    case IR_NOT:           gen_inst_not(ctx, inst); return;
    case IR_FNEG:          gen_inst_fneg(ctx, inst); return;
    case IR_LEA:           gen_inst_lea(ctx, inst); return;
    case IR_MEMCPY:        gen_inst_memcpy(ctx, inst); return;
    case IR_ATOMIC:        gen_inst_atomic(ctx, inst); return;
    case IR_ALIGN_PTR:     gen_inst_align_ptr(ctx, inst); return;
    case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: case IR_UDIV:
    case IR_MOD: case IR_UMOD: case IR_AND: case IR_OR: case IR_XOR:
    case IR_SHL: case IR_SHR: case IR_LSR:
                           gen_inst_int_binop(ctx, inst); return;
    case IR_LT: case IR_LE: case IR_ULT: case IR_ULE:
    case IR_EQ: case IR_NE:
                           gen_inst_int_cmp(ctx, inst); return;
    case IR_PARAM:         gen_inst_param(ctx, inst); return;
    case IR_CALL:          gen_inst_call(ctx, inst); return;
    case IR_BR:
      cg_emitf("  b .L%.*s_%d\n", ctx->f->name_len, ctx->f->name, inst->label_id);
      return;
    case IR_BR_COND:       gen_inst_br_cond(ctx, inst); return;
    case IR_RET:           gen_inst_ret(ctx, inst); return;
    default:
      fprintf(stderr, "gen_ir_inst: unsupported op %s\n", ir_op_name(inst->op));
      return;
  }
}

/* -------- gen_inst の op 別ヘルパ実装 -------- */

static void gen_inst_load_str(gen_ctx_t *ctx, ir_inst_t *inst) {
      /* 文字列リテラルのラベルアドレス (.LC<id>) を vreg に */
      char bd[8];
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
      cg_emitf("  adrp %s, %.*s@PAGE\n", d, inst->sym_len, inst->sym ? inst->sym : "");
      cg_emitf("  add %s, %s, %.*s@PAGEOFF\n", d, d, inst->sym_len, inst->sym ? inst->sym : "");
  release_dst(ctx, inst->dst, d, spill);
}

static void gen_inst_load_sym(gen_ctx_t *ctx, ir_inst_t *inst) {
      char bd[8];
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
      if (inst->is_got_funcref) {
        /* 関数アドレス: GOT 経由 (外部 libc 関数は @PAGE 直参照だと「does not have
         * address」でリンク失敗。GOT はローカル定義にも有効)。
         *   adrp d, _sym@GOTPAGE ; ldr d, [d, _sym@GOTPAGEOFF] */
        cg_emitf("  adrp %s, _%.*s@GOTPAGE\n", d, inst->sym_len, inst->sym ? inst->sym : "");
        cg_emitf("  ldr %s, [%s, _%.*s@GOTPAGEOFF]\n", d, d, inst->sym_len, inst->sym ? inst->sym : "");
      } else {
        /* グローバル変数のアドレス (_<name>@PAGE/PAGEOFF) を vreg に */
        cg_emitf("  adrp %s, _%.*s@PAGE\n", d, inst->sym_len, inst->sym ? inst->sym : "");
        cg_emitf("  add %s, %s, _%.*s@PAGEOFF\n", d, d, inst->sym_len, inst->sym ? inst->sym : "");
      }
  release_dst(ctx, inst->dst, d, spill);
}

static void gen_inst_load_tlv_addr(gen_ctx_t *ctx, ir_inst_t *inst) {
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
}

static void gen_inst_int_cast(gen_ctx_t *ctx, ir_inst_t *inst) {
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
}

static void gen_inst_vla_alloc(gen_ctx_t *ctx, ir_inst_t *inst) {
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
}

static void gen_inst_va_arg_area(gen_ctx_t *ctx, ir_inst_t *inst) {
      /* stack 上の variadic 引数領域の先頭 = x29 + total_size。
       * dst は spill (frame slot に書く)。 */
      char bd[8];
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
  emit_addsub_imm("add", d, "x29", ctx->total_size);
  release_dst(ctx, inst->dst, d, spill);
}

static void gen_inst_load_imm(gen_ctx_t *ctx, ir_inst_t *inst) {
      char bd[8];
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
  cg_emit_mov_imm(d, inst->src1.imm);
  release_dst(ctx, inst->dst, d, spill);
}

static void gen_inst_load_fp_imm(gen_ctx_t *ctx, ir_inst_t *inst) {
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
}

static void gen_inst_fp_binop(gen_ctx_t *ctx, ir_inst_t *inst) {
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
}

static void gen_inst_fp_cmp(gen_ctx_t *ctx, ir_inst_t *inst) {
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
}

static void gen_inst_f2i(gen_ctx_t *ctx, ir_inst_t *inst) {
      int src_double = (inst->src1.type == IR_TY_F64);
      const char *suf = src_double ? "d" : "s";
      cg_emitf("  ldr %s0, [x29, #%d]\n", suf, ctx->vreg_off[inst->src1.id]);
      char bd[8];
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
  cg_emitf("  fcvtzs %s, %s0\n", d, suf);
  release_dst(ctx, inst->dst, d, spill);
}

static void gen_inst_i2f(gen_ctx_t *ctx, ir_inst_t *inst) {
      char b1[8];
      const char *s1 = ensure_val_in(ctx, inst->src1, "x9", b1, sizeof(b1));
      int is_double = (inst->dst.type == IR_TY_F64);
      const char *suf = is_double ? "d" : "s";
  cg_emitf("  scvtf %s0, %s\n", suf, s1);
  cg_emitf("  str %s0, [x29, #%d]\n", suf, ctx->vreg_off[inst->dst.id]);
}

static void gen_inst_f2f(gen_ctx_t *ctx, ir_inst_t *inst) {
      /* float ↔ double 変換: fcvt */
      int src_double = (inst->src1.type == IR_TY_F64);
      int dst_double = (inst->dst.type == IR_TY_F64);
      const char *src_suf = src_double ? "d" : "s";
      const char *dst_suf = dst_double ? "d" : "s";
      cg_emitf("  ldr %s0, [x29, #%d]\n", src_suf, ctx->vreg_off[inst->src1.id]);
  cg_emitf("  fcvt %s1, %s0\n", dst_suf, src_suf);
  cg_emitf("  str %s1, [x29, #%d]\n", dst_suf, ctx->vreg_off[inst->dst.id]);
}

static void gen_inst_alloca(gen_ctx_t *ctx, ir_inst_t *inst) {
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
  emit_addsub_imm("add", d, "x29", off);
  release_dst(ctx, inst->dst, d, spill);
}

static void gen_inst_load(gen_ctx_t *ctx, ir_inst_t *inst) {
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
}

static void gen_inst_store(gen_ctx_t *ctx, ir_inst_t *inst) {
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
}

static void gen_inst_neg(gen_ctx_t *ctx, ir_inst_t *inst) {
      char b1[8], bd[8];
      const char *s1 = ensure_val_in(ctx, inst->src1, "x9", b1, sizeof(b1));
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
  cg_emitf("  neg %s, %s\n", d, s1);
  release_dst(ctx, inst->dst, d, spill);
}

static void gen_inst_not(gen_ctx_t *ctx, ir_inst_t *inst) {
      char b1[8], bd[8];
      const char *s1 = ensure_val_in(ctx, inst->src1, "x9", b1, sizeof(b1));
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
  cg_emitf("  mvn %s, %s\n", d, s1);
  release_dst(ctx, inst->dst, d, spill);
}

static void gen_inst_fneg(gen_ctx_t *ctx, ir_inst_t *inst) {
      int is_double = (inst->dst.type == IR_TY_F64);
      const char *suf = is_double ? "d" : "s";
      cg_emitf("  ldr %s0, [x29, #%d]\n", suf, ctx->vreg_off[inst->src1.id]);
  cg_emitf("  fneg %s0, %s0\n", suf, suf);
  cg_emitf("  str %s0, [x29, #%d]\n", suf, ctx->vreg_off[inst->dst.id]);
}

static void gen_inst_lea(gen_ctx_t *ctx, ir_inst_t *inst) {
      char b1[8], b2[8], bd[8];
      const char *s1 = ensure_val_in(ctx, inst->src1, "x9", b1, sizeof(b1));
      const char *s2 = ensure_val_in(ctx, inst->src2, "x10", b2, sizeof(b2));
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
  cg_emitf("  add %s, %s, %s\n", d, s1, s2);
  release_dst(ctx, inst->dst, d, spill);
}

static void gen_inst_memcpy(gen_ctx_t *ctx, ir_inst_t *inst) {
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
}

static void gen_inst_int_binop(gen_ctx_t *ctx, ir_inst_t *inst) {
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
}

static void gen_inst_int_cmp(gen_ctx_t *ctx, ir_inst_t *inst) {
      char b1[8], b2[8], bd[8];
      const char *s1 = ensure_val_in(ctx, inst->src1, "x9", b1, sizeof(b1));
      const char *s2 = ensure_val_in(ctx, inst->src2, "x10", b2, sizeof(b2));
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
      /* オペランドが両方 i32 以下なら 32bit 比較 (w レジスタ) を出す。codegen は値を
       * 64bit レジスタで保持するが、計算結果の i32 (TRUNC や funcall 戻り値) は上位
       * 32bit が未定義で、64bit 比較すると負の int が大きな正値に化ける。int の関係/
       * 等価比較は 32bit で行うのが C 準拠 (`int f(int x){return x-1;} f(0)!=-1` 等)。
       * 符号は IR が ULT/ULE か LT/LE かで既に振り分け済み (operand の is_unsigned)。
       * ポインタ / long (i64) は従来どおり 64bit 比較。 */
      char w1[8], w2[8];
      if (ir_type_size(inst->src1.type) <= 4 && ir_type_size(inst->src2.type) <= 4) {
        to_w_name(s1, w1, sizeof(w1));
        to_w_name(s2, w2, sizeof(w2));
        s1 = w1; s2 = w2;
      }
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
}

static void gen_inst_param(gen_ctx_t *ctx, ir_inst_t *inst) {
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
      } else if (is_fp && idx >= 8) {
        /* stack-passed float/double arg: 8B slot に置かれている。
         * d0 を scratch として使い、slot へ転送。 */
        int stack_off = ctx->total_size + (idx - 8) * 8;
        const char *suf = (inst->dst.type == IR_TY_F64) ? "d" : "s";
        cg_emitf("  ldr %s0, [x29, #%d]\n", suf, stack_off);
        if (inst->dst.id >= 0 && inst->dst.id < ctx->f->next_vreg_id) {
          cg_emitf("  str %s0, [x29, #%d]\n", suf, ctx->vreg_off[inst->dst.id]);
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
}

static void gen_inst_call(gen_ctx_t *ctx, ir_inst_t *inst) {
      /* Apple ARM64 ABI:
       *   - 通常: 整数引数 x0..x7、float/double 引数 s0..d7 (独立カウンタ)。
       *   - variadic: 固定引数のみ通常レジスタ、可変引数は全て stack に置く。
       *     stack は 16-byte align、各 variadic arg は 8B スロット。
       *     float は引数渡しで double に促進。 */
      int var_stack_bytes = 0;
      int fixed_limit = inst->is_variadic_call ? inst->nargs_fixed : inst->nargs;
      /* 非 variadic で 9 個以降の引数: stack 渡し (Apple ARM64 ABI 簡略版)。
       * 引数を整数/FP で分類し、それぞれ register が 8 個を超えた分のみ stack に
       * 積む。実際の ABI では int と FP の register カウンタは独立しているので、
       * "9 個目" は両カテゴリ別々に判定する必要がある。 */
      int extra_stack_args = 0;
      if (!inst->is_variadic_call && inst->nargs > 0) {
        /* どの引数が stack に乗るか先に決める。 */
        int int_count = 0, fp_count = 0;
        int stack_idx[64];
        int stack_n = 0;
        for (int i = 0; i < inst->nargs; i++) {
          int is_fp = (inst->args[i].type == IR_TY_F32 || inst->args[i].type == IR_TY_F64);
          if (is_fp) {
            if (fp_count < 8) fp_count++;
            else stack_idx[stack_n++] = i;
          } else {
            if (int_count < 8) int_count++;
            else stack_idx[stack_n++] = i;
          }
        }
        if (stack_n > 0) {
          extra_stack_args = stack_n;
          var_stack_bytes = ((extra_stack_args + 1) / 2) * 16;
          cg_emitf("  sub sp, sp, #%d\n", var_stack_bytes);
          for (int k = 0; k < stack_n; k++) {
            ir_val_t arg = inst->args[stack_idx[k]];
            int slot_off = k * 8;
            int is_fp = (arg.type == IR_TY_F32 || arg.type == IR_TY_F64);
            char buf[8];
            if (is_fp) {
              const char *suf = (arg.type == IR_TY_F64) ? "d" : "s";
              char tmp_reg[8];
              snprintf(tmp_reg, sizeof(tmp_reg), "%s0", suf);
              const char *src = ensure_val_in(ctx, arg, tmp_reg, buf, sizeof(buf));
              if (strcmp(src, tmp_reg) != 0) cg_emitf("  fmov %s, %s\n", tmp_reg, src);
              cg_emitf("  str %s, [sp, #%d]\n", tmp_reg, slot_off);
            } else {
              const char *src = ensure_val_in(ctx, arg, "x9", buf, sizeof(buf));
              cg_emitf("  str %s, [sp, #%d]\n", src, slot_off);
            }
          }
        }
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
      /* 固定引数を通常レジスタに積む (int は x0-x7, FP は d0-d7)。
       * 9 個目以降は既に stack に積んだので、register が満杯なら skip する。 */
      int int_idx = 0;
      int fp_idx = 0;
      for (int i = 0; i < fixed_limit; i++) {
        ir_val_t arg = inst->args[i];
        int is_fp = (arg.type == IR_TY_F32 || arg.type == IR_TY_F64);
        if (is_fp && fp_idx >= 8) continue;
        if (!is_fp && int_idx >= 8) continue;
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
      /* _Complex 戻り値 (HFA): dst は {re,im} スロットの PTR。d0/d1 (s0/s1) を書き戻す。 */
  if (inst->ret_complex_half > 0 && inst->dst.id >= 0 && inst->dst.id < ctx->f->next_vreg_id) {
    const char *suf = (inst->ret_complex_half == 8) ? "d" : "s";
    char buf[8];
    const char *p = ensure_val_in(ctx, inst->dst, "x9", buf, sizeof(buf));
    if (strcmp(p, "x9") != 0) cg_emitf("  mov x9, %s\n", p);
    cg_emitf("  str %s0, [x9]\n", suf);
    cg_emitf("  str %s1, [x9, #%d]\n", suf, (int)inst->ret_complex_half);
    return;
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
}

static void gen_inst_br_cond(gen_ctx_t *ctx, ir_inst_t *inst) {
      char b1[8];
      const char *s1 = ensure_val_in(ctx, inst->src1, "x9", b1, sizeof(b1));
  cg_emitf("  cbnz %s, .L%.*s_%d\n", s1,
            ctx->f->name_len, ctx->f->name, inst->label_id);
  cg_emitf("  b .L%.*s_%d\n",
            ctx->f->name_len, ctx->f->name, inst->else_label_id);
}

static void gen_inst_ret(gen_ctx_t *ctx, ir_inst_t *inst) {
      if (inst->ret_complex_half > 0) {
        /* _Complex 戻り値 (HFA): src1 は {re,im} スロットの PTR。re→d0/s0, im→d1/s1。 */
        const char *suf = (inst->ret_complex_half == 8) ? "d" : "s";
        char buf[8];
        const char *p = ensure_val_in(ctx, inst->src1, "x9", buf, sizeof(buf));
        if (strcmp(p, "x9") != 0) cg_emitf("  mov x9, %s\n", p);
        cg_emitf("  ldr %s0, [x9]\n", suf);
        cg_emitf("  ldr %s1, [x9, #%d]\n", suf, (int)inst->ret_complex_half);
      } else if (inst->src1.id != IR_VAL_NONE) {
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
      emit_addsub_imm("add", "sp", "sp", ctx->total_size);
  cg_emitf("  ret\n");
}

/* アトミック演算の結果 (reg11 = w11/x11) を dst へ書く。dst が phys reg なら
 * mov、spill なら frame slot へ str。RL は "w" または "x"。 */
static void atomic_store_result(gen_ctx_t *ctx, ir_val_t dst, const char *RL) {
  if (dst.id < 0 || dst.id >= ctx->f->next_vreg_id) return;
  if (ctx->f->vreg_phys_reg && ctx->f->vreg_phys_reg[dst.id] >= 0) {
    cg_emitf("  mov %s%d, %s11\n", RL, ctx->f->vreg_phys_reg[dst.id], RL);
  } else {
    cg_emitf("  str %s11, [x29, #%d]\n", RL, ctx->vreg_off[dst.id]);
  }
}

/* C11 アトミック操作を Apple ARM64 の LSE 命令 + バリアで生成する。
 * 全操作を seq_cst 強度 (ldar/stlr/ld...al/swpal/casal/dmb ish) で出す
 * (規格上、要求より強い順序付けは常に安全)。scratch は x9..x14 (regalloc は
 * x19..x28 のみ使うので衝突しない)。幅は 1/2/4/8 バイト。 */
static void gen_inst_atomic(gen_ctx_t *ctx, ir_inst_t *inst) {
  int w = inst->atomic_width ? inst->atomic_width : 4;
  const char *wsfx = (w == 1) ? "b" : (w == 2) ? "h" : "";  /* 命令の幅サフィックス */
  int x64 = (w == 8);
  const char *RL = x64 ? "x" : "w";  /* データレジスタの幅レター */
  char buf[8];

  if (inst->atomic_kind == IR_ATOMIC_FENCE) {
    cg_emitf("  dmb ish\n");
    return;
  }

  /* ptr を x9 に。 */
  const char *p = ensure_val_in(ctx, inst->src1, "x9", buf, sizeof(buf));
  if (strcmp(p, "x9") != 0) cg_emitf("  mov x9, %s\n", p);

  if (inst->atomic_kind == IR_ATOMIC_LOAD) {
    cg_emitf("  ldar%s %s11, [x9]\n", wsfx, RL);
    /* 符号付き 1/2 バイトは sign-extend (ldar は zero-extend)。 */
    if (!x64 && w < 4 && !inst->is_unsigned_load) {
      cg_emitf("  sxt%s w11, w11\n", wsfx);
    }
    atomic_store_result(ctx, inst->dst, RL);
    return;
  }

  if (inst->atomic_kind == IR_ATOMIC_STORE) {
    const char *v = ensure_val_in(ctx, inst->src2, "x10", buf, sizeof(buf));
    if (strcmp(v, "x10") != 0) cg_emitf("  mov x10, %s\n", v);
    cg_emitf("  stlr%s %s10, [x9]\n", wsfx, RL);
    return;
  }

  if (inst->atomic_kind == IR_ATOMIC_RMW) {
    const char *v = ensure_val_in(ctx, inst->src2, "x10", buf, sizeof(buf));
    if (strcmp(v, "x10") != 0) cg_emitf("  mov x10, %s\n", v);
    const char *op = NULL;
    switch (inst->atomic_rmw_op) {
      case IR_ARMW_ADD: op = "ldaddal"; break;
      case IR_ARMW_SUB: op = "ldaddal"; cg_emitf("  neg %s10, %s10\n", RL, RL); break;
      case IR_ARMW_OR:  op = "ldsetal"; break;
      case IR_ARMW_XOR: op = "ldeoral"; break;
      case IR_ARMW_AND: op = "ldclral"; cg_emitf("  mvn %s10, %s10\n", RL, RL); break;
      case IR_ARMW_XCHG: op = "swpal"; break;
      default: op = "ldaddal"; break;
    }
    cg_emitf("  %s%s %s10, %s11, [x9]\n", op, wsfx, RL, RL);  /* old → reg11 */
    if (!x64 && w < 4 && !inst->is_unsigned_load) {
      cg_emitf("  sxt%s w11, w11\n", wsfx);
    }
    atomic_store_result(ctx, inst->dst, RL);
    return;
  }

  if (inst->atomic_kind == IR_ATOMIC_CAS) {
    /* src2 = expected の PTR、src3 = desired。CASAL: Ws=expected(in)/old(out)。 */
    char bep[8], bde[8];
    const char *ep = ensure_val_in(ctx, inst->src2, "x10", bep, sizeof(bep));
    if (strcmp(ep, "x10") != 0) cg_emitf("  mov x10, %s\n", ep);
    cg_emitf("  ldr%s %s12, [x10]\n", wsfx, RL);  /* expected 値 */
    const char *de = ensure_val_in(ctx, inst->src3, "x13", bde, sizeof(bde));
    if (strcmp(de, "x13") != 0) cg_emitf("  mov x13, %s\n", de);
    cg_emitf("  mov %s14, %s12\n", RL, RL);        /* Ws = expected */
    cg_emitf("  casal%s %s14, %s13, [x9]\n", wsfx, RL, RL);  /* w14 = old */
    cg_emitf("  cmp %s14, %s12\n", RL, RL);
    cg_emitf("  cset w11, eq\n");                  /* 成否 */
    cg_emitf("  str%s %s14, [x10]\n", wsfx, RL);   /* *expected = old (成功時は不変) */
    atomic_store_result(ctx, inst->dst, "w");
    return;
  }
}

/* IR_ALIGN_PTR: dst = (src1 + (A-1)) & ~(A-1)。過剰整列ローカル (_Alignas(>16)) の
 * アドレスを実行時に丸める (A = alloca_align)。x29 は 16 整列のみなので、固定オフセット
 * では >16 整列にできない。VLA の add/and 丸めと同じ手法 (`and #-A` は AArch64 の
 * 有効な bitmask 即値)。 */
static void gen_inst_align_ptr(gen_ctx_t *ctx, ir_inst_t *inst) {
  int a = inst->alloca_align > 0 ? inst->alloca_align : 16;
  char b1[8], bd[8];
  const char *s1 = ensure_val_in(ctx, inst->src1, "x9", b1, sizeof(b1));
  int spill = 0;
  const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
  emit_addsub_imm("add", d, s1, a - 1);
  cg_emitf("  and %s, %s, #%d\n", d, d, -a);
  release_dst(ctx, inst->dst, d, spill);
}

static void gen_func(ir_func_t *f) {
  /* レジスタ割り付け */
  ir_regalloc_function(f);

  gen_ctx_t ctx = {0};
  ctx.f = f;
  layout_frame(&ctx);

  /* static 関数 (内部リンケージ) は .global を出さない (C11 6.2.2p3)。
   * 出すと別 TU の同名 static とリンク衝突する (duplicate symbol)。 */
  if (!f->is_static) cg_emitf(".global _%.*s\n", f->name_len, f->name);
  cg_emitf(".align 2\n");
  cg_emitf("_%.*s:\n", f->name_len, f->name);
  emit_addsub_imm("sub", "sp", "sp", ctx.total_size);
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
