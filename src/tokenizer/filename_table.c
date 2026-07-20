#include "tokenizer.h"
#include "../source_manager.h"

uint16_t tk_filename_intern_ctx(tokenizer_context_t *ctx, const char *name) {
  return ag_source_manager_intern_name(
      tk_context_source_manager(ctx), name);
}

const char *tk_filename_lookup_ctx(
    const tokenizer_context_t *ctx, uint16_t id) {
  return ag_source_manager_name(tk_context_source_manager(ctx), id);
}

void tk_filename_reset_translation_unit_ctx(tokenizer_context_t *ctx) {
  ag_source_manager_reset_translation_unit(
      tk_context_source_manager(ctx));
}
