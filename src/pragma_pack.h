#ifndef PRAGMA_PACK_H
#define PRAGMA_PACK_H

typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;

// Operations called by the preprocessor.
void pragma_pack_push_in(
    psx_parser_runtime_context_t *ctx, int alignment);
void pragma_pack_pop_in(psx_parser_runtime_context_t *ctx);
void pragma_pack_set_in(
    psx_parser_runtime_context_t *ctx, int alignment);
void pragma_pack_reset_in(psx_parser_runtime_context_t *ctx);
// Current #pragma pack alignment (0 = natural alignment).
int pragma_pack_current_alignment_in(
    const psx_parser_runtime_context_t *ctx);

#endif
