#include "codegen_emit.h"
#include "compilation_session.h"
#include "target_info.h"
#include "config/config.h"
#include "parser/parser.h"
#include "parser/semantic_ctx.h"
#include "parser/arena.h"
#include "frontend/translation_unit.h"
#include "tokenizer/tokenizer.h"
#include "tokenizer/allocator.h"
#include "preprocess/preprocess.h"
#include "diag/diag.h"
#include "ir/ir.h"
#include "hir/hir.h"
#include "lowering/hir_ir_builder.h"
#include "lowering/abi_lowering.h"
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
 * カウンタ getter は各アロケータが提供する。 */

static void print_mem_stats(
    const ag_compilation_session_t *session, size_t source_bytes) {
  size_t tok = tk_allocator_total_reserved_bytes_in(
      ag_compilation_session_token_allocator_context(session));
  size_t ast = arena_total_reserved_bytes_in(
      ag_compilation_session_arena_context(session));
  const ir_allocation_stats_t *ir_stats =
      ag_compilation_session_ir_allocation_stats_view(session);
  size_t ninst = ir_allocation_stats_instruction_peak(ir_stats);
  size_t nblk = ir_allocation_stats_block_peak(ir_stats);
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

static void clear_output_callback(
    ag_codegen_emit_context_t *emit_context) {
  gen_set_output_callback_in(emit_context, NULL, NULL);
}

static void dump_ir_if_requested(ir_module_t *module) {
  const char *dump_ir = getenv("AG_DUMP_IR");
  if (!module || !dump_ir || strcmp(dump_ir, "1") != 0) return;
  char *buf = malloc(1 << 16);
  if (!buf) return;
  ir_print_module_to_buf(module, buf, 1 << 16);
  fprintf(stderr, "%s", buf);
  free(buf);
}

static ir_module_t *build_resolved_function_module(
    const psx_frontend_stream_t *stream,
    const psx_frontend_function_t *function,
    const ir_build_options_t *options) {
  if (!stream || !stream->session || !function ||
      function->hir_root == PSX_HIR_NODE_ID_INVALID) return NULL;
  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(stream->session);
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  return ir_build_function_module_from_hir(
      hir,
      function->hir_root,
      options, &status);
}

static ir_abi_module_t *lower_module_abi(
    const ir_module_t *module, const ir_build_options_t *options) {
  ir_abi_type_context_t context = {
      .semantic_types = options ? options->semantic_types : NULL,
      .record_layouts = options ? options->record_layouts : NULL,
      .target = options ? options->target : NULL,
  };
  return ir_abi_lower_module(&context, module);
}

static ir_abi_data_module_t *lower_data_module_abi(
    const ir_data_module_t *module,
    const ir_build_options_t *options) {
  ir_abi_type_context_t context = {
      .semantic_types = options ? options->semantic_types : NULL,
      .record_layouts = options ? options->record_layouts : NULL,
      .target = options ? options->target : NULL,
  };
  return ir_abi_lower_data_module(&context, module);
}

static int wasm_emit_function_direct(
    wasm32_backend_context_t *backend,
    const psx_frontend_stream_t *stream,
    const psx_frontend_function_t *function,
    int object_mode, const ir_build_options_t *options) {
  ir_module_t *m = build_resolved_function_module(
      stream, function, options);
  if (!m) return 0;
  ir_opt_const_fold(m);
  ir_opt_dce(m);
  ir_abi_module_t *abi = lower_module_abi(m, options);
  if (!abi) {
    ir_module_free(m);
    return 0;
  }
  dump_ir_if_requested(m);
  if (object_mode) {
    wasm32_backend_obj_gen_ir_module(backend, m, abi);
  } else {
    wasm32_backend_wat_gen_ir_module(backend, m, abi);
  }
  ir_abi_module_free(abi);
  ir_module_free(m);
  return 1;
}

#ifndef AGC_TARGET_WASM32
static void arm64_emit_ir_module(
    ir_module_t *module, const ir_abi_module_t *abi, void *context) {
  gen_ir_module_in(
      (ag_codegen_emit_context_t *)context, module, abi);
}
#endif

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

typedef struct {
  char entry[128];
  char condition[128];
  char start_export[128];
  char resume_export[128];
  char status_export[128];
  char result_export[128];
  int enabled;
} wasm_pending_continuation_t;

typedef struct {
  ag_compilation_session_t *session;
  wasm_pending_continuation_t continuation;
  char diagnostic_locale[3];
  int diagnostic_record_limit;
  int diagnostic_byte_limit;
} agc_wasm_adapter_t;

static agc_wasm_adapter_t *wasm_adapter_from_handle(int handle) {
  return handle ? (agc_wasm_adapter_t *)(long)handle : NULL;
}

int agc_wasm_adapter_create(void) {
  agc_wasm_adapter_t *adapter = calloc(1, sizeof(*adapter));
  if (!adapter) return 0;
  memcpy(adapter->diagnostic_locale, "ja", sizeof(adapter->diagnostic_locale));
  adapter->diagnostic_record_limit = 128;
  adapter->diagnostic_byte_limit = 1024 * 1024;
  return (int)(long)adapter;
}

int agc_wasm_adapter_destroy(int handle) {
  agc_wasm_adapter_t *adapter = wasm_adapter_from_handle(handle);
  if (!adapter) return -1;
  ag_compilation_session_destroy(adapter->session);
  adapter->session = NULL;
  free(adapter);
  return 0;
}

int agc_wasm_adapter_set_diagnostic_locale(int handle, int locale_code) {
  agc_wasm_adapter_t *adapter = wasm_adapter_from_handle(handle);
  if (!adapter) return -1;
  switch (locale_code) {
    case 0:
      memcpy(adapter->diagnostic_locale, "ja",
             sizeof(adapter->diagnostic_locale));
      return 0;
    case 1:
      memcpy(adapter->diagnostic_locale, "en",
             sizeof(adapter->diagnostic_locale));
      return 0;
    default:
      return -1;
  }
}

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

int agc_wasm_adapter_set_continuation_options(
    int handle, int entry_addr, int condition_addr, int start_export_addr,
    int resume_export_addr, int status_export_addr, int result_export_addr) {
  agc_wasm_adapter_t *adapter = wasm_adapter_from_handle(handle);
  if (!adapter) return -1;
  if (!entry_addr && !condition_addr) {
    memset(&adapter->continuation, 0, sizeof(adapter->continuation));
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
  adapter->continuation = next;
  return 0;
}

int agc_wasm_adapter_diagnostic_set_limits(
    int handle, int max_records, int max_bytes) {
  agc_wasm_adapter_t *adapter = wasm_adapter_from_handle(handle);
  if (!adapter || max_records <= 0 || max_bytes <= 0) return -1;
  adapter->diagnostic_record_limit = max_records;
  adapter->diagnostic_byte_limit = max_bytes;
  return 0;
}

static int attach_wasm_backend_context(
    ag_compilation_session_t *session) {
  wasm32_backend_context_t *backend = wasm32_backend_context_create(
      ag_compilation_session_codegen_emit_context(session));
  if (!backend) return 0;
  if (!ag_compilation_session_set_backend_context(
          session, backend, wasm32_backend_context_destroy)) {
    wasm32_backend_context_destroy(backend);
    return 0;
  }
  return 1;
}

static void wasm_adapter_retain_session(
    agc_wasm_adapter_t *adapter, ag_compilation_session_t *session) {
  if (!adapter || adapter->session == session) return;
  ag_compilation_session_destroy(adapter->session);
  adapter->session = session;
}

static int agc_wasm_compile_to_memory(
                                      agc_wasm_adapter_t *adapter,
                                      int source_addr, int source_name_addr,
                                      int virtual_bundle_addr, int virtual_bundle_len,
                                      int max_header_files, int max_header_file_bytes,
                                      int max_header_total_bytes, int max_include_depth,
                                      int out_addr, int out_cap, int object_mode) {
  if (!adapter) return -1;
  ag_compilation_session_destroy(adapter->session);
  adapter->session = NULL;
  ag_target_info_t target = ag_target_info_wasm32();
  ag_compilation_session_t *session =
      ag_compilation_session_create(&target);
  if (!session) return -4;
  wasm_adapter_retain_session(adapter, session);
  ag_diagnostic_context_t *diagnostics =
      ag_compilation_session_diagnostic_context(session);
  diag_context_set_locale(
      diagnostics, adapter->diagnostic_locale);
  if (diag_context_set_limits(
          diagnostics, adapter->diagnostic_record_limit,
          adapter->diagnostic_byte_limit) != 0)
    return -4;
  diag_reset_records_in(diagnostics);
  if (!source_addr || !out_addr || out_cap <= 0) {
    return -1;
  }
  if (adapter->continuation.enabled &&
      !ag_compilation_session_set_continuation(
          session, adapter->continuation.entry,
          adapter->continuation.condition,
          adapter->continuation.start_export,
          adapter->continuation.resume_export,
          adapter->continuation.status_export,
          adapter->continuation.result_export)) {
    return -4;
  }
  if (
      !attach_wasm_backend_context(session)) {
    return -4;
  }
  ag_codegen_emit_context_t *emit_context =
      ag_compilation_session_codegen_emit_context(session);
  wasm32_backend_context_t *backend =
      ag_compilation_session_backend_context(session);

  if (!psx_frontend_reset_translation_unit_state_in_session(
          session)) {
    return -4;
  }

  char *source = (char *)(long)source_addr;
  const char *source_name = source_name_addr ? (const char *)(long)source_name_addr : "input.c";
  if (!source_name[0]) {
    return -1;
  }
  if (virtual_bundle_addr) {
    pp_virtual_headers_configure_in(
        ag_compilation_session_preprocessor_context(session),
        (const unsigned char *)(long)virtual_bundle_addr,
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
  token_t *tok = pp_stream_open_in(
      ag_compilation_session_preprocessor_context(session),
      &pps, source);

  if (object_mode) {
    wasm32_backend_obj_set_output_file(backend, NULL);
    wasm32_backend_obj_capture_output(backend, 1);
    wasm32_backend_obj_set_capture_limit(backend, (size_t)out_cap);
    wasm32_backend_obj_begin(backend);
  } else {
    gen_set_simple_formatter_in(emit_context, 1);
    gen_set_output_callback_in(emit_context, write_line_to_memory, &out);
    wasm32_backend_wat_begin(backend);
  }

  psx_frontend_stream_t stream = {0};
  if (!psx_frontend_stream_begin(
          &stream, session, tk_ctx, tok)) {
    clear_output_callback(emit_context);
    gen_set_simple_formatter_in(emit_context, 0);
    if (pps) pp_stream_close(pps);
    wasm32_backend_obj_capture_output(backend, 0);
    return -4;
  }
  ir_build_options_t ir_options = {
      .target = ag_compilation_session_target(session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          ag_compilation_session_semantic_context(session)),
      .record_decls = ps_ctx_record_decl_table_in(
          ag_compilation_session_semantic_context(session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          ag_compilation_session_semantic_context(session)),
      .continuation = ag_compilation_session_continuation(session),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(session),
      .allocation_stats =
          ag_compilation_session_ir_allocation_stats(session),
  };
  psx_frontend_function_t function;
  while (psx_frontend_next_function(&stream, &function)) {
    if (!wasm_emit_function_direct(
            backend, &stream, &function, object_mode, &ir_options)) {
      clear_output_callback(emit_context);
      gen_set_simple_formatter_in(emit_context, 0);
      if (pps) pp_stream_close(pps);
      wasm32_backend_obj_capture_output(backend, 0);
      return -3;
    }
    psx_frontend_free_processed_ast_in_session(session);
  }
  psx_frontend_stream_end(&stream);
  if (pps) pp_stream_close(pps);

  if (diag_has_error_records_in(diagnostics)) {
    clear_output_callback(emit_context);
    gen_set_simple_formatter_in(emit_context, 0);
    if (object_mode) wasm32_backend_obj_capture_output(backend, 0);
    return -5;
  }

  ir_data_module_t *data_module =
      lower_ir_translation_unit_data_in_session(session);
  if (!data_module) {
    clear_output_callback(emit_context);
    gen_set_simple_formatter_in(emit_context, 0);
    if (object_mode) wasm32_backend_obj_capture_output(backend, 0);
    return -3;
  }
  ir_abi_data_module_t *data_abi = lower_data_module_abi(
      data_module, &ir_options);
  if (!data_abi) {
    ir_data_module_free(data_module);
    clear_output_callback(emit_context);
    gen_set_simple_formatter_in(emit_context, 0);
    if (object_mode) wasm32_backend_obj_capture_output(backend, 0);
    return -3;
  }

  if (object_mode) {
    wasm32_backend_obj_emit_data_segments(
        backend, data_module, data_abi);
    ir_abi_data_module_free(data_abi);
    ir_data_module_free(data_module);
    wasm32_backend_obj_end(backend);
    wasm32_backend_obj_capture_output(backend, 0);
    if (wasm32_backend_obj_capture_limit_exceeded(backend)) {
      return -2;
    }
    obj_bytes = wasm32_backend_obj_take_output(backend, &obj_len);
    if (!obj_bytes) {
      return -4;
    }
    if (obj_len > (size_t)out_cap) {
      free(obj_bytes);
      return -2;
    }
    memcpy(out.buf, obj_bytes, obj_len);
    free(obj_bytes);
    return (int)obj_len;
  } else {
    wasm32_backend_wat_emit_data_segments(
        backend, data_module, data_abi);
    ir_abi_data_module_free(data_abi);
    ir_data_module_free(data_module);
    wasm32_backend_wat_end(backend);
    clear_output_callback(emit_context);
    gen_set_simple_formatter_in(emit_context, 0);
  }

  int result = out.overflow ? -2 : out.len;
  return result;
}

int agc_wasm_adapter_compile_wat(
    int handle, int source_addr, int out_addr, int out_cap) {
  return agc_wasm_compile_to_memory(
      wasm_adapter_from_handle(handle), source_addr, 0,
      0, 0, 0, 0, 0, 0, out_addr, out_cap, 0);
}

int agc_wasm_adapter_compile_object(
    int handle, int source_addr, int out_addr, int out_cap) {
  return agc_wasm_compile_to_memory(
      wasm_adapter_from_handle(handle), source_addr, 0,
      0, 0, 0, 0, 0, 0, out_addr, out_cap, 1);
}

int agc_wasm_adapter_compile_wat_named(
    int handle, int source_addr, int source_name_addr,
    int out_addr, int out_cap) {
  return agc_wasm_compile_to_memory(
      wasm_adapter_from_handle(handle), source_addr, source_name_addr,
      0, 0, 0, 0, 0, 0, out_addr, out_cap, 0);
}

int agc_wasm_adapter_compile_object_named(
    int handle, int source_addr, int source_name_addr,
    int out_addr, int out_cap) {
  return agc_wasm_compile_to_memory(
      wasm_adapter_from_handle(handle), source_addr, source_name_addr,
      0, 0, 0, 0, 0, 0, out_addr, out_cap, 1);
}

int agc_wasm_adapter_compile_wat_virtual(
    int handle, int source_addr, int source_name_addr,
    int bundle_addr, int bundle_len, int max_files, int max_file_bytes,
    int max_total_bytes, int max_depth, int out_addr, int out_cap) {
  return agc_wasm_compile_to_memory(
      wasm_adapter_from_handle(handle), source_addr, source_name_addr,
      bundle_addr, bundle_len, max_files, max_file_bytes,
      max_total_bytes, max_depth, out_addr, out_cap, 0);
}

int agc_wasm_adapter_compile_object_virtual(
    int handle, int source_addr, int source_name_addr,
    int bundle_addr, int bundle_len, int max_files, int max_file_bytes,
    int max_total_bytes, int max_depth, int out_addr, int out_cap) {
  return agc_wasm_compile_to_memory(
      wasm_adapter_from_handle(handle), source_addr, source_name_addr,
      bundle_addr, bundle_len, max_files, max_file_bytes,
      max_total_bytes, max_depth, out_addr, out_cap, 1);
}

static const ag_diagnostic_context_t *wasm_adapter_diagnostics(int handle) {
  agc_wasm_adapter_t *adapter = wasm_adapter_from_handle(handle);
  return adapter && adapter->session
             ? ag_compilation_session_diagnostic_context(adapter->session)
             : NULL;
}

int agc_wasm_adapter_diagnostic_count(int handle) {
  return diag_context_record_count(wasm_adapter_diagnostics(handle));
}

int agc_wasm_adapter_diagnostic_bytes(int handle) {
  return diag_context_record_bytes(wasm_adapter_diagnostics(handle));
}

int agc_wasm_adapter_diagnostic_limit_kind(int handle) {
  return diag_context_record_limit_kind(wasm_adapter_diagnostics(handle));
}

int agc_wasm_adapter_diagnostic_severity(int handle, int index) {
  return diag_context_record_severity(wasm_adapter_diagnostics(handle), index);
}

int agc_wasm_adapter_diagnostic_code_ptr(int handle, int index) {
  return (int)(long)diag_context_record_code(
      wasm_adapter_diagnostics(handle), index);
}

int agc_wasm_adapter_diagnostic_message_ptr(int handle, int index) {
  return (int)(long)diag_context_record_message(
      wasm_adapter_diagnostics(handle), index);
}

int agc_wasm_adapter_diagnostic_source_name_ptr(int handle, int index) {
  return (int)(long)diag_context_record_source_name(
      wasm_adapter_diagnostics(handle), index);
}

#define AGC_WASM_ADAPTER_DIAG_INT_GETTER(name, query) \
  int name(int handle, int index) {                   \
    return query(wasm_adapter_diagnostics(handle), index); \
  }
AGC_WASM_ADAPTER_DIAG_INT_GETTER(
    agc_wasm_adapter_diagnostic_start_line,
    diag_context_record_start_line)
AGC_WASM_ADAPTER_DIAG_INT_GETTER(
    agc_wasm_adapter_diagnostic_start_column,
    diag_context_record_start_column)
AGC_WASM_ADAPTER_DIAG_INT_GETTER(
    agc_wasm_adapter_diagnostic_start_offset,
    diag_context_record_start_offset)
AGC_WASM_ADAPTER_DIAG_INT_GETTER(
    agc_wasm_adapter_diagnostic_end_line,
    diag_context_record_end_line)
AGC_WASM_ADAPTER_DIAG_INT_GETTER(
    agc_wasm_adapter_diagnostic_end_column,
    diag_context_record_end_column)
AGC_WASM_ADAPTER_DIAG_INT_GETTER(
    agc_wasm_adapter_diagnostic_end_offset,
    diag_context_record_end_offset)
#undef AGC_WASM_ADAPTER_DIAG_INT_GETTER

int main(int argc, char **argv) {
  ag_target_info_t target =
#ifdef AGC_TARGET_WASM32
      ag_target_info_wasm32();
#else
      ag_target_info_host();
#endif
  ag_compilation_session_t *session =
      ag_compilation_session_create(&target);
  if (!session) return 1;
  ag_diagnostic_context_t *diagnostics =
      ag_compilation_session_diagnostic_context(session);
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
    diag_emit_internalf_in(
        diagnostics, DIAG_ERR_INTERNAL_USAGE,
        diag_message_for_in(diagnostics, DIAG_ERR_INTERNAL_USAGE),
        prog_disp);
    return 1;
  }
#else
  if (argc != 2) {
    diag_emit_internalf_in(
        diagnostics, DIAG_ERR_INTERNAL_USAGE,
        diag_message_for_in(diagnostics, DIAG_ERR_INTERNAL_USAGE),
        prog_disp);
    return 1;
  }
  input_path = argv[1];
#endif

  const char *input_disp = diag_display_path(input_path);
  char *source = read_file_contents(input_path);
  if (!source) {
    diag_emit_internalf_in(
        diagnostics, DIAG_ERR_INTERNAL_INPUT_READ_FAILED,
        diag_message_for_in(
            diagnostics, DIAG_ERR_INTERNAL_INPUT_READ_FAILED),
        input_disp);
    return 1;
  }

  int session_ready = 1;
#ifdef AGC_TARGET_WASM32
  if (session_ready && continuation_entry) {
    session_ready = ag_compilation_session_set_continuation(
        session, continuation_entry, continuation_condition,
        continuation_start, continuation_resume,
        continuation_status, continuation_result);
  }
  if (session_ready) session_ready = attach_wasm_backend_context(session);
#endif
  if (!session_ready) {
    ag_compilation_session_destroy(session);
    free(source);
    return 1;
  }
  ag_codegen_emit_context_t *emit_context =
      ag_compilation_session_codegen_emit_context(session);
#ifdef AGC_TARGET_WASM32
  wasm32_backend_context_t *backend =
      ag_compilation_session_backend_context(session);
#endif
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
  token_t *tok = pp_stream_open_in(
      ag_compilation_session_preprocessor_context(session),
      &pps, source);

#ifdef AGC_TARGET_WASM32
  FILE *wasm_obj_out = NULL;
  if (wasm_object_mode) {
    wasm_obj_out = fopen(output_path, "wb");
    if (!wasm_obj_out) {
      diag_error_id_t id = DIAG_ERR_CODEGEN_WASM_OBJECT_OPEN_FAILED;
      diag_emit_internalf_in(
          diagnostics, id, diag_message_for_in(diagnostics, id),
          diag_display_path(output_path));
      if (pps) pp_stream_close(pps);
      ag_compilation_session_destroy(session);
      free(source);
      return 1;
    }
    wasm32_backend_obj_set_output_file(backend, wasm_obj_out);
    wasm32_backend_obj_begin(backend);
  } else {
    gen_set_output_callback_in(emit_context, write_line_to_file, stdout);
    wasm32_backend_wat_begin(backend);
  }
#else
  gen_set_output_callback_in(emit_context, write_line_to_file, stdout);
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
    clear_output_callback(emit_context);
    ag_compilation_session_destroy(session);
    free(source);
    return 1;
  }
  ir_build_options_t ir_options = {
      .target = ag_compilation_session_target(session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          ag_compilation_session_semantic_context(session)),
      .record_decls = ps_ctx_record_decl_table_in(
          ag_compilation_session_semantic_context(session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          ag_compilation_session_semantic_context(session)),
      .continuation = ag_compilation_session_continuation(session),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(session),
      .allocation_stats =
          ag_compilation_session_ir_allocation_stats(session),
  };
  psx_frontend_function_t function;
  while (psx_frontend_next_function(&stream, &function)) {
#ifdef AGC_TARGET_WASM32
    if (!wasm_emit_function_direct(
            backend, &stream, &function, wasm_object_mode, &ir_options)) {
#else
    ir_module_t *function_module = build_resolved_function_module(
        &stream, &function, &ir_options);
    if (!function_module) {
#endif
      diag_emit_internalf_in(
          diagnostics, DIAG_ERR_CODEGEN_IR_BUILD_EMIT_FAILED, "%s",
          diag_message_for_in(
              diagnostics, DIAG_ERR_CODEGEN_IR_BUILD_EMIT_FAILED));
      if (pps) pp_stream_close(pps);
      ag_compilation_session_destroy(session);
      free(source);
      return 1;
    }
#ifndef AGC_TARGET_WASM32
    ir_opt_const_fold(function_module);
    ir_opt_dce(function_module);
    dump_ir_if_requested(function_module);
    ir_abi_module_t *abi = lower_module_abi(
        function_module, &ir_options);
    if (!abi) {
      ir_module_free(function_module);
      diag_emit_internalf_in(
          diagnostics, DIAG_ERR_CODEGEN_IR_BUILD_EMIT_FAILED, "%s",
          diag_message_for_in(
              diagnostics, DIAG_ERR_CODEGEN_IR_BUILD_EMIT_FAILED));
      ag_compilation_session_destroy(session);
      free(source);
      return 1;
    }
    arm64_emit_ir_module(function_module, abi, emit_context);
    ir_abi_module_free(abi);
    ir_module_free(function_module);
#endif
    psx_frontend_free_processed_ast_in_session(session);
  }
  psx_frontend_stream_end(&stream);
  if (pps) pp_stream_close(pps);

  if (diag_has_error_records_in(diagnostics)) {
#ifdef AGC_TARGET_WASM32
    if (wasm_object_mode) {
      fclose(wasm_obj_out);
      remove(output_path);
    }
#endif
    clear_output_callback(emit_context);
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
    clear_output_callback(emit_context);
    ag_compilation_session_destroy(session);
    free(source);
    return 1;
  }

#ifdef AGC_TARGET_WASM32
  ir_abi_data_module_t *data_abi = lower_data_module_abi(
      data_module, &ir_options);
  if (!data_abi) {
    ir_data_module_free(data_module);
    if (wasm_object_mode) {
      fclose(wasm_obj_out);
      remove(output_path);
    }
    clear_output_callback(emit_context);
    ag_compilation_session_destroy(session);
    free(source);
    return 1;
  }
  if (wasm_object_mode) {
    wasm32_backend_obj_emit_data_segments(
        backend, data_module, data_abi);
    wasm32_backend_obj_end(backend);
    fclose(wasm_obj_out);
  } else {
    wasm32_backend_wat_emit_data_segments(
        backend, data_module, data_abi);
    wasm32_backend_wat_end(backend);
  }
  ir_abi_data_module_free(data_abi);
#else
  // lowering 済みの文字列・浮動小数点定数・global object を emit。
  gen_string_literals_in(emit_context, data_module);
  gen_float_literals_in(emit_context, data_module);
  gen_global_vars_in(emit_context, data_module);
#endif
  ir_data_module_free(data_module);
  if (!wasm_object_mode) clear_output_callback(emit_context);

  if (getenv("AG_MEM_STATS")) print_mem_stats(session, strlen(source));

  ag_compilation_session_destroy(session);
  free(source);
  return 0;
}
