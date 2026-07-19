#include "backend_context.h"

#include "../../codegen_emit.h"
#include "wasm32_machine_module.h"
#include "wasm32_ir.h"
#include "wasm32_obj.h"
#include <stdlib.h>

struct wasm32_backend_context_t {
  wasm32_ir_context_t *ir;
  wasm32_obj_context_t *obj;
  wasm32_machine_module_t machine_module;
};

static const wasm32_machine_module_t *build_machine_module(
    wasm32_backend_context_t *ctx, const ir_module_t *module,
    const ir_abi_module_t *abi) {
  wasm32_machine_module_dispose(&ctx->machine_module);
  return wasm32_machine_module_build(
             module, abi, &ctx->machine_module)
             ? &ctx->machine_module : NULL;
}

wasm32_backend_context_t *wasm32_backend_context_create(
    ag_codegen_emit_context_t *emit_context) {
  if (!emit_context) return NULL;
  wasm32_backend_context_t *ctx = calloc(1, sizeof(*ctx));
  if (!ctx) return NULL;
  ctx->ir = wasm32_ir_context_create(emit_context);
  ctx->obj = wasm32_obj_context_create(
      cg_context_diagnostics(emit_context));
  if (!ctx->ir || !ctx->obj) {
    wasm32_ir_context_destroy(ctx->ir);
    wasm32_obj_context_destroy(ctx->obj);
    free(ctx);
    return NULL;
  }
  return ctx;
}

void wasm32_backend_context_destroy(void *context) {
  wasm32_backend_context_t *ctx = context;
  if (!ctx) return;
  wasm32_machine_module_dispose(&ctx->machine_module);
  wasm32_ir_context_destroy(ctx->ir);
  wasm32_obj_context_destroy(ctx->obj);
  free(ctx);
}

void wasm32_backend_wat_begin(wasm32_backend_context_t *ctx) {
  wasm32_module_begin_in(ctx->ir);
}

void wasm32_backend_wat_gen_ir_module(
    wasm32_backend_context_t *ctx, ir_module_t *module,
    const ir_abi_module_t *abi) {
  const wasm32_machine_module_t *machine_module =
      build_machine_module(ctx, module, abi);
  wasm32_gen_machine_module_in(ctx->ir, machine_module);
  wasm32_machine_module_dispose(&ctx->machine_module);
}

void wasm32_backend_wat_emit_data_segments(
    wasm32_backend_context_t *ctx,
    const ir_data_module_t *data_module,
    const ir_abi_data_module_t *data_abi) {
  wasm32_emit_data_segments_in(ctx->ir, data_module, data_abi);
}

void wasm32_backend_wat_end(wasm32_backend_context_t *ctx) {
  wasm32_module_end_in(ctx->ir);
}

void wasm32_backend_obj_set_output_file(
    wasm32_backend_context_t *ctx, FILE *out) {
  wasm32_obj_set_output_file_in(ctx->obj, out);
}

void wasm32_backend_obj_capture_output(
    wasm32_backend_context_t *ctx, int enabled) {
  wasm32_obj_capture_output_in(ctx->obj, enabled);
}

void wasm32_backend_obj_set_capture_limit(
    wasm32_backend_context_t *ctx, size_t max_bytes) {
  wasm32_obj_set_capture_limit_in(ctx->obj, max_bytes);
}

int wasm32_backend_obj_capture_limit_exceeded(
    wasm32_backend_context_t *ctx) {
  return wasm32_obj_capture_limit_exceeded_in(ctx->obj);
}

unsigned char *wasm32_backend_obj_take_output(
    wasm32_backend_context_t *ctx, size_t *out_len) {
  return wasm32_obj_take_output_in(ctx->obj, out_len);
}

void wasm32_backend_obj_begin(wasm32_backend_context_t *ctx) {
  wasm32_obj_begin_in(ctx->obj);
}

void wasm32_backend_obj_gen_ir_module(
    wasm32_backend_context_t *ctx, ir_module_t *module,
    const ir_abi_module_t *abi) {
  const wasm32_machine_module_t *machine_module =
      build_machine_module(ctx, module, abi);
  wasm32_obj_gen_machine_module_in(ctx->obj, machine_module);
  wasm32_machine_module_dispose(&ctx->machine_module);
}

void wasm32_backend_obj_emit_data_segments(
    wasm32_backend_context_t *ctx,
    const ir_data_module_t *data_module,
    const ir_abi_data_module_t *data_abi) {
  wasm32_obj_emit_data_segments_in(ctx->obj, data_module, data_abi);
}

void wasm32_backend_obj_end(wasm32_backend_context_t *ctx) {
  wasm32_obj_end_in(ctx->obj);
}
