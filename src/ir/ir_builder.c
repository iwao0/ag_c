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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LVARS 256

typedef struct {
  ir_module_t *m;
  ir_func_t *f;
  int failed;
  /* offset → ALLOCA vreg (ポインタ) のマップ */
  int lvar_offset[MAX_LVARS];
  int lvar_vreg[MAX_LVARS];
  int lvar_count;
} ir_build_ctx_t;

static void fail(ir_build_ctx_t *ctx, const char *msg) {
  if (ctx->failed) return;
  ctx->failed = 1;
  fprintf(stderr, "ir_build: unsupported: %s\n", msg);
}

static int find_alloca_vreg(ir_build_ctx_t *ctx, int offset) {
  for (int i = 0; i < ctx->lvar_count; i++) {
    if (ctx->lvar_offset[i] == offset) return ctx->lvar_vreg[i];
  }
  return -1;
}

static int alloca_for_lvar(ir_build_ctx_t *ctx, int offset, int size, int align) {
  int existing = find_alloca_vreg(ctx, offset);
  if (existing >= 0) return existing;
  if (ctx->lvar_count >= MAX_LVARS) {
    fail(ctx, "too many local variables");
    return -1;
  }
  int v = ir_func_new_vreg(ctx->f);
  ir_inst_t *inst = ir_inst_new(IR_ALLOCA);
  inst->dst = ir_val_vreg(v, IR_TY_PTR);
  inst->alloca_size = size;
  inst->alloca_align = align;
  ir_func_append_inst(ctx->f, inst);
  ctx->lvar_offset[ctx->lvar_count] = offset;
  ctx->lvar_vreg[ctx->lvar_count] = v;
  ctx->lvar_count++;
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
      int ptr_vreg = alloca_for_lvar(ctx, lv->offset, 4, 4);
      if (ptr_vreg < 0) return ir_val_none();
      int v = ir_func_new_vreg(ctx->f);
      ir_inst_t *inst = ir_inst_new(IR_LOAD);
      inst->dst = ir_val_vreg(v, IR_TY_I32);
      inst->src1 = ir_val_vreg(ptr_vreg, IR_TY_PTR);
      ir_func_append_inst(ctx->f, inst);
      return inst->dst;
    }
    case ND_ASSIGN: {
      /* lhs は LVAR を想定 */
      if (!node->lhs || node->lhs->kind != ND_LVAR) {
        fail(ctx, "assign target is not LVAR");
        return ir_val_none();
      }
      node_lvar_t *lv = (node_lvar_t *)node->lhs;
      int ptr_vreg = alloca_for_lvar(ctx, lv->offset, 4, 4);
      if (ptr_vreg < 0) return ir_val_none();
      ir_val_t rhs = build_expr(ctx, node->rhs);
      if (ctx->failed) return ir_val_none();
      ir_inst_t *st = ir_inst_new(IR_STORE);
      st->src1 = ir_val_vreg(ptr_vreg, IR_TY_PTR);
      st->src2 = rhs;
      ir_func_append_inst(ctx->f, st);
      return rhs;
    }
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_MOD: {
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
    case ND_ASSIGN: {
      (void)build_expr(ctx, node);
      return;
    }
    default:
      fail(ctx, "unsupported statement node");
      return;
  }
}

static int build_function(ir_build_ctx_t *ctx, node_func_t *fn) {
  if (fn->nargs > 0) {
    fail(ctx, "function with parameters (Phase 2 unsupported)");
    return 0;
  }
  if (fn->is_variadic) {
    fail(ctx, "variadic function (Phase 2 unsupported)");
    return 0;
  }
  ctx->f = ir_func_new(ctx->m, fn->funcname, fn->funcname_len, IR_TY_I32);
  ctx->lvar_count = 0;
  build_stmt(ctx, fn->base.rhs);
  if (ctx->failed) return 0;
  /* main の暗黙 `return 0` を付与しておく (AST 直 codegen と同等の挙動) */
  if (!ctx->f->cur_block || !ctx->f->cur_block->tail || ctx->f->cur_block->tail->op != IR_RET) {
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
