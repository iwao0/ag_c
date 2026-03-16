#include "../ag_c.h"
#include "../parser/parser.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

// ラベルの一意番号を生成するカウンタ
static int label_count = 0;
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

// レジスタ名取得ヘルパー
static const char *freg(int is_float, int reg_idx) {
  if (is_float == 1) return (reg_idx == 0) ? "s0" : "s1";
  return (reg_idx == 0) ? "d0" : "d1";
}

// child_type の値をスタックからポップし、target_type にキャストして FPU レジスタ (s0/d0 または s1/d1) にロードする
static void gen_pop_fpu(int target_type, int child_type, int reg_idx) {
  const char *s = (reg_idx == 0) ? "s0" : "s1";
  const char *d = (reg_idx == 0) ? "d0" : "d1";
  
  if (child_type == 1) {       // float がプッシュされている
    printf("  ldr %s, [sp], #16\n", s);
    if (target_type == 2) {
      printf("  fcvt %s, %s\n", d, s); // float -> double
    }
  } else if (child_type == 2) { // double がプッシュされている
    printf("  ldr %s, [sp], #16\n", d);
    if (target_type == 1) {
      printf("  fcvt %s, %s\n", s, d); // double -> float
    }
  } else {                      // int がプッシュされている
    printf("  ldr x%d, [sp], #16\n", reg_idx);
    if (target_type == 1) {
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
    if (node->is_float) {
      node_num_t *num = as_num(node);
      // 浮動小数点リテラルをデータセクションからロード
      printf("  adrp x0, .LCF%d@PAGE\n", num->fval_id);
      printf("  add x0, x0, .LCF%d@PAGEOFF\n", num->fval_id);
      if (node->is_float == 1) {
        printf("  ldr s0, [x0]\n");
        printf("  str s0, [sp, #-16]!\n");
      } else {
        printf("  ldr d0, [x0]\n");
        printf("  str d0, [sp, #-16]!\n");
      }
    } else {
      printf("  mov x0, #%d\n", as_num(node)->val);
      printf("  str x0, [sp, #-16]!\n");
    }
    return;
  case ND_LVAR:
    gen_lval(node);
    printf("  ldr x0, [sp], #16\n");
    if (node->is_float == 1) {
      printf("  ldr s0, [x0]\n");
      printf("  str s0, [sp, #-16]!\n");
    } else if (node->is_float == 2) {
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
    if (node->is_float == 1) {
      // float 代入
      gen_pop_fpu(1, node->rhs->is_float, 0); // s0 に rhs をロード
      printf("  ldr x0, [sp], #16\n"); // lhs アドレス
      printf("  str s0, [x0]\n");
      printf("  str s0, [sp, #-16]!\n");
    } else if (node->is_float == 2) {
      // double 代入
      gen_pop_fpu(2, node->rhs->is_float, 0); // d0 に rhs をロード
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
  int fpu_op_type = node->is_float; // ADD/SUB/MUL/DIV の場合は結果型
  if (node->kind == ND_EQ || node->kind == ND_NE || node->kind == ND_LT || node->kind == ND_LE) {
    // 比較演算の場合はオペランドの型を見る
    if (node->lhs && node->lhs->is_float > fpu_op_type) fpu_op_type = node->lhs->is_float;
    if (node->rhs && node->rhs->is_float > fpu_op_type) fpu_op_type = node->rhs->is_float;
  }

  if (fpu_op_type) {
    // FPU 演算
    gen_expr(node->lhs);
    gen_expr(node->rhs);
    const char *r0 = freg(fpu_op_type, 0);
    const char *r1 = freg(fpu_op_type, 1);
    
    gen_pop_fpu(fpu_op_type, node->rhs->is_float, 1); // rhs を r1 に
    gen_pop_fpu(fpu_op_type, node->lhs->is_float, 0); // lhs を r0 に

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
    gen_expr(node->lhs);
    if (node->is_float == 1) {             // 関数の戻り値が float
      gen_pop_fpu(1, node->lhs->is_float, 0); // s0 にロード
    } else if (node->is_float == 2) {      // 関数の戻り値が double
      gen_pop_fpu(2, node->lhs->is_float, 0); // d0 にロード
    } else {                               // 関数の戻り値が 整数
      if (node->lhs->is_float == 1) {
        printf("  ldr s0, [sp], #16\n");
        printf("  fcvtzs x0, s0\n");       // float->int
      } else if (node->lhs->is_float == 2) {
        printf("  ldr d0, [sp], #16\n");
        printf("  fcvtzs x0, d0\n");       // double->int
      } else {
        printf("  ldr x0, [sp], #16\n");   // そのまま int
      }
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
    int lbl = label_count++;
    printf(".Lbegin%d:\n", lbl);
    gen_expr(ctrl->base.lhs);
    printf("  ldr x0, [sp], #16\n");
    printf("  cbz x0, .Lend%d\n", lbl);
    gen_stmt(ctrl->base.rhs);
    printf("  b .Lbegin%d\n", lbl);
    printf(".Lend%d:\n", lbl);
    return;
  }
  case ND_FOR: {
    node_ctrl_t *ctrl = as_ctrl(node);
    int lbl = label_count++;
    if (ctrl->init) {
      gen_expr(ctrl->init);
      printf("  add sp, sp, #16\n");
    }
    printf(".Lbegin%d:\n", lbl);
    if (ctrl->base.lhs) {
      gen_expr(ctrl->base.lhs);
      printf("  ldr x0, [sp], #16\n");
      printf("  cbz x0, .Lend%d\n", lbl);
    }
    gen_stmt(ctrl->base.rhs);
    if (ctrl->inc) {
      gen_expr(ctrl->inc);
      printf("  add sp, sp, #16\n");
    }
    printf("  b .Lbegin%d\n", lbl);
    printf(".Lend%d:\n", lbl);
    return;
  }
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
    printf("  .asciz \"");
    for (int i = 0; i < lit->len; i++) {
      char c = lit->str[i];
      if (c == '\\' && i + 1 < lit->len) {
        char next = lit->str[i + 1];
        if (next == 'n') { printf("\\n"); i++; continue; }
        if (next == 't') { printf("\\t"); i++; continue; }
        if (next == '\\') { printf("\\\\"); i++; continue; }
        if (next == '"') { printf("\\\""); i++; continue; }
      }
      printf("%c", c);
    }
    printf("\"\n");
  }
  printf(".text\n");
}

void gen_float_literals(void) {
  if (!float_literals) return;
  printf(".section __DATA,__data\n");
  printf(".align 3\n");
  for (float_lit_t *lit = float_literals; lit; lit = lit->next) {
    printf(".LCF%d:\n", lit->id);
    if (lit->is_float == 1) {
      // float (32bit) 定数出力: IEEE754 format
      union { float f; uint32_t i; } u = { .f = (float)lit->fval };
      printf("  .word %u\n", u.i);
    } else {
      // double (64bit) 定数出力
      union { double d; uint64_t i; } u = { .d = lit->fval };
      printf("  .quad %llu\n", (unsigned long long)u.i);
    }
  }
  printf(".text\n");
}
