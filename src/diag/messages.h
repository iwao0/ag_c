#ifndef DIAG_MESSAGES_H
#define DIAG_MESSAGES_H

#include "error_catalog.h"

/**
 * @brief エラーIDに対応する日本語メッセージを取得する。
 * @param id エラーID。
 * @return 日本語メッセージ。未定義IDの場合は汎用メッセージ。
 */
const char *diag_message_ja(diag_error_id_t id);

/**
 * @brief エラーIDに対応する英語メッセージを取得する。
 * @param id エラーID。
 * @return 英語メッセージ。未定義IDの場合は汎用メッセージ。
 */
const char *diag_message_en(diag_error_id_t id);

#endif
