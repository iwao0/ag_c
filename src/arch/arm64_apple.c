#include "../ag_c.h"
#include "../parser/parser.h"
#include <stdio.h>

// ラベルの一意番号を生成するカウンタ
static int label_count = 0;

// Apple Silicon (ARM64) 向けのアセンブリコード生成

// 26個の変数(a-z) * 8バイト = 208バイト + フレームポインタ/リンクレジスタ用16バイト = 224
// 16バイトアラインメントに合わせる → 224
#define STACK_SIZE 224

void gen_main_prologue(void) {
  printf(".global _main\n");
  printf(".align 2\n");
  printf("_main:\n");
  // スタックフレームの確保
  printf("  stp x29, x30, [sp, #-%d]!\n", STACK_SIZE);
  printf("  mov x29, sp\n");
}

void gen_main_epilogue(void) {
  // スタックフレームの解放
  printf("  ldp x29, x30, [sp], #%d\n", STACK_SIZE);
  printf("  ret\n");
}

// 左辺値（変数のアドレス）をスタックへプッシュする
static void gen_lval(node_t *node) {
  if (node->kind != ND_LVAR) {
    fprintf(stderr, "代入の左辺値が変数ではありません\n");
    return;
  }
  // x29(フレームポインタ) + 16(stp分) + offset がローカル変数のアドレス
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
    // 変数の値を読み出す
    gen_lval(node);
    printf("  ldr x0, [sp], #16\n"); // アドレスをポップ
    printf("  ldr x0, [x0]\n");      // アドレスから値をロード
    printf("  str x0, [sp, #-16]!\n");
    return;
  case ND_ASSIGN:
    // 左辺のアドレスをスタックへ、右辺の値をスタックへ
    gen_lval(node->lhs);
    gen(node->rhs);
    printf("  ldr x1, [sp], #16\n"); // 右辺の値
    printf("  ldr x0, [sp], #16\n"); // 左辺のアドレス
    printf("  str x1, [x0]\n");      // アドレスへ値を格納
    printf("  str x1, [sp, #-16]!\n"); // 代入式の結果=右辺値をプッシュ
    return;
  case ND_RETURN:
    gen(node->lhs); // 戻り値の式を評価
    printf("  ldr x0, [sp], #16\n");
    printf("  ldp x29, x30, [sp], #%d\n", STACK_SIZE);
    printf("  ret\n");
    // return 後は到達しないが、スタックの整合性のためダミープッシュ
    printf("  mov x0, #0\n");
    printf("  str x0, [sp, #-16]!\n");
    return;
  case ND_IF: {
    int lbl = label_count++;
    gen(node->lhs); // 条件式
    printf("  ldr x0, [sp], #16\n");
    printf("  cbz x0, .Lelse%d\n", lbl);
    gen(node->rhs); // then 節
    if (node->els) {
      printf("  b .Lend%d\n", lbl);
      printf(".Lelse%d:\n", lbl);
      gen(node->els); // else 節
      printf(".Lend%d:\n", lbl);
    } else {
      printf(".Lelse%d:\n", lbl);
    }
    return;
  }
  case ND_WHILE: {
    int lbl = label_count++;
    printf(".Lbegin%d:\n", lbl);
    gen(node->lhs); // 条件式
    printf("  ldr x0, [sp], #16\n");
    printf("  cbz x0, .Lend%d\n", lbl);
    gen(node->rhs); // ループ本体
    printf("  ldr x0, [sp], #16\n"); // 本体の結果を捨てる
    printf("  b .Lbegin%d\n", lbl);
    printf(".Lend%d:\n", lbl);
    // whileは値を返さないのでダミーをプッシュ
    printf("  mov x0, #0\n");
    printf("  str x0, [sp, #-16]!\n");
    return;
  }
  case ND_FOR: {
    int lbl = label_count++;
    if (node->init) {
      gen(node->init); // 初期化式
      printf("  ldr x0, [sp], #16\n"); // 結果を捨てる
    }
    printf(".Lbegin%d:\n", lbl);
    if (node->lhs) {
      gen(node->lhs); // 条件式
      printf("  ldr x0, [sp], #16\n");
      printf("  cbz x0, .Lend%d\n", lbl);
    }
    gen(node->rhs); // ループ本体
    printf("  ldr x0, [sp], #16\n"); // 本体の結果を捨てる
    if (node->inc) {
      gen(node->inc); // インクリメント
      printf("  ldr x0, [sp], #16\n"); // 結果を捨てる
    }
    printf("  b .Lbegin%d\n", lbl);
    printf(".Lend%d:\n", lbl);
    // forは値を返さないのでダミーをプッシュ
    printf("  mov x0, #0\n");
    printf("  str x0, [sp, #-16]!\n");
    return;
  }
  default:
    break;
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

  // 今回の演算結果を再びスタックへプッシュする
  printf("  str x0, [sp, #-16]!\n");
}
