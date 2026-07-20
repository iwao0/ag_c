#ifndef DIAG_DIAG_H
#define DIAG_DIAG_H

#include "error_catalog.h"
#include "warning_catalog.h"
#include "messages.h"
#include "../tokenizer/token.h"

typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;
typedef struct ag_source_manager_t ag_source_manager_t;

ag_diagnostic_context_t *diag_context_create(
    ag_source_manager_t *source_manager);
void diag_context_destroy(ag_diagnostic_context_t *context);
ag_source_manager_t *diag_context_source_manager(
    const ag_diagnostic_context_t *context);
int diag_context_set_limits(
    ag_diagnostic_context_t *context, int max_records, int max_bytes);
void diag_context_set_locale(
    ag_diagnostic_context_t *context, const char *locale);
const char *diag_context_get_locale(
    const ag_diagnostic_context_t *context);

const char *diag_message_for_in(
    const ag_diagnostic_context_t *context, diag_error_id_t id);
const char *diag_warn_message_for_in(
    const ag_diagnostic_context_t *context, diag_warn_id_t id);

/**
 * 構造化診断の座標は正規化済みUTF-8入力上のbyte基準。
 * offsetは0始まり、line/columnは1始まり、endは範囲外終端(exclusive)を表す。
 */
void diag_reset_records_in(ag_diagnostic_context_t *context);
int agc_wasm_diagnostic_api_version(void);
int diag_context_record_count(const ag_diagnostic_context_t *context);
int diag_context_record_bytes(const ag_diagnostic_context_t *context);
int diag_context_record_limit_kind(const ag_diagnostic_context_t *context);
int diag_context_record_severity(
    const ag_diagnostic_context_t *context, int index);
const char *diag_context_record_code(
    const ag_diagnostic_context_t *context, int index);
const char *diag_context_record_message(
    const ag_diagnostic_context_t *context, int index);
const char *diag_context_record_source_name(
    const ag_diagnostic_context_t *context, int index);
int diag_context_record_start_line(
    const ag_diagnostic_context_t *context, int index);
int diag_context_record_start_column(
    const ag_diagnostic_context_t *context, int index);
int diag_context_record_start_offset(
    const ag_diagnostic_context_t *context, int index);
int diag_context_record_end_line(
    const ag_diagnostic_context_t *context, int index);
int diag_context_record_end_column(
    const ag_diagnostic_context_t *context, int index);
int diag_context_record_end_offset(
    const ag_diagnostic_context_t *context, int index);
int diag_has_error_records_in(const ag_diagnostic_context_t *context);
int diag_limit_kind_in(const ag_diagnostic_context_t *context);

/**
 * @brief テキストIDに対応するローカライズ済みテキストを取得する。
 * @param id テキストID。
 * @return ローカライズ済みテキスト。未定義時は "unknown.text"。
 */
const char *diag_text_for_in(
    const ag_diagnostic_context_t *context, diag_text_id_t id);

/**
 * @brief 入力位置を指定して診断を出力し、プロセスを終了する。
 * @param id 出力するエラーID。
 * @param input 元入力全体（キャレット表示用）。
 * @param loc 入力中のエラー位置ポインタ。
 * @param fmt 可変引数付きフォーマット文字列。
 * @return 戻らない。
 */
_Noreturn void diag_emit_atf_in(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const char *input, const char *loc, const char *fmt, ...);

/**
 * @brief トークン位置を指定して診断を出力し、プロセスを終了する。
 * @param id 出力するエラーID。
 * @param tok エラー位置を表すトークン。
 * @param fmt 可変引数付きフォーマット文字列。
 * @return 戻らない。
 */
_Noreturn void diag_emit_tokf_in(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const token_t *tok, const char *fmt, ...);

/** Store and print a recoverable source diagnostic without terminating. */
int diag_report_atf_in(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const char *input, const char *loc, const char *fmt, ...);
int diag_report_tokf_in(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const token_t *tok, const char *fmt, ...);

/**
 * @brief トークン位置を指定して警告を出力する（プロセスは終了しない）。
 * @param id 警告ID。
 * @param tok 警告位置を表すトークン。
 * @param fmt 可変引数付きフォーマット文字列。
 */
void diag_warn_tokf_in(
    ag_diagnostic_context_t *context, diag_warn_id_t id,
    const token_t *tok, const char *fmt, ...);

/**
 * @brief 入力位置なしの内部診断を出力し、プロセスを終了する。
 * @param id 出力するエラーID。
 * @param fmt 可変引数付きフォーマット文字列。
 * @return 戻らない。
 */
_Noreturn void diag_emit_internalf_in(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const char *fmt, ...);

/**
 * @brief 入力位置なしの内部診断を出力する（プロセスは終了しない）。
 * @param id 出力するエラーID。
 * @param fmt 可変引数付きフォーマット文字列。
 */
void diag_report_internalf_in(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const char *fmt, ...);

#endif
