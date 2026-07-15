#include "tokenizer.h"
#include <stdlib.h>
#include <string.h>

uint16_t tk_filename_intern_ctx(tokenizer_context_t *ctx, const char *name) {
  if (!ctx || !name) return 0;
  for (uint16_t i = 0; i < ctx->filename_table_count; i++) {
    if (ctx->filename_table[i] == name ||
        !strcmp(ctx->filename_table[i], name))
      return i;
  }
  if (ctx->filename_table_count >= TK_FILENAME_TABLE_CAP) return 0;
  size_t len = strlen(name);
  char *copy = malloc(len + 1);
  if (!copy) return 0;
  memcpy(copy, name, len + 1);
  ctx->filename_table[ctx->filename_table_count] = copy;
  return ctx->filename_table_count++;
}

const char *tk_filename_lookup_ctx(
    const tokenizer_context_t *ctx, uint16_t id) {
  if (!ctx || id >= ctx->filename_table_count) return NULL;
  return ctx->filename_table[id];
}

void tk_filename_reset_translation_unit_ctx(tokenizer_context_t *ctx) {
  if (!ctx) return;
  for (uint16_t i = 0; i < ctx->filename_table_count; i++) {
    free(ctx->filename_table[i]);
    ctx->filename_table[i] = NULL;
  }
  ctx->filename_table_count = 0;
}
