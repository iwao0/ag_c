#include "ag_c.h"
#include "parser/parser.h"
#include "tokenizer/tokenizer.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "引数の個数が正しくありません\n");
    return 1;
  }

  // トークナイズ
  token = tokenize(argv[1]);

  // パースしてAST（抽象構文木）を構築
  node_t *node = expr();

  // アセンブリの前半部分を出力
  gen_main_prologue();

  // ASTを下りながらコード生成
  gen(node);

  // スタックトップに式全体の値が残っているので、x0
  // (実質w0)にポップして返り値とする
  printf("  ldr x0, [sp], #16\n");

  // アセンブリの後半部分を出力
  gen_main_epilogue();

  return 0;
}
