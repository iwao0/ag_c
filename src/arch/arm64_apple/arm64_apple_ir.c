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
#include "../../ir/ir.h"
#include "../../diag/diag.h"
#include "../../lowering/frame_layout.h"
#include "../../lowering/abi_lowering.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  ag_codegen_emit_context_t *emit_context;
  const ir_abi_module_t *abi;
  ir_func_t *f;
  int *vreg_off;
  int alloca_base;
  int result_area_off;
  int total_size;
  int *alloca_region_off;
  int alloca_next;
  /* x19..x28 (10 個 callee-saved) のうち、この関数が使った reg のフラグ */
  int reg_used[10];
  int saved_count;
  int saved_area_size;  /* prologue で確保する saved area のバイト数 */
} gen_ctx_t;

#define arm64_cg_emitf(ctx, ...) \
  cg_emitf_in((ctx)->emit_context, __VA_ARGS__)

static const ir_abi_signature_t *function_abi(
    const gen_ctx_t *ctx) {
  return ctx ? ir_abi_function_signature(ctx->abi, ctx->f) : NULL;
}

static const ir_abi_signature_t *call_abi(
    const gen_ctx_t *ctx, const ir_inst_t *call) {
  return ctx ? ir_abi_call_signature(ctx->abi, call) : NULL;
}

static const ir_abi_argument_t *call_arguments(
    const gen_ctx_t *ctx, const ir_inst_t *call,
    int *argument_count) {
  size_t count = 0;
  const ir_abi_argument_t *arguments = ir_abi_call_arguments(
      ctx ? ctx->abi : NULL, call, &count);
  if (count > INT_MAX || (count > 0 && !arguments)) abort();
  if (argument_count) *argument_count = (int)count;
  return arguments;
}

static int round_up(int v, int a) {
  return (v + a - 1) / a * a;
}

static int scan_alloca_end(ir_func_t *f, int start_offset) {
  frame_layout_t layout;
  frame_layout_reset(&layout);
  frame_layout_reserve_prefix(&layout, start_offset);
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      if (i->op == IR_ALLOCA) {
        int sz = i->alloca_size > 0 ? i->alloca_size : 8;
        int al = i->alloca_align > 0 ? i->alloca_align : 8;
        (void)frame_layout_allocate(&layout, sz, al);
      }
    }
  }
  return layout.next_offset;
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
  ctx->result_area_off = -1;
  int frame_prefix = vreg_base + nvregs * 8;
  const ir_abi_signature_t *abi = function_abi(ctx);
  if (ir_abi_signature_result_is_indirect(abi)) {
    ctx->result_area_off = frame_prefix;
    frame_prefix += 8;
  }
  ctx->alloca_base = frame_prefix;
  ctx->alloca_next = ctx->alloca_base;
  ctx->alloca_region_off = calloc((size_t)(nvregs > 0 ? nvregs : 1), sizeof(int));
  int raw = scan_alloca_end(ctx->f, ctx->alloca_base);
  ctx->total_size = round_up(raw, 16);
  if (ctx->total_size < 32) ctx->total_size = 32;
}

/* `add/sub dst, src, #imm` を emit する。ARM64 の add/sub 即値は imm12 (0..4095) または
 * imm12<<12 (4096 の倍数) しか符号化できず、`sub sp, sp, #4112` や `add x19, x29, #8576`
 * のような 4095 超の即値は無効命令になる (大きいスタックフレームで発生)。4095 を超える場合は
 * 4096 の倍数部 (lsl #12) と端数の 2 命令に分割する (clang と同じ)。imm は非負・16MB 未満を想定。 */
static void emit_addsub_imm(
    gen_ctx_t *ctx, const char *op,
    const char *dst, const char *src, int imm) {
  if (imm <= 4095) {
    arm64_cg_emitf(ctx, "  %s %s, %s, #%d\n", op, dst, src, imm);
    return;
  }
  arm64_cg_emitf(ctx, "  %s %s, %s, #%d, lsl #12\n", op, dst, src, (imm >> 12) & 0xfff);
  if (imm & 0xfff) arm64_cg_emitf(ctx, "  %s %s, %s, #%d\n", op, dst, dst, imm & 0xfff);
}

static int frame_offset_scale_for_reg(const char *reg) {
  if (!reg || !reg[0]) return 8;
  switch (reg[0]) {
    case 'b': return 1;
    case 'h': return 2;
    case 'w':
    case 's': return 4;
    default: return 8;
  }
}

static int frame_offset_fits_unsigned(const char *reg, int off) {
  int scale = frame_offset_scale_for_reg(reg);
  return off >= 0 && off <= 4095 * scale && (off % scale) == 0;
}

static void emit_frame_load(gen_ctx_t *ctx, const char *reg, int off) {
  if (frame_offset_fits_unsigned(reg, off)) {
    arm64_cg_emitf(ctx, "  ldr %s, [x29, #%d]\n", reg, off);
    return;
  }
  emit_addsub_imm(ctx, "add", "x16", "x29", off);
  arm64_cg_emitf(ctx, "  ldr %s, [x16]\n", reg);
}

static void emit_frame_store(gen_ctx_t *ctx, const char *reg, int off) {
  if (frame_offset_fits_unsigned(reg, off)) {
    arm64_cg_emitf(ctx, "  str %s, [x29, #%d]\n", reg, off);
    return;
  }
  const char *addr = (strcmp(reg, "x16") == 0 || strcmp(reg, "w16") == 0) ? "x17" : "x16";
  emit_addsub_imm(ctx, "add", addr, "x29", off);
  arm64_cg_emitf(ctx, "  str %s, [%s]\n", reg, addr);
}

/* prologue: x29/x30 のあと、使われた callee-saved reg を保存する。 */
static void emit_save_regs(gen_ctx_t *ctx) {
  int off = 16;
  for (int i = 0; i < 10; i++) {
    if (!ctx->reg_used[i]) continue;
    arm64_cg_emitf(ctx, "  str x%d, [x29, #%d]\n", 19 + i, off);
    off += 8;
  }
}

/* epilogue: 保存した reg を復元する。 */
static void emit_restore_regs(gen_ctx_t *ctx) {
  int off = 16;
  for (int i = 0; i < 10; i++) {
    if (!ctx->reg_used[i]) continue;
    arm64_cg_emitf(ctx, "  ldr x%d, [x29, #%d]\n", 19 + i, off);
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
    cg_emit_mov_imm_in(ctx->emit_context, scratch, v.imm);
    return out_buf;
  }
  if (v.id == IR_VAL_NONE) {
    snprintf(out_buf, out_size, "%s", scratch);
    arm64_cg_emitf(ctx, "  mov %s, #0\n", scratch);
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
    emit_frame_load(ctx, scratch, ctx->vreg_off[vv]);
    return out_buf;
  }
  snprintf(out_buf, out_size, "%s", scratch);
  arm64_cg_emitf(ctx, "  mov %s, #0\n", scratch);
  return out_buf;
}

static const char *ensure_abi_argument_in(
    gen_ctx_t *ctx, const ir_abi_argument_t *argument,
    const char *scratch, char *out_buf, size_t out_size) {
  if (!argument) abort();
  if (argument->access == IR_ABI_ARGUMENT_DIRECT)
    return ensure_val_in(
        ctx, argument->source, scratch, out_buf, out_size);
  if (argument->access != IR_ABI_ARGUMENT_LOAD ||
      argument->source.type != IR_TY_PTR)
    abort();
  const char *address_scratch = strcmp(scratch, "x16") == 0
                                    ? "x17" : "x16";
  char address_buf[8];
  const char *address = ensure_val_in(
      ctx, argument->source, address_scratch,
      address_buf, sizeof(address_buf));
  snprintf(out_buf, out_size, "%s", scratch);
  if (argument->byte_offset == 0) {
    arm64_cg_emitf(ctx, "  ldr %s, [%s]\n", scratch, address);
  } else {
    arm64_cg_emitf(ctx, "  ldr %s, [%s, #%d]\n",
                   scratch, address, argument->byte_offset);
  }
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
  emit_frame_store(ctx, reg, ctx->vreg_off[dst.id]);
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
static void gen_inst_param_bind(gen_ctx_t *ctx, ir_inst_t *inst);
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
      arm64_cg_emitf(ctx, ".L%.*s_%d:\n", ctx->f->name_len, ctx->f->name, inst->label_id);
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
    case IR_PARAM_BIND:    gen_inst_param_bind(ctx, inst); return;
    case IR_CALL:          gen_inst_call(ctx, inst); return;
    case IR_BR:
      arm64_cg_emitf(ctx, "  b .L%.*s_%d\n", ctx->f->name_len, ctx->f->name, inst->label_id);
      return;
    case IR_BR_COND:       gen_inst_br_cond(ctx, inst); return;
    case IR_RET:           gen_inst_ret(ctx, inst); return;
    default:
      diag_emit_internalf_in(
          cg_context_diagnostics(ctx->emit_context),
          DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP,
          diag_message_for_in(
              cg_context_diagnostics(ctx->emit_context),
              DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP),
          ir_op_name(inst->op));
  }
}

/* -------- gen_inst の op 別ヘルパ実装 -------- */

static void gen_inst_load_str(gen_ctx_t *ctx, ir_inst_t *inst) {
      /* 文字列リテラルのラベルアドレス (.LC<id>) を vreg に */
      char bd[8];
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
      arm64_cg_emitf(ctx, "  adrp %s, %.*s@PAGE\n", d, inst->sym_len, inst->sym ? inst->sym : "");
      arm64_cg_emitf(ctx, "  add %s, %s, %.*s@PAGEOFF\n", d, d, inst->sym_len, inst->sym ? inst->sym : "");
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
        arm64_cg_emitf(ctx, "  adrp %s, _%.*s@GOTPAGE\n", d, inst->sym_len, inst->sym ? inst->sym : "");
        arm64_cg_emitf(ctx, "  ldr %s, [%s, _%.*s@GOTPAGEOFF]\n", d, d, inst->sym_len, inst->sym ? inst->sym : "");
      } else {
        /* グローバル変数のアドレス (_<name>@PAGE/PAGEOFF) を vreg に */
        arm64_cg_emitf(ctx, "  adrp %s, _%.*s@PAGE\n", d, inst->sym_len, inst->sym ? inst->sym : "");
        arm64_cg_emitf(ctx, "  add %s, %s, _%.*s@PAGEOFF\n", d, d, inst->sym_len, inst->sym ? inst->sym : "");
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
      arm64_cg_emitf(ctx, "  adrp x0, _%.*s@TLVPPAGE\n", inst->sym_len, inst->sym ? inst->sym : "");
      arm64_cg_emitf(ctx, "  ldr x0, [x0, _%.*s@TLVPPAGEOFF]\n", inst->sym_len, inst->sym ? inst->sym : "");
      arm64_cg_emitf(ctx, "  ldr x8, [x0]\n");
      arm64_cg_emitf(ctx, "  blr x8\n");
  if (inst->dst.id >= 0 && inst->dst.id < ctx->f->next_vreg_id) {
    emit_frame_store(ctx, "x0", ctx->vreg_off[inst->dst.id]);
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
        arm64_cg_emitf(ctx, "  uxtw %s, %s\n", d, w_src);
      } else if (inst->op == IR_SEXT) {
        arm64_cg_emitf(ctx, "  sxtw %s, %s\n", d, w_src);
      } else {
        arm64_cg_emitf(ctx, "  mov %s, %s\n", w_dst, w_src);
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
      if (strcmp(src, "x9") != 0) arm64_cg_emitf(ctx, "  mov x9, %s\n", src);
      arm64_cg_emitf(ctx, "  add x9, x9, #15\n");
      arm64_cg_emitf(ctx, "  and x9, x9, #-16\n");
      arm64_cg_emitf(ctx, "  sub sp, sp, x9\n");
      arm64_cg_emitf(ctx, "  mov x9, sp\n");
  if (inst->dst.id >= 0 && inst->dst.id < ctx->f->next_vreg_id) {
    emit_frame_store(ctx, "x9", ctx->vreg_off[inst->dst.id]);
  }
}

static void gen_inst_va_arg_area(gen_ctx_t *ctx, ir_inst_t *inst) {
      /* stack 上の variadic 引数領域の先頭 = x29 + total_size。
       * dst は spill (frame slot に書く)。 */
      char bd[8];
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
  emit_addsub_imm(ctx, "add", d, "x29", ctx->total_size);
  release_dst(ctx, inst->dst, d, spill);
}

static void gen_inst_load_imm(gen_ctx_t *ctx, ir_inst_t *inst) {
      char bd[8];
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
  cg_emit_mov_imm_in(ctx->emit_context, d, inst->src1.imm);
  release_dst(ctx, inst->dst, d, spill);
}

static void gen_inst_load_fp_imm(gen_ctx_t *ctx, ir_inst_t *inst) {
      /* float/double 即値: int として一旦 x9 にロードし、fmov で fp reg に転送、
       * frame に str する。dst は spill 扱い。 */
      if (inst->dst.id < 0 || inst->dst.id >= ctx->f->next_vreg_id) return;
      if (inst->dst.type == IR_TY_F32) {
        union { float f; uint32_t i; } u = { .f = (float)inst->src1.fp_imm };
        cg_emit_mov_imm_in(ctx->emit_context, "x9", (long long)u.i);
        arm64_cg_emitf(ctx, "  fmov s0, w9\n");
        emit_frame_store(ctx, "s0", ctx->vreg_off[inst->dst.id]);
      } else {
        union { double d; uint64_t i; } u = { .d = inst->src1.fp_imm };
        cg_emit_mov_imm_in(ctx->emit_context, "x9", (long long)u.i);
        arm64_cg_emitf(ctx, "  fmov d0, x9\n");
        emit_frame_store(ctx, "d0", ctx->vreg_off[inst->dst.id]);
  }
}

static void gen_inst_fp_binop(gen_ctx_t *ctx, ir_inst_t *inst) {
      /* float vreg は frame 経由。d0/d1 (or s0/s1) にロード、演算、frame に str。 */
      int is_double = (inst->dst.type == IR_TY_F64);
      const char *suf = is_double ? "d" : "s";
      char r0[4], r1[4];
      snprintf(r0, sizeof(r0), "%s0", suf);
      snprintf(r1, sizeof(r1), "%s1", suf);
      emit_frame_load(ctx, r0, ctx->vreg_off[inst->src1.id]);
      emit_frame_load(ctx, r1, ctx->vreg_off[inst->src2.id]);
      const char *op = "fadd";
      switch (inst->op) {
        case IR_FADD: op = "fadd"; break;
        case IR_FSUB: op = "fsub"; break;
        case IR_FMUL: op = "fmul"; break;
        case IR_FDIV: op = "fdiv"; break;
        default: break;
      }
      arm64_cg_emitf(ctx, "  %s %s0, %s0, %s1\n", op, suf, suf, suf);
  emit_frame_store(ctx, r0, ctx->vreg_off[inst->dst.id]);
}

static void gen_inst_fp_cmp(gen_ctx_t *ctx, ir_inst_t *inst) {
      int src_double = (inst->src1.type == IR_TY_F64) || (inst->src2.type == IR_TY_F64);
      const char *suf = src_double ? "d" : "s";
      char r0[4], r1[4];
      snprintf(r0, sizeof(r0), "%s0", suf);
      snprintf(r1, sizeof(r1), "%s1", suf);
      emit_frame_load(ctx, r0, ctx->vreg_off[inst->src1.id]);
      emit_frame_load(ctx, r1, ctx->vreg_off[inst->src2.id]);
      arm64_cg_emitf(ctx, "  fcmp %s0, %s1\n", suf, suf);
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
  arm64_cg_emitf(ctx, "  cset %s, %s\n", d, cond);
  release_dst(ctx, inst->dst, d, spill);
}

static void gen_inst_f2i(gen_ctx_t *ctx, ir_inst_t *inst) {
      int src_double = (inst->src1.type == IR_TY_F64);
      const char *suf = src_double ? "d" : "s";
      char r0[4];
      snprintf(r0, sizeof(r0), "%s0", suf);
      emit_frame_load(ctx, r0, ctx->vreg_off[inst->src1.id]);
      char bd[8];
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
  arm64_cg_emitf(ctx, "  %s %s, %s0\n", inst->is_unsigned ? "fcvtzu" : "fcvtzs", d, suf);
  release_dst(ctx, inst->dst, d, spill);
}

static void gen_inst_i2f(gen_ctx_t *ctx, ir_inst_t *inst) {
      char b1[8];
      const char *s1 = ensure_val_in(ctx, inst->src1, "x9", b1, sizeof(b1));
      int is_double = (inst->dst.type == IR_TY_F64);
      const char *suf = is_double ? "d" : "s";
  arm64_cg_emitf(ctx, "  %s %s0, %s\n", inst->is_unsigned ? "ucvtf" : "scvtf", suf, s1);
  char r0[4];
  snprintf(r0, sizeof(r0), "%s0", suf);
  emit_frame_store(ctx, r0, ctx->vreg_off[inst->dst.id]);
}

static void gen_inst_f2f(gen_ctx_t *ctx, ir_inst_t *inst) {
      /* float ↔ double 変換: fcvt */
      int src_double = (inst->src1.type == IR_TY_F64);
      int dst_double = (inst->dst.type == IR_TY_F64);
      const char *src_suf = src_double ? "d" : "s";
      const char *dst_suf = dst_double ? "d" : "s";
      char r0[4], r1[4];
      snprintf(r0, sizeof(r0), "%s0", src_suf);
      snprintf(r1, sizeof(r1), "%s1", dst_suf);
      emit_frame_load(ctx, r0, ctx->vreg_off[inst->src1.id]);
  arm64_cg_emitf(ctx, "  fcvt %s1, %s0\n", dst_suf, src_suf);
  emit_frame_store(ctx, r1, ctx->vreg_off[inst->dst.id]);
}

static void gen_inst_alloca(gen_ctx_t *ctx, ir_inst_t *inst) {
      int sz = inst->alloca_size > 0 ? inst->alloca_size : 8;
      int al = inst->alloca_align > 0 ? inst->alloca_align : 8;
      frame_layout_t layout = {.next_offset = ctx->alloca_next};
      int off = frame_layout_allocate(&layout, sz, al);
      ctx->alloca_next = layout.next_offset;
      if (inst->dst.id >= 0 && inst->dst.id < ctx->f->next_vreg_id) {
        ctx->alloca_region_off[inst->dst.id] = off;
      }
      char bd[8];
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
  emit_addsub_imm(ctx, "add", d, "x29", off);
  release_dst(ctx, inst->dst, d, spill);
}

static void gen_inst_load(gen_ctx_t *ctx, ir_inst_t *inst) {
      char bp[8];
      const char *ptr = ensure_val_in(ctx, inst->src1, "x9", bp, sizeof(bp));
      /* float/double: frame に書く (spill 経路で統一) */
      if (inst->dst.type == IR_TY_F32) {
        arm64_cg_emitf(ctx, "  ldr s0, [%s]\n", ptr);
        emit_frame_store(ctx, "s0", ctx->vreg_off[inst->dst.id]);
        return;
      }
      if (inst->dst.type == IR_TY_F64) {
        arm64_cg_emitf(ctx, "  ldr d0, [%s]\n", ptr);
        emit_frame_store(ctx, "d0", ctx->vreg_off[inst->dst.id]);
        return;
      }
      char bd[8];
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x10", bd, sizeof(bd), &spill);
      char w_d[8];
      to_w_name(d, w_d, sizeof(w_d));
      if (inst->is_unsigned) {
        switch (inst->dst.type) {
          /* unsigned: ldrb/ldrh/ldr w は自動で zero-extend する */
          case IR_TY_I8:  arm64_cg_emitf(ctx, "  ldrb %s, [%s]\n", w_d, ptr); break;
          case IR_TY_I16: arm64_cg_emitf(ctx, "  ldrh %s, [%s]\n", w_d, ptr); break;
          case IR_TY_I32: arm64_cg_emitf(ctx, "  ldr %s, [%s]\n", w_d, ptr); break;
          default:        arm64_cg_emitf(ctx, "  ldr %s, [%s]\n", d, ptr); break;
        }
      } else {
        switch (inst->dst.type) {
          case IR_TY_I8:  arm64_cg_emitf(ctx, "  ldrsb %s, [%s]\n", d, ptr); break;
          case IR_TY_I16: arm64_cg_emitf(ctx, "  ldrsh %s, [%s]\n", d, ptr); break;
          case IR_TY_I32: arm64_cg_emitf(ctx, "  ldrsw %s, [%s]\n", d, ptr); break;
          default:        arm64_cg_emitf(ctx, "  ldr %s, [%s]\n", d, ptr); break;
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
          emit_frame_load(ctx, "s0", ctx->vreg_off[inst->src2.id]);
        }
        arm64_cg_emitf(ctx, "  str s0, [%s]\n", ptr);
        return;
      }
      if (inst->src2.type == IR_TY_F64) {
        if (inst->src2.id >= 0 && inst->src2.id < ctx->f->next_vreg_id) {
          emit_frame_load(ctx, "d0", ctx->vreg_off[inst->src2.id]);
        }
        arm64_cg_emitf(ctx, "  str d0, [%s]\n", ptr);
        return;
      }
      char bv[8];
      const char *val = ensure_val_in(ctx, inst->src2, "x10", bv, sizeof(bv));
      char wval[8];
      to_w_name(val, wval, sizeof(wval));
      switch (inst->src2.type) {
        case IR_TY_I8:  arm64_cg_emitf(ctx, "  strb %s, [%s]\n", wval, ptr); break;
        case IR_TY_I16: arm64_cg_emitf(ctx, "  strh %s, [%s]\n", wval, ptr); break;
        case IR_TY_I32: arm64_cg_emitf(ctx, "  str %s, [%s]\n", wval, ptr); break;
    default:        arm64_cg_emitf(ctx, "  str %s, [%s]\n", val, ptr); break;
  }
}

static void gen_inst_neg(gen_ctx_t *ctx, ir_inst_t *inst) {
      char b1[8], bd[8];
      const char *s1 = ensure_val_in(ctx, inst->src1, "x9", b1, sizeof(b1));
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
  arm64_cg_emitf(ctx, "  neg %s, %s\n", d, s1);
  release_dst(ctx, inst->dst, d, spill);
}

static void gen_inst_not(gen_ctx_t *ctx, ir_inst_t *inst) {
      char b1[8], bd[8];
      const char *s1 = ensure_val_in(ctx, inst->src1, "x9", b1, sizeof(b1));
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
  arm64_cg_emitf(ctx, "  mvn %s, %s\n", d, s1);
  release_dst(ctx, inst->dst, d, spill);
}

static void gen_inst_fneg(gen_ctx_t *ctx, ir_inst_t *inst) {
      int is_double = (inst->dst.type == IR_TY_F64);
      const char *suf = is_double ? "d" : "s";
      char r0[4];
      snprintf(r0, sizeof(r0), "%s0", suf);
      emit_frame_load(ctx, r0, ctx->vreg_off[inst->src1.id]);
  arm64_cg_emitf(ctx, "  fneg %s0, %s0\n", suf, suf);
  emit_frame_store(ctx, r0, ctx->vreg_off[inst->dst.id]);
}

static void gen_inst_lea(gen_ctx_t *ctx, ir_inst_t *inst) {
      char b1[8], b2[8], bd[8];
      const char *s1 = ensure_val_in(ctx, inst->src1, "x9", b1, sizeof(b1));
      const char *s2 = ensure_val_in(ctx, inst->src2, "x10", b2, sizeof(b2));
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
  arm64_cg_emitf(ctx, "  add %s, %s, %s\n", d, s1, s2);
  release_dst(ctx, inst->dst, d, spill);
}

static void gen_inst_memcpy(gen_ctx_t *ctx, ir_inst_t *inst) {
      char bp1[8], bp2[8];
      const char *dst_ptr = ensure_val_in(ctx, inst->src1, "x9", bp1, sizeof(bp1));
      const char *src_ptr = ensure_val_in(ctx, inst->src2, "x10", bp2, sizeof(bp2));
      int n = inst->alloca_size;
      int off = 0;
      for (; off + 8 <= n; off += 8) {
        arm64_cg_emitf(ctx, "  ldr x11, [%s, #%d]\n", src_ptr, off);
        arm64_cg_emitf(ctx, "  str x11, [%s, #%d]\n", dst_ptr, off);
      }
      for (; off + 4 <= n; off += 4) {
        arm64_cg_emitf(ctx, "  ldr w11, [%s, #%d]\n", src_ptr, off);
        arm64_cg_emitf(ctx, "  str w11, [%s, #%d]\n", dst_ptr, off);
      }
      for (; off + 2 <= n; off += 2) {
        arm64_cg_emitf(ctx, "  ldrh w11, [%s, #%d]\n", src_ptr, off);
        arm64_cg_emitf(ctx, "  strh w11, [%s, #%d]\n", dst_ptr, off);
      }
  for (; off < n; off++) {
    arm64_cg_emitf(ctx, "  ldrb w11, [%s, #%d]\n", src_ptr, off);
    arm64_cg_emitf(ctx, "  strb w11, [%s, #%d]\n", dst_ptr, off);
  }
}

static void gen_inst_int_binop(gen_ctx_t *ctx, ir_inst_t *inst) {
      char b1[8], b2[8], bd[8];
      const char *s1 = ensure_val_in(ctx, inst->src1, "x9", b1, sizeof(b1));
      const char *s2 = ensure_val_in(ctx, inst->src2, "x10", b2, sizeof(b2));
      int spill = 0;
      const char *d = acquire_dst(ctx, inst->dst, "x9", bd, sizeof(bd), &spill);
      const char *spill_reg = d;
      char w1[8], w2[8], wd[8];
      if (ir_type_size(inst->dst.type) <= 4) {
        to_w_name(s1, w1, sizeof(w1));
        to_w_name(s2, w2, sizeof(w2));
        to_w_name(d, wd, sizeof(wd));
        s1 = w1; s2 = w2; d = wd;
      }
      switch (inst->op) {
        case IR_ADD: arm64_cg_emitf(ctx, "  add %s, %s, %s\n", d, s1, s2); break;
        case IR_SUB: arm64_cg_emitf(ctx, "  sub %s, %s, %s\n", d, s1, s2); break;
        case IR_MUL: arm64_cg_emitf(ctx, "  mul %s, %s, %s\n", d, s1, s2); break;
        case IR_DIV: arm64_cg_emitf(ctx, "  sdiv %s, %s, %s\n", d, s1, s2); break;
        case IR_UDIV: arm64_cg_emitf(ctx, "  udiv %s, %s, %s\n", d, s1, s2); break;
        case IR_MOD:
          arm64_cg_emitf(ctx, "  sdiv %s11, %s, %s\n", ir_type_size(inst->dst.type) <= 4 ? "w" : "x", s1, s2);
          arm64_cg_emitf(ctx, "  msub %s, %s11, %s, %s\n", d, ir_type_size(inst->dst.type) <= 4 ? "w" : "x", s2, s1);
          break;
        case IR_UMOD:
          arm64_cg_emitf(ctx, "  udiv %s11, %s, %s\n", ir_type_size(inst->dst.type) <= 4 ? "w" : "x", s1, s2);
          arm64_cg_emitf(ctx, "  msub %s, %s11, %s, %s\n", d, ir_type_size(inst->dst.type) <= 4 ? "w" : "x", s2, s1);
          break;
        case IR_AND: arm64_cg_emitf(ctx, "  and %s, %s, %s\n", d, s1, s2); break;
        case IR_OR:  arm64_cg_emitf(ctx, "  orr %s, %s, %s\n", d, s1, s2); break;
        case IR_XOR: arm64_cg_emitf(ctx, "  eor %s, %s, %s\n", d, s1, s2); break;
        case IR_SHL: arm64_cg_emitf(ctx, "  lsl %s, %s, %s\n", d, s1, s2); break;
        case IR_SHR: arm64_cg_emitf(ctx, "  asr %s, %s, %s\n", d, s1, s2); break;
    case IR_LSR: arm64_cg_emitf(ctx, "  lsr %s, %s, %s\n", d, s1, s2); break;
    default: break;
  }
  release_dst(ctx, inst->dst, spill_reg, spill);
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
      arm64_cg_emitf(ctx, "  cmp %s, %s\n", s1, s2);
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
  arm64_cg_emitf(ctx, "  cset %s, %s\n", d, cond);
  release_dst(ctx, inst->dst, d, spill);
}

static int abi_piece_is_fp(const ir_abi_piece_t *piece) {
  return piece && (piece->type == IR_TY_F32 || piece->type == IR_TY_F64);
}

static int incoming_piece_index(
    const ir_abi_signature_t *signature, size_t physical_index) {
  if (!signature || physical_index >= signature->param_count) abort();
  int fp = abi_piece_is_fp(&signature->param_pieces[physical_index]);
  int index = 0;
  for (size_t i = 0; i < physical_index; i++) {
    if (abi_piece_is_fp(&signature->param_pieces[i]) == fp) index++;
  }
  return index;
}

static const char *incoming_piece_register(
    gen_ctx_t *ctx, const ir_abi_signature_t *signature,
    size_t physical_index, char *buffer, size_t buffer_size) {
  const ir_abi_piece_t *piece = &signature->param_pieces[physical_index];
  int index = incoming_piece_index(signature, physical_index);
  int fp = abi_piece_is_fp(piece);
  if (index < 8) {
    if (fp) {
      snprintf(buffer, buffer_size, "%c%d",
               piece->type == IR_TY_F64 ? 'd' : 's', index);
    } else {
      snprintf(buffer, buffer_size, "x%d", index);
    }
    return buffer;
  }
  int stack_offset = ctx->total_size + (index - 8) * 8;
  if (fp) {
    snprintf(buffer, buffer_size, "%c17",
             piece->type == IR_TY_F64 ? 'd' : 's');
  } else {
    snprintf(buffer, buffer_size, "x17");
  }
  emit_frame_load(ctx, buffer, stack_offset);
  return buffer;
}

static void emit_parameter_piece_store(
    gen_ctx_t *ctx, const char *destination, int byte_offset,
    ir_type_t type, const char *source) {
  char narrow_source[8];
  const char *opcode = "str";
  if (type == IR_TY_I8 || type == IR_TY_I16 || type == IR_TY_I32) {
    to_w_name(source, narrow_source, sizeof(narrow_source));
    source = narrow_source;
    if (type == IR_TY_I8) opcode = "strb";
    else if (type == IR_TY_I16) opcode = "strh";
  }
  if (byte_offset == 0)
    arm64_cg_emitf(ctx, "  %s %s, [%s]\n", opcode, source, destination);
  else
    arm64_cg_emitf(ctx, "  %s %s, [%s, #%d]\n",
                   opcode, source, destination, byte_offset);
}

static void emit_indirect_parameter_copy(
    gen_ctx_t *ctx, const char *destination,
    const char *source, int size) {
  int offset = 0;
  for (; offset + 8 <= size; offset += 8) {
    arm64_cg_emitf(ctx, "  ldr x9, [%s, #%d]\n", source, offset);
    arm64_cg_emitf(ctx, "  str x9, [%s, #%d]\n", destination, offset);
  }
  for (; offset + 4 <= size; offset += 4) {
    arm64_cg_emitf(ctx, "  ldr w9, [%s, #%d]\n", source, offset);
    arm64_cg_emitf(ctx, "  str w9, [%s, #%d]\n", destination, offset);
  }
  for (; offset + 2 <= size; offset += 2) {
    arm64_cg_emitf(ctx, "  ldrh w9, [%s, #%d]\n", source, offset);
    arm64_cg_emitf(ctx, "  strh w9, [%s, #%d]\n", destination, offset);
  }
  for (; offset < size; offset++) {
    arm64_cg_emitf(ctx, "  ldrb w9, [%s, #%d]\n", source, offset);
    arm64_cg_emitf(ctx, "  strb w9, [%s, #%d]\n", destination, offset);
  }
}

static void gen_inst_param_bind(gen_ctx_t *ctx, ir_inst_t *inst) {
  const ir_abi_signature_t *signature = function_abi(ctx);
  size_t piece_count = 0;
  size_t physical_index = 0;
  const ir_abi_piece_t *pieces = ir_abi_signature_parameter_pieces(
      signature, inst->parameter_index, &piece_count, &physical_index);
  if (!pieces || piece_count == 0 || inst->src1.type != IR_TY_PTR) abort();

  for (size_t i = 0; i < piece_count; i++) {
    char source_buffer[8];
    const char *source = incoming_piece_register(
        ctx, signature, physical_index + i,
        source_buffer, sizeof(source_buffer));
    char destination_buffer[8];
    const char *destination = ensure_val_in(
        ctx, inst->src1, "x16", destination_buffer,
        sizeof(destination_buffer));
    if (pieces[i].kind == IR_ABI_PIECE_INDIRECT) {
      emit_indirect_parameter_copy(
          ctx, destination, source, pieces[i].source_size);
    } else {
      emit_parameter_piece_store(
          ctx, destination, pieces[i].byte_offset,
          pieces[i].type, source);
    }
  }
}

static void gen_inst_call(gen_ctx_t *ctx, ir_inst_t *inst) {
      const ir_abi_signature_t *abi = call_abi(ctx, inst);
      if (!abi) abort();
      int argument_count = 0;
      const ir_abi_argument_t *arguments = call_arguments(
          ctx, inst, &argument_count);
      /* Apple ARM64 ABI:
       *   - 通常: 整数引数 x0..x7、float/double 引数 s0..d7 (独立カウンタ)。
       *   - variadic: 固定引数のみ通常レジスタ、可変引数は全て stack に置く。
       *     stack は 16-byte align、各 variadic arg は 8B スロット。
       *     float は引数渡しで double に促進。 */
      int var_stack_bytes = 0;
      int is_variadic_call = abi->is_variadic;
      int fixed_limit = (int)abi->fixed_param_count;
      /* 非 variadic で 9 個以降の引数: stack 渡し (Apple ARM64 ABI 簡略版)。
       * 引数を整数/FP で分類し、それぞれ register が 8 個を超えた分のみ stack に
       * 積む。実際の ABI では int と FP の register カウンタは独立しているので、
       * "9 個目" は両カテゴリ別々に判定する必要がある。 */
      int extra_stack_args = 0;
      if (!is_variadic_call && argument_count > 0) {
        /* どの引数が stack に乗るか先に決める。 */
        int int_count = 0, fp_count = 0;
        int *stack_idx = malloc(
            (size_t)argument_count * sizeof(*stack_idx));
        if (!stack_idx) abort();
        int stack_n = 0;
        for (int i = 0; i < argument_count; i++) {
          int is_fp = (arguments[i].type == IR_TY_F32 ||
                       arguments[i].type == IR_TY_F64);
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
          arm64_cg_emitf(ctx, "  sub sp, sp, #%d\n", var_stack_bytes);
          for (int k = 0; k < stack_n; k++) {
            const ir_abi_argument_t *arg =
                &arguments[stack_idx[k]];
            int slot_off = k * 8;
            int is_fp = (arg->type == IR_TY_F32 ||
                         arg->type == IR_TY_F64);
            char buf[8];
            if (is_fp) {
              const char *suf = (arg->type == IR_TY_F64) ? "d" : "s";
              char tmp_reg[8];
              snprintf(tmp_reg, sizeof(tmp_reg), "%s0", suf);
              const char *src = ensure_abi_argument_in(
                  ctx, arg, tmp_reg, buf, sizeof(buf));
              if (strcmp(src, tmp_reg) != 0) arm64_cg_emitf(ctx, "  fmov %s, %s\n", tmp_reg, src);
              arm64_cg_emitf(ctx, "  str %s, [sp, #%d]\n", tmp_reg, slot_off);
            } else {
              const char *src = ensure_abi_argument_in(
                  ctx, arg, "x9", buf, sizeof(buf));
              arm64_cg_emitf(ctx, "  str %s, [sp, #%d]\n", src, slot_off);
            }
          }
        }
        free(stack_idx);
      }
      if (is_variadic_call) {
        int nargs_var = argument_count - fixed_limit;
        var_stack_bytes = ((nargs_var + 1) / 2) * 16;
        if (var_stack_bytes > 0) {
          arm64_cg_emitf(ctx, "  sub sp, sp, #%d\n", var_stack_bytes);
        }
        /* 可変引数を stack slot に書く */
        for (int i = fixed_limit; i < argument_count; i++) {
          const ir_abi_argument_t *arg = &arguments[i];
          int slot_off = (i - fixed_limit) * 8;
          if (arg->type == IR_TY_F32) {
            /* float → double に昇格して書く */
            char buf[8];
            const char *src = ensure_abi_argument_in(
                ctx, arg, "s0", buf, sizeof(buf));
            if (strcmp(src, "s0") != 0) arm64_cg_emitf(ctx, "  fmov s0, %s\n", src);
            arm64_cg_emitf(ctx, "  fcvt d0, s0\n");
            arm64_cg_emitf(ctx, "  str d0, [sp, #%d]\n", slot_off);
          } else if (arg->type == IR_TY_F64) {
            char buf[8];
            const char *src = ensure_abi_argument_in(
                ctx, arg, "d0", buf, sizeof(buf));
            if (strcmp(src, "d0") != 0) arm64_cg_emitf(ctx, "  fmov d0, %s\n", src);
            arm64_cg_emitf(ctx, "  str d0, [sp, #%d]\n", slot_off);
          } else {
            char buf[8];
            const char *src = ensure_abi_argument_in(
                ctx, arg, "x9", buf, sizeof(buf));
            arm64_cg_emitf(ctx, "  str %s, [sp, #%d]\n", src, slot_off);
          }
        }
      }
      /* 固定引数を通常レジスタに積む (int は x0-x7, FP は d0-d7)。
       * 9 個目以降は既に stack に積んだので、register が満杯なら skip する。 */
      int int_idx = 0;
      int fp_idx = 0;
      for (int i = 0; i < fixed_limit; i++) {
        if (i >= argument_count) abort();
        const ir_abi_argument_t *arg = &arguments[i];
        int is_fp = (arg->type == IR_TY_F32 ||
                     arg->type == IR_TY_F64);
        if (is_fp && fp_idx >= 8) continue;
        if (!is_fp && int_idx >= 8) continue;
        char regname[8];
        if (is_fp) {
          const char *suf = (arg->type == IR_TY_F64) ? "d" : "s";
          snprintf(regname, sizeof(regname), "%s%d", suf, fp_idx++);
        } else {
          snprintf(regname, sizeof(regname), "x%d", int_idx++);
        }
        char buf[8];
        const char *src = ensure_abi_argument_in(
            ctx, arg, regname, buf, sizeof(buf));
        if (strcmp(src, regname) != 0) {
          if (is_fp) {
            arm64_cg_emitf(ctx, "  fmov %s, %s\n", regname, src);
          } else {
            arm64_cg_emitf(ctx, "  mov %s, %s\n", regname, src);
          }
        }
      }
      /* struct return: ret_area を x8 にロード */
      if (ir_abi_signature_result_is_indirect(abi) &&
          abi->result_area.id != IR_VAL_NONE) {
        char buf[8];
        const char *src = ensure_val_in(
            ctx, abi->result_area, "x8", buf, sizeof(buf));
        if (strcmp(src, "x8") != 0) {
          arm64_cg_emitf(ctx, "  mov x8, %s\n", src);
        }
      }
      if (inst->callee.id != IR_VAL_NONE) {
        /* 間接呼び出し: callee を x16 (= ip0、ABI 上 caller-saved の scratch)
         * にロードして blr。引数 reg (x0..x7) とは衝突しない。 */
        char buf[8];
        const char *src = ensure_val_in(ctx, inst->callee, "x16", buf, sizeof(buf));
        if (strcmp(src, "x16") != 0) arm64_cg_emitf(ctx, "  mov x16, %s\n", src);
        arm64_cg_emitf(ctx, "  blr x16\n");
      } else {
        arm64_cg_emitf(ctx, "  bl _%.*s\n", inst->sym_len, inst->sym ? inst->sym : "");
      }
      /* variadic で stack を使った分を戻す */
      if (var_stack_bytes > 0) {
        arm64_cg_emitf(ctx, "  add sp, sp, #%d\n", var_stack_bytes);
      }
      /* Multi-piece result: write each ABI result register to the logical
       * result object selected before target lowering. */
  size_t result_piece_count = 0;
  const ir_abi_piece_t *result_pieces =
      ir_abi_signature_result_pieces(abi, &result_piece_count);
  if (result_piece_count > 1 && abi->result_area.id != IR_VAL_NONE) {
    char buf[8];
    const char *p = ensure_val_in(
        ctx, abi->result_area, "x9", buf, sizeof(buf));
    if (strcmp(p, "x9") != 0) arm64_cg_emitf(ctx, "  mov x9, %s\n", p);
    for (size_t piece_index = 0; piece_index < result_piece_count;
         piece_index++) {
      const ir_abi_piece_t *piece = &result_pieces[piece_index];
      if (piece->kind != IR_ABI_PIECE_COMPLEX_REAL &&
          piece->kind != IR_ABI_PIECE_COMPLEX_IMAGINARY)
        abort();
      char source[8];
      snprintf(source, sizeof(source), "%c%zu",
               piece->type == IR_TY_F64 ? 'd' : 's', piece_index);
      emit_parameter_piece_store(
          ctx, "x9", piece->byte_offset, piece->type, source);
    }
    return;
  }
  if (ir_abi_signature_result_is_direct_aggregate(abi) &&
      abi->result_area.id != IR_VAL_NONE) {
    const ir_abi_piece_t *piece = result_pieces;
    if (!piece) abort();
    char buffer[8];
    const char *destination = ensure_val_in(
        ctx, abi->result_area, "x16", buffer, sizeof(buffer));
    emit_parameter_piece_store(
        ctx, destination, piece->byte_offset, piece->type, "x0");
    return;
  }
      /* 戻り値: float なら s0/d0、それ以外 x0 を dst slot へ。 */
  if (inst->dst.id >= 0 && inst->dst.id < ctx->f->next_vreg_id) {
    if (inst->dst.type == IR_TY_F32) {
      emit_frame_store(ctx, "s0", ctx->vreg_off[inst->dst.id]);
    } else if (inst->dst.type == IR_TY_F64) {
      emit_frame_store(ctx, "d0", ctx->vreg_off[inst->dst.id]);
    } else {
      emit_frame_store(ctx, "x0", ctx->vreg_off[inst->dst.id]);
    }
  }
}

static void gen_inst_br_cond(gen_ctx_t *ctx, ir_inst_t *inst) {
      char b1[8];
      const char *s1 = ensure_val_in(ctx, inst->src1, "x9", b1, sizeof(b1));
  arm64_cg_emitf(ctx, "  cbnz %s, .L%.*s_%d\n", s1,
            ctx->f->name_len, ctx->f->name, inst->label_id);
  arm64_cg_emitf(ctx, "  b .L%.*s_%d\n",
            ctx->f->name_len, ctx->f->name, inst->else_label_id);
}

static void gen_inst_ret(gen_ctx_t *ctx, ir_inst_t *inst) {
      const ir_abi_signature_t *abi = function_abi(ctx);
      if (!abi) abort();
      size_t result_piece_count = 0;
      const ir_abi_piece_t *result_pieces =
          ir_abi_signature_result_pieces(abi, &result_piece_count);
      if (ir_abi_signature_result_is_indirect(abi)) {
        if (inst->src1.type != IR_TY_PTR ||
            inst->src1.id == IR_VAL_NONE ||
            ctx->result_area_off < 0)
          abort();
        char source_buffer[8];
        const char *source = ensure_val_in(
            ctx, inst->src1, "x10",
            source_buffer, sizeof(source_buffer));
        emit_frame_load(ctx, "x11", ctx->result_area_off);
        emit_indirect_parameter_copy(
            ctx, "x11", source,
            ir_abi_signature_result_source_size(abi));
      } else if (result_piece_count > 1) {
        /* Multi-piece result: load the logical object into the registers
         * selected by ABI lowering. */
        if (inst->src1.type != IR_TY_PTR ||
            inst->src1.id == IR_VAL_NONE)
          abort();
        char buf[8];
        const char *p = ensure_val_in(ctx, inst->src1, "x9", buf, sizeof(buf));
        if (strcmp(p, "x9") != 0) arm64_cg_emitf(ctx, "  mov x9, %s\n", p);
        for (size_t piece_index = 0; piece_index < result_piece_count;
             piece_index++) {
          const ir_abi_piece_t *piece = &result_pieces[piece_index];
          if (piece->kind != IR_ABI_PIECE_COMPLEX_REAL &&
              piece->kind != IR_ABI_PIECE_COMPLEX_IMAGINARY)
            abort();
          const char *load = piece->type == IR_TY_F64 ? "ldr d" : "ldr s";
          if (piece->byte_offset == 0)
            arm64_cg_emitf(ctx, "  %s%zu, [x9]\n", load, piece_index);
          else
            arm64_cg_emitf(ctx, "  %s%zu, [x9, #%d]\n", load,
                           piece_index, piece->byte_offset);
        }
      } else if (ir_abi_signature_result_is_direct_aggregate(abi)) {
        if (inst->src1.type != IR_TY_PTR ||
            inst->src1.id == IR_VAL_NONE)
          abort();
        const ir_abi_piece_t *piece = result_pieces;
        if (!piece) abort();
        char buffer[8];
        const char *source = ensure_val_in(
            ctx, inst->src1, "x9", buffer, sizeof(buffer));
        const char *load = piece->type == IR_TY_I64 ? "ldr x0" :
                           piece->type == IR_TY_I32 ? "ldr w0" :
                           piece->type == IR_TY_I16 ? "ldrh w0" :
                           piece->type == IR_TY_I8 ? "ldrb w0" : NULL;
        if (!load) abort();
        arm64_cg_emitf(ctx, "  %s, [%s]\n", load, source);
      } else if (inst->src1.id != IR_VAL_NONE) {
        if (inst->src1.type == IR_TY_F32) {
          char buf[8];
          const char *src = ensure_val_in(ctx, inst->src1, "s0", buf, sizeof(buf));
          if (strcmp(src, "s0") != 0) arm64_cg_emitf(ctx, "  fmov s0, %s\n", src);
        } else if (inst->src1.type == IR_TY_F64) {
          char buf[8];
          const char *src = ensure_val_in(ctx, inst->src1, "d0", buf, sizeof(buf));
          if (strcmp(src, "d0") != 0) arm64_cg_emitf(ctx, "  fmov d0, %s\n", src);
        } else {
          char buf[8];
          const char *src = ensure_val_in(ctx, inst->src1, "x0", buf, sizeof(buf));
          if (strcmp(src, "x0") != 0) arm64_cg_emitf(ctx, "  mov x0, %s\n", src);
        }
      } else if (ir_abi_signature_direct_result_type(abi) != IR_TY_VOID) {
        arm64_cg_emitf(ctx, "  mov x0, #0\n");
      }
      emit_restore_regs(ctx);
      arm64_cg_emitf(ctx, "  mov sp, x29\n");
      arm64_cg_emitf(ctx, "  ldp x29, x30, [sp]\n");
      emit_addsub_imm(ctx, "add", "sp", "sp", ctx->total_size);
  arm64_cg_emitf(ctx, "  ret\n");
}

/* アトミック演算の結果 (reg11 = w11/x11) を dst へ書く。dst が phys reg なら
 * mov、spill なら frame slot へ str。RL は "w" または "x"。 */
static void atomic_store_result(gen_ctx_t *ctx, ir_val_t dst, const char *RL) {
  if (dst.id < 0 || dst.id >= ctx->f->next_vreg_id) return;
  if (ctx->f->vreg_phys_reg && ctx->f->vreg_phys_reg[dst.id] >= 0) {
    arm64_cg_emitf(ctx, "  mov %s%d, %s11\n", RL, ctx->f->vreg_phys_reg[dst.id], RL);
  } else {
    char reg[8];
    snprintf(reg, sizeof(reg), "%s11", RL);
    emit_frame_store(ctx, reg, ctx->vreg_off[dst.id]);
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
    arm64_cg_emitf(ctx, "  dmb ish\n");
    return;
  }

  /* ptr を x9 に。 */
  const char *p = ensure_val_in(ctx, inst->src1, "x9", buf, sizeof(buf));
  if (strcmp(p, "x9") != 0) arm64_cg_emitf(ctx, "  mov x9, %s\n", p);

  if (inst->atomic_kind == IR_ATOMIC_LOAD) {
    arm64_cg_emitf(ctx, "  ldar%s %s11, [x9]\n", wsfx, RL);
    /* 符号付き 1/2 バイトは sign-extend (ldar は zero-extend)。 */
    if (!x64 && w < 4 && !inst->is_unsigned) {
      arm64_cg_emitf(ctx, "  sxt%s w11, w11\n", wsfx);
    }
    atomic_store_result(ctx, inst->dst, RL);
    return;
  }

  if (inst->atomic_kind == IR_ATOMIC_STORE) {
    const char *v = ensure_val_in(ctx, inst->src2, "x10", buf, sizeof(buf));
    if (strcmp(v, "x10") != 0) arm64_cg_emitf(ctx, "  mov x10, %s\n", v);
    arm64_cg_emitf(ctx, "  stlr%s %s10, [x9]\n", wsfx, RL);
    return;
  }

  if (inst->atomic_kind == IR_ATOMIC_RMW) {
    const char *v = ensure_val_in(ctx, inst->src2, "x10", buf, sizeof(buf));
    if (strcmp(v, "x10") != 0) arm64_cg_emitf(ctx, "  mov x10, %s\n", v);
    const char *op = NULL;
    switch (inst->atomic_rmw_op) {
      case IR_ARMW_ADD: op = "ldaddal"; break;
      case IR_ARMW_SUB: op = "ldaddal"; arm64_cg_emitf(ctx, "  neg %s10, %s10\n", RL, RL); break;
      case IR_ARMW_OR:  op = "ldsetal"; break;
      case IR_ARMW_XOR: op = "ldeoral"; break;
      case IR_ARMW_AND: op = "ldclral"; arm64_cg_emitf(ctx, "  mvn %s10, %s10\n", RL, RL); break;
      case IR_ARMW_XCHG: op = "swpal"; break;
      default: op = "ldaddal"; break;
    }
    arm64_cg_emitf(ctx, "  %s%s %s10, %s11, [x9]\n", op, wsfx, RL, RL);  /* old → reg11 */
    if (!x64 && w < 4 && !inst->is_unsigned) {
      arm64_cg_emitf(ctx, "  sxt%s w11, w11\n", wsfx);
    }
    atomic_store_result(ctx, inst->dst, RL);
    return;
  }

  if (inst->atomic_kind == IR_ATOMIC_CAS) {
    /* src2 = expected の PTR、src3 = desired。CASAL: Ws=expected(in)/old(out)。 */
    char bep[8], bde[8];
    const char *ep = ensure_val_in(ctx, inst->src2, "x10", bep, sizeof(bep));
    if (strcmp(ep, "x10") != 0) arm64_cg_emitf(ctx, "  mov x10, %s\n", ep);
    arm64_cg_emitf(ctx, "  ldr%s %s12, [x10]\n", wsfx, RL);  /* expected 値 */
    const char *de = ensure_val_in(ctx, inst->src3, "x13", bde, sizeof(bde));
    if (strcmp(de, "x13") != 0) arm64_cg_emitf(ctx, "  mov x13, %s\n", de);
    arm64_cg_emitf(ctx, "  mov %s14, %s12\n", RL, RL);        /* Ws = expected */
    arm64_cg_emitf(ctx, "  casal%s %s14, %s13, [x9]\n", wsfx, RL, RL);  /* w14 = old */
    arm64_cg_emitf(ctx, "  cmp %s14, %s12\n", RL, RL);
    arm64_cg_emitf(ctx, "  cset w11, eq\n");                  /* 成否 */
    arm64_cg_emitf(ctx, "  str%s %s14, [x10]\n", wsfx, RL);   /* *expected = old (成功時は不変) */
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
  emit_addsub_imm(ctx, "add", d, s1, a - 1);
  arm64_cg_emitf(ctx, "  and %s, %s, #%d\n", d, d, -a);
  release_dst(ctx, inst->dst, d, spill);
}

static void gen_func(
    ag_codegen_emit_context_t *emit_context, ir_func_t *f,
    const ir_abi_module_t *abi) {
  /* レジスタ割り付け */
  ir_regalloc_function(f);

  gen_ctx_t ctx = {0};
  ctx.emit_context = emit_context;
  ctx.abi = abi;
  ctx.f = f;
  layout_frame(&ctx);

  /* static 関数 (内部リンケージ) は .global を出さない (C11 6.2.2p3)。
   * 出すと別 TU の同名 static とリンク衝突する (duplicate symbol)。 */
  if (!f->is_static)
    arm64_cg_emitf(&ctx, ".global _%.*s\n", f->name_len, f->name);
  arm64_cg_emitf(&ctx, ".align 2\n");
  arm64_cg_emitf(&ctx, "_%.*s:\n", f->name_len, f->name);
  emit_addsub_imm(&ctx, "sub", "sp", "sp", ctx.total_size);
  arm64_cg_emitf(&ctx, "  stp x29, x30, [sp]\n");
  arm64_cg_emitf(&ctx, "  mov x29, sp\n");
  emit_save_regs(&ctx);
  if (ctx.result_area_off >= 0)
    emit_frame_store(&ctx, "x8", ctx.result_area_off);

  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      gen_inst(&ctx, i);
    }
  }

  free(ctx.vreg_off);
  free(ctx.alloca_region_off);
}

void gen_ir_module_in(
    ag_codegen_emit_context_t *emit_context, ir_module_t *m,
    const ir_abi_module_t *abi) {
  if (!emit_context) abort();
  if (!m) return;
  for (ir_func_t *f = m->funcs; f; f = f->next) {
    gen_func(emit_context, f, abi);
  }
}
