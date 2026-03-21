#ifndef DIAG_DIAG_H
#define DIAG_DIAG_H

#include "error_catalog.h"
#include "warning_catalog.h"
#include "messages.h"
#include "../tokenizer/token.h"

/**
 * @brief 診断メッセージのロケールを設定する。
 * @param locale ロケール名（例: "ja", "en"）。NULL の場合は既定ロケールを維持する。
 */
void diag_set_locale(const char *locale);

/**
 * @brief 現在の診断ロケールを取得する。
 * @return 現在有効なロケール文字列。
 */
const char *diag_get_locale(void);

/**
 * @brief エラーIDに対応する既定メッセージを取得する。
 * @param id エラーID。
 * @return エラーIDに対応するメッセージ文字列。未定義IDの場合は汎用メッセージ。
 */
const char *diag_message_for(diag_error_id_t id);
const char *diag_warn_message_for(diag_warn_id_t id);

/**
 * @brief テキストIDに対応するローカライズ済みテキストを取得する。
 * @param id テキストID。
 * @return ローカライズ済みテキスト。未定義時は "unknown.text"。
 */
const char *diag_text_for(diag_text_id_t id);

/**
 * @brief 入力位置を指定して診断を出力し、プロセスを終了する。
 * @param id 出力するエラーID。
 * @param input 元入力全体（キャレット表示用）。
 * @param loc 入力中のエラー位置ポインタ。
 * @param fmt 可変引数付きフォーマット文字列。
 * @return 戻らない。
 */
void diag_emit_atf(diag_error_id_t id, const char *input, const char *loc, const char *fmt, ...)
    __attribute__((noreturn));

/**
 * @brief トークン位置を指定して診断を出力し、プロセスを終了する。
 * @param id 出力するエラーID。
 * @param tok エラー位置を表すトークン。
 * @param fmt 可変引数付きフォーマット文字列。
 * @return 戻らない。
 */
void diag_emit_tokf(diag_error_id_t id, const token_t *tok, const char *fmt, ...)
    __attribute__((noreturn));

/**
 * @brief トークン位置を指定して警告を出力する（プロセスは終了しない）。
 * @param id 警告ID。
 * @param tok 警告位置を表すトークン。
 * @param fmt 可変引数付きフォーマット文字列。
 */
void diag_warn_tokf(diag_warn_id_t id, const token_t *tok, const char *fmt, ...);

/**
 * @brief 入力位置なしの内部診断を出力し、プロセスを終了する。
 * @param id 出力するエラーID。
 * @param fmt 可変引数付きフォーマット文字列。
 * @return 戻らない。
 */
void diag_emit_internalf(diag_error_id_t id, const char *fmt, ...) __attribute__((noreturn));

#endif
