/*
 * Phase 5: ブロック内線形スキャンレジスタ割り付け。
 *
 * 簡略版の前提:
 *   - 各 vreg は SSA 風に「1 回だけ def」される (builder の現在の挙動)
 *   - ブロック境界では全 vreg を spill とみなす (= block 内に閉じた割り付け)
 *   - 関数呼び出し (IR_CALL) の前後で caller-saved レジスタは全 invalidate
 *   - 物理レジスタプール: x9..x15 (7 個の caller-saved)
 *   - 割り付け不可な vreg は f->vreg_phys_reg[v] = -1 (codegen が frame slot を使う)
 */

#include "ir.h"
#include <stdlib.h>
#include <string.h>

#define PHYS_REG_FIRST 19
#define PHYS_REG_COUNT 10  /* x19..x28 (callee-saved) */

/* 命令の使用 vreg を walk して last_use[v] を更新する */
static void mark_uses(ir_inst_t *inst, int *last_use, int nvregs, int n) {
  if (inst->src1.id >= 0 && inst->src1.id < nvregs) last_use[inst->src1.id] = n;
  if (inst->src2.id >= 0 && inst->src2.id < nvregs) last_use[inst->src2.id] = n;
  if (inst->src3.id >= 0 && inst->src3.id < nvregs) last_use[inst->src3.id] = n;
  for (int k = 0; k < inst->nargs; k++) {
    if (inst->args && inst->args[k].value.id >= 0 &&
        inst->args[k].value.id < nvregs) {
      last_use[inst->args[k].value.id] = n;
    }
  }
  if (inst->result_storage.id >= 0 && inst->result_storage.id < nvregs) {
    last_use[inst->result_storage.id] = n;
  }
  /* 間接呼び出しの callee も use として扱う (regalloc 用) */
  if (inst->callee.id >= 0 && inst->callee.id < nvregs) {
    last_use[inst->callee.id] = n;
  }
}

/* dst の生存範囲を「last_use 命令番号まで」と決めた上で、
 * 命令 n 時点で空いている物理レジスタを返す (なければ -1 で spill)。 */
static int try_alloc_reg(int *reg_holder, int *last_use, int v, int n) {
  for (int j = 0; j < PHYS_REG_COUNT; j++) {
    int holder = reg_holder[j];
    if (holder == -1) {
      reg_holder[j] = v;
      return PHYS_REG_FIRST + j;
    }
    /* 既存 vreg の last use が現在以前なら解放できる */
    if (last_use[holder] < n) {
      reg_holder[j] = v;
      return PHYS_REG_FIRST + j;
    }
  }
  return -1;
}

/* 全 block の (start_n, end_n) 命令番号範囲を集める。
 * block_id → (start, end_exclusive)。返り値: 総命令数。 */
static int collect_block_ranges(ir_func_t *f, int *starts, int *ends, int max_blocks) {
  int n = 0;
  for (ir_block_t *b = f->entry; b; b = b->next) {
    if (b->id < max_blocks) starts[b->id] = n;
    for (ir_inst_t *inst = b->head; inst; inst = inst->next) {
      (void)inst;
      n++;
    }
    if (b->id < max_blocks) ends[b->id] = n;
  }
  return n;
}

void ir_regalloc_function(ir_func_t *f) {
  if (!f) return;
  int nvregs = f->next_vreg_id;
  if (nvregs <= 0) return;
  if (!f->vreg_phys_reg) {
    f->vreg_phys_reg = calloc((size_t)nvregs, sizeof(int));
  }
  for (int i = 0; i < nvregs; i++) f->vreg_phys_reg[i] = -1;

  /* Pass 1: block 範囲を取得 */
  int max_blocks = f->next_block_id > 0 ? f->next_block_id : 1;
  int *block_start = calloc((size_t)max_blocks, sizeof(int));
  int *block_end = calloc((size_t)max_blocks, sizeof(int));
  for (int i = 0; i < max_blocks; i++) { block_start[i] = -1; block_end[i] = -1; }
  collect_block_ranges(f, block_start, block_end, max_blocks);

  /* Pass 2: first_def / last_use を取得 */
  int *first_def = calloc((size_t)nvregs, sizeof(int));
  int *last_use = calloc((size_t)nvregs, sizeof(int));
  for (int i = 0; i < nvregs; i++) { first_def[i] = -1; last_use[i] = -1; }
  int n = 0;
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *inst = b->head; inst; inst = inst->next) {
      if (inst->dst.id >= 0 && inst->dst.id < nvregs && first_def[inst->dst.id] == -1) {
        first_def[inst->dst.id] = n;
      }
      mark_uses(inst, last_use, nvregs, n);
      n++;
    }
  }

  /* Pass 3: 後方分岐 (= ループバック) を見つけ、ループ範囲で touched な vreg の
   * live range を最低でもループ末尾まで延長する。 */
  n = 0;
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *inst = b->head; inst; inst = inst->next) {
      int targets[2] = {-1, -1};
      int nt = 0;
      if (inst->op == IR_BR) targets[nt++] = inst->label_id;
      else if (inst->op == IR_BR_COND) {
        targets[nt++] = inst->label_id;
        targets[nt++] = inst->else_label_id;
      }
      for (int ti = 0; ti < nt; ti++) {
        int tid = targets[ti];
        if (tid < 0 || tid >= max_blocks) continue;
        int t_start = block_start[tid];
        if (t_start < 0 || t_start >= n) continue; /* 前方分岐 */
        int loop_end = n + 1; /* ループ末尾 = この分岐命令の次まで */
        for (int v = 0; v < nvregs; v++) {
          int fd = first_def[v];
          int lu = last_use[v];
          if (fd < 0 || lu < 0) continue;
          /* touched in [t_start, loop_end) */
          int touched = (fd >= t_start && fd < loop_end) ||
                        (lu >= t_start && lu < loop_end);
          if (touched && lu < loop_end) {
            last_use[v] = loop_end;
          }
        }
      }
      n++;
    }
  }

  /* Pass 4: linear scan で物理レジスタにマップ */
  int reg_holder[PHYS_REG_COUNT];
  for (int j = 0; j < PHYS_REG_COUNT; j++) reg_holder[j] = -1;
  n = 0;
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *inst = b->head; inst; inst = inst->next) {
      int v = inst->dst.id;
      if (v >= 0 && v < nvregs && last_use[v] >= 0) {
        /* CALL / TLV_ADDR は caller-saved を全 clobber するので、dst は
         * 常に frame slot に書く (phys 割り付け対象外)。 */
        if (inst->op != IR_CALL && inst->op != IR_LOAD_TLV_ADDR &&
            inst->op != IR_VLA_ALLOC) {
          int reg = try_alloc_reg(reg_holder, last_use, v, n);
          f->vreg_phys_reg[v] = reg;
        }
      }
      n++;
    }
  }
  free(block_start);
  free(block_end);
  free(first_def);
  free(last_use);
}
