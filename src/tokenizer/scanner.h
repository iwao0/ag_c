#ifndef TOKENIZER_SCANNER_H
#define TOKENIZER_SCANNER_H

#include <stdbool.h>

typedef struct tokenizer_context_t tokenizer_context_t;

/** @brief 空白・コメント・行継続をスキップし、位置情報を更新する。 */
char *tk_skip_ignored_ctx(tokenizer_context_t *ctx, char *p,
                          bool *at_bol, bool *has_space, int *line_no);
/** @brief 識別子開始文字かを判定し、消費バイト数を返す。 */
bool tk_scan_ident_start(const char *p, int *adv);
/** @brief 識別子継続文字かを判定し、消費バイト数を返す。 */
bool tk_scan_ident_continue(const char *p, int *adv);

#endif
