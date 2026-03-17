#include "../ag_c.h"
#include "../parser/parser.h"
#include "../tokenizer/escape.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdlib.h>

// ラベルの一意番号を生成するカウンタ
static int label_count = 0;
static int break_labels[128];
static int continue_labels[128];
static int control_depth = 0;
// 浮動小数点定数用ラベルカウンタ

// Apple Silicon (ARM64) 向けのアセンブリコード生成

// 26個の変数(a-z) * 8バイト = 208バイト + フレームポインタ/リンクレジスタ用16バイト = 224
// 16バイトアラインメントに合わせる → 224
#define STACK_SIZE 1024

static node_mem_t *as_mem(node_t *node) { return (node_mem_t *)node; }
static node_lvar_t *as_lvar(node_t *node) { return (node_lvar_t *)node; }
static node_num_t *as_num(node_t *node) { return (node_num_t *)node; }
static node_block_t *as_block(node_t *node) { return (node_block_t *)node; }
static node_func_t *as_func(node_t *node) { return (node_func_t *)node; }
static node_ctrl_t *as_ctrl(node_t *node) { return (node_ctrl_t *)node; }
static node_string_t *as_string(node_t *node) { return (node_string_t *)node; }
static node_case_t *as_case(node_t *node) { return (node_case_t *)node; }
static node_default_t *as_default(node_t *node) { return (node_default_t *)node; }

static void push_control_labels(int break_lbl, int continue_lbl) {
  if (control_depth >= 128) {
    fprintf(stderr, "制御構文のネストが深すぎます\n");
    exit(1);
  }
  break_labels[control_depth] = break_lbl;
  continue_labels[control_depth] = continue_lbl;
  control_depth++;
}

static void pop_control_labels(void) {
  if (control_depth > 0) control_depth--;
}

static int current_break_label(void) {
  if (control_depth == 0) return -1;
  return break_labels[control_depth - 1];
}

static int current_continue_label(void) {
  for (int i = control_depth - 1; i >= 0; i--) {
    if (continue_labels[i] >= 0) return continue_labels[i];
  }
  return -1;
}

typedef struct {
  node_case_t **cases;
  int case_count;
  int case_cap;
  node_default_t *default_node;
} switch_collect_t;

static void switch_collect_add_case(switch_collect_t *sc, node_case_t *c) {
  if (sc->case_count >= sc->case_cap) {
    sc->case_cap = sc->case_cap ? sc->case_cap * 2 : 8;
    sc->cases = realloc(sc->cases, sizeof(node_case_t *) * (size_t)sc->case_cap);
  }
  sc->cases[sc->case_count++] = c;
}

static void collect_switch_labels(node_t *node, switch_collect_t *sc) {
  if (!node) return;
  if (node->kind == ND_SWITCH) return; // ネストしたswitchは別スコープ

  if (node->kind == ND_CASE) {
    node_case_t *c = as_case(node);
    c->label_id = label_count++;
    switch_collect_add_case(sc, c);
    collect_switch_labels(c->base.rhs, sc);
    return;
  }
  if (node->kind == ND_DEFAULT) {
    if (sc->default_node) {
      fprintf(stderr, "default が重複しています\n");
      exit(1);
    }
    node_default_t *d = as_default(node);
    d->label_id = label_count++;
    sc->default_node = d;
    collect_switch_labels(d->base.rhs, sc);
    return;
  }
  if (node->kind == ND_BLOCK) {
    node_block_t *b = as_block(node);
    for (int i = 0; b->body[i]; i++) {
      collect_switch_labels(b->body[i], sc);
    }
    return;
  }
}

static void gen_stmt(node_t *node);

static void gen_switch_body(node_t *node) {
  if (!node) return;
  if (node->kind == ND_BLOCK) {
    node_block_t *b = as_block(node);
    for (int i = 0; b->body[i]; i++) {
      gen_switch_body(b->body[i]);
    }
    return;
  }
  if (node->kind == ND_CASE) {
    node_case_t *c = as_case(node);
    printf(".Lcase%d:\n", c->label_id);
    gen_stmt(c->base.rhs);
    return;
  }
  if (node->kind == ND_DEFAULT) {
    node_default_t *d = as_default(node);
    printf(".Ldefault%d:\n", d->label_id);
    gen_stmt(d->base.rhs);
    return;
  }
  gen_stmt(node);
}

static int is_main_func(const node_func_t *fn) {
  return fn->funcname_len == 4 && strncmp(fn->funcname, "main", 4) == 0;
}

void gen_main_prologue(void) {
  // 関数定義で生成するため空にする（互換性維持）
}

void gen_main_epilogue(void) {
  // 関数定義で生成するため空にする（互換性維持）
}

static void gen_expr(node_t *node);
static void gen_stmt(node_t *node);

static void gen_load_x0_from_addr(int type_size) {
  if (type_size == 1)
    printf("  ldrb w0, [x1]\n");
  else if (type_size == 2)
    printf("  ldrh w0, [x1]\n");
  else if (type_size == 4)
    printf("  ldr w0, [x1]\n");
  else
    printf("  ldr x0, [x1]\n");
}

static void gen_store_x0_to_addr(int type_size) {
  if (type_size == 1)
    printf("  strb w0, [x1]\n");
  else if (type_size == 2)
    printf("  strh w0, [x1]\n");
  else if (type_size == 4)
    printf("  str w0, [x1]\n");
  else
    printf("  str x0, [x1]\n");
}

// レジスタ名取得ヘルパー
static const char *freg(int fp_kind, int reg_idx) {
  if (fp_kind == TK_FLOAT_KIND_FLOAT) return (reg_idx == 0) ? "s0" : "s1";
  return (reg_idx == 0) ? "d0" : "d1";
}

// child_type の値をスタックからポップし、target_type にキャストして FPU レジスタ (s0/d0 または s1/d1) にロードする
static void gen_pop_fpu(int target_type, int child_type, int reg_idx) {
  const char *s = (reg_idx == 0) ? "s0" : "s1";
  const char *d = (reg_idx == 0) ? "d0" : "d1";
  
  if (child_type == TK_FLOAT_KIND_FLOAT) { // float がプッシュされている
    printf("  ldr %s, [sp], #16\n", s);
    if (target_type >= TK_FLOAT_KIND_DOUBLE) {
      printf("  fcvt %s, %s\n", d, s); // float -> double
    }
  } else if (child_type >= TK_FLOAT_KIND_DOUBLE) { // double/long double(現状lowering) がプッシュされている
    printf("  ldr %s, [sp], #16\n", d);
    if (target_type == TK_FLOAT_KIND_FLOAT) {
      printf("  fcvt %s, %s\n", s, d); // double -> float
    }
  } else {                      // int がプッシュされている
    printf("  ldr x%d, [sp], #16\n", reg_idx);
    if (target_type == TK_FLOAT_KIND_FLOAT) {
      printf("  scvtf %s, x%d\n", s, reg_idx); // int -> float
    } else {
      printf("  scvtf %s, x%d\n", d, reg_idx); // int -> double
    }
  }
}

// 左辺値（変数のアドレス）をスタックへプッシュする
static void gen_lval(node_t *node) {
  if (node->kind == ND_DEREF) {
    // *p = x の左辺値: p の値（アドレス）をプッシュ
    gen_expr(node->lhs);
    return;
  }
  if (node->kind != ND_LVAR) {
    fprintf(stderr, "代入の左辺値が変数ではありません\n");
    return;
  }
  printf("  add x0, x29, #%d\n", 16 + as_lvar(node)->offset);
  printf("  str x0, [sp, #-16]!\n");
}

static void gen_expr(node_t *node) {
  switch (node->kind) {
  case ND_NUM:
    if (node->fp_kind) {
      node_num_t *num = as_num(node);
      // 浮動小数点リテラルをデータセクションからロード
      printf("  adrp x0, .LCF%d@PAGE\n", num->fval_id);
      printf("  add x0, x0, .LCF%d@PAGEOFF\n", num->fval_id);
      if (node->fp_kind == TK_FLOAT_KIND_FLOAT) {
        printf("  ldr s0, [x0]\n");
        printf("  str s0, [sp, #-16]!\n");
      } else {
        printf("  ldr d0, [x0]\n");
        printf("  str d0, [sp, #-16]!\n");
      }
    } else {
      printf("  mov x0, #%lld\n", as_num(node)->val);
      printf("  str x0, [sp, #-16]!\n");
    }
    return;
  case ND_LVAR:
    gen_lval(node);
    printf("  ldr x0, [sp], #16\n");
    if (node->fp_kind == TK_FLOAT_KIND_FLOAT) {
      printf("  ldr s0, [x0]\n");
      printf("  str s0, [sp, #-16]!\n");
    } else if (node->fp_kind >= TK_FLOAT_KIND_DOUBLE) {
      printf("  ldr d0, [x0]\n");
      printf("  str d0, [sp, #-16]!\n");
    } else {
      if (as_lvar(node)->mem.type_size == 1)
        printf("  ldrb w0, [x0]\n");
      else if (as_lvar(node)->mem.type_size == 2)
        printf("  ldrh w0, [x0]\n");
      else if (as_lvar(node)->mem.type_size == 4)
        printf("  ldr w0, [x0]\n");
      else
        printf("  ldr x0, [x0]\n");
      printf("  str x0, [sp, #-16]!\n");
    }
    return;
  case ND_DEREF:
    gen_expr(node->lhs);
    printf("  ldr x0, [sp], #16\n");
    if (as_mem(node)->type_size == 1)
      printf("  ldrb w0, [x0]\n");
    else if (as_mem(node)->type_size == 2)
      printf("  ldrh w0, [x0]\n");
    else if (as_mem(node)->type_size == 4)
      printf("  ldr w0, [x0]\n");
    else
      printf("  ldr x0, [x0]\n");
    printf("  str x0, [sp, #-16]!\n");
    return;
  case ND_ADDR:
    gen_lval(node->lhs);
    return;
  case ND_STRING:
    // 文字列ラベルのアドレスをロード
    printf("  adrp x0, %s@PAGE\n", as_string(node)->string_label);
    printf("  add x0, x0, %s@PAGEOFF\n", as_string(node)->string_label);
    printf("  str x0, [sp, #-16]!\n");
    return;
  case ND_ASSIGN:
    gen_lval(node->lhs);
    gen_expr(node->rhs);
    if (node->fp_kind == TK_FLOAT_KIND_FLOAT) {
      // float 代入
      gen_pop_fpu(TK_FLOAT_KIND_FLOAT, node->rhs->fp_kind, 0); // s0 に rhs をロード
      printf("  ldr x0, [sp], #16\n"); // lhs アドレス
      printf("  str s0, [x0]\n");
      printf("  str s0, [sp, #-16]!\n");
    } else if (node->fp_kind >= TK_FLOAT_KIND_DOUBLE) {
      // double/long double(現状lowering) 代入
      gen_pop_fpu(TK_FLOAT_KIND_DOUBLE, node->rhs->fp_kind, 0); // d0 に rhs をロード
      printf("  ldr x0, [sp], #16\n"); // lhs アドレス
      printf("  str d0, [x0]\n");
      printf("  str d0, [sp, #-16]!\n");
    } else {
      printf("  ldr x1, [sp], #16\n");
      printf("  ldr x0, [sp], #16\n");
      if (as_mem(node)->type_size == 1)
        printf("  strb w1, [x0]\n");
      else if (as_mem(node)->type_size == 2)
        printf("  strh w1, [x0]\n");
      else if (as_mem(node)->type_size == 4)
        printf("  str w1, [x0]\n");
      else
        printf("  str x1, [x0]\n");
      printf("  str x1, [sp, #-16]!\n");
    }
    return;
  case ND_PRE_INC:
  case ND_PRE_DEC:
  case ND_POST_INC:
  case ND_POST_DEC: {
    node_t *target = node->lhs;
    int type_size = 8;
    if (target->kind == ND_LVAR) type_size = as_lvar(target)->mem.type_size;
    else if (target->kind == ND_DEREF) type_size = as_mem(target)->type_size ? as_mem(target)->type_size : 8;

    gen_lval(target);
    printf("  ldr x1, [sp], #16\n"); // x1: addr
    gen_load_x0_from_addr(type_size);   // x0: old value

    if (node->kind == ND_POST_INC || node->kind == ND_POST_DEC) {
      printf("  mov x2, x0\n");         // x2: return old value
    }

    if (node->kind == ND_PRE_INC || node->kind == ND_POST_INC)
      printf("  add x0, x0, #1\n");
    else
      printf("  sub x0, x0, #1\n");

    gen_store_x0_to_addr(type_size);    // store updated value

    if (node->kind == ND_POST_INC || node->kind == ND_POST_DEC)
      printf("  str x2, [sp, #-16]!\n");
    else
      printf("  str x0, [sp, #-16]!\n");
    return;
  }
  case ND_LOGAND: {
    int false_lbl = label_count++;
    int end_lbl = label_count++;
    gen_expr(node->lhs);
    printf("  ldr x0, [sp], #16\n");
    printf("  cbz x0, .Lfalse%d\n", false_lbl);
    gen_expr(node->rhs);
    printf("  ldr x0, [sp], #16\n");
    printf("  cbz x0, .Lfalse%d\n", false_lbl);
    printf("  mov x0, #1\n");
    printf("  b .Lend%d\n", end_lbl);
    printf(".Lfalse%d:\n", false_lbl);
    printf("  mov x0, #0\n");
    printf(".Lend%d:\n", end_lbl);
    printf("  str x0, [sp, #-16]!\n");
    return;
  }
  case ND_LOGOR: {
    int true_lbl = label_count++;
    int end_lbl = label_count++;
    gen_expr(node->lhs);
    printf("  ldr x0, [sp], #16\n");
    printf("  cbnz x0, .Ltrue%d\n", true_lbl);
    gen_expr(node->rhs);
    printf("  ldr x0, [sp], #16\n");
    printf("  cbnz x0, .Ltrue%d\n", true_lbl);
    printf("  mov x0, #0\n");
    printf("  b .Lend%d\n", end_lbl);
    printf(".Ltrue%d:\n", true_lbl);
    printf("  mov x0, #1\n");
    printf(".Lend%d:\n", end_lbl);
    printf("  str x0, [sp, #-16]!\n");
    return;
  }
  case ND_TERNARY: {
    node_ctrl_t *ctrl = as_ctrl(node);
    int else_lbl = label_count++;
    int end_lbl = label_count++;
    gen_expr(ctrl->base.lhs);
    printf("  ldr x0, [sp], #16\n");
    printf("  cbz x0, .Lelse%d\n", else_lbl);
    gen_expr(ctrl->base.rhs);
    printf("  b .Lend%d\n", end_lbl);
    printf(".Lelse%d:\n", else_lbl);
    gen_expr(ctrl->els);
    printf(".Lend%d:\n", end_lbl);
    return;
  }
  case ND_FUNCALL: {
    node_func_t *fn = as_func(node);
    // 引数を評価してレジスタに格納
    for (int i = 0; i < fn->nargs; i++) {
      gen_expr(fn->args[i]);
    }
    // スタックからレジスタへ (逆順にポップ)
    for (int i = fn->nargs - 1; i >= 0; i--) {
      printf("  ldr x%d, [sp], #16\n", i);
    }
    // 関数呼び出し
    printf("  bl _%.*s\n", fn->funcname_len, fn->funcname);
    // 戻り値をスタックにプッシュ
    printf("  str x0, [sp, #-16]!\n");
    return;
  }
  default:
    break;
  }

  // 二項演算
  tk_float_kind_t fpu_op_type = node->fp_kind; // ADD/SUB/MUL/DIV の場合は結果型
  if (node->kind == ND_EQ || node->kind == ND_NE || node->kind == ND_LT || node->kind == ND_LE) {
    // 比較演算の場合はオペランドの型を見る
    if (node->lhs && node->lhs->fp_kind > fpu_op_type) fpu_op_type = node->lhs->fp_kind;
    if (node->rhs && node->rhs->fp_kind > fpu_op_type) fpu_op_type = node->rhs->fp_kind;
  }

  if (fpu_op_type) {
    // FPU 演算
    gen_expr(node->lhs);
    gen_expr(node->rhs);
    const char *r0 = freg(fpu_op_type, 0);
    const char *r1 = freg(fpu_op_type, 1);
    
    gen_pop_fpu(fpu_op_type, node->rhs->fp_kind, 1); // rhs を r1 に
    gen_pop_fpu(fpu_op_type, node->lhs->fp_kind, 0); // lhs を r0 に

    switch (node->kind) {
    case ND_ADD:
      printf("  fadd %s, %s, %s\n", r0, r0, r1);
      break;
    case ND_SUB:
      printf("  fsub %s, %s, %s\n", r0, r0, r1);
      break;
    case ND_MUL:
      printf("  fmul %s, %s, %s\n", r0, r0, r1);
      break;
    case ND_DIV:
      printf("  fdiv %s, %s, %s\n", r0, r0, r1);
      break;
    case ND_EQ:
      printf("  fcmp %s, %s\n", r0, r1);
      printf("  cset x0, eq\n");
      printf("  str x0, [sp, #-16]!\n");
      return;
    case ND_NE:
      printf("  fcmp %s, %s\n", r0, r1);
      printf("  cset x0, ne\n");
      printf("  str x0, [sp, #-16]!\n");
      return;
    case ND_LT:
      printf("  fcmp %s, %s\n", r0, r1);
      printf("  cset x0, lo\n");
      printf("  str x0, [sp, #-16]!\n");
      return;
    case ND_LE:
      printf("  fcmp %s, %s\n", r0, r1);
      printf("  cset x0, ls\n");
      printf("  str x0, [sp, #-16]!\n");
      return;
    default:
      break;
    }
    printf("  str %s, [sp, #-16]!\n", r0);
    return;
  }

  // 整数二項演算
  gen_expr(node->lhs);
  gen_expr(node->rhs);

  printf("  ldr x1, [sp], #16\n");
  printf("  ldr x0, [sp], #16\n");

  switch (node->kind) {
  case ND_ADD:
    printf("  add x0, x0, x1\n");
    break;
  case ND_SUB:
    printf("  sub x0, x0, x1\n");
    break;
  case ND_MUL:
    printf("  mul x0, x0, x1\n");
    break;
  case ND_DIV:
    printf("  sdiv x0, x0, x1\n");
    break;
  case ND_MOD:
    printf("  sdiv x2, x0, x1\n");
    printf("  msub x0, x2, x1, x0\n");
    break;
  case ND_EQ:
    printf("  cmp x0, x1\n");
    printf("  cset x0, eq\n");
    break;
  case ND_NE:
    printf("  cmp x0, x1\n");
    printf("  cset x0, ne\n");
    break;
  case ND_LT:
    printf("  cmp x0, x1\n");
    printf("  cset x0, lt\n");
    break;
  case ND_LE:
    printf("  cmp x0, x1\n");
    printf("  cset x0, le\n");
    break;
  case ND_BITAND:
    printf("  and x0, x0, x1\n");
    break;
  case ND_BITXOR:
    printf("  eor x0, x0, x1\n");
    break;
  case ND_BITOR:
    printf("  orr x0, x0, x1\n");
    break;
  case ND_SHL:
    printf("  lsl x0, x0, x1\n");
    break;
  case ND_SHR:
    printf("  asr x0, x0, x1\n");
    break;
  default:
    break;
  }

  printf("  str x0, [sp, #-16]!\n");
}

static void gen_stmt(node_t *node) {
  switch (node->kind) {
  case ND_BLOCK:
    for (int i = 0; as_block(node)->body[i]; i++) {
      gen_stmt(as_block(node)->body[i]);
    }
    return;
  case ND_RETURN:
    if (node->lhs) {
      gen_expr(node->lhs);
      if (node->fp_kind == TK_FLOAT_KIND_FLOAT) { // 関数の戻り値が float
        gen_pop_fpu(TK_FLOAT_KIND_FLOAT, node->lhs->fp_kind, 0); // s0 にロード
      } else if (node->fp_kind >= TK_FLOAT_KIND_DOUBLE) { // 関数の戻り値が double/long double(現状lowering)
        gen_pop_fpu(TK_FLOAT_KIND_DOUBLE, node->lhs->fp_kind, 0); // d0 にロード
      } else {                               // 関数の戻り値が 整数
        if (node->lhs->fp_kind == TK_FLOAT_KIND_FLOAT) {
          printf("  ldr s0, [sp], #16\n");
          printf("  fcvtzs x0, s0\n");       // float->int
        } else if (node->lhs->fp_kind >= TK_FLOAT_KIND_DOUBLE) {
          printf("  ldr d0, [sp], #16\n");
          printf("  fcvtzs x0, d0\n");       // double->int
        } else {
          printf("  ldr x0, [sp], #16\n");   // そのまま int
        }
      }
    } else {
      // void 関数の return; は戻り値レジスタを0で統一
      printf("  mov x0, #0\n");
    }
    printf("  ldp x29, x30, [sp]\n");
    printf("  add sp, sp, #%d\n", STACK_SIZE);
    printf("  ret\n");
    return;
  case ND_FUNCDEF: {
    node_func_t *fn = as_func(node);
    // 関数ラベルの出力
    printf(".global _%.*s\n", fn->funcname_len, fn->funcname);
    printf(".align 2\n");
    printf("_%.*s:\n", fn->funcname_len, fn->funcname);
    // プロローグ
    printf("  sub sp, sp, #%d\n", STACK_SIZE);
    printf("  stp x29, x30, [sp]\n");
    printf("  mov x29, sp\n");
    // 仮引数をレジスタからローカル変数スロットへ保存
    for (int i = 0; i < fn->nargs; i++) {
      printf("  str x%d, [x29, #%d]\n", i, 16 + as_lvar(fn->args[i])->offset);
    }
    // 関数本体
    gen_stmt(fn->base.rhs);
    // main の return が無い場合は 0 を返す
    if (is_main_func(fn)) {
      printf("  mov x0, #0\n");
    }
    // エピローグ
    printf("  ldp x29, x30, [sp]\n");
    printf("  add sp, sp, #%d\n", STACK_SIZE);
    printf("  ret\n");
    return;
  }
  case ND_IF: {
    node_ctrl_t *ctrl = as_ctrl(node);
    int lbl = label_count++;
    gen_expr(ctrl->base.lhs);
    printf("  ldr x0, [sp], #16\n");
    printf("  cbz x0, .Lelse%d\n", lbl);
    gen_stmt(ctrl->base.rhs);
    if (ctrl->els) {
      printf("  b .Lend%d\n", lbl);
      printf(".Lelse%d:\n", lbl);
      gen_stmt(ctrl->els);
      printf(".Lend%d:\n", lbl);
    } else {
      printf("  b .Lend%d\n", lbl);
      printf(".Lelse%d:\n", lbl);
      printf(".Lend%d:\n", lbl);
    }
    return;
  }
  case ND_WHILE: {
    node_ctrl_t *ctrl = as_ctrl(node);
    int begin_lbl = label_count++;
    int cont_lbl = label_count++;
    int end_lbl = label_count++;
    push_control_labels(end_lbl, cont_lbl);
    printf(".Lbegin%d:\n", begin_lbl);
    printf(".Lcont%d:\n", cont_lbl);
    gen_expr(ctrl->base.lhs);
    printf("  ldr x0, [sp], #16\n");
    printf("  cbz x0, .Lend%d\n", end_lbl);
    gen_stmt(ctrl->base.rhs);
    printf("  b .Lbegin%d\n", begin_lbl);
    printf(".Lend%d:\n", end_lbl);
    pop_control_labels();
    return;
  }
  case ND_DO_WHILE: {
    node_ctrl_t *ctrl = as_ctrl(node);
    int begin_lbl = label_count++;
    int cont_lbl = label_count++;
    int end_lbl = label_count++;
    push_control_labels(end_lbl, cont_lbl);
    printf(".Lbegin%d:\n", begin_lbl);
    gen_stmt(ctrl->base.rhs);
    printf(".Lcont%d:\n", cont_lbl);
    gen_expr(ctrl->base.lhs);
    printf("  ldr x0, [sp], #16\n");
    printf("  cbnz x0, .Lbegin%d\n", begin_lbl);
    printf(".Lend%d:\n", end_lbl);
    pop_control_labels();
    return;
  }
  case ND_FOR: {
    node_ctrl_t *ctrl = as_ctrl(node);
    int begin_lbl = label_count++;
    int cont_lbl = label_count++;
    int end_lbl = label_count++;
    push_control_labels(end_lbl, cont_lbl);
    if (ctrl->init) {
      gen_expr(ctrl->init);
      printf("  add sp, sp, #16\n");
    }
    printf(".Lbegin%d:\n", begin_lbl);
    if (ctrl->base.lhs) {
      gen_expr(ctrl->base.lhs);
      printf("  ldr x0, [sp], #16\n");
      printf("  cbz x0, .Lend%d\n", end_lbl);
    }
    gen_stmt(ctrl->base.rhs);
    printf(".Lcont%d:\n", cont_lbl);
    if (ctrl->inc) {
      gen_expr(ctrl->inc);
      printf("  add sp, sp, #16\n");
    }
    printf("  b .Lbegin%d\n", begin_lbl);
    printf(".Lend%d:\n", end_lbl);
    pop_control_labels();
    return;
  }
  case ND_SWITCH: {
    node_ctrl_t *ctrl = as_ctrl(node);
    int end_lbl = label_count++;

    gen_expr(ctrl->base.lhs);
    printf("  ldr x0, [sp], #16\n");

    switch_collect_t sc = {0};
    collect_switch_labels(ctrl->base.rhs, &sc);

    for (int i = 0; i < sc.case_count; i++) {
      node_case_t *c = sc.cases[i];
      printf("  mov x1, #%lld\n", c->val);
      printf("  cmp x0, x1\n");
      printf("  beq .Lcase%d\n", c->label_id);
    }
    if (sc.default_node) {
      printf("  b .Ldefault%d\n", sc.default_node->label_id);
    } else {
      printf("  b .Lend%d\n", end_lbl);
    }

    push_control_labels(end_lbl, -1); // switch は continue 対象外
    gen_switch_body(ctrl->base.rhs);
    pop_control_labels();

    printf(".Lend%d:\n", end_lbl);
    free(sc.cases);
    return;
  }
  case ND_BREAK:
    if (current_break_label() < 0) {
      fprintf(stderr, "break はループまたはswitch内でのみ使用できます\n");
      exit(1);
    }
    printf("  b .Lend%d\n", current_break_label());
    return;
  case ND_CONTINUE:
    if (current_continue_label() < 0) {
      fprintf(stderr, "continue はループ内でのみ使用できます\n");
      exit(1);
    }
    printf("  b .Lcont%d\n", current_continue_label());
    return;
  default:
    gen_expr(node);
    printf("  add sp, sp, #16\n");
    return;
  }
}

void gen(node_t *node) {
  gen_stmt(node);
}

void gen_string_literals(void) {
  if (!string_literals) return;
  printf(".section __TEXT,__cstring\n");
  for (string_lit_t *lit = string_literals; lit; lit = lit->next) {
    printf("%s:\n", lit->label);
    int i = 0;
    while (i < lit->len) {
      uint32_t v = 0;
      if (lit->str[i] == '\\') {
        tk_parse_escape_value(lit->str, lit->len, &i, &v);
      } else {
        v = (unsigned char)lit->str[i];
        i++;
      }
      if (lit->char_width == TK_CHAR_WIDTH_CHAR) {
        printf("  .byte %u\n", (unsigned)(v & 0xFF));
      } else if (lit->char_width == TK_CHAR_WIDTH_CHAR16) {
        printf("  .hword %u\n", (unsigned)(v & 0xFFFF));
      } else {
        printf("  .word %u\n", (unsigned)v);
      }
    }
    if (lit->char_width == TK_CHAR_WIDTH_CHAR) printf("  .byte 0\n");
    else if (lit->char_width == TK_CHAR_WIDTH_CHAR16) printf("  .hword 0\n");
    else printf("  .word 0\n");
  }
  printf(".text\n");
}

void gen_float_literals(void) {
  if (!float_literals) return;
  printf(".section __DATA,__data\n");
  printf(".align 3\n");
  for (float_lit_t *lit = float_literals; lit; lit = lit->next) {
    printf(".LCF%d:\n", lit->id);
    if (lit->fp_kind == TK_FLOAT_KIND_FLOAT) {
      // float (32bit) 定数出力: IEEE754 format
      union { float f; uint32_t i; } u = { .f = (float)lit->fval };
      printf("  .word %u\n", u.i);
    } else {
      // double (64bit) 定数出力。
      // note: long double is currently lowered to double in codegen.
      union { double d; uint64_t i; } u = { .d = lit->fval };
      printf("  .quad %llu\n", (unsigned long long)u.i);
    }
  }
  printf(".text\n");
}
