#ifndef PRAGMA_PACK_H
#define PRAGMA_PACK_H

typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;

// プリプロセッサから呼ばれる操作
void pragma_pack_push_in(
    psx_parser_runtime_context_t *ctx, int alignment);
void pragma_pack_pop_in(psx_parser_runtime_context_t *ctx);
void pragma_pack_set_in(
    psx_parser_runtime_context_t *ctx, int alignment);
void pragma_pack_reset_in(psx_parser_runtime_context_t *ctx);
// #pragma pack の現在のアライメント値（0 = 自然アライメント）
int pragma_pack_current_alignment_in(
    const psx_parser_runtime_context_t *ctx);

#endif
