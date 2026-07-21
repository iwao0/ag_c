#ifndef ARCH_WASM32_OBJ_BUFFER_H
#define ARCH_WASM32_OBJ_BUFFER_H

#include <stddef.h>
#include <stdint.h>

typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;

typedef struct {
  ag_diagnostic_context_t *diagnostic_context;
  unsigned char *data;
  uint32_t len;
  uint32_t cap;
  uint32_t max_len;
  int overflow;
} wasm32_obj_buffer_t;

int wasm32_obj_buffer_reserve(
    wasm32_obj_buffer_t *buffer, size_t additional_bytes);
void wasm32_obj_buffer_u8(wasm32_obj_buffer_t *buffer, unsigned value);
void wasm32_obj_buffer_bytes(
    wasm32_obj_buffer_t *buffer, const void *bytes, size_t byte_count);
void wasm32_obj_buffer_u32le(
    wasm32_obj_buffer_t *buffer, uint32_t value);
void wasm32_obj_buffer_uleb(
    wasm32_obj_buffer_t *buffer, uint32_t value);
void wasm32_obj_buffer_sleb(
    wasm32_obj_buffer_t *buffer, int64_t value);
uint32_t wasm32_obj_buffer_uleb5(
    wasm32_obj_buffer_t *buffer, uint32_t value);
void wasm32_obj_buffer_patch_uleb5(
    unsigned char *destination, uint32_t value);
void wasm32_obj_buffer_string(
    wasm32_obj_buffer_t *buffer, const char *string, int length);
void wasm32_obj_buffer_section(
    wasm32_obj_buffer_t *output, int section_id,
    const wasm32_obj_buffer_t *payload);
void wasm32_obj_buffer_custom_section(
    wasm32_obj_buffer_t *output, const char *name,
    const wasm32_obj_buffer_t *payload);

#endif
