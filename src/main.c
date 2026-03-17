#include "ag_c.h"
#include "config/config.h"
#include "parser/parser.h"
#include "tokenizer/tokenizer.h"
#include "preprocess/preprocess.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file_contents(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) return NULL;

  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return NULL;
  }
  long size = ftell(fp);
  if (size < 0) {
    fclose(fp);
    return NULL;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return NULL;
  }

  char *buf = calloc((size_t)size + 1, 1);
  if (!buf) {
    fclose(fp);
    return NULL;
  }
  size_t nread = fread(buf, 1, (size_t)size, fp);
  fclose(fp);
  if (nread != (size_t)size) {
    free(buf);
    return NULL;
  }
  buf[nread] = '\0';
  return buf;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "使い方: %s <input.c>\n", argv[0]);
    return 1;
  }

  char *source = read_file_contents(argv[1]);
  if (!source) {
    fprintf(stderr, "入力ファイルを読み込めませんでした: %s\n", argv[1]);
    return 1;
  }

  load_config_toml();

  // トークナイズ
  tk_set_filename(argv[1]);
  token = tk_tokenize(source);

  // プリプロセス（マクロ展開やディレクティブ処理）
  token = preprocess(token);

  // パースしてAST（抽象構文木）を構築（関数定義の列）
  node_t **code = ps_program();

  // 各関数定義のコード生成
  for (int i = 0; code[i]; i++) {
    gen(code[i]);
  }

  // 文字列と浮動小数点数データの出力
  gen_string_literals();
  gen_float_literals();

  free(source);
  return 0;
}
