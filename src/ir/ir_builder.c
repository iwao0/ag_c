/*
 * AST → IR ビルダー (Phase 2 最小版)。
 *
 * 対応 AST node:
 *   ND_FUNCDEF (引数なし、戻り値は i32)
 *   ND_BLOCK
 *   ND_RETURN
 *   ND_NUM   (整数のみ、i32 として扱う)
 *   ND_LVAR  (整数 i32 のみ)
 *   ND_ASSIGN (LVAR 左辺、整数式右辺)
 *   ND_ADD / ND_SUB / ND_MUL / ND_DIV / ND_MOD
 *
 * 対応外の node を踏むと ctx->failed を立て、build_module は NULL を返す。
 *
 * ローカル変数モデル: 各 lvar offset に対し IR_ALLOCA で frame slot を確保し、
 * その vreg (ポインタ) を offset→vreg マップに記録する。
 * LVAR 参照は LOAD、ASSIGN は STORE。
 */

#include "ir_builder.h"
#include "ir.h"
#include "../parser/ast.h"
#include "../parser/internal/decl.h"   /* lvar_t / psx_decl_find_lvar_by_offset */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LVARS 256
#define MAX_LOOP_DEPTH 32
#define MAX_CASES 256

/* break/continue 用ループスタック。それぞれ「飛び先ブロック」。 */
typedef struct {
  ir_block_t *continue_block;
  ir_block_t *break_block;
} loop_ctx_t;

/* 現在 build 中の switch 内で「AST の case/default node → IR block」のマップ。
 * ND_CASE / ND_DEFAULT が body の中に現れたとき、対応する block へ
 * switch_to_new_block する。switch はネスト可能なので退避用配列もある。 */
typedef struct {
  void *ast_node;       /* node_case_t* または node_default_t* */
  ir_block_t *block;
} case_map_entry_t;

typedef struct {
  ir_module_t *m;
  ir_func_t *f;
  /* 現在処理中の関数 AST。lvars リストを引くため。 */
  node_func_t *cur_fn;
  int failed;
  /* offset → ALLOCA vreg (ポインタ) のマップ */
  int lvar_offset[MAX_LVARS];
  int lvar_vreg[MAX_LVARS];
  int lvar_count;
  loop_ctx_t loops[MAX_LOOP_DEPTH];
  int loop_depth;
  case_map_entry_t case_map[MAX_CASES];
  int case_map_count;
} ir_build_ctx_t;

static void fail(ir_build_ctx_t *ctx, const char *msg) {
  if (ctx->failed) return;
  ctx->failed = 1;
  /* AG_USE_IR=1 が明示されたときのみ verbose にメッセージを出す。
   * default では silent に fallback (Phase 7a 以降)。 */
  const char *use_ir = getenv("AG_USE_IR");
  if (use_ir && strcmp(use_ir, "1") == 0) {
    fprintf(stderr, "ir_build: unsupported: %s\n", msg);
  }
}

static int find_alloca_vreg(ir_build_ctx_t *ctx, int offset) {
  for (int i = 0; i < ctx->lvar_count; i++) {
    if (ctx->lvar_offset[i] == offset) return ctx->lvar_vreg[i];
  }
  return -1;
}

/* ND_LVAR.offset を含む lvar_t (= 親 owner) を探す。struct メンバアクセスでは
 * `s.m` の ND_LVAR が (s.offset + member_offset) を持つので、ここで `s` 全体を
 * 引き当てる。スカラ lvar は自分自身を返す (delta = 0)。
 * 現在処理中の関数の lvars リスト (parser が保存) を walk する。 */
static lvar_t *find_owning_lvar(ir_build_ctx_t *ctx, int offset) {
  lvar_t *head = ctx && ctx->cur_fn ? ctx->cur_fn->lvars : NULL;
  for (lvar_t *var = head; var; var = var->next_all) {
    int sz = var->size > 0 ? var->size : 1;
    if (var->offset <= offset && offset < var->offset + sz) return var;
  }
  return NULL;
}

/* スカラ要素 (struct member 含む) の「ロード時の値の IR 型」。
 * mem.type_size を見る。配列名は別経路 (ND_ADDR 経由) を通る。 */
static ir_type_t elem_value_type(int type_size) {
  if (type_size >= 8) return IR_TY_PTR;
  if (type_size == 4) return IR_TY_I32;
  if (type_size == 2) return IR_TY_I16;
  return IR_TY_I8;
}

static ir_type_t lvar_value_type(node_lvar_t *lv) {
  int elem = lv->mem.type_size > 0 ? lv->mem.type_size : 4;
  return elem_value_type(elem);
}

/* owner (= lvar_t の親) に対して ALLOCA を 1 回だけ確保し、その vreg を返す。
 * 同じ owner offset で再度呼ばれたら既存 vreg を返す。 */
static int alloca_for_owner(ir_build_ctx_t *ctx, lvar_t *var) {
  if (!var) {
    fail(ctx, "owning lvar not found");
    return -1;
  }
  int existing = find_alloca_vreg(ctx, var->offset);
  if (existing >= 0) return existing;
  if (ctx->lvar_count >= MAX_LVARS) {
    fail(ctx, "too many local variables");
    return -1;
  }
  int size = var->size > 0 ? var->size : 4;
  int elem = var->elem_size > 0 ? var->elem_size : 4;
  int align = (elem >= 8) ? 8 : (elem >= 4 ? 4 : (elem >= 2 ? 2 : 1));
  /* struct のような複合型は 8B align を優先 (簡略化) */
  if (size >= 8 && align < 8) align = 8;
  int v = ir_func_new_vreg(ctx->f);
  ir_inst_t *inst = ir_inst_new(IR_ALLOCA);
  inst->dst = ir_val_vreg(v, IR_TY_PTR);
  inst->alloca_size = size;
  inst->alloca_align = align;
  ir_func_append_inst(ctx->f, inst);
  ctx->lvar_offset[ctx->lvar_count] = var->offset;
  ctx->lvar_vreg[ctx->lvar_count] = v;
  ctx->lvar_count++;
  return v;
}

/* 整数即値を LOAD_IMM で生成し vreg を返す。 */
__attribute__((unused))
static int emit_load_imm(ir_build_ctx_t *ctx, long long imm, ir_type_t ty) {
  int v = ir_func_new_vreg(ctx->f);
  ir_inst_t *inst = ir_inst_new(IR_LOAD_IMM);
  inst->dst = ir_val_vreg(v, ty);
  inst->src1 = ir_val_imm(ty, imm);
  ir_func_append_inst(ctx->f, inst);
  return v;
}

/* 2 項演算を emit して結果 vreg を返す。 */
static int emit_binop(ir_build_ctx_t *ctx, ir_op_t op, ir_val_t a, ir_val_t b, ir_type_t ty) {
  int v = ir_func_new_vreg(ctx->f);
  ir_inst_t *inst = ir_inst_new(op);
  inst->dst = ir_val_vreg(v, ty);
  inst->src1 = a;
  inst->src2 = b;
  ir_func_append_inst(ctx->f, inst);
  return v;
}

/* ND_LVAR.offset → 「実際にアクセスすべきアドレスを持つ vreg」を返す。
 * struct メンバの場合は owner ALLOCA + delta (= IR_LEA) を構築する。 */
static int address_of_lvar(ir_build_ctx_t *ctx, int offset) {
  lvar_t *owner = find_owning_lvar(ctx, offset);
  if (!owner) {
    fail(ctx, "lvar not found");
    return -1;
  }
  int base_vreg = alloca_for_owner(ctx, owner);
  if (base_vreg < 0) return -1;
  int delta = offset - owner->offset;
  if (delta == 0) return base_vreg;
  /* base + delta */
  int v = ir_func_new_vreg(ctx->f);
  ir_inst_t *lea = ir_inst_new(IR_LEA);
  lea->dst = ir_val_vreg(v, IR_TY_PTR);
  lea->src1 = ir_val_vreg(base_vreg, IR_TY_PTR);
  lea->src2 = ir_val_imm(IR_TY_I64, delta);
  ir_func_append_inst(ctx->f, lea);
  return v;
}

/* 式を IR にビルドし、結果値 (vreg or immediate) を返す。 */
static ir_val_t build_expr(ir_build_ctx_t *ctx, node_t *node) {
  if (!node || ctx->failed) return ir_val_none();
  switch (node->kind) {
    case ND_NUM: {
      node_num_t *n = (node_num_t *)node;
      int v = ir_func_new_vreg(ctx->f);
      ir_inst_t *inst = ir_inst_new(IR_LOAD_IMM);
      inst->dst = ir_val_vreg(v, IR_TY_I32);
      inst->src1 = ir_val_imm(IR_TY_I32, n->val);
      ir_func_append_inst(ctx->f, inst);
      return inst->dst;
    }
    case ND_LVAR: {
      node_lvar_t *lv = (node_lvar_t *)node;
      ir_type_t vty = lvar_value_type(lv);
      int ptr_vreg = address_of_lvar(ctx, lv->offset);
      if (ptr_vreg < 0) return ir_val_none();
      /* bitfield 読み出し:
       *   v_load   = *ptr  (storage unit、通常 i32)
       *   v_shr    = v_load >> bit_offset
       *   v_masked = v_shr & ((1<<bw)-1)
       *   signed: さらに sign extend ((v ^ sign_bit) - sign_bit) */
      int bw = lv->mem.bit_width;
      if (bw > 0) {
        int v_load = ir_func_new_vreg(ctx->f);
        ir_inst_t *ld = ir_inst_new(IR_LOAD);
        ld->dst = ir_val_vreg(v_load, IR_TY_I32);
        ld->src1 = ir_val_vreg(ptr_vreg, IR_TY_PTR);
        ir_func_append_inst(ctx->f, ld);
        int bo = lv->mem.bit_offset;
        ir_val_t cur = ir_val_vreg(v_load, IR_TY_I32);
        if (bo > 0) {
          int v_shr = emit_binop(ctx, IR_SHR, cur, ir_val_imm(IR_TY_I32, bo), IR_TY_I32);
          cur = ir_val_vreg(v_shr, IR_TY_I32);
        }
        long long mask = (bw >= 64) ? -1LL : ((1LL << bw) - 1);
        int v_masked = emit_binop(ctx, IR_AND, cur, ir_val_imm(IR_TY_I32, mask), IR_TY_I32);
        cur = ir_val_vreg(v_masked, IR_TY_I32);
        if (lv->mem.bit_is_signed && bw < 32) {
          long long sign_bit = 1LL << (bw - 1);
          int v_xor = emit_binop(ctx, IR_XOR, cur, ir_val_imm(IR_TY_I32, sign_bit), IR_TY_I32);
          int v_sub = emit_binop(ctx, IR_SUB,
                                  ir_val_vreg(v_xor, IR_TY_I32),
                                  ir_val_imm(IR_TY_I32, sign_bit), IR_TY_I32);
          cur = ir_val_vreg(v_sub, IR_TY_I32);
        }
        return cur;
      }
      int v = ir_func_new_vreg(ctx->f);
      ir_inst_t *inst = ir_inst_new(IR_LOAD);
      inst->dst = ir_val_vreg(v, vty);
      inst->src1 = ir_val_vreg(ptr_vreg, IR_TY_PTR);
      ir_func_append_inst(ctx->f, inst);
      return inst->dst;
    }
    case ND_ASSIGN: {
      if (!node->lhs) {
        fail(ctx, "assign without target");
        return ir_val_none();
      }
      /* struct 値代入: ND_ASSIGN.type_size が要素サイズを超えるなら memcpy 経路。
       * struct のサイズは node_mem_t.type_size に parser が入れている。 */
      {
        node_mem_t *amem = (node_mem_t *)node;
        int assign_size = amem->type_size;
        if (assign_size > 8) {
          /* dst のアドレスを得る */
          int dst_ptr_vreg = -1;
          if (node->lhs->kind == ND_LVAR) {
            dst_ptr_vreg = address_of_lvar(ctx, ((node_lvar_t *)node->lhs)->offset);
          } else if (node->lhs->kind == ND_DEREF) {
            ir_val_t ptr = build_expr(ctx, node->lhs->lhs);
            if (ctx->failed) return ir_val_none();
            if (ptr.id >= 0) dst_ptr_vreg = ptr.id;
          } else {
            fail(ctx, "struct assign dst not LVAR/DEREF");
            return ir_val_none();
          }
          if (dst_ptr_vreg < 0) return ir_val_none();
          /* rhs が >8B struct 戻り値の関数呼び出しなら、戻り値を dst へ直接書かせる。 */
          if (node->rhs && node->rhs->kind == ND_FUNCALL &&
              node->rhs->ret_struct_size > 8) {
            node_func_t *callee = (node_func_t *)node->rhs;
            if (callee->callee || callee->is_variadic || callee->nargs > 8) {
              fail(ctx, "indirect/variadic/many-arg struct-return call");
              return ir_val_none();
            }
            ir_val_t *cargs = NULL;
            if (callee->nargs > 0) {
              cargs = calloc((size_t)callee->nargs, sizeof(ir_val_t));
              for (int i = 0; i < callee->nargs; i++) {
                node_t *arg = callee->args[i];
                int arg_full_size = 0;
                if (arg && arg->kind == ND_LVAR) {
                  lvar_t *owner = find_owning_lvar(ctx, ((node_lvar_t *)arg)->offset);
                  if (owner) arg_full_size = owner->size;
                  if (arg_full_size == 0) arg_full_size = ((node_lvar_t *)arg)->mem.type_size;
                }
                if (arg_full_size > 8) {
                  int src_ptr = address_of_lvar(ctx, ((node_lvar_t *)arg)->offset);
                  if (src_ptr < 0) return ir_val_none();
                  int tmp = ir_func_new_vreg(ctx->f);
                  ir_inst_t *ia = ir_inst_new(IR_ALLOCA);
                  ia->dst = ir_val_vreg(tmp, IR_TY_PTR);
                  ia->alloca_size = arg_full_size;
                  ia->alloca_align = 8;
                  ir_func_append_inst(ctx->f, ia);
                  ir_inst_t *cp = ir_inst_new(IR_MEMCPY);
                  cp->src1 = ir_val_vreg(tmp, IR_TY_PTR);
                  cp->src2 = ir_val_vreg(src_ptr, IR_TY_PTR);
                  cp->alloca_size = arg_full_size;
                  ir_func_append_inst(ctx->f, cp);
                  cargs[i] = ir_val_vreg(tmp, IR_TY_PTR);
                } else {
                  cargs[i] = build_expr(ctx, arg);
                  if (ctx->failed) return ir_val_none();
                }
              }
            }
            int v = ir_func_new_vreg(ctx->f);
            ir_inst_t *call = ir_inst_new(IR_CALL);
            call->dst = ir_val_vreg(v, IR_TY_I32);
            call->sym = callee->funcname;
            call->sym_len = callee->funcname_len;
            call->args = cargs;
            call->nargs = callee->nargs;
            call->ret_struct_size = node->rhs->ret_struct_size;
            call->ret_struct_area = ir_val_vreg(dst_ptr_vreg, IR_TY_PTR);
            ir_func_append_inst(ctx->f, call);
            return ir_val_vreg(dst_ptr_vreg, IR_TY_PTR);
          }
          /* src のアドレスを得る (通常の struct から struct コピー) */
          int src_ptr_vreg = -1;
          if (node->rhs && node->rhs->kind == ND_LVAR) {
            src_ptr_vreg = address_of_lvar(ctx, ((node_lvar_t *)node->rhs)->offset);
          } else if (node->rhs && node->rhs->kind == ND_DEREF) {
            ir_val_t ptr = build_expr(ctx, node->rhs->lhs);
            if (ctx->failed) return ir_val_none();
            if (ptr.id >= 0) src_ptr_vreg = ptr.id;
          } else {
            fail(ctx, "struct assign src not LVAR/DEREF");
            return ir_val_none();
          }
          if (src_ptr_vreg < 0) return ir_val_none();
          ir_inst_t *cp = ir_inst_new(IR_MEMCPY);
          cp->src1 = ir_val_vreg(dst_ptr_vreg, IR_TY_PTR);
          cp->src2 = ir_val_vreg(src_ptr_vreg, IR_TY_PTR);
          cp->alloca_size = assign_size;
          ir_func_append_inst(ctx->f, cp);
          return ir_val_vreg(dst_ptr_vreg, IR_TY_PTR);
        }
      }
      if (node->lhs->kind == ND_LVAR) {
        node_lvar_t *lv = (node_lvar_t *)node->lhs;
        ir_type_t vty = lvar_value_type(lv);
        int ptr_vreg = address_of_lvar(ctx, lv->offset);
        if (ptr_vreg < 0) return ir_val_none();
        ir_val_t rhs = build_expr(ctx, node->rhs);
        if (ctx->failed) return ir_val_none();
        /* bitfield 書き込み:
         *   v_old   = *ptr
         *   inv     = ~((mask) << bit_offset)
         *   v_clr   = v_old & inv
         *   v_rhs_m = rhs & mask
         *   v_shl   = v_rhs_m << bit_offset
         *   v_new   = v_clr | v_shl
         *   *ptr    = v_new
         * 代入式の値は rhs (masking 前)。 */
        int bw = lv->mem.bit_width;
        if (bw > 0) {
          int bo = lv->mem.bit_offset;
          long long mask = (bw >= 64) ? -1LL : ((1LL << bw) - 1);
          long long shifted_mask = mask << bo;
          long long inv_mask = ~shifted_mask;
          int v_old = ir_func_new_vreg(ctx->f);
          ir_inst_t *ld = ir_inst_new(IR_LOAD);
          ld->dst = ir_val_vreg(v_old, IR_TY_I32);
          ld->src1 = ir_val_vreg(ptr_vreg, IR_TY_PTR);
          ir_func_append_inst(ctx->f, ld);
          int v_clr = emit_binop(ctx, IR_AND,
                                  ir_val_vreg(v_old, IR_TY_I32),
                                  ir_val_imm(IR_TY_I32, inv_mask), IR_TY_I32);
          ir_val_t rhs_int = rhs;
          rhs_int.type = IR_TY_I32;
          int v_rhs_m = emit_binop(ctx, IR_AND, rhs_int,
                                    ir_val_imm(IR_TY_I32, mask), IR_TY_I32);
          ir_val_t cur = ir_val_vreg(v_rhs_m, IR_TY_I32);
          if (bo > 0) {
            int v_shl = emit_binop(ctx, IR_SHL, cur,
                                    ir_val_imm(IR_TY_I32, bo), IR_TY_I32);
            cur = ir_val_vreg(v_shl, IR_TY_I32);
          }
          int v_new = emit_binop(ctx, IR_OR,
                                  ir_val_vreg(v_clr, IR_TY_I32), cur, IR_TY_I32);
          ir_inst_t *st = ir_inst_new(IR_STORE);
          st->src1 = ir_val_vreg(ptr_vreg, IR_TY_PTR);
          st->src2 = ir_val_vreg(v_new, IR_TY_I32);
          ir_func_append_inst(ctx->f, st);
          return rhs;
        }
        /* 通常のスカラ代入 */
        rhs.type = vty;
        ir_inst_t *st = ir_inst_new(IR_STORE);
        st->src1 = ir_val_vreg(ptr_vreg, IR_TY_PTR);
        st->src2 = rhs;
        ir_func_append_inst(ctx->f, st);
        return rhs;
      }
      if (node->lhs->kind == ND_DEREF) {
        /* *p = rhs。p が struct メンバアクセス由来なら bit_width が乗る。 */
        node_mem_t *mm = (node_mem_t *)node->lhs;
        int bw = mm->bit_width;
        ir_val_t ptr = build_expr(ctx, node->lhs->lhs);
        if (ctx->failed) return ir_val_none();
        ir_val_t rhs = build_expr(ctx, node->rhs);
        if (ctx->failed) return ir_val_none();
        if (bw > 0) {
          int bo = mm->bit_offset;
          long long mask = (bw >= 64) ? -1LL : ((1LL << bw) - 1);
          long long shifted_mask = mask << bo;
          long long inv_mask = ~shifted_mask;
          int v_old = ir_func_new_vreg(ctx->f);
          ir_inst_t *ld = ir_inst_new(IR_LOAD);
          ld->dst = ir_val_vreg(v_old, IR_TY_I32);
          ld->src1 = ptr;
          ir_func_append_inst(ctx->f, ld);
          int v_clr = emit_binop(ctx, IR_AND,
                                  ir_val_vreg(v_old, IR_TY_I32),
                                  ir_val_imm(IR_TY_I32, inv_mask), IR_TY_I32);
          ir_val_t rhs_int = rhs;
          rhs_int.type = IR_TY_I32;
          int v_rhs_m = emit_binop(ctx, IR_AND, rhs_int,
                                    ir_val_imm(IR_TY_I32, mask), IR_TY_I32);
          ir_val_t cur = ir_val_vreg(v_rhs_m, IR_TY_I32);
          if (bo > 0) {
            int v_shl = emit_binop(ctx, IR_SHL, cur,
                                    ir_val_imm(IR_TY_I32, bo), IR_TY_I32);
            cur = ir_val_vreg(v_shl, IR_TY_I32);
          }
          int v_new = emit_binop(ctx, IR_OR,
                                  ir_val_vreg(v_clr, IR_TY_I32), cur, IR_TY_I32);
          ir_inst_t *st = ir_inst_new(IR_STORE);
          st->src1 = ptr;
          st->src2 = ir_val_vreg(v_new, IR_TY_I32);
          ir_func_append_inst(ctx->f, st);
          return rhs;
        }
        rhs.type = IR_TY_I32;
        ir_inst_t *st = ir_inst_new(IR_STORE);
        st->src1 = ptr;
        st->src2 = rhs;
        ir_func_append_inst(ctx->f, st);
        return rhs;
      }
      fail(ctx, "assign target is not LVAR or DEREF");
      return ir_val_none();
    }
    case ND_ADDR: {
      /* &*x = x: ag_c の AST では `s.m` が `*(&s + off)` で表現され、
       * `&s.m` は ND_ADDR(ND_DEREF(...)) になる。これを deref キャンセルで
       * ポインタ式に展開する。 */
      if (node->lhs && node->lhs->kind == ND_DEREF) {
        return build_expr(ctx, node->lhs->lhs);
      }
      if (!node->lhs || node->lhs->kind != ND_LVAR) {
        fail(ctx, "& of non-lvar (Phase 4b unsupported)");
        return ir_val_none();
      }
      node_lvar_t *lv = (node_lvar_t *)node->lhs;
      int ptr_vreg = address_of_lvar(ctx, lv->offset);
      if (ptr_vreg < 0) return ir_val_none();
      return ir_val_vreg(ptr_vreg, IR_TY_PTR);
    }
    case ND_DEREF: {
      ir_val_t ptr = build_expr(ctx, node->lhs);
      if (ctx->failed) return ir_val_none();
      /* bitfield 読み出し: bit_width > 0 のとき struct メンバが bitfield。 */
      node_mem_t *mm = (node_mem_t *)node;
      int bw = mm->bit_width;
      if (bw > 0) {
        int v_load = ir_func_new_vreg(ctx->f);
        ir_inst_t *ld = ir_inst_new(IR_LOAD);
        ld->dst = ir_val_vreg(v_load, IR_TY_I32);
        ld->src1 = ptr;
        ir_func_append_inst(ctx->f, ld);
        int bo = mm->bit_offset;
        ir_val_t cur = ir_val_vreg(v_load, IR_TY_I32);
        if (bo > 0) {
          int v_shr = emit_binop(ctx, IR_SHR, cur, ir_val_imm(IR_TY_I32, bo), IR_TY_I32);
          cur = ir_val_vreg(v_shr, IR_TY_I32);
        }
        long long mask = (bw >= 64) ? -1LL : ((1LL << bw) - 1);
        int v_masked = emit_binop(ctx, IR_AND, cur, ir_val_imm(IR_TY_I32, mask), IR_TY_I32);
        cur = ir_val_vreg(v_masked, IR_TY_I32);
        if (mm->bit_is_signed && bw < 32) {
          long long sign_bit = 1LL << (bw - 1);
          int v_xor = emit_binop(ctx, IR_XOR, cur, ir_val_imm(IR_TY_I32, sign_bit), IR_TY_I32);
          int v_sub = emit_binop(ctx, IR_SUB,
                                  ir_val_vreg(v_xor, IR_TY_I32),
                                  ir_val_imm(IR_TY_I32, sign_bit), IR_TY_I32);
          cur = ir_val_vreg(v_sub, IR_TY_I32);
        }
        return cur;
      }
      int v = ir_func_new_vreg(ctx->f);
      ir_inst_t *inst = ir_inst_new(IR_LOAD);
      /* Phase 4b: int* のみ前提 (deref 結果は i32) */
      inst->dst = ir_val_vreg(v, IR_TY_I32);
      inst->src1 = ptr;
      ir_func_append_inst(ctx->f, inst);
      return inst->dst;
    }
    case ND_FUNCALL: {
      node_func_t *fn = (node_func_t *)node;
      if (fn->callee) {
        fail(ctx, "indirect function call (Phase 4a unsupported)");
        return ir_val_none();
      }
      if (fn->is_variadic) {
        fail(ctx, "variadic call (Phase 4a unsupported)");
        return ir_val_none();
      }
      if (fn->nargs > 8) {
        fail(ctx, "more than 8 arguments (Phase 4a unsupported)");
        return ir_val_none();
      }
      ir_val_t *cargs = NULL;
      if (fn->nargs > 0) {
        cargs = calloc((size_t)fn->nargs, sizeof(ir_val_t));
        for (int i = 0; i < fn->nargs; i++) {
          node_t *arg = fn->args[i];
          int arg_full_size = 0;
          if (arg && arg->kind == ND_LVAR) {
            lvar_t *owner = find_owning_lvar(ctx, ((node_lvar_t *)arg)->offset);
            if (owner) arg_full_size = owner->size;
            if (arg_full_size == 0) arg_full_size = ((node_lvar_t *)arg)->mem.type_size;
          }
          if (arg_full_size > 8) {
            /* struct 引数: 一時 frame slot に memcpy し、そのアドレスを渡す。 */
            int src_ptr = address_of_lvar(ctx, ((node_lvar_t *)arg)->offset);
            if (src_ptr < 0) return ir_val_none();
            int tmp_vreg = ir_func_new_vreg(ctx->f);
            ir_inst_t *ia = ir_inst_new(IR_ALLOCA);
            ia->dst = ir_val_vreg(tmp_vreg, IR_TY_PTR);
            ia->alloca_size = arg_full_size;
            ia->alloca_align = 8;
            ir_func_append_inst(ctx->f, ia);
            ir_inst_t *cp = ir_inst_new(IR_MEMCPY);
            cp->src1 = ir_val_vreg(tmp_vreg, IR_TY_PTR);
            cp->src2 = ir_val_vreg(src_ptr, IR_TY_PTR);
            cp->alloca_size = arg_full_size;
            ir_func_append_inst(ctx->f, cp);
            cargs[i] = ir_val_vreg(tmp_vreg, IR_TY_PTR);
          } else {
            cargs[i] = build_expr(ctx, arg);
            if (ctx->failed) return ir_val_none();
          }
        }
      }
      int v = ir_func_new_vreg(ctx->f);
      ir_inst_t *call = ir_inst_new(IR_CALL);
      call->dst = ir_val_vreg(v, IR_TY_I32);
      call->sym = fn->funcname;
      call->sym_len = fn->funcname_len;
      call->args = cargs;
      call->nargs = fn->nargs;
      ir_func_append_inst(ctx->f, call);
      return call->dst;
    }
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
    case ND_LT:
    case ND_LE:
    case ND_EQ:
    case ND_NE: {
      ir_val_t l = build_expr(ctx, node->lhs);
      if (ctx->failed) return ir_val_none();
      ir_val_t r = build_expr(ctx, node->rhs);
      if (ctx->failed) return ir_val_none();
      ir_op_t op = IR_ADD;
      switch (node->kind) {
        case ND_ADD: op = IR_ADD; break;
        case ND_SUB: op = IR_SUB; break;
        case ND_MUL: op = IR_MUL; break;
        case ND_DIV: op = IR_DIV; break;
        case ND_MOD: op = IR_MOD; break;
        case ND_LT:  op = IR_LT;  break;
        case ND_LE:  op = IR_LE;  break;
        case ND_EQ:  op = IR_EQ;  break;
        case ND_NE:  op = IR_NE;  break;
        default: break;
      }
      int v = ir_func_new_vreg(ctx->f);
      ir_inst_t *inst = ir_inst_new(op);
      inst->dst = ir_val_vreg(v, IR_TY_I32);
      inst->src1 = l;
      inst->src2 = r;
      ir_func_append_inst(ctx->f, inst);
      return inst->dst;
    }
    default:
      fail(ctx, "unsupported expression node");
      return ir_val_none();
  }
}

/* 現在ブロックの末尾に「分岐 / return」が無ければ自動で BR を補う。
 * これは if/while/for の各分岐の最後に呼ぶ。 */
static int cur_block_is_terminated(ir_func_t *f) {
  if (!f->cur_block || !f->cur_block->tail) return 0;
  ir_op_t op = f->cur_block->tail->op;
  return op == IR_BR || op == IR_BR_COND || op == IR_RET;
}

static void emit_br(ir_build_ctx_t *ctx, ir_block_t *target) {
  if (cur_block_is_terminated(ctx->f)) return;
  ir_inst_t *br = ir_inst_new(IR_BR);
  br->label_id = target->id;
  ir_func_append_inst(ctx->f, br);
}

static void emit_br_cond(ir_build_ctx_t *ctx, ir_val_t cond,
                          ir_block_t *t_block, ir_block_t *f_block) {
  if (cur_block_is_terminated(ctx->f)) return;
  ir_inst_t *br = ir_inst_new(IR_BR_COND);
  br->src1 = cond;
  br->label_id = t_block->id;
  br->else_label_id = f_block->id;
  ir_func_append_inst(ctx->f, br);
}

/* 新しいブロックに切り替える。先頭に IR_LABEL を置いておく
 * (codegen 側でラベル定義として出力)。 */
static void switch_to_new_block(ir_build_ctx_t *ctx, ir_block_t *b) {
  ir_func_switch_block(ctx->f, b);
  ir_inst_t *lbl = ir_inst_new(IR_LABEL);
  lbl->label_id = b->id;
  ir_func_append_inst(ctx->f, lbl);
}

static void push_loop(ir_build_ctx_t *ctx, ir_block_t *cont_b, ir_block_t *break_b) {
  if (ctx->loop_depth >= MAX_LOOP_DEPTH) {
    fail(ctx, "loop nesting too deep");
    return;
  }
  ctx->loops[ctx->loop_depth].continue_block = cont_b;
  ctx->loops[ctx->loop_depth].break_block = break_b;
  ctx->loop_depth++;
}

static void pop_loop(ir_build_ctx_t *ctx) {
  if (ctx->loop_depth > 0) ctx->loop_depth--;
}

/* 現在の switch スコープに「AST node → IR block」を登録する。
 * 同じ AST node には常に同じ block を返す。 */
static ir_block_t *register_case_block(ir_build_ctx_t *ctx, void *ast_node) {
  for (int i = 0; i < ctx->case_map_count; i++) {
    if (ctx->case_map[i].ast_node == ast_node) return ctx->case_map[i].block;
  }
  if (ctx->case_map_count >= MAX_CASES) {
    fail(ctx, "too many case labels in switch");
    return NULL;
  }
  ir_block_t *b = ir_block_new(ctx->f);
  ctx->case_map[ctx->case_map_count].ast_node = ast_node;
  ctx->case_map[ctx->case_map_count].block = b;
  ctx->case_map_count++;
  return b;
}

static ir_block_t *find_case_block(ir_build_ctx_t *ctx, void *ast_node) {
  for (int i = 0; i < ctx->case_map_count; i++) {
    if (ctx->case_map[i].ast_node == ast_node) return ctx->case_map[i].block;
  }
  return NULL;
}

/* AST body を pre-walk して全 ND_CASE / ND_DEFAULT を集める。
 * 各 case に IR block を割り当て (case_map に登録)、走査用の配列に詰める。 */
static void collect_case_labels(ir_build_ctx_t *ctx, node_t *body,
                                  node_case_t **cases, int *case_count,
                                  node_default_t **out_default) {
  if (!body) return;
  if (body->kind == ND_SWITCH) return; /* ネストした switch は別スコープ */
  if (body->kind == ND_CASE) {
    if (*case_count < MAX_CASES) {
      cases[(*case_count)++] = (node_case_t *)body;
    }
    register_case_block(ctx, body);
    /* case ラベル自身は文を持たないことが多いが、ag_c の AST では
     * 次の文へつながる構造になっているので fall-through 探索のため
     * lhs/rhs を再帰する */
  }
  if (body->kind == ND_DEFAULT) {
    *out_default = (node_default_t *)body;
    register_case_block(ctx, body);
  }
  if (body->kind == ND_BLOCK) {
    node_block_t *blk = (node_block_t *)body;
    if (blk->body) {
      for (int i = 0; blk->body[i]; i++) {
        collect_case_labels(ctx, blk->body[i], cases, case_count, out_default);
      }
    }
    return;
  }
  /* if/while/for/do-while の中の case も拾う (C 仕様上 switch スコープ内) */
  if (body->lhs) collect_case_labels(ctx, body->lhs, cases, case_count, out_default);
  if (body->rhs) collect_case_labels(ctx, body->rhs, cases, case_count, out_default);
  if (body->kind == ND_IF || body->kind == ND_FOR) {
    node_ctrl_t *c = (node_ctrl_t *)body;
    if (c->els) collect_case_labels(ctx, c->els, cases, case_count, out_default);
    if (c->init) collect_case_labels(ctx, c->init, cases, case_count, out_default);
    if (c->inc) collect_case_labels(ctx, c->inc, cases, case_count, out_default);
  }
}

static void build_stmt(ir_build_ctx_t *ctx, node_t *node) {
  if (!node || ctx->failed) return;
  switch (node->kind) {
    case ND_BLOCK: {
      node_block_t *b = (node_block_t *)node;
      if (b->body) {
        for (int i = 0; b->body[i]; i++) {
          build_stmt(ctx, b->body[i]);
          if (ctx->failed) return;
        }
      }
      return;
    }
    case ND_RETURN: {
      /* struct 戻り値: *ret_area = node->lhs を memcpy して void return。 */
      if (ctx->f->ret_struct_size > 0 && node->lhs) {
        int src_ptr = -1;
        if (node->lhs->kind == ND_LVAR) {
          src_ptr = address_of_lvar(ctx, ((node_lvar_t *)node->lhs)->offset);
        } else if (node->lhs->kind == ND_DEREF) {
          ir_val_t p = build_expr(ctx, node->lhs->lhs);
          if (ctx->failed) return;
          src_ptr = p.id;
        } else {
          fail(ctx, "struct return value is not LVAR/DEREF");
          return;
        }
        if (src_ptr < 0) return;
        ir_inst_t *cp = ir_inst_new(IR_MEMCPY);
        cp->src1 = ir_val_vreg(ctx->f->ret_area_vreg, IR_TY_PTR);
        cp->src2 = ir_val_vreg(src_ptr, IR_TY_PTR);
        cp->alloca_size = ctx->f->ret_struct_size;
        ir_func_append_inst(ctx->f, cp);
        ir_inst_t *inst = ir_inst_new(IR_RET);
        inst->src1 = ir_val_none();
        ir_func_append_inst(ctx->f, inst);
        return;
      }
      ir_val_t v = ir_val_none();
      if (node->lhs) {
        v = build_expr(ctx, node->lhs);
        if (ctx->failed) return;
      } else {
        v = ir_val_imm(IR_TY_I32, 0);
      }
      ir_inst_t *inst = ir_inst_new(IR_RET);
      inst->src1 = v;
      ir_func_append_inst(ctx->f, inst);
      return;
    }
    case ND_NUM:
      /* 副作用のない単独式 (= 宣言由来のプレースホルダなど) */
      return;
    case ND_ASSIGN:
    case ND_FUNCALL: {
      /* 式文 stmt: 式を評価して値を捨てる */
      (void)build_expr(ctx, node);
      return;
    }
    case ND_IF: {
      node_ctrl_t *c = (node_ctrl_t *)node;
      ir_val_t cond = build_expr(ctx, node->lhs);
      if (ctx->failed) return;
      ir_block_t *then_b = ir_block_new(ctx->f);
      ir_block_t *merge_b = ir_block_new(ctx->f);
      ir_block_t *else_b = c->els ? ir_block_new(ctx->f) : merge_b;
      emit_br_cond(ctx, cond, then_b, else_b);
      /* then 節 */
      switch_to_new_block(ctx, then_b);
      build_stmt(ctx, node->rhs);
      if (ctx->failed) return;
      emit_br(ctx, merge_b);
      /* else 節 */
      if (c->els) {
        switch_to_new_block(ctx, else_b);
        build_stmt(ctx, c->els);
        if (ctx->failed) return;
        emit_br(ctx, merge_b);
      }
      switch_to_new_block(ctx, merge_b);
      return;
    }
    case ND_WHILE: {
      ir_block_t *cond_b = ir_block_new(ctx->f);
      ir_block_t *body_b = ir_block_new(ctx->f);
      ir_block_t *exit_b = ir_block_new(ctx->f);
      emit_br(ctx, cond_b);
      switch_to_new_block(ctx, cond_b);
      ir_val_t cv = build_expr(ctx, node->lhs);
      if (ctx->failed) return;
      emit_br_cond(ctx, cv, body_b, exit_b);
      push_loop(ctx, cond_b, exit_b);
      switch_to_new_block(ctx, body_b);
      build_stmt(ctx, node->rhs);
      pop_loop(ctx);
      if (ctx->failed) return;
      emit_br(ctx, cond_b);
      switch_to_new_block(ctx, exit_b);
      return;
    }
    case ND_DO_WHILE: {
      ir_block_t *body_b = ir_block_new(ctx->f);
      ir_block_t *cond_b = ir_block_new(ctx->f);
      ir_block_t *exit_b = ir_block_new(ctx->f);
      emit_br(ctx, body_b);
      push_loop(ctx, cond_b, exit_b);
      switch_to_new_block(ctx, body_b);
      build_stmt(ctx, node->rhs);
      pop_loop(ctx);
      if (ctx->failed) return;
      emit_br(ctx, cond_b);
      switch_to_new_block(ctx, cond_b);
      ir_val_t cv = build_expr(ctx, node->lhs);
      if (ctx->failed) return;
      emit_br_cond(ctx, cv, body_b, exit_b);
      switch_to_new_block(ctx, exit_b);
      return;
    }
    case ND_FOR: {
      node_ctrl_t *c = (node_ctrl_t *)node;
      if (c->init) {
        build_stmt(ctx, c->init);
        if (ctx->failed) return;
      }
      ir_block_t *cond_b = ir_block_new(ctx->f);
      ir_block_t *body_b = ir_block_new(ctx->f);
      ir_block_t *step_b = ir_block_new(ctx->f);
      ir_block_t *exit_b = ir_block_new(ctx->f);
      emit_br(ctx, cond_b);
      switch_to_new_block(ctx, cond_b);
      if (node->lhs) {
        ir_val_t cv = build_expr(ctx, node->lhs);
        if (ctx->failed) return;
        emit_br_cond(ctx, cv, body_b, exit_b);
      } else {
        /* 条件式なし: 無条件で body へ */
        emit_br(ctx, body_b);
      }
      push_loop(ctx, step_b, exit_b);
      switch_to_new_block(ctx, body_b);
      build_stmt(ctx, node->rhs);
      pop_loop(ctx);
      if (ctx->failed) return;
      emit_br(ctx, step_b);
      switch_to_new_block(ctx, step_b);
      if (c->inc) {
        build_stmt(ctx, c->inc);
        if (ctx->failed) return;
      }
      emit_br(ctx, cond_b);
      switch_to_new_block(ctx, exit_b);
      return;
    }
    case ND_SWITCH: {
      /* 制御値を eval。連続比較方式で各 case と比較し、一致すれば対応 block へ
       * 飛ぶ。最後に default (なければ end) へ無条件 jump。
       * body 内の ND_CASE/ND_DEFAULT は IR_LABEL として配置される。
       * break はループスタックの break_block に登録した end_block へ。 */
      ir_val_t control = build_expr(ctx, node->lhs);
      if (ctx->failed) return;
      /* 退避: 外側 switch の case_map を保存 */
      case_map_entry_t saved[MAX_CASES];
      int saved_count = ctx->case_map_count;
      memcpy(saved, ctx->case_map, sizeof(case_map_entry_t) * (size_t)saved_count);
      ctx->case_map_count = 0;
      /* collect: body 内の case/default に block を割り当て */
      node_case_t *cases[MAX_CASES];
      int case_count = 0;
      node_default_t *default_node = NULL;
      collect_case_labels(ctx, node->rhs, cases, &case_count, &default_node);
      ir_block_t *end_block = ir_block_new(ctx->f);
      /* dispatch */
      for (int i = 0; i < case_count; i++) {
        int v_cmp = ir_func_new_vreg(ctx->f);
        ir_inst_t *eq = ir_inst_new(IR_EQ);
        eq->dst = ir_val_vreg(v_cmp, IR_TY_I32);
        eq->src1 = control;
        eq->src2 = ir_val_imm(IR_TY_I32, cases[i]->val);
        ir_func_append_inst(ctx->f, eq);
        ir_block_t *case_b = find_case_block(ctx, cases[i]);
        ir_block_t *next_b = ir_block_new(ctx->f);
        ir_inst_t *brc = ir_inst_new(IR_BR_COND);
        brc->src1 = ir_val_vreg(v_cmp, IR_TY_I32);
        brc->label_id = case_b->id;
        brc->else_label_id = next_b->id;
        ir_func_append_inst(ctx->f, brc);
        switch_to_new_block(ctx, next_b);
      }
      /* 全 case 不一致なら default or end へ */
      ir_block_t *fallback = default_node ? find_case_block(ctx, default_node) : end_block;
      ir_inst_t *br = ir_inst_new(IR_BR);
      br->label_id = fallback->id;
      ir_func_append_inst(ctx->f, br);
      /* body (case/default は build_stmt 内で switch_to_new_block)。
       * switch 内の continue は外側ループの continue_block へ抜ける必要があるので、
       * 外側のものを継承する。 */
      ir_block_t *outer_cont = (ctx->loop_depth > 0)
                                 ? ctx->loops[ctx->loop_depth - 1].continue_block
                                 : NULL;
      push_loop(ctx, outer_cont, end_block);
      build_stmt(ctx, node->rhs);
      pop_loop(ctx);
      if (!ctx->failed) emit_br(ctx, end_block);
      switch_to_new_block(ctx, end_block);
      /* case_map を復元 */
      ctx->case_map_count = saved_count;
      memcpy(ctx->case_map, saved, sizeof(case_map_entry_t) * (size_t)saved_count);
      return;
    }
    case ND_CASE:
    case ND_DEFAULT: {
      ir_block_t *case_b = find_case_block(ctx, node);
      if (!case_b) {
        fail(ctx, "case/default outside switch");
        return;
      }
      emit_br(ctx, case_b);
      switch_to_new_block(ctx, case_b);
      /* ag_c の parser は `case N: stmt` を ND_CASE.rhs = stmt として格納する。
       * rhs (= ラベル後の文) を続けて build する。 */
      if (node->rhs) build_stmt(ctx, node->rhs);
      return;
    }
    case ND_BREAK: {
      if (ctx->loop_depth == 0) {
        fail(ctx, "break outside loop");
        return;
      }
      emit_br(ctx, ctx->loops[ctx->loop_depth - 1].break_block);
      /* break 以降の文は到達不能だが、Phase 3 では特に対応せず
       * 新しいブロックに移動して残りの文を別に保持しておく */
      ir_block_t *dead = ir_block_new(ctx->f);
      switch_to_new_block(ctx, dead);
      return;
    }
    case ND_CONTINUE: {
      if (ctx->loop_depth == 0) {
        fail(ctx, "continue outside loop");
        return;
      }
      emit_br(ctx, ctx->loops[ctx->loop_depth - 1].continue_block);
      ir_block_t *dead = ir_block_new(ctx->f);
      switch_to_new_block(ctx, dead);
      return;
    }
    default:
      fail(ctx, "unsupported statement node");
      return;
  }
}

static int build_function(ir_build_ctx_t *ctx, node_func_t *fn) {
  if (fn->is_variadic) {
    fail(ctx, "variadic function (Phase 4a unsupported)");
    return 0;
  }
  if (fn->nargs > 8) {
    fail(ctx, "function with more than 8 params (Phase 4a unsupported)");
    return 0;
  }
  ctx->f = ir_func_new(ctx->m, fn->funcname, fn->funcname_len, IR_TY_I32);
  ctx->cur_fn = fn;
  ctx->lvar_count = 0;
  ctx->loop_depth = 0;
  /* >8B struct 戻り値の関数では prologue で x8 を受け取る (Apple ARM64 ABI 簡略版)。
   * ≤8B struct はそのまま x0 で 1 レジスタ返却 (= scalar 経路と同じ動作)。 */
  ctx->f->ret_struct_size = fn->base.ret_struct_size > 8 ? fn->base.ret_struct_size : 0;
  if (ctx->f->ret_struct_size > 0) {
    int v = ir_func_new_vreg(ctx->f);
    ir_inst_t *p = ir_inst_new(IR_PARAM);
    p->dst = ir_val_vreg(v, IR_TY_PTR);
    /* src1.imm = -1 で「x8 を受け取る」を表す。codegen で特別扱い。 */
    p->src1 = ir_val_imm(IR_TY_I32, -1);
    ir_func_append_inst(ctx->f, p);
    ctx->f->ret_area_vreg = v;
  }
  /* 仮引数: IR_PARAM で第 i 引数を受け取り、ALLOCA + STORE で frame slot に保存。
   * 以降の本体で LVAR 参照されたときに通常の LOAD が走るようになる。 */
  for (int i = 0; i < fn->nargs; i++) {
    node_t *arg = fn->args[i];
    if (!arg || arg->kind != ND_LVAR) {
      fail(ctx, "non-lvar parameter (Phase 4a unsupported)");
      return 0;
    }
    node_lvar_t *lv = (node_lvar_t *)arg;
    lvar_t *owner = find_owning_lvar(ctx, lv->offset);
    int param_full_size = owner && owner->size > 0 ? owner->size : lv->mem.type_size;
    if (param_full_size > 8) {
      /* struct 引数 (Apple ARM64 ABI 簡略版): 呼び出し側が一時 buffer に copy
       * したポインタを x{i} で渡してくる前提。callee は struct slot を確保し、
       * IR_MEMCPY で値を取り込む。 */
      int param_vreg = ir_func_new_vreg(ctx->f);
      ir_inst_t *p = ir_inst_new(IR_PARAM);
      p->dst = ir_val_vreg(param_vreg, IR_TY_PTR);
      p->src1 = ir_val_imm(IR_TY_I32, i);
      ir_func_append_inst(ctx->f, p);
      int slot_vreg = address_of_lvar(ctx, lv->offset);
      if (slot_vreg < 0) return 0;
      ir_inst_t *cp = ir_inst_new(IR_MEMCPY);
      cp->src1 = ir_val_vreg(slot_vreg, IR_TY_PTR);
      cp->src2 = ir_val_vreg(param_vreg, IR_TY_PTR);
      cp->alloca_size = param_full_size;
      ir_func_append_inst(ctx->f, cp);
      continue;
    }
    ir_type_t vty = lvar_value_type(lv);
    int param_vreg = ir_func_new_vreg(ctx->f);
    ir_inst_t *p = ir_inst_new(IR_PARAM);
    p->dst = ir_val_vreg(param_vreg, vty);
    p->src1 = ir_val_imm(IR_TY_I32, i);
    ir_func_append_inst(ctx->f, p);
    int ptr_vreg = address_of_lvar(ctx, lv->offset);
    if (ptr_vreg < 0) return 0;
    ir_inst_t *st = ir_inst_new(IR_STORE);
    st->src1 = ir_val_vreg(ptr_vreg, IR_TY_PTR);
    st->src2 = ir_val_vreg(param_vreg, vty);
    ir_func_append_inst(ctx->f, st);
  }
  build_stmt(ctx, fn->base.rhs);
  if (ctx->failed) return 0;
  /* 末尾に RET が無いとき、main は暗黙 `return 0` を補う (AST 直 codegen 互換)。
   * それ以外の関数は補わない (未定義動作のままにしておく)。 */
  int is_main = (fn->funcname_len == 4 &&
                 fn->funcname && memcmp(fn->funcname, "main", 4) == 0);
  if (is_main && (!ctx->f->cur_block || !ctx->f->cur_block->tail ||
                  ctx->f->cur_block->tail->op != IR_RET)) {
    ir_inst_t *r = ir_inst_new(IR_RET);
    r->src1 = ir_val_imm(IR_TY_I32, 0);
    ir_func_append_inst(ctx->f, r);
  }
  /* main 以外でも、末尾が分岐/return で終わってない場合は安全のため ret 0 を補う
   * (ABI 上 caller が戻り値を期待していなければ実害なし)。 */
  if (!is_main && (!ctx->f->cur_block || !ctx->f->cur_block->tail ||
                   (ctx->f->cur_block->tail->op != IR_RET &&
                    ctx->f->cur_block->tail->op != IR_BR))) {
    ir_inst_t *r = ir_inst_new(IR_RET);
    r->src1 = ir_val_imm(IR_TY_I32, 0);
    ir_func_append_inst(ctx->f, r);
  }
  return 1;
}

ir_module_t *ir_build_module(node_t **code) {
  ir_build_ctx_t ctx = {0};
  ctx.m = ir_module_new();
  if (!code) return ctx.m;
  for (int i = 0; code[i]; i++) {
    node_t *n = code[i];
    if (n->kind != ND_FUNCDEF) {
      fail(&ctx, "top-level node is not a function definition");
      return NULL;
    }
    if (!build_function(&ctx, (node_func_t *)n)) return NULL;
  }
  return ctx.m;
}
