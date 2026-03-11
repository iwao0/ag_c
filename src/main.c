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

  // パースしてAST（抽象構文木）を構築（関数定義の列）
  program();

  // 各関数定義のコード生成
  for (int i = 0; code[i]; i++) {
    gen(code[i]);
  }

  // 文字列と浮動小数点数データの出力
  gen_string_literals();
  gen_float_literals();

  return 0;
}
