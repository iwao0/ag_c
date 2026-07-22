#ifndef PREPROCESS_H
#define PREPROCESS_H

#include "../target_info.h"
#include "../tokenizer/tokenizer.h"

typedef struct ag_preprocessor_context_t ag_preprocessor_context_t;
typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;

typedef struct {
  const char *name;
  int name_len;
  int is_function_like;
  int is_variadic;
  int parameter_count;
  const char *source_name;
  const char *source_input;
  int source_byte_offset;
  int source_byte_length;
} ag_pp_macro_view_t;

ag_preprocessor_context_t *pp_context_create(
    ag_diagnostic_context_t *diagnostic_context,
    tokenizer_context_t *tokenizer_context,
    const ag_target_info_t *target);
void pp_context_destroy(ag_preprocessor_context_t *context);
ag_diagnostic_context_t *pp_context_diagnostics(
    const ag_preprocessor_context_t *context);
tokenizer_context_t *pp_context_tokenizer(
    const ag_preprocessor_context_t *context);
const ag_target_info_t *pp_context_target(
    const ag_preprocessor_context_t *context);
void pp_context_set_language_analysis_mode(
    ag_preprocessor_context_t *context, int enabled);
const char *pp_context_project_root(
    const ag_preprocessor_context_t *context);
const char *pp_context_include_root(
    const ag_preprocessor_context_t *context);

/* 遅延プリプロセス生成器。状態は呼び出し元の Preprocessor context に保持する。
 * pp_stream_open_in は predefined マクロを永続側に作り、recyclable 生成器を開き、
 * カーソル前進フックを登録して先頭トークンを返す。frontend stream beginで
 * その先頭から消費すると、前進のたびに先読み materialize + 通過トークン解放が走る。 */
typedef struct pp_stream pp_stream_t;
token_t *pp_stream_open_in(ag_preprocessor_context_t *context,
                           pp_stream_t **out_s,
                           const char *src);
void pp_stream_close(pp_stream_t *s);

/* Read-only language-analysis views. They are valid only until the current
 * preprocessor stream is closed or another translation unit is opened. */
int pp_macro_count_in(const ag_preprocessor_context_t *context);
int pp_macro_view_at_in(const ag_preprocessor_context_t *context, int index,
                        ag_pp_macro_view_t *out);
const char *pp_macro_parameter_at_in(const ag_preprocessor_context_t *context,
                                     int macro_index, int parameter_index);
/* Returns the required UTF-8 byte length, excluding the trailing NUL. */
int pp_macro_format_replacement_in(const ag_preprocessor_context_t *context,
                                   int macro_index, char *out,
                                   size_t out_size);

/** Wasm JS APIから渡されたcompile単位のvirtual header bundleを設定する。 */
void pp_virtual_headers_configure_in(
    ag_preprocessor_context_t *context,
    const unsigned char *bundle, size_t bundle_len,
    int max_files, int max_file_bytes,
    int max_total_bytes, int max_include_depth);
void pp_virtual_headers_clear_in(ag_preprocessor_context_t *context);
void pp_virtual_dependencies_reset_in(ag_preprocessor_context_t *context);
int pp_virtual_dependency_count_in(
    const ag_preprocessor_context_t *context);
const char *pp_virtual_dependency_name_at_in(
    const ag_preprocessor_context_t *context, int index);

#endif
