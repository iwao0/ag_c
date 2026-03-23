#include "codegen_backend.h"
#include "config/config.h"
#include "parser/parser.h"
#include "tokenizer/tokenizer.h"
#include "preprocess/preprocess.h"
#include "diag/diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *diag_display_path(const char *path) {
  if (!path) return "";
  if (path[0] != '/') return path;
  const char *slash = strrchr(path, '/');
  return (slash && slash[1] != '\0') ? slash + 1 : path;
}

static void write_line_to_file(const char *line, size_t len, void *user_data) {
  FILE *out = (FILE *)user_data;
  fwrite(line, 1, len, out);
}

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
  const char *prog_disp = (argc > 0) ? diag_display_path(argv[0]) : "ag_c";
  if (argc != 2) {
    diag_emit_internalf(DIAG_ERR_INTERNAL_USAGE,
                        diag_message_for(DIAG_ERR_INTERNAL_USAGE), prog_disp);
    return 1;
  }

  const char *input_disp = diag_display_path(argv[1]);
  char *source = read_file_contents(argv[1]);
  if (!source) {
    diag_emit_internalf(DIAG_ERR_INTERNAL_INPUT_READ_FAILED,
                        diag_message_for(DIAG_ERR_INTERNAL_INPUT_READ_FAILED), input_disp);
    return 1;
  }

  load_config_toml(argv[1]);

  // トークナイズ
  tk_set_filename_ctx(tk_get_default_context(), (char *)input_disp);
  tokenizer_context_t *tk_ctx = tk_get_default_context();
  token_t *tok = tk_tokenize_ctx(tk_ctx, source);

  // プリプロセス（マクロ展開やディレクティブ処理）
  tok = preprocess_ctx(tk_ctx, tok);

  // パースしてAST（抽象構文木）を構築（関数定義の列）
  node_t **code = ps_program_ctx(tk_ctx, tok);
  gen_set_output_callback(write_line_to_file, stdout);

  // 各関数定義のコード生成
  for (int i = 0; code[i]; i++) {
    gen(code[i]);
  }

  // 文字列と浮動小数点数データの出力
  gen_string_literals();
  gen_float_literals();
  // グローバル変数データの出力
  gen_global_vars();
  gen_set_output_callback(NULL, NULL);

  free(source);
  return 0;
}
