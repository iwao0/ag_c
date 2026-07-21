#include "wasm32_obj_buffer.h"

#include "../../diag/diag.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static void *buffer_realloc(
    ag_diagnostic_context_t *diagnostics, void *pointer, size_t size) {
  void *result = realloc(pointer, size);
  if (!result)
    diag_emit_internalf_in(
        diagnostics, DIAG_ERR_INTERNAL_OOM, "%s",
        diag_message_for_in(diagnostics, DIAG_ERR_INTERNAL_OOM));
  return result;
}

int wasm32_obj_buffer_reserve(
    wasm32_obj_buffer_t *buffer, size_t additional_bytes) {
  if (additional_bytes > UINT32_MAX ||
      buffer->len > UINT32_MAX - (uint32_t)additional_bytes) {
    buffer->overflow = 1;
    return 0;
  }
  uint32_t required = buffer->len + (uint32_t)additional_bytes;
  if (buffer->max_len && required > buffer->max_len) {
    buffer->overflow = 1;
    return 0;
  }
  if (required <= buffer->cap) return 1;
  uint32_t capacity = buffer->cap
      ? (buffer->cap > UINT32_MAX / 2
             ? UINT32_MAX : buffer->cap * 2)
      : 128;
  while (capacity < required && capacity <= UINT32_MAX / 2)
    capacity *= 2;
  if (capacity < required) capacity = required;
  if (buffer->max_len && capacity > buffer->max_len)
    capacity = buffer->max_len;
  buffer->data = buffer_realloc(
      buffer->diagnostic_context, buffer->data, capacity);
  buffer->cap = capacity;
  return 1;
}

void wasm32_obj_buffer_u8(
    wasm32_obj_buffer_t *buffer, unsigned value) {
  if (!wasm32_obj_buffer_reserve(buffer, 1)) return;
  buffer->data[buffer->len++] = (unsigned char)value;
}

void wasm32_obj_buffer_bytes(
    wasm32_obj_buffer_t *buffer, const void *bytes, size_t byte_count) {
  if (byte_count == 0) return;
  if (!wasm32_obj_buffer_reserve(buffer, byte_count)) return;
  memcpy(buffer->data + buffer->len, bytes, byte_count);
  buffer->len += (uint32_t)byte_count;
}

void wasm32_obj_buffer_u32le(
    wasm32_obj_buffer_t *buffer, uint32_t value) {
  wasm32_obj_buffer_u8(buffer, value & 0xff);
  wasm32_obj_buffer_u8(buffer, (value >> 8) & 0xff);
  wasm32_obj_buffer_u8(buffer, (value >> 16) & 0xff);
  wasm32_obj_buffer_u8(buffer, (value >> 24) & 0xff);
}

void wasm32_obj_buffer_uleb(
    wasm32_obj_buffer_t *buffer, uint32_t value) {
  do {
    unsigned char byte = (unsigned char)(value & 0x7f);
    value >>= 7;
    if (value) byte |= 0x80;
    wasm32_obj_buffer_u8(buffer, byte);
  } while (value);
}

void wasm32_obj_buffer_sleb(
    wasm32_obj_buffer_t *buffer, int64_t value) {
  int more = 1;
  while (more) {
    unsigned char byte = (unsigned char)(value & 0x7f);
    int sign = byte & 0x40;
    uint64_t shifted = (uint64_t)value >> 7;
    if (value < 0) shifted |= (~(uint64_t)0) << (64 - 7);
    value = (int64_t)shifted;
    if ((value == 0 && !sign) || (value == -1 && sign))
      more = 0;
    else
      byte |= 0x80;
    wasm32_obj_buffer_u8(buffer, byte);
  }
}

uint32_t wasm32_obj_buffer_uleb5(
    wasm32_obj_buffer_t *buffer, uint32_t value) {
  uint32_t offset = buffer->len;
  for (int index = 0; index < 5; index++) {
    unsigned char byte = (unsigned char)(value & 0x7f);
    value >>= 7;
    if (index != 4) byte |= 0x80;
    wasm32_obj_buffer_u8(buffer, byte);
  }
  return offset;
}

void wasm32_obj_buffer_patch_uleb5(
    unsigned char *destination, uint32_t value) {
  for (int index = 0; index < 5; index++) {
    unsigned char byte = (unsigned char)(value & 0x7f);
    value >>= 7;
    if (index != 4) byte |= 0x80;
    destination[index] = byte;
  }
}

void wasm32_obj_buffer_string(
    wasm32_obj_buffer_t *buffer, const char *string, int length) {
  wasm32_obj_buffer_uleb(buffer, (uint32_t)length);
  wasm32_obj_buffer_bytes(buffer, string, (size_t)length);
}

void wasm32_obj_buffer_section(
    wasm32_obj_buffer_t *output, int section_id,
    const wasm32_obj_buffer_t *payload) {
  wasm32_obj_buffer_u8(output, (unsigned)section_id);
  wasm32_obj_buffer_uleb(output, payload->len);
  wasm32_obj_buffer_bytes(output, payload->data, payload->len);
}

void wasm32_obj_buffer_custom_section(
    wasm32_obj_buffer_t *output, const char *name,
    const wasm32_obj_buffer_t *payload) {
  wasm32_obj_buffer_t section = {
      .diagnostic_context = output->diagnostic_context};
  wasm32_obj_buffer_string(&section, name, (int)strlen(name));
  wasm32_obj_buffer_bytes(
      &section, payload->data, payload->len);
  wasm32_obj_buffer_section(output, 0, &section);
  free(section.data);
}
