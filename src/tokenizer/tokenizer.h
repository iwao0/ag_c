#ifndef TOKENIZER_H
#define TOKENIZER_H

#include "token.h"
#include <stddef.h>

typedef struct tokenizer_context_t tokenizer_context_t;
typedef struct tk_allocator_context_t tk_allocator_context_t;
typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;
typedef struct ag_source_manager_t ag_source_manager_t;
typedef void (*tk_cursor_hook_t)(void *user_data, token_t *cursor);
typedef void (*tk_ensure_lookahead_hook_t)(void *user_data);

/** @brief Runtime configuration context for the tokenizer. */
struct tokenizer_context_t {
  tk_allocator_context_t *allocator_context;
  ag_diagnostic_context_t *diagnostic_context;
  ag_source_manager_t *source_manager;
  bool strict_c11_mode;
  bool enable_trigraphs;
  bool enable_binary_literals;
  bool enable_c11_audit_extensions;
  token_t *current_token;
  tk_cursor_hook_t cursor_hook;
  void *cursor_hook_user_data;
  tk_ensure_lookahead_hook_t ensure_lookahead_hook;
  void *ensure_lookahead_hook_user_data;
  bool tolerate_untokenizable;
  void *tolerate_jump_target;
  size_t stats_base_chunks;
  size_t stats_base_reserved_bytes;
  size_t max_token_len_for_test;
};

/**
 * @brief Return the current token cursor for a context.
 * @param ctx Target context.
 * @return Current token, or `NULL` when unset.
 */
token_t *tk_get_current_token_ctx(tokenizer_context_t *ctx);
/**
 * @brief Update the current token cursor for a context.
 * @param ctx Target context.
 * @param tok New current token; may be `NULL`.
 */
void tk_set_current_token_ctx(tokenizer_context_t *ctx, token_t *tok);

/**
 * @brief Convert a token kind to a readable string.
 * @param kind Token kind to convert.
 * @param len Output for the string length; may be `NULL` if unnecessary.
 * @return String representation of the kind.
 */
const char *tk_token_kind_str(token_kind_t kind, int *len);

/**
 * @brief Consume the next token in a context if it is the one-character punctuator `op`.
 * @param ctx Target context.
 * @param op Expected one-character punctuator.
 * @return `true` when matched and consumed; `false` without mutation otherwise.
 */
bool tk_consume_ctx(tokenizer_context_t *ctx, char op);
/**
 * @brief Consume the next token in a context if it is the punctuator string `op`.
 * @param ctx Target context.
 * @param op Expected punctuator string.
 * @return `true` when matched and consumed; `false` without mutation otherwise.
 */
bool tk_consume_str_ctx(tokenizer_context_t *ctx, const char *op);
/**
 * @brief Consume and return the next token in a context if it is an identifier.
 * @param ctx Target context.
 * @return Consumed identifier token, or `NULL` without mutation if unmatched.
 */
token_ident_t *tk_consume_ident_ctx(tokenizer_context_t *ctx);

/**
 * @brief Require and consume the one-character punctuator `op` in a context.
 * @param ctx Target context.
 * @param op Expected one-character punctuator.
 * @warning A mismatch or unset current token terminates with a diagnostic.
 */
void tk_expect_ctx(tokenizer_context_t *ctx, char op);

/**
 * @brief Require an integer literal in a context, consume it, and return its `int` value.
 * @param ctx Target context.
 * @return Consumed integer value.
 * @warning A mismatch, out-of-range value, or unset current token terminates with a diagnostic.
 */
int tk_expect_number_ctx(tokenizer_context_t *ctx);

/**
 * @brief Test whether the current token in a context is EOF.
 * @param ctx Target context.
 * @return `true` at EOF; `false` otherwise, including when unset.
 */
bool tk_at_eof_ctx(tokenizer_context_t *ctx);

/**
 * @brief Tokenize an input string in a context.
 * @param ctx Target context.
 * @param p Input string.
 * @return First token, with `TK_EOF` at the end.
 * @warning An invalid lexical element terminates through the diagnostic API.
 */
token_t *tk_tokenize_ctx(tokenizer_context_t *ctx, const char *p);

/* Lazy pull tokenizer.  Start with tk_stream_open; each tk_stream_next call
 * produces exactly one token (TK_EOF once at end of input, then NULL).  The
 * returned token's ->next is unset, so the caller links tokens.  Close the
 * session with tk_stream_close.  This API supports a lex/preprocess/parse
 * pipeline without retaining all tokens at once. */
typedef struct tk_token_stream tk_token_stream_t;
void tk_stream_open(tk_token_stream_t *s, tokenizer_context_t *ctx, const char *in);
token_t *tk_stream_next(tk_token_stream_t *s);
void tk_stream_close(tk_token_stream_t *s);
/* While true, consume an untokenizable character (` @ $), unterminated literal,
 * or invalid number as one TK_UNKNOWN character instead of failing immediately.
 * The preprocessor enables this only while skipping a false `#if 0` branch or
 * scanning ahead within a line, where the contents will be discarded. */
void tk_set_tolerate_untokenizable_ctx(tokenizer_context_t *ctx, bool v);
/* Internal to TK_DIAG_* macros: jump back to tk_stream_next's setjmp in tolerant mode. */
void tk_tolerate_longjmp_if_active_ctx(tokenizer_context_t *ctx);
/* Heap-allocated variant for callers that retain the opaque structure by pointer. */
tk_token_stream_t *tk_stream_new(tokenizer_context_t *ctx, const char *in);
void tk_stream_delete(tk_token_stream_t *s);

/* Install the parser cursor-advance hook for the token-stream driver; NULL removes it. */
void tk_set_cursor_hook_ctx(tokenizer_context_t *ctx, tk_cursor_hook_t fn,
                            void *user_data);
/* Return the current cursor-advance hook for saving/restoring during nested processing. */
tk_cursor_hook_t tk_get_cursor_hook_ctx(tokenizer_context_t *ctx);
void *tk_get_cursor_hook_user_data_ctx(tokenizer_context_t *ctx);
/* Call before deep lookahead that does not advance the cursor.  The registered
 * generator satisfies forward lookahead (installed by the preprocessor through
 * tk_set_ensure_lookahead_hook).  This is a no-op when no hook is registered. */
void tk_set_ensure_lookahead_hook_ctx(tokenizer_context_t *ctx,
                                      tk_ensure_lookahead_hook_t fn,
                                      void *user_data);
void tk_ensure_lookahead_ctx(tokenizer_context_t *ctx);

/**
 * @brief Return a context's input string for diagnostic display.
 * @param ctx Target context.
 * @return Configured input string, or `NULL` when unset.
 */
const char *tk_get_user_input_ctx(tokenizer_context_t *ctx);
/**
 * @brief Set a context's input string for diagnostic display.
 * @param ctx Target context.
 * @param p Input string to set.
 */
void tk_set_user_input_ctx(tokenizer_context_t *ctx, const char *p);

/**
 * @brief Return a context's file name for diagnostic display.
 * @param ctx Target context.
 * @return Configured file name, or `NULL` when unset.
 */
const char *tk_get_filename_ctx(tokenizer_context_t *ctx);
/**
 * @brief Set a context's file name for diagnostic display.
 * @param ctx Target context.
 * @param name File name to set.
 */
void tk_set_filename_ctx(tokenizer_context_t *ctx, const char *name);

/**
 * @brief Initialize a context with explicit external dependencies.
 * @param ctx Context to initialize.
 * @param diagnostic_context Caller-owned diagnostic context.
 * @param allocator_context Caller-owned token allocator.
 * @param source_manager Caller-owned source manager.
 * @return 1 on success, or 0 when a required dependency is absent.
 */
int tk_context_init(
    tokenizer_context_t *ctx,
    ag_diagnostic_context_t *diagnostic_context,
    tk_allocator_context_t *allocator_context,
    ag_source_manager_t *source_manager);
/**
 * @brief Initialize a context solely for traversing an existing token sequence.
 * @warning Do not pass it to tokenize/stream APIs that generate tokens.
 */
int tk_cursor_context_init(
    tokenizer_context_t *ctx,
    ag_diagnostic_context_t *diagnostic_context);
ag_diagnostic_context_t *tk_context_diagnostics(
    const tokenizer_context_t *ctx);
tk_allocator_context_t *tk_context_allocator(
    const tokenizer_context_t *ctx);
ag_source_manager_t *tk_context_source_manager(
    const tokenizer_context_t *ctx);
/** @brief Release runtime storage owned by the context itself. */
void tk_context_dispose(tokenizer_context_t *ctx);
uint16_t tk_filename_intern_ctx(tokenizer_context_t *ctx, const char *name);
const char *tk_filename_lookup_ctx(
    const tokenizer_context_t *ctx, uint16_t id);
void tk_filename_reset_translation_unit_ctx(tokenizer_context_t *ctx);
/** @brief Return whether strict C11 mode is enabled in the context. */
bool tk_ctx_get_strict_c11_mode(const tokenizer_context_t *ctx);
/**
 * @brief Enable or disable strict C11 mode in the context.
 * @param ctx Target context.
 * @param strict `true` to enable.
 */
void tk_ctx_set_strict_c11_mode(tokenizer_context_t *ctx, bool strict);
/** @brief Return whether trigraph replacement is enabled in the context. */
bool tk_ctx_get_enable_trigraphs(const tokenizer_context_t *ctx);
/**
 * @brief Enable or disable trigraph replacement in the context.
 * @param ctx Target context.
 * @param enable `true` to enable.
 */
void tk_ctx_set_enable_trigraphs(tokenizer_context_t *ctx, bool enable);
/** @brief Return whether the binary-integer-literal extension is enabled. */
bool tk_ctx_get_enable_binary_literals(const tokenizer_context_t *ctx);
/**
 * @brief Enable or disable the binary-integer-literal extension.
 * @param ctx Target context.
 * @param enable `true` to enable.
 */
void tk_ctx_set_enable_binary_literals(tokenizer_context_t *ctx, bool enable);
/** @brief Return whether C11 audit logging is enabled in the context. */
bool tk_ctx_get_enable_c11_audit_extensions(const tokenizer_context_t *ctx);
/**
 * @brief Enable or disable C11 audit logging in the context.
 * @param ctx Target context.
 * @param enable `true` to enable.
 */
void tk_ctx_set_enable_c11_audit_extensions(tokenizer_context_t *ctx, bool enable);

/** @brief Tokenizer memory-allocation statistics. */
typedef struct {
  size_t alloc_count;
  size_t alloc_bytes;
  size_t peak_alloc_bytes;
} tokenizer_stats_t;

/** @brief Reset tokenizer statistics counters. */
void tk_reset_tokenizer_stats_ctx(tokenizer_context_t *ctx);
/**
 * @brief Return current tokenizer statistics.
 * @return Statistics containing `alloc_count`, `alloc_bytes`, and `peak_alloc_bytes`.
 */
tokenizer_stats_t tk_get_tokenizer_stats_ctx(
    const tokenizer_context_t *ctx);

#endif
