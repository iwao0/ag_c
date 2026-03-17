#include "parser_switch_ctx.h"
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

void psw_push_ctx(void) {
  switch_ctx_t *ctx = calloc(1, sizeof(switch_ctx_t));
  ctx->next = switch_ctx;
  switch_ctx = ctx;
}

void psw_pop_ctx(void) {
  switch_ctx_t *ctx = switch_ctx;
  if (!ctx) return;
  switch_ctx = ctx->next;
  free(ctx->case_vals);
  free(ctx);
}

int psw_has_ctx(void) {
  return switch_ctx != NULL;
}

void psw_register_case(long long v, token_t *tok) {
  if (!switch_ctx) {
    tk_error_tok(tok, "case は switch 内でのみ使用できます");
  }
  for (int i = 0; i < switch_ctx->ncase; i++) {
    if (switch_ctx->case_vals[i] == v) {
      tk_error_tok(tok, "case %lld が重複しています", v);
    }
  }
  if (switch_ctx->ncase >= switch_ctx->cap) {
    switch_ctx->cap = switch_ctx->cap ? switch_ctx->cap * 2 : 8;
    switch_ctx->case_vals = realloc(switch_ctx->case_vals, sizeof(long long) * (size_t)switch_ctx->cap);
  }
  switch_ctx->case_vals[switch_ctx->ncase++] = v;
}

void psw_register_default(token_t *tok) {
  if (!switch_ctx) {
    tk_error_tok(tok, "default は switch 内でのみ使用できます");
  }
  if (switch_ctx->has_default) {
    tk_error_tok(tok, "default が重複しています");
  }
  switch_ctx->has_default = 1;
}

