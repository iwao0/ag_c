#include "translation_unit_data_lowering.h"

#include "../parser/parser_public.h"
#include "../tokenizer/literals.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
  ir_data_module_t *module;
  int failed;
} translation_unit_data_lowering_t;

typedef struct {
  unsigned char *bytes;
  int length;
  int capacity;
} byte_writer_t;

static void write_byte(unsigned char byte, void *user) {
  byte_writer_t *writer = user;
  if (!writer || writer->length >= writer->capacity) return;
  writer->bytes[writer->length++] = byte;
}

static void lower_string_literal(string_lit_t *literal, void *user) {
  translation_unit_data_lowering_t *lowering = user;
  if (!lowering || lowering->failed) return;
  psx_string_lit_view_t view = ps_string_lit_view(literal);
  int name_len = view.label ? (int)strlen(view.label) : 0;
  int byte_size = tk_emit_string_literal_bytes(
      view.str, view.len, (int)view.char_width, true, NULL, NULL);
  if (!view.label || name_len <= 0 || byte_size <= 0) {
    lowering->failed = 1;
    return;
  }
  unsigned char *bytes = malloc((size_t)byte_size);
  if (!bytes) {
    lowering->failed = 1;
    return;
  }
  byte_writer_t writer = {bytes, 0, byte_size};
  tk_emit_string_literal_bytes(
      view.str, view.len, (int)view.char_width, true, write_byte, &writer);
  ir_data_object_t *object = ir_data_module_add_object(
      lowering->module, view.label, name_len, IR_DATA_STRING);
  int width = (int)view.char_width;
  if (width <= 0) width = 1;
  if (!object || writer.length != byte_size ||
      !ir_data_object_set_bytes(object, bytes, byte_size)) {
    lowering->failed = 1;
    free(bytes);
    return;
  }
  object->alignment = width;
  object->element_size = width;
  object->is_static = 1;
  object->is_read_only = 1;
  free(bytes);
}

ir_data_module_t *lower_ir_translation_unit_data(void) {
  ir_data_module_t *module = ir_data_module_new();
  if (!module) return NULL;
  translation_unit_data_lowering_t lowering = {module, 0};
  ps_iter_string_literals(lower_string_literal, &lowering);
  if (lowering.failed) {
    ir_data_module_free(module);
    return NULL;
  }
  return module;
}
