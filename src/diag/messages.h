#ifndef DIAG_MESSAGES_H
#define DIAG_MESSAGES_H

#include "error_catalog.h"
#include "warning_catalog.h"

typedef enum {
  DIAG_TEXT_WARNING = 1,
  DIAG_TEXT_C11_AUDIT_PREFIX = 2,
  DIAG_TEXT_C11_AUDIT_BINARY_LITERAL_EXTENSION = 3,
  DIAG_TEXT_TAG_NAME = 4,
  DIAG_TEXT_MEMBER_NAME = 5,
  DIAG_TEXT_ENUMERATOR_NAME = 6,
  DIAG_TEXT_PARAMETER = 7,
  DIAG_TEXT_TAG_TYPE_SUFFIX = 8,
  DIAG_TEXT_TAG_TYPE = 9,
  DIAG_TEXT_BREAK = 10,
  DIAG_TEXT_CONTINUE = 11,
  DIAG_TEXT_LOOP_OR_SWITCH_SCOPE = 12,
  DIAG_TEXT_LOOP_SCOPE = 13,
  DIAG_TEXT_GOTO_LABEL_AFTER = 14,
  DIAG_TEXT_SWITCH_SCOPE = 15,
  DIAG_TEXT_CASE = 16,
  DIAG_TEXT_DEFAULT = 17,
  DIAG_TEXT_LABEL = 18,
  DIAG_TEXT_ARRAY_DESIGNATOR_INDEX = 19,
  DIAG_TEXT_ARRAY_SIZE = 20,
  DIAG_TEXT_WHILE = 21,
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
