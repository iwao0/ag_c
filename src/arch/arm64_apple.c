#include "../ag_c.h"
#include "../parser/parser.h"
#include <stdio.h>

// Apple Silicon (ARM64) 向けのアセンブリコード生成

void gen_main_prologue(void) {
  printf(".global _main\n");
  printf(".align 2\n");
  printf("_main:\n");
}

void gen_main_epilogue(void) { printf("  ret\n"); }

void gen(struct node_t *node) {
  if (node->kind == ND_NUM) {
    // 値を x0 へロードし、スタックへプッシュする
    printf("  mov x0, #%d\n", node->val);
    printf("  str x0, [sp, #-16]!\n");
    return;
  }

  gen(node->lhs);
  gen(node->rhs);

  // いったんスタックに退避した計算結果をポップしてレジスタへ復元
  printf("  ldr x1, [sp], #16\n"); // 右辺
  printf("  ldr x0, [sp], #16\n"); // 左辺

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
  case ND_NUM: // 到達しない
    break;
  }

  // 今回の演算結果を再びスタックへプッシュする
  printf("  str x0, [sp, #-16]!\n");
}
