#include "codegen_backend.h"
#include "config/config.h"
#include "parser/parser.h"
#include "tokenizer/tokenizer.h"
#include "preprocess/preprocess.h"
#include "diag/diag.h"
#include "ir/ir.h"
#include "ir/ir_builder.h"
#include "arch/arm64_apple_ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

/* メモリ計測モード (環境変数 AG_MEM_STATS=1)。コンパイル各段が確保したメモリの
 * 内訳とプロセスのピーク RSS を stderr に出す。8MB 級のタイト環境への移植検討用。
 * カウンタ getter は各アロケータが提供する (ir.h / 下記 extern)。 */
size_t tk_allocator_total_reserved_bytes(void);  /* src/tokenizer/allocator.c */
size_t arena_total_reserved_bytes(void);         /* src/parser/arena.c */

static void print_mem_stats(size_t source_bytes) {
  size_t tok  = tk_allocator_total_reserved_bytes();
  size_t ast  = arena_total_reserved_bytes();
  size_t ninst = ir_inst_total_count();
  size_t nblk  = ir_block_total_count();
  size_t ir_inst_bytes  = ninst * sizeof(ir_inst_t);
  size_t ir_block_bytes = nblk  * sizeof(ir_block_t);
  const double MB = 1024.0 * 1024.0;
  struct rusage ru;
  long peak_rss = (getrusage(RUSAGE_SELF, &ru) == 0) ? ru.ru_maxrss : 0;
  /* macOS の ru_maxrss はバイト、Linux は KB。移植先で要確認。 */
  fprintf(stderr,
          "=== AG_MEM_STATS ===\n"
          "source            : %zu bytes\n"
          "token arena       : %.2f MB\n"
          "AST arena         : %.2f MB\n"
          "IR instructions   : %zu (%.2f MB @ %zu B)\n"
          "IR blocks         : %zu (%.2f MB @ %zu B)\n"
          "tracked subtotal  : %.2f MB\n"
          "peak RSS          : %.2f MB (ru_maxrss=%ld)\n",
          source_bytes,
          tok / MB, ast / MB,
          ninst, ir_inst_bytes / MB, sizeof(ir_inst_t),
          nblk,  ir_block_bytes / MB, sizeof(ir_block_t),
          (tok + ast + ir_inst_bytes + ir_block_bytes) / MB,
          peak_rss / MB, peak_rss);
}

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
  tk_set_filename_ctx(tk_get_default_context(), input_disp);
  tokenizer_context_t *tk_ctx = tk_get_default_context();
  token_t *tok = tk_tokenize_ctx(tk_ctx, source);

  // プリプロセス（マクロ展開やディレクティブ処理）
  tok = preprocess_ctx(tk_ctx, tok);

  // パースしてAST（抽象構文木）を構築（関数定義の列）
  node_t **code = ps_program_ctx(tk_ctx, tok);
  gen_set_output_callback(write_line_to_file, stdout);

  // AST → IR → ASM のコード生成。
  // Phase 7o で fixture 100% を IR 経路で通過させたため、AST 直 codegen は削除し
  // 常に IR 経由とした。AG_DUMP_IR=1 で stderr に IR ダンプを出す。
  ir_module_t *m = ir_build_module(code);
  if (!m) {
    fprintf(stderr, "ir_build_module failed\n");
    free(source);
    return 1;
  }
  const char *dump_ir = getenv("AG_DUMP_IR");
  if (dump_ir && strcmp(dump_ir, "1") == 0) {
    char *buf = malloc(1 << 16);
    ir_print_module_to_buf(m, buf, 1 << 16);
    fprintf(stderr, "%s", buf);
    free(buf);
  }
  gen_ir_module(m);

  // 文字列・浮動小数点定数・グローバル変数のデータセクションを emit。
  // (parser が tokenize/parse 中に登録したテーブルを順に書き出す)
  gen_string_literals();
  gen_float_literals();
  gen_global_vars();
  gen_set_output_callback(NULL, NULL);

  if (getenv("AG_MEM_STATS")) print_mem_stats(strlen(source));

  free(source);
  return 0;
}
