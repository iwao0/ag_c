#ifndef PREPROCESS_H
#define PREPROCESS_H

#include "../target_info.h"
#include "../tokenizer/tokenizer.h"

typedef struct ag_preprocessor_context_t ag_preprocessor_context_t;

ag_preprocessor_context_t *pp_context_create(void);
void pp_context_destroy(ag_preprocessor_context_t *context);
ag_preprocessor_context_t *pp_context_activate(
    ag_preprocessor_context_t *context);
ag_preprocessor_context_t *pp_context_active(void);

/** @brief 明示Tokenizer/targetでプリプロセスを実行する。 */
token_t *preprocess_for_target_ctx(tokenizer_context_t *tk_ctx,
                                   const ag_target_info_t *target,
                                   token_t *tok);

/* トークンストリーム経路 (`#` 指令の無いファイル専用) の遅延プリプロセス生成器。
 * pp_stream_open_for_target は predefined マクロを永続側に作り、recyclable 生成器を開き、
 * カーソル前進フックを登録して先頭トークンを返す。frontend stream beginで
 * その先頭から消費すると、前進のたびに先読み materialize + 通過トークン解放が走る。 */
typedef struct pp_stream pp_stream_t;
token_t *pp_stream_open_for_target(pp_stream_t **out_s,
                                   tokenizer_context_t *tk_ctx,
                                   const ag_target_info_t *target,
                                   const char *src);
void pp_stream_close(pp_stream_t *s);

/** Wasm JS APIから渡されたcompile単位のvirtual header bundleを設定する。 */
void pp_virtual_headers_configure(const unsigned char *bundle, size_t bundle_len,
                                  int max_files, int max_file_bytes,
                                  int max_total_bytes, int max_include_depth);
void pp_virtual_headers_clear(void);

#endif
