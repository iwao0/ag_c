#ifndef DIAG_DIAG_H
#define DIAG_DIAG_H

#include "error_catalog.h"
#include "warning_catalog.h"
#include "messages.h"
#include "../tokenizer/token.h"

typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;
typedef struct ag_source_manager_t ag_source_manager_t;
typedef void (*ag_diagnostic_fatal_recovery_fn)(void *context);

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

/* Language-service callers may capture a fatal source diagnostic and leave
 * the current parse through a caller-owned non-local recovery boundary.
 * Normal compilation leaves this unset and retains the existing exit(1)
 * behavior. The callback must not return. */
void diag_context_set_fatal_recovery(
    ag_diagnostic_context_t *context,
    ag_diagnostic_fatal_recovery_fn recovery, void *recovery_context);
void diag_context_clear_fatal_recovery(
    ag_diagnostic_context_t *context);
/* Capture-only mode stores diagnostics without writing them to stderr. */
void diag_context_set_capture_only(
    ag_diagnostic_context_t *context, int capture_only);

const char *diag_message_for_in(
    const ag_diagnostic_context_t *context, diag_error_id_t id);
const char *diag_warn_message_for_in(
    const ag_diagnostic_context_t *context, diag_warn_id_t id);

/**
 * Structured-diagnostic coordinates use byte positions in normalized UTF-8
 * input.  Offsets are zero-based, lines and columns are one-based, and end
 * denotes the exclusive end of the range.
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
 * @brief Return localized text for a text ID.
 * @param id Text ID.
 * @return Localized text, or "unknown.text" when undefined.
 */
const char *diag_text_for_in(
    const ag_diagnostic_context_t *context, diag_text_id_t id);

/**
 * @brief Emit a diagnostic at an input position and terminate the process.
 * @param id Error ID to emit.
 * @param input Complete original input (for caret display).
 * @param loc Pointer to the error position in the input.
 * @param fmt Variadic format string.
 * @return Does not return.
 */
_Noreturn void diag_emit_atf_in(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const char *input, const char *loc, const char *fmt, ...);

/**
 * @brief Emit a diagnostic at a token position and terminate the process.
 * @param id Error ID to emit.
 * @param tok Token identifying the error position.
 * @param fmt Variadic format string.
 * @return Does not return.
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
 * @brief Emit a warning at a token position without terminating the process.
 * @param id Warning ID.
 * @param tok Token identifying the warning position.
 * @param fmt Variadic format string.
 */
void diag_warn_tokf_in(
    ag_diagnostic_context_t *context, diag_warn_id_t id,
    const token_t *tok, const char *fmt, ...);

/**
 * @brief Emit an internal diagnostic without an input position and terminate.
 * @param id Error ID to emit.
 * @param fmt Variadic format string.
 * @return Does not return.
 */
_Noreturn void diag_emit_internalf_in(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const char *fmt, ...);

/**
 * @brief Emit an internal diagnostic without an input position or termination.
 * @param id Error ID to emit.
 * @param fmt Variadic format string.
 */
void diag_report_internalf_in(
    ag_diagnostic_context_t *context, diag_error_id_t id,
    const char *fmt, ...);

#endif
