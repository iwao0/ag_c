#ifndef TOKENIZER_DIAG_HELPER_H
#define TOKENIZER_DIAG_HELPER_H

#include "../diag/diag.h"
#include "context.h"
#include "tokenizer.h"

#define TK_DIAG_MESSAGE_IN(ctx, id) \
  diag_message_for_in(tk_context_diagnostics((ctx)), (id))
#define TK_DIAG_TEXT_IN(ctx, id) \
  diag_text_for_in(tk_context_diagnostics((ctx)), (id))

/**
 * @brief Internal helper that emits a location-based diagnostic with tokenizer input context.
 */
/* In tolerant mode (while skipping a false `#if 0` branch or scanning ahead
 * within a line), tk_tolerate_longjmp_if_active_ctx jumps back to
 * tk_stream_next before reaching diag_emit_atf.  Normal mode still emits the
 * diagnostic and exits. */
#define TK_DIAG_ATF_IN(ctx, id, loc, fmt, ...) \
  do { \
    tokenizer_context_t *tk_diag_ctx__ = (ctx); \
    ag_diagnostic_context_t *tk_diagnostics__ = \
        tk_context_diagnostics(tk_diag_ctx__); \
    tk_tolerate_longjmp_if_active_ctx(tk_diag_ctx__); \
    diag_emit_atf_in( \
        tk_diagnostics__, (id), tk_get_user_input_ctx(tk_diag_ctx__), \
        (loc), (fmt), ##__VA_ARGS__); \
  } while (0)

/**
 * @brief Internal helper that emits a location-based diagnostic with the catalog default message.
 */
#define TK_DIAG_AT_IN(ctx, id, loc) \
  TK_DIAG_ATF_IN( \
      (ctx), (id), (loc), "%s", TK_DIAG_MESSAGE_IN((ctx), (id)))

/**
 * @brief Internal helper that emits a token-based diagnostic with the catalog default message.
 */
#define TK_DIAG_TOK_IN(ctx, id, tok) \
  diag_emit_tokf_in( \
      tk_context_diagnostics((ctx)), (id), (tok), "%s", \
      TK_DIAG_MESSAGE_IN((ctx), (id)))

#endif
