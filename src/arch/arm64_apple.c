#include "../ag_c.h"
#include "../parser/parser.h"
#include <stdio.h>
#include <string.h>

// ラベルの一意番号を生成するカウンタ
static int label_count = 0;

// Apple Silicon (ARM64) 向けのアセンブリコード生成

// 26個の変数(a-z) * 8バイト = 208バイト + フレームポインタ/リンクレジスタ用16バイト = 224
// 16バイトアラインメントに合わせる → 224
#define STACK_SIZE 224

void gen_main_prologue(void) {
  // 関数定義で生成するため空にする（互換性維持）
}

void gen_main_epilogue(void) {
  // 関数定義で生成するため空にする（互換性維持）
}

// 左辺値（変数のアドレス）をスタックへプッシュする
static void gen_lval(node_t *node) {
  if (node->kind != ND_LVAR) {
    fprintf(stderr, "代入の左辺値が変数ではありません\n");
    return;
  }
  printf("  add x0, x29, #%d\n", 16 + node->offset);
  printf("  str x0, [sp, #-16]!\n");
}

void gen(struct node_t *node) {
  switch (node->kind) {
  case ND_NUM:
    printf("  mov x0, #%d\n", node->val);
    printf("  str x0, [sp, #-16]!\n");
    return;
  case ND_LVAR:
    gen_lval(node);
    printf("  ldr x0, [sp], #16\n");
    printf("  ldr x0, [x0]\n");
    printf("  str x0, [sp, #-16]!\n");
    return;
  case ND_ASSIGN:
    gen_lval(node->lhs);
    gen(node->rhs);
    printf("  ldr x1, [sp], #16\n");
    printf("  ldr x0, [sp], #16\n");
    printf("  str x1, [x0]\n");
    printf("  str x1, [sp, #-16]!\n");
    return;
  case ND_RETURN:
    gen(node->lhs);
    printf("  ldr x0, [sp], #16\n");
    printf("  ldp x29, x30, [sp], #%d\n", STACK_SIZE);
    printf("  ret\n");
    printf("  mov x0, #0\n");
    printf("  str x0, [sp, #-16]!\n");
    return;
  case ND_BLOCK:
    for (int i = 0; node->body[i]; i++) {
      gen(node->body[i]);
      if (node->body[i + 1]) {
        printf("  ldr x0, [sp], #16\n");
      }
    }
    return;
  case ND_FUNCDEF: {
    // 関数ラベルの出力
    printf(".global _%.*s\n", node->funcname_len, node->funcname);
    printf(".align 2\n");
    printf("_%.*s:\n", node->funcname_len, node->funcname);
    // プロローグ
    printf("  stp x29, x30, [sp, #-%d]!\n", STACK_SIZE);
    printf("  mov x29, sp\n");
    // 仮引数をレジスタからローカル変数スロットへ保存
    for (int i = 0; i < node->nargs; i++) {
      printf("  str x%d, [x29, #%d]\n", i, 16 + node->args[i]->offset);
    }
    // 関数本体
    gen(node->rhs);
    printf("  ldr x0, [sp], #16\n");
    // エピローグ
    printf("  ldp x29, x30, [sp], #%d\n", STACK_SIZE);
    printf("  ret\n");
    return;
  }
  case ND_FUNCALL: {
    // 引数を評価してレジスタに格納
    for (int i = 0; i < node->nargs; i++) {
      gen(node->args[i]);
    }
    // スタックからレジスタへ (逆順にポップ)
    for (int i = node->nargs - 1; i >= 0; i--) {
      printf("  ldr x%d, [sp], #16\n", i);
    }
    // 関数呼び出し
    printf("  bl _%.*s\n", node->funcname_len, node->funcname);
    // 戻り値をスタックにプッシュ
    printf("  str x0, [sp, #-16]!\n");
    return;
  }
  case ND_IF: {
    int lbl = label_count++;
    gen(node->lhs);
    printf("  ldr x0, [sp], #16\n");
    printf("  cbz x0, .Lelse%d\n", lbl);
    gen(node->rhs);
    if (node->els) {
      printf("  b .Lend%d\n", lbl);
      printf(".Lelse%d:\n", lbl);
      gen(node->els);
      printf(".Lend%d:\n", lbl);
    } else {
      printf("  b .Lend%d\n", lbl);
      printf(".Lelse%d:\n", lbl);
      // else節がない場合、条件偽のときにダミー値をプッシュ（スタックバランス）
      printf("  mov x0, #0\n");
      printf("  str x0, [sp, #-16]!\n");
      printf(".Lend%d:\n", lbl);
    }
    return;
  }
  case ND_WHILE: {
    int lbl = label_count++;
    printf(".Lbegin%d:\n", lbl);
    gen(node->lhs);
    printf("  ldr x0, [sp], #16\n");
    printf("  cbz x0, .Lend%d\n", lbl);
    gen(node->rhs);
    printf("  ldr x0, [sp], #16\n");
    printf("  b .Lbegin%d\n", lbl);
    printf(".Lend%d:\n", lbl);
    printf("  mov x0, #0\n");
    printf("  str x0, [sp, #-16]!\n");
    return;
  }
  case ND_FOR: {
    int lbl = label_count++;
    if (node->init) {
      gen(node->init);
      printf("  ldr x0, [sp], #16\n");
    }
    printf(".Lbegin%d:\n", lbl);
    if (node->lhs) {
      gen(node->lhs);
      printf("  ldr x0, [sp], #16\n");
      printf("  cbz x0, .Lend%d\n", lbl);
    }
    gen(node->rhs);
    printf("  ldr x0, [sp], #16\n");
    if (node->inc) {
      gen(node->inc);
      printf("  ldr x0, [sp], #16\n");
    }
    printf("  b .Lbegin%d\n", lbl);
    printf(".Lend%d:\n", lbl);
    printf("  mov x0, #0\n");
    printf("  str x0, [sp, #-16]!\n");
    return;
  }
  default:
    break;
  }

  gen(node->lhs);
  gen(node->rhs);

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
