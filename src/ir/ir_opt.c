/*
 * Phase 6: IR 上の簡易最適化パス。
 *   - 即値伝播 (LOAD_IMM の値を後段の src として直接埋め込む)
 *   - 定数畳み込み (ADD/SUB/MUL/DIV/MOD/AND/OR/XOR/SHL/SHR + 比較を計算)
 *   - LEA dst, src, #0 → src の即値伝播
 *   - デッドコード削除 (副作用なし命令で dst が一度も使われない)
 *
 * 命令同士の関係を「vreg の def/use」だけで判断するシンプル版。
 * SSA 風前提 (1 vreg = 1 def) なので、LOAD_IMM の定数値は全 use で同値。
 */

#include "ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 定数伝播 + 算術畳み込み                                              */
/* ------------------------------------------------------------------ */

typedef struct {
  int *is_const;
  long long *value;
} const_map_t;

static void cm_init(const_map_t *cm, int nvregs) {
  cm->is_const = calloc((size_t)nvregs, sizeof(int));
  cm->value = calloc((size_t)nvregs, sizeof(long long));
}

static void cm_free(const_map_t *cm) {
  free(cm->is_const);
  free(cm->value);
}

/* val を可能なら imm に置換する (vreg が const_map にあれば imm にする)。
 * 副作用: src 側の vreg は元のまま (vreg id を変えるが値は同じ)。 */
static void substitute_with_const(const_map_t *cm, ir_val_t *val, int nvregs) {
  if (!val) return;
  if (val->id < 0 || val->id >= nvregs) return;
  if (cm->is_const[val->id]) {
    long long v = cm->value[val->id];
    ir_type_t ty = val->type;
    val->id = IR_VAL_IMM;
    val->imm = v;
    val->type = ty;
  }
}

/* 整数二項演算を host 側で計算。成功なら out にセットして 1 を返す。
 * 0 除算は失敗扱い (元の命令を残す)。 */
static int eval_binop(ir_op_t op, long long a, long long b, ir_type_t ty, long long *out) {
  int is_i32 = ir_type_size(ty) <= 4;
  switch (op) {
    case IR_ADD: *out = a + b; return 1;
    case IR_SUB: *out = a - b; return 1;
    case IR_MUL: *out = a * b; return 1;
    case IR_DIV: if (b == 0) return 0; *out = a / b; return 1;
    case IR_MOD: if (b == 0) return 0; *out = a % b; return 1;
    case IR_AND: *out = a & b; return 1;
    case IR_OR:  *out = a | b; return 1;
    case IR_XOR: *out = a ^ b; return 1;
    case IR_SHL:
      if (is_i32) {
        unsigned int ua = (unsigned int)a;
        *out = (long long)(int)(ua << (b & 31));
      } else {
        *out = (long long)((unsigned long long)a << (b & 63));
      }
      return 1;
    case IR_SHR:
      if (is_i32) {
        int sa = (int)(unsigned int)a;
        *out = (long long)(sa >> (b & 31));
      } else {
        *out = a >> (b & 63);
      }
      return 1;
    case IR_LSR:
      if (is_i32) {
        unsigned int ua = (unsigned int)a;
        *out = (long long)(ua >> (b & 31));
      } else {
        *out = (long long)((unsigned long long)a >> (b & 63));
      }
      return 1;
    case IR_EQ:  *out = (a == b) ? 1 : 0; return 1;
    case IR_NE:  *out = (a != b) ? 1 : 0; return 1;
    case IR_LT:  *out = (a < b)  ? 1 : 0; return 1;
    case IR_LE:  *out = (a <= b) ? 1 : 0; return 1;
    case IR_ULT: *out = ((unsigned long long)a <  (unsigned long long)b) ? 1 : 0; return 1;
    case IR_ULE: *out = ((unsigned long long)a <= (unsigned long long)b) ? 1 : 0; return 1;
    case IR_LEA: *out = a + b; return 1; /* LEA は単純加算 (アドレス) */
    default: return 0;
  }
}

static void const_fold_func(ir_func_t *f) {
  int nvregs = f->next_vreg_id;
  if (nvregs <= 0) return;
  const_map_t cm;
  cm_init(&cm, nvregs);
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *inst = b->head; inst; inst = inst->next) {
      /* src を imm 置換 (定数伝播) */
      substitute_with_const(&cm, &inst->src1, nvregs);
      substitute_with_const(&cm, &inst->src2, nvregs);
      for (int i = 0; i < inst->nargs; i++) {
        if (inst->args) substitute_with_const(&cm, &inst->args[i], nvregs);
      }
      substitute_with_const(&cm, &inst->ret_struct_area, nvregs);

      /* LOAD_IMM: dst が定数値 */
      if (inst->op == IR_LOAD_IMM) {
        if (inst->dst.id >= 0 && inst->dst.id < nvregs) {
          cm.is_const[inst->dst.id] = 1;
          cm.value[inst->dst.id] = inst->src1.imm;
        }
        continue;
      }
      /* 二項演算で両側 imm → LOAD_IMM に変換 */
      if (inst->src1.id == IR_VAL_IMM && inst->src2.id == IR_VAL_IMM &&
          inst->dst.id >= 0 && inst->dst.id < nvregs) {
        long long v;
        if (eval_binop(inst->op, inst->src1.imm, inst->src2.imm, inst->dst.type, &v)) {
          inst->op = IR_LOAD_IMM;
          inst->src1.id = IR_VAL_IMM;
          inst->src1.type = inst->dst.type;
          inst->src1.imm = v;
          inst->src2.id = IR_VAL_NONE;
          cm.is_const[inst->dst.id] = 1;
          cm.value[inst->dst.id] = v;
          continue;
        }
      }
      /* LEA dst, src, #0 → src を伝播 (LEA を NOP 化、dst = src の値) */
      if (inst->op == IR_LEA && inst->src2.id == IR_VAL_IMM && inst->src2.imm == 0) {
        if (inst->src1.id == IR_VAL_IMM &&
            inst->dst.id >= 0 && inst->dst.id < nvregs) {
          inst->op = IR_LOAD_IMM;
          inst->src1.type = inst->dst.type;
          inst->src2.id = IR_VAL_NONE;
          cm.is_const[inst->dst.id] = 1;
          cm.value[inst->dst.id] = inst->src1.imm;
          continue;
        }
      }
      /* dst が定義されたが定数化できなかった: 非定数として扱う */
      if (inst->dst.id >= 0 && inst->dst.id < nvregs) {
        cm.is_const[inst->dst.id] = 0;
      }
    }
  }
  cm_free(&cm);
}

void ir_opt_const_fold(ir_module_t *m) {
  if (!m) return;
  for (ir_func_t *f = m->funcs; f; f = f->next) {
    const_fold_func(f);
  }
}

/* ------------------------------------------------------------------ */
/* デッドコード削除                                                    */
/* ------------------------------------------------------------------ */

/* 副作用がある = 削除不可な命令か */
static int has_side_effect(ir_op_t op) {
  switch (op) {
    case IR_STORE:
    case IR_MEMCPY:
    case IR_CALL:
    case IR_BR:
    case IR_BR_COND:
    case IR_RET:
    case IR_LABEL:
    case IR_PARAM:
    case IR_ALLOCA:    /* フレーム上の位置に意味がある */
    case IR_LOAD_TLV_ADDR: /* 内部で blr __tlv_bootstrap を発行する */
    case IR_VLA_ALLOC:    /* SP を動的に変更する */
    case IR_ATOMIC:       /* メモリ書き換え / 順序付け効果。結果未使用でも消さない */
      return 1;
    default:
      return 0;
  }
}

static void count_uses(ir_inst_t *inst, int *use_cnt, int nvregs) {
  if (inst->src1.id >= 0 && inst->src1.id < nvregs) use_cnt[inst->src1.id]++;
  if (inst->src2.id >= 0 && inst->src2.id < nvregs) use_cnt[inst->src2.id]++;
  if (inst->src3.id >= 0 && inst->src3.id < nvregs) use_cnt[inst->src3.id]++;
  for (int k = 0; k < inst->nargs; k++) {
    if (inst->args && inst->args[k].id >= 0 && inst->args[k].id < nvregs) {
      use_cnt[inst->args[k].id]++;
    }
  }
  if (inst->ret_struct_area.id >= 0 && inst->ret_struct_area.id < nvregs) {
    use_cnt[inst->ret_struct_area.id]++;
  }
  /* 間接呼び出しの callee も use として数える */
  if (inst->callee.id >= 0 && inst->callee.id < nvregs) {
    use_cnt[inst->callee.id]++;
  }
}

/* inst を block から外す。実際にはオペコードを IR_NOP にする (連結リストを保つため)。 */
static void mark_nop(ir_inst_t *inst) {
  inst->op = IR_NOP;
  inst->dst.id = IR_VAL_NONE;
  inst->src1.id = IR_VAL_NONE;
  inst->src2.id = IR_VAL_NONE;
  inst->nargs = 0;
}

static int dce_pass_func(ir_func_t *f) {
  int nvregs = f->next_vreg_id;
  if (nvregs <= 0) return 0;
  int *use_cnt = calloc((size_t)nvregs, sizeof(int));
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *inst = b->head; inst; inst = inst->next) {
      count_uses(inst, use_cnt, nvregs);
    }
  }
  int removed = 0;
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *inst = b->head; inst; inst = inst->next) {
      if (has_side_effect(inst->op)) continue;
      if (inst->op == IR_NOP) continue;
      if (inst->dst.id < 0 || inst->dst.id >= nvregs) continue;
      if (use_cnt[inst->dst.id] == 0) {
        /* この命令を消す前に、src の use 数を減らす */
        if (inst->src1.id >= 0 && inst->src1.id < nvregs) use_cnt[inst->src1.id]--;
        if (inst->src2.id >= 0 && inst->src2.id < nvregs) use_cnt[inst->src2.id]--;
        mark_nop(inst);
        removed++;
      }
    }
  }
  free(use_cnt);
  return removed;
}

void ir_opt_dce(ir_module_t *m) {
  if (!m) return;
  for (ir_func_t *f = m->funcs; f; f = f->next) {
    /* fixpoint まで回す (定数畳み込みで消える命令の連鎖を反復解消する) */
    while (dce_pass_func(f) > 0) {}
  }
}

void ir_opt_copy_propagate(ir_module_t *m) {
  /* 現状は const_fold が即値伝播も兼ねるので追加処理なし。 */
  (void)m;
}
