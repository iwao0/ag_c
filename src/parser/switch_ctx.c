#include "internal/switch_ctx.h"
#include "internal/diag.h"
#include "internal/dynarray.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include <stdlib.h>

typedef struct switch_ctx_t switch_ctx_t;
struct switch_ctx_t {
  switch_ctx_t *next;
  long long *case_vals;
  int ncase;
  int cap;
  int has_default;
};

static switch_ctx_t *switch_ctx = NULL;

void psx_switch_push_ctx(void) {
  switch_ctx_t *ctx = calloc(1, sizeof(switch_ctx_t));
  ctx->next = switch_ctx;
  switch_ctx = ctx;
}

void psx_switch_pop_ctx(void) {
  switch_ctx_t *ctx = switch_ctx;
  if (!ctx) return;
  switch_ctx = ctx->next;
  free(ctx->case_vals);
  free(ctx);
}

int psx_switch_has_ctx(void) {
  return switch_ctx != NULL;
}

void psx_switch_register_case(long long v, token_t *tok) {
  if (!switch_ctx) {
    psx_diag_only_in(tok, "case", "switch 内");
  }
  for (int i = 0; i < switch_ctx->ncase; i++) {
    if (switch_ctx->case_vals[i] == v) {
      diag_emit_tokf(DIAG_ERR_PARSER_SWITCH_DUPLICATE_CASE, tok,
                     diag_message_for(DIAG_ERR_PARSER_SWITCH_DUPLICATE_CASE), v);
    }
  }
  if (switch_ctx->ncase >= switch_ctx->cap) {
    switch_ctx->cap = pda_next_cap(switch_ctx->cap, switch_ctx->ncase + 1);
    switch_ctx->case_vals = pda_xreallocarray(switch_ctx->case_vals, (size_t)switch_ctx->cap, sizeof(long long));
  }
  switch_ctx->case_vals[switch_ctx->ncase++] = v;
}

void psx_switch_register_default(token_t *tok) {
  if (!switch_ctx) {
    psx_diag_only_in(tok, "default", "switch 内");
  }
  if (switch_ctx->has_default) {
    diag_emit_tokf(DIAG_ERR_PARSER_SWITCH_DUPLICATE_DEFAULT, tok, "%s",
                   diag_message_for(DIAG_ERR_PARSER_SWITCH_DUPLICATE_DEFAULT));
  }
  switch_ctx->has_default = 1;
}
