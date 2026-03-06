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

  // パースしてAST（抽象構文木）を構築（複文対応）
  program();

  // アセンブリの前半部分を出力
  gen_main_prologue();

  // 各文のASTを下りながらコード生成
  for (int i = 0; code[i]; i++) {
    gen(code[i]);

    // 文の評価結果はスタックに残っているので、ポップしてx0に入れる
    printf("  ldr x0, [sp], #16\n");
  }

  // アセンブリの後半部分を出力（最後の文の結果がx0に入った状態でret）
  gen_main_epilogue();

  return 0;
}
