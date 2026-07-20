#ifndef PREPROCESS_H
#define PREPROCESS_H

#include "../target_info.h"
#include "../tokenizer/tokenizer.h"

typedef struct ag_preprocessor_context_t ag_preprocessor_context_t;
typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;

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

/** Wasm JS APIから渡されたcompile単位のvirtual header bundleを設定する。 */
void pp_virtual_headers_configure_in(
    ag_preprocessor_context_t *context,
    const unsigned char *bundle, size_t bundle_len,
    int max_files, int max_file_bytes,
    int max_total_bytes, int max_include_depth);
void pp_virtual_headers_clear_in(ag_preprocessor_context_t *context);

#endif
