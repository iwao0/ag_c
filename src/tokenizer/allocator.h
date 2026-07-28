#ifndef TOKENIZER_ALLOCATOR_PUBLIC_H
#define TOKENIZER_ALLOCATOR_PUBLIC_H

#include <stddef.h>

typedef struct tk_allocator_context_t tk_allocator_context_t;
typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;

/** @brief Create an allocator borrowing the required diagnostic context. */
tk_allocator_context_t *tk_allocator_context_create(
    ag_diagnostic_context_t *diagnostic_context);
void tk_allocator_context_destroy(tk_allocator_context_t *ctx);
ag_diagnostic_context_t *tk_allocator_diagnostics(
    const tk_allocator_context_t *ctx);

/** @brief Set the estimated input size to tune the chunk strategy. */
void tk_allocator_set_expected_size_in(
    tk_allocator_context_t *ctx, size_t bytes);
/** @brief Allocate zero-initialized memory from the tokenizer arena. */
void *tk_allocator_calloc_in(
    tk_allocator_context_t *ctx, size_t n, size_t size);
/** @brief Return the number of allocated chunks. */
size_t tk_allocator_total_chunks_in(const tk_allocator_context_t *ctx);
/** @brief Return the peak number of bytes reserved concurrently. */
size_t tk_allocator_total_reserved_bytes_in(
    const tk_allocator_context_t *ctx);

/* ---- Recyclable arena (token-stream path) ---- */
/** @brief Toggle recyclable mode; when 1, calloc allocates from the recyclable side. */
void tk_allocator_set_recyclable_in(
    tk_allocator_context_t *ctx, int on);
/** @brief Return the current recyclable-mode setting. */
int tk_allocator_recyclable_is_enabled_in(
    const tk_allocator_context_t *ctx);
/** @brief Called on cursor advance to release old recyclable chunks passed by the cursor. */
void tk_allocator_recyc_on_cursor_in(
    tk_allocator_context_t *ctx, const void *cursor);
/** @brief Pin/unpin this position to retain older tokens during _Generic backtracking. */
void tk_allocator_recyc_pin_in(
    tk_allocator_context_t *ctx, const void *p);
void tk_allocator_recyc_unpin_in(tk_allocator_context_t *ctx);
void tk_allocator_recyc_stream_pin_in(
    tk_allocator_context_t *ctx, const void *p);
void tk_allocator_recyc_stream_unpin_in(tk_allocator_context_t *ctx);
/** @brief Release the entire recyclable arena at the end of compilation. */
void tk_allocator_recyc_reset_in(tk_allocator_context_t *ctx);
/** @brief Release both persistent and recyclable arenas at translation-unit start. */
void tk_allocator_reset_translation_unit_in(tk_allocator_context_t *ctx);

#endif
