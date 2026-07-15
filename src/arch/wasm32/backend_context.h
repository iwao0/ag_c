#ifndef AG_WASM32_BACKEND_CONTEXT_H
#define AG_WASM32_BACKEND_CONTEXT_H

typedef struct wasm32_backend_context_t wasm32_backend_context_t;
typedef struct ag_codegen_emit_context_t ag_codegen_emit_context_t;

wasm32_backend_context_t *wasm32_backend_context_create(
    ag_codegen_emit_context_t *emit_context);
void wasm32_backend_context_activate(void *context);
void wasm32_backend_context_deactivate(void *context);
void wasm32_backend_context_destroy(void *context);

#endif
