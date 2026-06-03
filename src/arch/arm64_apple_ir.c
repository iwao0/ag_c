/*
 * ARM64 Apple ABI: IR → ASM 出力 (Phase 2 最小版)。
 *
 * フレームレイアウト:
 *   [x29 + 0 ..16]      saved x29, x30
 *   [x29 + 16 .. 16+N*8] vreg slots (8B 単位、N = func->next_vreg_id)
 *   [x29 + 16+N*8 ..]   ALLOCA 領域
 *
 * Phase 2 では「全 vreg をフレームスロットに割り付ける」だけの最小実装。
 * 動作確認用なので生成コードは冗長 (Phase 5 でレジスタ割り付けに置換予定)。
 *
 * サポート命令: LOAD_IMM, LOAD, STORE, ALLOCA, ADD, SUB, MUL, DIV, MOD, RET。
 */

#include "arm64_apple_ir.h"
#include "arm64_apple_emit.h"
#include "../ir/ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  ir_func_t *f;
  int *vreg_off;      /* vreg id → frame offset (x29 からの正のオフセット) */
  int alloca_base;    /* ALLOCA 領域の開始 offset */
  int total_size;     /* フレーム合計 (16-byte align 済み) */
  /* alloca: ALLOCA 命令のたびに alloca_next を進める。dst.vreg にその開始 offset を入れる。
   * alloca_dst_off[i] = i 番目の vreg が ALLOCA で確保した領域の x29 オフセット */
  int *alloca_region_off;
  int alloca_next;
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

static void layout_frame(gen_ctx_t *ctx) {
  int nvregs = ctx->f->next_vreg_id;
  ctx->vreg_off = calloc((size_t)(nvregs > 0 ? nvregs : 1), sizeof(int));
  for (int i = 0; i < nvregs; i++) {
    ctx->vreg_off[i] = 16 + i * 8;
  }
  ctx->alloca_base = 16 + nvregs * 8;
  ctx->alloca_next = ctx->alloca_base;
  ctx->alloca_region_off = calloc((size_t)(nvregs > 0 ? nvregs : 1), sizeof(int));
  int alloca_total = scan_alloca_total(ctx->f);
  /* フレーム合計を 16-byte align */
  int raw = ctx->alloca_base + alloca_total;
  ctx->total_size = round_up(raw, 16);
  /* 最小 32 byte は確保 (空関数でも prologue/epilogue が動くように) */
  if (ctx->total_size < 32) ctx->total_size = 32;
}

/* vreg を x9 にロードする。即値ならその値、vreg ならフレームから ldr。 */
static void load_val_to(gen_ctx_t *ctx, ir_val_t v, const char *reg) {
  if (v.id == IR_VAL_IMM) {
    cg_emitf("  mov %s, #%lld\n", reg, v.imm);
    return;
  }
  if (v.id >= 0 && v.id < ctx->f->next_vreg_id) {
    cg_emitf("  ldr %s, [x29, #%d]\n", reg, ctx->vreg_off[v.id]);
    return;
  }
  /* IR_VAL_NONE: undefined */
  cg_emitf("  mov %s, #0\n", reg);
}

static void store_val_from(gen_ctx_t *ctx, ir_val_t dst, const char *reg) {
  if (dst.id < 0 || dst.id >= ctx->f->next_vreg_id) return;
  cg_emitf("  str %s, [x29, #%d]\n", reg, ctx->vreg_off[dst.id]);
}

static void gen_inst(gen_ctx_t *ctx, ir_inst_t *inst) {
  switch (inst->op) {
    case IR_LABEL:
      cg_emitf(".L%.*s_%d:\n", ctx->f->name_len, ctx->f->name, inst->label_id);
      return;
    case IR_LOAD_IMM:
      load_val_to(ctx, inst->src1, "x9");
      store_val_from(ctx, inst->dst, "x9");
      return;
    case IR_ALLOCA: {
      int sz = inst->alloca_size > 0 ? inst->alloca_size : 8;
      int al = inst->alloca_align > 0 ? inst->alloca_align : 8;
      ctx->alloca_next = round_up(ctx->alloca_next, al);
      int off = ctx->alloca_next;
      ctx->alloca_next += sz;
      if (inst->dst.id >= 0 && inst->dst.id < ctx->f->next_vreg_id) {
        ctx->alloca_region_off[inst->dst.id] = off;
      }
      /* dst.vreg = x29 + off (アドレス) */
      cg_emitf("  add x9, x29, #%d\n", off);
      store_val_from(ctx, inst->dst, "x9");
      return;
    }
    case IR_LOAD: {
      /* dst = *src1 */
      load_val_to(ctx, inst->src1, "x9");  /* ptr */
      /* 型に応じてロードサイズ */
      switch (inst->dst.type) {
        case IR_TY_I8:  cg_emitf("  ldrsb x10, [x9]\n"); break;
        case IR_TY_I16: cg_emitf("  ldrsh x10, [x9]\n"); break;
        case IR_TY_I32: cg_emitf("  ldrsw x10, [x9]\n"); break;
        default:        cg_emitf("  ldr x10, [x9]\n"); break;
      }
      store_val_from(ctx, inst->dst, "x10");
      return;
    }
    case IR_STORE: {
      /* *src1 = src2 */
      load_val_to(ctx, inst->src1, "x9");   /* ptr */
      load_val_to(ctx, inst->src2, "x10");  /* val */
      switch (inst->src2.type) {
        case IR_TY_I8:  cg_emitf("  strb w10, [x9]\n"); break;
        case IR_TY_I16: cg_emitf("  strh w10, [x9]\n"); break;
        case IR_TY_I32: cg_emitf("  str w10, [x9]\n"); break;
        default:        cg_emitf("  str x10, [x9]\n"); break;
      }
      return;
    }
    case IR_ADD:
    case IR_SUB:
    case IR_MUL:
    case IR_DIV:
    case IR_MOD: {
      load_val_to(ctx, inst->src1, "x9");
      load_val_to(ctx, inst->src2, "x10");
      switch (inst->op) {
        case IR_ADD: cg_emitf("  add x9, x9, x10\n"); break;
        case IR_SUB: cg_emitf("  sub x9, x9, x10\n"); break;
        case IR_MUL: cg_emitf("  mul x9, x9, x10\n"); break;
        case IR_DIV: cg_emitf("  sdiv x9, x9, x10\n"); break;
        case IR_MOD:
          cg_emitf("  sdiv x11, x9, x10\n");
          cg_emitf("  msub x9, x11, x10, x9\n");
          break;
        default: break;
      }
      store_val_from(ctx, inst->dst, "x9");
      return;
    }
    case IR_LT:
    case IR_LE:
    case IR_EQ:
    case IR_NE: {
      load_val_to(ctx, inst->src1, "x9");
      load_val_to(ctx, inst->src2, "x10");
      cg_emitf("  cmp x9, x10\n");
      const char *cond = "eq";
      switch (inst->op) {
        case IR_LT: cond = "lt"; break;
        case IR_LE: cond = "le"; break;
        case IR_EQ: cond = "eq"; break;
        case IR_NE: cond = "ne"; break;
        default: break;
      }
      cg_emitf("  cset x9, %s\n", cond);
      store_val_from(ctx, inst->dst, "x9");
      return;
    }
    case IR_BR:
      cg_emitf("  b .L%.*s_%d\n", ctx->f->name_len, ctx->f->name, inst->label_id);
      return;
    case IR_BR_COND:
      load_val_to(ctx, inst->src1, "x9");
      /* cbz/cbnz は 32bit/64bit のどちらでも動く。条件は 0/非0。 */
      cg_emitf("  cbnz x9, .L%.*s_%d\n",
                ctx->f->name_len, ctx->f->name, inst->label_id);
      cg_emitf("  b .L%.*s_%d\n",
                ctx->f->name_len, ctx->f->name, inst->else_label_id);
      return;
    case IR_RET: {
      if (inst->src1.id != IR_VAL_NONE) {
        load_val_to(ctx, inst->src1, "x0");
      } else {
        cg_emitf("  mov x0, #0\n");
      }
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
  gen_ctx_t ctx = {0};
  ctx.f = f;
  layout_frame(&ctx);

  cg_emitf(".global _%.*s\n", f->name_len, f->name);
  cg_emitf(".align 2\n");
  cg_emitf("_%.*s:\n", f->name_len, f->name);
  /* prologue */
  cg_emitf("  sub sp, sp, #%d\n", ctx.total_size);
  cg_emitf("  stp x29, x30, [sp]\n");
  cg_emitf("  mov x29, sp\n");

  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      gen_inst(&ctx, i);
    }
  }
  /* 末尾に ret が無ければ補う (safety) */
  /* (Phase 2 では IR_RET が build 時に必ず挿入されている前提) */

  free(ctx.vreg_off);
  free(ctx.alloca_region_off);
}

void gen_ir_module(ir_module_t *m) {
  if (!m) return;
  for (ir_func_t *f = m->funcs; f; f = f->next) {
    gen_func(f);
  }
}
