#include "backend_context.h"

#include "wasm32_ir.h"
#include "wasm32_obj.h"
#include <stdlib.h>

struct wasm32_backend_context_t {
  wasm32_ir_context_t *ir;
  wasm32_ir_context_t *previous_ir;
  wasm32_obj_context_t *obj;
  wasm32_obj_context_t *previous_obj;
  int is_active;
};

wasm32_backend_context_t *wasm32_backend_context_create(
    ag_codegen_emit_context_t *emit_context) {
  if (!emit_context) return NULL;
  wasm32_backend_context_t *ctx = calloc(1, sizeof(*ctx));
  if (!ctx) return NULL;
  ctx->ir = wasm32_ir_context_create(emit_context);
  ctx->obj = wasm32_obj_context_create();
  if (!ctx->ir || !ctx->obj) {
    wasm32_ir_context_destroy(ctx->ir);
    wasm32_obj_context_destroy(ctx->obj);
    free(ctx);
    return NULL;
  }
  return ctx;
}

void wasm32_backend_context_activate(void *context) {
  wasm32_backend_context_t *ctx = context;
  if (!ctx || ctx->is_active) return;
  ctx->previous_ir = wasm32_ir_context_activate(ctx->ir);
  ctx->previous_obj = wasm32_obj_context_activate(ctx->obj);
  ctx->is_active = 1;
}

void wasm32_backend_context_deactivate(void *context) {
  wasm32_backend_context_t *ctx = context;
  if (!ctx || !ctx->is_active) return;
  wasm32_obj_context_activate(ctx->previous_obj);
  wasm32_ir_context_activate(ctx->previous_ir);
  ctx->previous_obj = NULL;
  ctx->previous_ir = NULL;
  ctx->is_active = 0;
}

void wasm32_backend_context_destroy(void *context) {
  wasm32_backend_context_t *ctx = context;
  if (!ctx) return;
  wasm32_backend_context_deactivate(ctx);
  wasm32_ir_context_destroy(ctx->ir);
  wasm32_obj_context_destroy(ctx->obj);
  free(ctx);
}
