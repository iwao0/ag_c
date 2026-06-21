#ifndef TOKENIZER_SCANNER_H
#define TOKENIZER_SCANNER_H

#include <stdbool.h>

/** @brief 空白・コメント・行継続をスキップし、位置情報を更新する。 */
char *tk_skip_ignored(char *p, bool *at_bol, bool *has_space, int *line_no);
/** @brief 識別子開始文字かを判定し、消費バイト数を返す。 */
bool tk_scan_ident_start(const char *p, int *adv);
/** @brief 識別子継続文字かを判定し、消費バイト数を返す。 */
bool tk_scan_ident_continue(const char *p, int *adv);

#endif
