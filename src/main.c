#include "codegen_backend.h"
#include "compilation_session.h"
#include "target_info.h"
#include "config/config.h"
#include "parser/parser.h"
#include "frontend/translation_unit.h"
#include "tokenizer/tokenizer.h"
#include "tokenizer/allocator.h"
#include "preprocess/preprocess.h"
#include "diag/diag.h"
#include "ir/ir.h"
#include "lowering/ir_builder.h"
#include "lowering/translation_unit_data_lowering.h"
#include "arch/arm64_apple/arm64_apple_ir.h"
#include "arch/wasm32/wasm32_ir.h"
#include "arch/wasm32/wasm32_obj.h"
#include "arch/wasm32/backend_context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

/* メモリ計測モード (環境変数 AG_MEM_STATS=1)。コンパイル各段が確保したメモリの
 * 内訳とプロセスのピーク RSS を stderr に出す。8MB 級のタイト環境への移植検討用。
 * カウンタ getter は各アロケータが提供する (ir.h / 下記 extern)。 */
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

static int wasm_emit_function_direct(
    node_t *fn, int object_mode, const ir_build_options_t *options) {
  ir_module_t *m = ir_build_function_module_with_options(fn, options);
  if (!m) return 0;
  {
    const char *dump_ir = getenv("AG_DUMP_IR");
    if (dump_ir && strcmp(dump_ir, "1") == 0) {
      char *buf = malloc(1 << 16);
      if (buf) {
        ir_print_module_to_buf(m, buf, 1 << 16);
        fprintf(stderr, "%s", buf);
        free(buf);
      }
    }
  }
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

static ag_compilation_session_t *wasm_adapter_session;
typedef struct {
  char entry[128];
  char condition[128];
  char start_export[128];
  char resume_export[128];
  char status_export[128];
  char result_export[128];
  int enabled;
} wasm_pending_continuation_t;
static wasm_pending_continuation_t wasm_pending_continuation;

static int copy_wasm_option_string(char out[128], int address,
                                   const char *fallback) {
  const char *source = address ? (const char *)(long)address : fallback;
  if (!source || !source[0]) return 0;
  int i = 0;
  while (i < 127 && source[i]) {
    out[i] = source[i];
    i++;
  }
  if (source[i]) return 0;
  out[i] = '\0';
  return 1;
}

int agc_wasm_set_continuation_options(
    int entry_addr, int condition_addr, int start_export_addr,
    int resume_export_addr, int status_export_addr, int result_export_addr) {
  if (!entry_addr && !condition_addr) {
    memset(&wasm_pending_continuation, 0, sizeof(wasm_pending_continuation));
    return 0;
  }
  wasm_pending_continuation_t next = {0};
  if (!copy_wasm_option_string(next.entry, entry_addr, NULL) ||
      !copy_wasm_option_string(next.condition, condition_addr, NULL) ||
      !copy_wasm_option_string(next.start_export, start_export_addr,
                               next.entry) ||
      !copy_wasm_option_string(next.resume_export, resume_export_addr,
                               "__agc_continuation_resume") ||
      !copy_wasm_option_string(next.status_export, status_export_addr,
                               "__agc_continuation_status") ||
      !copy_wasm_option_string(next.result_export, result_export_addr,
                               "__agc_continuation_result"))
    return -1;
  next.enabled = 1;
  wasm_pending_continuation = next;
  return 0;
}

static int attach_wasm_backend_context(
    ag_compilation_session_t *session) {
  wasm32_backend_context_t *backend = wasm32_backend_context_create();
  if (!backend) return 0;
  if (!ag_compilation_session_set_backend_context(
          session, backend,
          wasm32_backend_context_activate,
          wasm32_backend_context_deactivate,
          wasm32_backend_context_destroy)) {
    wasm32_backend_context_destroy(backend);
    return 0;
  }
  return 1;
}

static void wasm_publish_and_destroy_session(
    ag_compilation_session_t *session) {
  if (!session) return;
  int is_adapter_session = session == wasm_adapter_session;
  ag_diagnostic_context_t *diagnostic_context =
      ag_compilation_session_diagnostic_context(session);
  if (diagnostic_context) diag_context_publish(diagnostic_context);
  ag_compilation_session_destroy(session);
  if (is_adapter_session) wasm_adapter_session = NULL;
}

static int agc_wasm_compile_to_memory(int source_addr, int source_name_addr,
                                      int virtual_bundle_addr, int virtual_bundle_len,
                                      int max_header_files, int max_header_file_bytes,
                                      int max_header_total_bytes, int max_include_depth,
                                      int out_addr, int out_cap, int object_mode) {
  if (wasm_adapter_session) {
    ag_compilation_session_destroy(wasm_adapter_session);
    wasm_adapter_session = NULL;
  }
  diag_reset_records();
  pp_virtual_headers_clear();
  if (!source_addr || !out_addr || out_cap <= 0) return -1;

  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session =
      ag_compilation_session_create(&target);
  if (!session) return -4;
  wasm_adapter_session = session;
  if (wasm_pending_continuation.enabled &&
      !ag_compilation_session_set_continuation(
          session, wasm_pending_continuation.entry,
          wasm_pending_continuation.condition,
          wasm_pending_continuation.start_export,
          wasm_pending_continuation.resume_export,
          wasm_pending_continuation.status_export,
          wasm_pending_continuation.result_export)) {
    wasm_publish_and_destroy_session(session);
    return -4;
  }
  if (
      !attach_wasm_backend_context(session)) {
    wasm_publish_and_destroy_session(session);
    return -4;
  }
  if (!ag_compilation_session_activate(session)) {
    wasm_publish_and_destroy_session(session);
    return -4;
  }

  if (!psx_frontend_reset_translation_unit_state_in_session(
          session)) {
    wasm_publish_and_destroy_session(session);
    return -4;
  }

  char *source = (char *)(long)source_addr;
  const char *source_name = source_name_addr ? (const char *)(long)source_name_addr : "input.c";
  if (!source_name[0]) {
    wasm_publish_and_destroy_session(session);
    return -1;
  }
  if (virtual_bundle_addr) {
    pp_virtual_headers_configure((const unsigned char *)(long)virtual_bundle_addr,
                                 (size_t)virtual_bundle_len,
                                 max_header_files, max_header_file_bytes,
                                 max_header_total_bytes, max_include_depth);
  }
  wasm_memory_output_t out = {(char *)(long)out_addr, out_cap, 0, 0};
  out.buf[0] = '\0';
  unsigned char *obj_bytes = NULL;
  size_t obj_len = 0;

  tokenizer_context_t *tk_ctx = ag_compilation_session_tokenizer(session);
  tk_set_filename_ctx(tk_ctx, source_name);

  pp_stream_t *pps = NULL;
  token_t *tok = pp_stream_open_for_target(
      &pps, tk_ctx, ag_compilation_session_target(session), source);

  if (object_mode) {
    wasm32_obj_set_output_file(NULL);
    wasm32_obj_capture_output(1);
    wasm32_obj_set_capture_limit((size_t)out_cap);
    wasm32_obj_begin();
  } else {
    gen_set_simple_formatter(1);
    gen_set_output_callback(write_line_to_memory, &out);
    wasm32_module_begin();
  }

  psx_frontend_stream_t stream = {0};
  if (!psx_frontend_stream_begin(
          &stream, session, tk_ctx, tok)) {
    clear_output_callback();
    gen_set_simple_formatter(0);
    if (pps) pp_stream_close(pps);
    wasm32_obj_capture_output(0);
    wasm_publish_and_destroy_session(session);
    return -4;
  }
  ir_build_options_t ir_options = {
      .target = ag_compilation_session_target(session),
      .continuation = ag_compilation_session_continuation(session),
  };
  for (node_t *fn; (fn = psx_frontend_next_function(&stream)) != NULL; ) {
    if (!wasm_emit_function_direct(fn, object_mode, &ir_options)) {
      clear_output_callback();
      gen_set_simple_formatter(0);
      if (pps) pp_stream_close(pps);
      wasm32_obj_capture_output(0);
      wasm_publish_and_destroy_session(session);
      return -3;
    }
    psx_frontend_free_processed_ast_in_session(session);
  }
  psx_frontend_stream_end(&stream);
  if (pps) pp_stream_close(pps);

  if (diag_has_error_records()) {
    clear_output_callback();
    gen_set_simple_formatter(0);
    if (object_mode) wasm32_obj_capture_output(0);
    wasm_publish_and_destroy_session(session);
    return -5;
  }

  ir_data_module_t *data_module =
      lower_ir_translation_unit_data_in_session(session);
  if (!data_module) {
    clear_output_callback();
    gen_set_simple_formatter(0);
    if (object_mode) wasm32_obj_capture_output(0);
    wasm_publish_and_destroy_session(session);
    return -3;
  }

  if (object_mode) {
    wasm32_obj_emit_data_segments(data_module);
    ir_data_module_free(data_module);
    wasm32_obj_end();
    wasm32_obj_capture_output(0);
    if (wasm32_obj_capture_limit_exceeded()) {
      wasm_publish_and_destroy_session(session);
      return -2;
    }
    obj_bytes = wasm32_obj_take_output(&obj_len);
    if (!obj_bytes) {
      wasm_publish_and_destroy_session(session);
      return -4;
    }
    if (obj_len > (size_t)out_cap) {
      free(obj_bytes);
      wasm_publish_and_destroy_session(session);
      return -2;
    }
    memcpy(out.buf, obj_bytes, obj_len);
    free(obj_bytes);
    wasm_publish_and_destroy_session(session);
    return (int)obj_len;
  } else {
    wasm32_emit_data_segments(data_module);
    ir_data_module_free(data_module);
    wasm32_module_end();
    clear_output_callback();
    gen_set_simple_formatter(0);
  }

  int result = out.overflow ? -2 : out.len;
  wasm_publish_and_destroy_session(session);
  return result;
}

int agc_wasm_compile_wat(int source_addr, int out_addr, int out_cap) {
  return agc_wasm_compile_to_memory(source_addr, 0, 0, 0, 0, 0, 0, 0,
                                    out_addr, out_cap, 0);
}

int agc_wasm_compile_object(int source_addr, int out_addr, int out_cap) {
  return agc_wasm_compile_to_memory(source_addr, 0, 0, 0, 0, 0, 0, 0,
                                    out_addr, out_cap, 1);
}

int agc_wasm_compile_wat_named(int source_addr, int source_name_addr,
                               int out_addr, int out_cap) {
  return agc_wasm_compile_to_memory(source_addr, source_name_addr, 0, 0, 0, 0, 0, 0,
                                    out_addr, out_cap, 0);
}

int agc_wasm_compile_object_named(int source_addr, int source_name_addr,
                                  int out_addr, int out_cap) {
  return agc_wasm_compile_to_memory(source_addr, source_name_addr, 0, 0, 0, 0, 0, 0,
                                    out_addr, out_cap, 1);
}

int agc_wasm_compile_wat_virtual(int source_addr, int source_name_addr,
                                 int bundle_addr, int bundle_len,
                                 int max_files, int max_file_bytes,
                                 int max_total_bytes, int max_depth,
                                 int out_addr, int out_cap) {
  return agc_wasm_compile_to_memory(source_addr, source_name_addr,
                                    bundle_addr, bundle_len, max_files,
                                    max_file_bytes, max_total_bytes, max_depth,
                                    out_addr, out_cap, 0);
}

int agc_wasm_compile_object_virtual(int source_addr, int source_name_addr,
                                    int bundle_addr, int bundle_len,
                                    int max_files, int max_file_bytes,
                                    int max_total_bytes, int max_depth,
                                    int out_addr, int out_cap) {
  return agc_wasm_compile_to_memory(source_addr, source_name_addr,
                                    bundle_addr, bundle_len, max_files,
                                    max_file_bytes, max_total_bytes, max_depth,
                                    out_addr, out_cap, 1);
}

int main(int argc, char **argv) {
  const char *prog_disp = (argc > 0) ? diag_display_path(argv[0]) : "ag_c";
  const char *input_path = NULL;
  int wasm_object_mode = 0;
#ifdef AGC_TARGET_WASM32
  const char *output_path = NULL;
  const char *continuation_entry = NULL;
  const char *continuation_condition = NULL;
  const char *continuation_start = NULL;
  const char *continuation_resume = NULL;
  const char *continuation_status = NULL;
  const char *continuation_result = NULL;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-c") == 0) {
      wasm_object_mode = 1;
    } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
      output_path = argv[++i];
    } else if (strcmp(argv[i], "--continuation-entry") == 0 &&
               i + 1 < argc) {
      continuation_entry = argv[++i];
    } else if (strcmp(argv[i], "--continuation-frame-condition") == 0 &&
               i + 1 < argc) {
      continuation_condition = argv[++i];
    } else if (strcmp(argv[i], "--continuation-start-export") == 0 &&
               i + 1 < argc) {
      continuation_start = argv[++i];
    } else if (strcmp(argv[i], "--continuation-resume-export") == 0 &&
               i + 1 < argc) {
      continuation_resume = argv[++i];
    } else if (strcmp(argv[i], "--continuation-status-export") == 0 &&
               i + 1 < argc) {
      continuation_status = argv[++i];
    } else if (strcmp(argv[i], "--continuation-result-export") == 0 &&
               i + 1 < argc) {
      continuation_result = argv[++i];
    } else if (argv[i][0] != '-' && !input_path) {
      input_path = argv[i];
    } else {
      input_path = NULL;
      break;
    }
  }
  if (!input_path || (wasm_object_mode && !output_path) ||
      (continuation_entry && !wasm_object_mode) ||
      (!!continuation_entry != !!continuation_condition)) {
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

  ag_target_info_t target =
#ifdef AGC_TARGET_WASM32
      ag_target_info_wasm32();
#else
      ag_target_info_host();
#endif
  ag_compilation_session_t *session =
      ag_compilation_session_create(&target);
  int session_ready = session != NULL;
#ifdef AGC_TARGET_WASM32
  if (session_ready && continuation_entry) {
    session_ready = ag_compilation_session_set_continuation(
        session, continuation_entry, continuation_condition,
        continuation_start, continuation_resume,
        continuation_status, continuation_result);
  }
  if (session_ready) session_ready = attach_wasm_backend_context(session);
#endif
  if (!session_ready || !ag_compilation_session_activate(session)) {
    ag_compilation_session_destroy(session);
    free(source);
    return 1;
  }
  if (!psx_frontend_reset_translation_unit_state_in_session(
          session)) {
    ag_compilation_session_destroy(session);
    free(source);
    return 1;
  }

  load_config_toml_in_session(session, input_path);

  tokenizer_context_t *tk_ctx = ag_compilation_session_tokenizer(session);
  tk_set_filename_ctx(tk_ctx, input_disp);

  /* トークン経路: 字句解析→プリプロセスを遅延 pull 化し、パーサがカーソルを進めるたびに
   * 先読み分だけ materialize して通過済みトークンを解放する。トークンのピークメモリが
   * O(ウィンドウ) になる。マクロ・条件指令 (#define/#if 等, Stage 3)、#line (Stage 4: 遅延
   * line_delta / file_override)、#include (Stage 5: 被 include を遅延字句フレームとして push)
   * をすべて扱える。 */
  pp_stream_t *pps = NULL;
  token_t *tok = pp_stream_open_for_target(
      &pps, tk_ctx, ag_compilation_session_target(session), source);

#ifdef AGC_TARGET_WASM32
  FILE *wasm_obj_out = NULL;
  if (wasm_object_mode) {
    wasm_obj_out = fopen(output_path, "wb");
    if (!wasm_obj_out) {
      diag_emit_internalf(DIAG_ERR_INTERNAL_USAGE, "%s", "failed to open Wasm object output");
      if (pps) pp_stream_close(pps);
      ag_compilation_session_destroy(session);
      free(source);
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
  // 非関数のトップレベル宣言はfrontend item driverが逐次適用し、データセクションは末尾。
  // AG_DUMP_IR=1 で各関数の IR を stderr にダンプ。
  psx_frontend_stream_t stream = {0};
  if (!psx_frontend_stream_begin(
          &stream, session, tk_ctx, tok)) {
    if (pps) pp_stream_close(pps);
#ifdef AGC_TARGET_WASM32
    if (wasm_object_mode) {
      fclose(wasm_obj_out);
      remove(output_path);
    }
#endif
    clear_output_callback();
    ag_compilation_session_destroy(session);
    free(source);
    return 1;
  }
  ir_build_options_t ir_options = {
      .target = ag_compilation_session_target(session),
      .continuation = ag_compilation_session_continuation(session),
  };
  for (node_t *fn; (fn = psx_frontend_next_function(&stream)) != NULL; ) {
#ifdef AGC_TARGET_WASM32
    if (!wasm_emit_function_direct(fn, wasm_object_mode, &ir_options)) {
#else
    if (!ir_build_emit_function_with_options(
            fn, &ir_options, gen_ir_module)) {
#endif
      diag_emit_internalf(DIAG_ERR_CODEGEN_IR_BUILD_EMIT_FAILED, "%s",
                          diag_message_for(DIAG_ERR_CODEGEN_IR_BUILD_EMIT_FAILED));
      if (pps) pp_stream_close(pps);
      ag_compilation_session_destroy(session);
      free(source);
      return 1;
    }
    psx_frontend_free_processed_ast_in_session(session);
  }
  psx_frontend_stream_end(&stream);
  if (pps) pp_stream_close(pps);

  if (diag_has_error_records()) {
#ifdef AGC_TARGET_WASM32
    if (wasm_object_mode) {
      fclose(wasm_obj_out);
      remove(output_path);
    }
#endif
    clear_output_callback();
    ag_compilation_session_destroy(session);
    free(source);
    return 1;
  }

  ir_data_module_t *data_module =
      lower_ir_translation_unit_data_in_session(session);
  if (!data_module) {
#ifdef AGC_TARGET_WASM32
    if (wasm_object_mode) {
      fclose(wasm_obj_out);
      remove(output_path);
    }
#endif
    clear_output_callback();
    ag_compilation_session_destroy(session);
    free(source);
    return 1;
  }

#ifdef AGC_TARGET_WASM32
  if (wasm_object_mode) {
    wasm32_obj_emit_data_segments(data_module);
    wasm32_obj_end();
    fclose(wasm_obj_out);
  } else {
    wasm32_emit_data_segments(data_module);
    wasm32_module_end();
  }
#else
  // lowering 済みの文字列・浮動小数点定数・global object を emit。
  gen_string_literals(data_module);
  gen_float_literals(data_module);
  gen_global_vars(data_module);
#endif
  ir_data_module_free(data_module);
  if (!wasm_object_mode) clear_output_callback();

  if (getenv("AG_MEM_STATS")) print_mem_stats(strlen(source));

  ag_compilation_session_destroy(session);
  free(source);
  return 0;
}
