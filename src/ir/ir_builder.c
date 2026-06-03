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
#define MAX_LOOP_DEPTH 32

/* break/continue 用ループスタック。それぞれ「飛び先ブロック」。 */
typedef struct {
  ir_block_t *continue_block;
  ir_block_t *break_block;
} loop_ctx_t;

typedef struct {
  ir_module_t *m;
  ir_func_t *f;
  int failed;
  /* offset → ALLOCA vreg (ポインタ) のマップ */
  int lvar_offset[MAX_LVARS];
  int lvar_vreg[MAX_LVARS];
  int lvar_count;
  loop_ctx_t loops[MAX_LOOP_DEPTH];
  int loop_depth;
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
          cargs[i] = build_expr(ctx, fn->args[i]);
          if (ctx->failed) return ir_val_none();
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
  ctx->lvar_count = 0;
  ctx->loop_depth = 0;
  /* 仮引数: IR_PARAM で第 i 引数を受け取り、ALLOCA + STORE で frame slot に保存。
   * 以降の本体で LVAR 参照されたときに通常の LOAD が走るようになる。 */
  for (int i = 0; i < fn->nargs; i++) {
    node_t *arg = fn->args[i];
    if (!arg || arg->kind != ND_LVAR) {
      fail(ctx, "non-lvar parameter (Phase 4a unsupported)");
      return 0;
    }
    node_lvar_t *lv = (node_lvar_t *)arg;
    int param_vreg = ir_func_new_vreg(ctx->f);
    ir_inst_t *p = ir_inst_new(IR_PARAM);
    p->dst = ir_val_vreg(param_vreg, IR_TY_I32);
    p->src1 = ir_val_imm(IR_TY_I32, i);
    ir_func_append_inst(ctx->f, p);
    int ptr_vreg = alloca_for_lvar(ctx, lv->offset, 4, 4);
    if (ptr_vreg < 0) return 0;
    ir_inst_t *st = ir_inst_new(IR_STORE);
    st->src1 = ir_val_vreg(ptr_vreg, IR_TY_PTR);
    st->src2 = ir_val_vreg(param_vreg, IR_TY_I32);
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
