#ifndef DIAG_MESSAGES_H
#define DIAG_MESSAGES_H

#include "error_catalog.h"
#include "warning_catalog.h"

typedef enum {
  DIAG_TEXT_WARNING = 1,
  DIAG_TEXT_C11_AUDIT_PREFIX = 2,
  DIAG_TEXT_C11_AUDIT_BINARY_LITERAL_EXTENSION = 3,
} diag_text_id_t;

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

/**
 * @brief テキストIDに対応する日本語テキストを取得する。
 * @param id テキストID。
 * @return 日本語テキスト。未定義時は NULL。
 */
const char *diag_text_ja(diag_text_id_t id);

/**
 * @brief テキストIDに対応する英語テキストを取得する。
 * @param id テキストID。
 * @return 英語テキスト。未定義時は NULL。
 */
const char *diag_text_en(diag_text_id_t id);

const char *diag_warn_message_ja(diag_warn_id_t id);
const char *diag_warn_message_en(diag_warn_id_t id);

#endif
