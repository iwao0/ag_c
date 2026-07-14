#include "translation_unit_data_lowering.h"

#include "../ir/abi_lowering.h"
#include "../parser/parser_public.h"
#include "../tokenizer/literals.h"
#include <stdint.h>
#include <stdio.h>
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

typedef struct {
  translation_unit_data_lowering_t *lowering;
  ir_data_object_t *object;
  global_var_t *global;
} global_data_lowering_t;

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

static void lower_float_literal(float_lit_t *literal, void *user) {
  translation_unit_data_lowering_t *lowering = user;
  if (!lowering || lowering->failed) return;
  psx_float_lit_view_t view = ps_float_lit_view(literal);
  char name[32];
  int name_len = snprintf(name, sizeof(name), ".LCF%d", view.id);
  unsigned char bytes[8] = {0};
  int byte_size = view.fp_kind == TK_FLOAT_KIND_FLOAT ? 4 : 8;
  uint64_t bits = 0;
  if (byte_size == 4) {
    union {
      float value;
      uint32_t bits;
    } encoded = {(float)view.fval};
    bits = encoded.bits;
  } else {
    union {
      double value;
      uint64_t bits;
    } encoded = {view.fval};
    bits = encoded.bits;
  }
  for (int i = 0; i < byte_size; i++)
    bytes[i] = (unsigned char)(bits >> (8 * i));
  ir_data_object_t *object = ir_data_module_add_object(
      lowering->module, name, name_len, IR_DATA_FLOAT);
  if (!object || !ir_data_object_set_bytes(object, bytes, byte_size)) {
    lowering->failed = 1;
    return;
  }
  object->alignment = byte_size;
  object->element_size = byte_size;
  object->is_static = 1;
}

static int storage_alignment(const global_var_t *global, int storage_size) {
  const psx_type_t *type = ps_gvar_get_decl_type(global);
  if (type && type->align > 0) return type->align;
  if (storage_size >= 8) return 8;
  if (storage_size >= 4) return 4;
  if (storage_size >= 2) return 2;
  return 1;
}

static int write_bits(ir_data_object_t *object, int offset,
                      unsigned long long value, int size) {
  if (!object || !object->bytes || offset < 0 || size <= 0 ||
      offset > object->byte_size - size)
    return 0;
  for (int i = 0; i < size; i++)
    object->bytes[offset + i] = (unsigned char)(value >> (8 * i));
  return 1;
}

static int lower_symbol_reloc(global_data_lowering_t *ctx, int offset,
                              psx_gvar_init_value_t value,
                              const psx_type_t *callable_type) {
  char *name = NULL;
  int name_len = 0;
  ir_data_reloc_kind_t kind = IR_DATA_RELOC_DATA;
  ir_callable_sig_t callable_sig = {0};
  const ir_callable_sig_t *sig = NULL;
  if (value.symbol_ref.kind == PSX_GVAR_SYMBOL_REF_STRING_LITERAL) {
    name = value.symbol_ref.symbol;
    name_len = name ? (int)strlen(name) : 0;
  } else if (ps_gvar_symbol_ref_named_function(
          value.symbol_ref, &name, &name_len)) {
    kind = IR_DATA_RELOC_FUNCTION;
    if (ir_abi_callable_sig_from_type(callable_type, &callable_sig))
      sig = &callable_sig;
  } else if (!ps_gvar_symbol_ref_named(
                 value.symbol_ref, &name, &name_len)) {
    return 0;
  }
  return ir_data_object_add_reloc(
             ctx->object, offset, value.size, kind, name, name_len,
             value.symbol_ref.addend, sig) != NULL;
}

static int lower_init_value(global_data_lowering_t *ctx, int offset,
                            psx_gvar_init_value_t value,
                            const psx_type_t *callable_type) {
  if (value.kind == PSX_GVAR_INIT_VALUE_SYMBOL)
    return lower_symbol_reloc(ctx, offset, value, callable_type);
  if (value.kind == PSX_GVAR_INIT_VALUE_FLOAT) {
    psx_gvar_fp_bits_t bits;
    if (!ps_gvar_fp_bit_pattern(value.fp_kind, value.fvalue, &bits)) return 0;
    return write_bits(ctx->object, offset, bits.bits, bits.size);
  }
  return write_bits(ctx->object, offset, (unsigned long long)value.value,
                    value.size);
}

static int lower_init_slot(void *user, int index,
                           psx_gvar_init_slot_value_t value,
                           const psx_gvar_init_slots_layout_t *layout) {
  global_data_lowering_t *ctx = user;
  int offset = index * layout->elem_size;
  if (!lower_init_value(ctx, offset, value,
                        ps_gvar_get_decl_type(ctx->global))) {
    ctx->lowering->failed = 1;
    return 0;
  }
  return 1;
}

static void lower_aggregate_scalar(void *user, const tag_member_info_t *member,
                                   int slot, long long offset) {
  global_data_lowering_t *ctx = user;
  psx_gvar_init_member_value_t value =
      ps_gvar_init_member_value(ctx->global, slot, member);
  if (offset < 0 || offset > INT32_MAX ||
      !lower_init_value(ctx, (int)offset, value,
                        ps_tag_member_decl_type(member)))
    ctx->lowering->failed = 1;
}

static void lower_aggregate_bitfield_unit(
    void *user, const psx_gvar_bitfield_unit_t *unit,
    long long base_offset) {
  global_data_lowering_t *ctx = user;
  long long offset = base_offset + unit->offset;
  if (offset < 0 || offset > INT32_MAX ||
      !write_bits(ctx->object, (int)offset, unit->packed, unit->size))
    ctx->lowering->failed = 1;
}

static void lower_aggregate_bitfield_member(
    void *user, const tag_member_info_t *member, int slot,
    long long base_offset) {
  global_data_lowering_t *ctx = user;
  long long offset = base_offset + member->offset;
  unsigned long long packed = ps_gvar_init_slot_bitfield_bits(
      ctx->global, slot, member->bit_width, member->bit_offset);
  int size = ps_tag_member_decl_value_size(member);
  if (offset < 0 || offset > INT32_MAX ||
      !write_bits(ctx->object, (int)offset, packed, size))
    ctx->lowering->failed = 1;
}

static void lower_aggregate_padding(void *user, long long offset, int size) {
  (void)user;
  (void)offset;
  (void)size;
}

static const psx_gvar_aggregate_walk_ops_t aggregate_lowering_ops = {
    .scalar = lower_aggregate_scalar,
    .bitfield_unit = lower_aggregate_bitfield_unit,
    .bitfield_member = lower_aggregate_bitfield_member,
    .padding = lower_aggregate_padding,
};

static int lower_global_aggregate(
    void *user, const psx_gvar_initializer_class_t *init_class) {
  (void)init_class;
  global_data_lowering_t *ctx = user;
  return ps_gvar_walk_aggregate_initializer(
      ctx->global, 0, &aggregate_lowering_ops, ctx);
}

static int lower_global_slots(
    void *user, const psx_gvar_init_slots_layout_t *layout,
    const psx_gvar_initializer_class_t *init_class) {
  (void)init_class;
  global_data_lowering_t *ctx = user;
  return ps_gvar_walk_init_slot_values(
      ctx->global, layout, layout->elem_count, lower_init_slot, ctx);
}

static int lower_global_scalar(
    void *user, psx_gvar_init_scalar_value_t value,
    const psx_gvar_initializer_class_t *init_class) {
  global_data_lowering_t *ctx = user;
  if (!init_class->has_payload) return 1;
  return lower_init_value(
      ctx, 0, value, ps_gvar_get_decl_type(ctx->global));
}

static const psx_gvar_initializer_visit_ops_t global_lowering_ops = {
    .aggregate = lower_global_aggregate,
    .slots = lower_global_slots,
    .scalar = lower_global_scalar,
};

static void lower_global_object(global_var_t *global, void *user) {
  translation_unit_data_lowering_t *lowering = user;
  if (!lowering || lowering->failed) return;
  char *name = ps_gvar_name(global);
  int name_len = ps_gvar_name_len(global);
  int storage_size = ps_gvar_storage_size(global, 4);
  ir_data_object_t *object = ir_data_module_add_object(
      lowering->module, name, name_len, IR_DATA_OBJECT);
  if (!object || storage_size <= 0) {
    lowering->failed = 1;
    return;
  }
  object->byte_size = storage_size;
  object->alignment = storage_alignment(global, storage_size);
  object->is_extern = ps_gvar_is_extern_decl(global) ? 1 : 0;
  object->is_static = ps_gvar_is_static_storage(global) ? 1 : 0;
  object->is_thread_local = ps_gvar_is_thread_local(global) ? 1 : 0;
  object->has_explicit_initializer =
      ps_gvar_has_explicit_initializer(global) ? 1 : 0;
  psx_gvar_initializer_class_t init_class =
      ps_gvar_initializer_class(global, 1);
  if (object->is_extern || !object->has_explicit_initializer) return;

  unsigned char *bytes = calloc((size_t)storage_size, 1);
  if (!bytes || !ir_data_object_set_bytes(object, bytes, storage_size)) {
    free(bytes);
    lowering->failed = 1;
    return;
  }
  free(bytes);
  global_data_lowering_t global_lowering = {
      .lowering = lowering,
      .object = object,
      .global = global,
  };
  if (!ps_gvar_visit_initializer_classified(
          global, &init_class, storage_size,
          &global_lowering_ops, &global_lowering))
    lowering->failed = 1;
}

ir_data_module_t *lower_ir_translation_unit_data(void) {
  ir_data_module_t *module = ir_data_module_new();
  if (!module) return NULL;
  translation_unit_data_lowering_t lowering = {module, 0};
  ps_iter_string_literals(lower_string_literal, &lowering);
  ps_iter_float_literals(lower_float_literal, &lowering);
  ps_iter_globals(lower_global_object, &lowering);
  if (lowering.failed) {
    ir_data_module_free(module);
    return NULL;
  }
  return module;
}
