#include "codegen_backend.h"
#include "config/config.h"
#include "parser/parser.h"
#include "tokenizer/tokenizer.h"
#include "preprocess/preprocess.h"
#include "diag/diag.h"
#include "ir/ir.h"
#include "ir/ir_builder.h"
#include "arch/arm64_apple_ir.h"
#include "arch/wasm32_ir.h"
#include "arch/wasm32_obj.h"
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

typedef struct {
  char *buf;
  int cap;
  int len;
  int overflow;
} wasm_memory_output_t;

static void write_line_to_memory(const char *line, size_t len, void *user_data) {
  wasm_memory_output_t *out = (wasm_memory_output_t *)user_data;
  for (size_t i = 0; i < len; i++) {
    if (out->len + 1 < out->cap) {
      out->buf[out->len] = line[i];
    } else {
      out->overflow = 1;
    }
    out->len++;
  }
  if (out->cap > 0) {
    int nul = out->len < out->cap ? out->len : out->cap - 1;
    out->buf[nul] = '\0';
  }
}

static void clear_output_callback(void) {
  gen_output_line_fn cb = 0;
  void *user_data = 0;
  gen_set_output_callback(cb, user_data);
}

static int wasm_emit_function_direct(node_t *fn, int object_mode) {
  ir_module_t *m = ir_build_function_module(fn);
  if (!m) return 0;
  if (object_mode) {
    wasm32_obj_gen_ir_module(m);
  } else {
    wasm32_gen_ir_module(m);
  }
  ir_module_free(m);
  return 1;
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

static int agc_wasm_compile_to_memory(int source_addr, int out_addr, int out_cap,
                                      int object_mode) {
  if (!source_addr || !out_addr || out_cap <= 0) return -1;

  char *source = (char *)(long)source_addr;
  wasm_memory_output_t out = {(char *)(long)out_addr, out_cap, 0, 0};
  out.buf[0] = '\0';
  unsigned char *obj_bytes = NULL;
  size_t obj_len = 0;

  const char *input_disp = "input.c";
  tk_set_filename_ctx(tk_get_default_context(), input_disp);
  tokenizer_context_t *tk_ctx = tk_get_default_context();

  pp_stream_t *pps = NULL;
  token_t *tok = pp_stream_open(&pps, tk_ctx, source);

  if (object_mode) {
    wasm32_obj_set_output_file(NULL);
    wasm32_obj_capture_output(1);
    wasm32_obj_begin();
  } else {
    gen_set_simple_formatter(1);
    gen_set_output_callback(write_line_to_memory, &out);
    wasm32_module_begin();
  }

  ps_stream_begin(tk_ctx, tok);
  for (node_t *fn; (fn = ps_next_function()) != NULL; ) {
    if (!wasm_emit_function_direct(fn, object_mode)) {
      clear_output_callback();
      gen_set_simple_formatter(0);
      if (pps) pp_stream_close(pps);
      wasm32_obj_capture_output(0);
      return -3;
    }
    ps_free_processed_ast();
  }
  if (pps) pp_stream_close(pps);

  if (object_mode) {
    wasm32_obj_emit_data_segments();
    wasm32_obj_end();
    wasm32_obj_capture_output(0);
    obj_bytes = wasm32_obj_take_output(&obj_len);
    if (!obj_bytes) return -4;
    if (obj_len > (size_t)out_cap) {
      free(obj_bytes);
      return -2;
    }
    memcpy(out.buf, obj_bytes, obj_len);
    free(obj_bytes);
    return (int)obj_len;
  } else {
    wasm32_emit_data_segments();
    wasm32_module_end();
    clear_output_callback();
    gen_set_simple_formatter(0);
  }

  return out.overflow ? -2 : out.len;
}

int agc_wasm_compile_wat(int source_addr, int out_addr, int out_cap) {
  return agc_wasm_compile_to_memory(source_addr, out_addr, out_cap, 0);
}

int agc_wasm_compile_object(int source_addr, int out_addr, int out_cap) {
  return agc_wasm_compile_to_memory(source_addr, out_addr, out_cap, 1);
}

int main(int argc, char **argv) {
  const char *prog_disp = (argc > 0) ? diag_display_path(argv[0]) : "ag_c";
  const char *input_path = NULL;
  int wasm_object_mode = 0;
#ifdef AGC_TARGET_WASM32
  const char *output_path = NULL;
  if (argc == 2) {
    input_path = argv[1];
  } else if (argc == 5 && strcmp(argv[1], "-c") == 0 && strcmp(argv[2], "-o") == 0) {
    wasm_object_mode = 1;
    output_path = argv[3];
    input_path = argv[4];
  } else {
    diag_emit_internalf(DIAG_ERR_INTERNAL_USAGE,
                        diag_message_for(DIAG_ERR_INTERNAL_USAGE), prog_disp);
    return 1;
  }
#else
  if (argc != 2) {
    diag_emit_internalf(DIAG_ERR_INTERNAL_USAGE,
                        diag_message_for(DIAG_ERR_INTERNAL_USAGE), prog_disp);
    return 1;
  }
  input_path = argv[1];
#endif

  const char *input_disp = diag_display_path(input_path);
  char *source = read_file_contents(input_path);
  if (!source) {
    diag_emit_internalf(DIAG_ERR_INTERNAL_INPUT_READ_FAILED,
                        diag_message_for(DIAG_ERR_INTERNAL_INPUT_READ_FAILED), input_disp);
    return 1;
  }

  load_config_toml(input_path);

  tk_set_filename_ctx(tk_get_default_context(), input_disp);
  tokenizer_context_t *tk_ctx = tk_get_default_context();

  /* トークン経路: 字句解析→プリプロセスを遅延 pull 化し、パーサがカーソルを進めるたびに
   * 先読み分だけ materialize して通過済みトークンを解放する。トークンのピークメモリが
   * O(ウィンドウ) になる。マクロ・条件指令 (#define/#if 等, Stage 3)、#line (Stage 4: 遅延
   * line_delta / file_override)、#include (Stage 5: 被 include を遅延字句フレームとして push)
   * をすべて扱える。 */
  pp_stream_t *pps = NULL;
  token_t *tok = pp_stream_open(&pps, tk_ctx, source);

#ifdef AGC_TARGET_WASM32
  FILE *wasm_obj_out = NULL;
  if (wasm_object_mode) {
    wasm_obj_out = fopen(output_path, "wb");
    if (!wasm_obj_out) {
      diag_emit_internalf(DIAG_ERR_INTERNAL_USAGE, "%s", "failed to open Wasm object output");
      return 1;
    }
    wasm32_obj_set_output_file(wasm_obj_out);
    wasm32_obj_begin();
  } else {
    gen_set_output_callback(write_line_to_file, stdout);
    wasm32_module_begin();
  }
#else
  gen_set_output_callback(write_line_to_file, stdout);
#endif

  // 関数ごとストリーミング: パース→IR build→最適化+codegen→AST/IR 解放 を 1 関数ずつ
  // 回す。全関数の AST・IR を同時保持しないので、ピークメモリが「ファイル全体」でなく
  // 「最大の 1 関数 + 永続テーブル + トークンウィンドウ」になる (8MB 級のタイト環境向け)。
  // 非関数のトップレベル宣言は ps_next_function 内で副作用処理され、データセクションは末尾。
  // AG_DUMP_IR=1 で各関数の IR を stderr にダンプ。
  ps_stream_begin(tk_ctx, tok);
  for (node_t *fn; (fn = ps_next_function()) != NULL; ) {
#ifdef AGC_TARGET_WASM32
    if (!wasm_emit_function_direct(fn, wasm_object_mode)) {
#else
    if (!ir_build_emit_function(fn,
                                gen_ir_module
                                )) {
#endif
      diag_emit_internalf(DIAG_ERR_CODEGEN_IR_BUILD_EMIT_FAILED, "%s",
                          diag_message_for(DIAG_ERR_CODEGEN_IR_BUILD_EMIT_FAILED));
      free(source);
      return 1;
    }
    ps_free_processed_ast();  // この関数の AST (parser arena) を解放
  }
  if (pps) pp_stream_close(pps);

#ifdef AGC_TARGET_WASM32
  if (wasm_object_mode) {
    wasm32_obj_emit_data_segments();
    wasm32_obj_end();
    fclose(wasm_obj_out);
  } else {
    wasm32_emit_data_segments();
    wasm32_module_end();
  }
#else
  // 文字列・浮動小数点定数・グローバル変数のデータセクションを emit。
  // (parser が tokenize/parse 中に登録したテーブルを順に書き出す)
  gen_string_literals();
  gen_float_literals();
  gen_global_vars();
#endif
  if (!wasm_object_mode) clear_output_callback();

  if (getenv("AG_MEM_STATS")) print_mem_stats(strlen(source));

  free(source);
  return 0;
}
