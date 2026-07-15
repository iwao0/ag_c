#include "translation_unit_data_lowering.h"

#include "abi_lowering.h"
#include "../parser/global_registry.h"
#include "../parser/gvar_public.h"
#include "../parser/literal_public.h"
#include "../parser/semantic_ctx.h"
#include "../parser/type.h"
#include "../semantic/initializer_resolution.h"
#include "../type_layout.h"
#include "../tokenizer/literals.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  ir_data_module_t *module;
  psx_semantic_context_t *semantic_context;
  const psx_semantic_type_table_t *semantic_types;
  const psx_record_decl_table_t *record_decls;
  const psx_record_layout_table_t *record_layouts;
  const ag_target_info_t *target;
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
  const psx_initializer_scalar_leaf_list_t *leaves;
} global_data_lowering_t;

static int type_size_id(
    const translation_unit_data_lowering_t *lowering,
    psx_type_id_t type_id) {
  return ps_type_sizeof_id_with_records(
      lowering ? lowering->semantic_types : NULL,
      lowering ? lowering->record_layouts : NULL,
      type_id,
      lowering ? lowering->target : NULL);
}

static int type_alignment_id(
    const translation_unit_data_lowering_t *lowering,
    psx_type_id_t type_id) {
  return ps_type_alignof_id_with_records(
      lowering ? lowering->semantic_types : NULL,
      lowering ? lowering->record_layouts : NULL,
      type_id,
      lowering ? lowering->target : NULL);
}

static psx_type_id_t scalar_element_type_id(
    const translation_unit_data_lowering_t *lowering,
    psx_type_id_t type_id) {
  if (!lowering || !lowering->semantic_types) return PSX_TYPE_ID_INVALID;
  const psx_type_t *type = psx_semantic_type_table_lookup(
      lowering->semantic_types, type_id);
  while (type && type->kind == PSX_TYPE_ARRAY) {
    type_id = psx_semantic_type_table_base(
        lowering->semantic_types, type_id).type_id;
    type = psx_semantic_type_table_lookup(
        lowering->semantic_types, type_id);
  }
  return type ? type_id : PSX_TYPE_ID_INVALID;
}

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
                              psx_type_id_t callable_type_id) {
  char *name = NULL;
  int name_len = 0;
  ir_data_reloc_kind_t kind = IR_DATA_RELOC_DATA;
  ir_callable_sig_t callable_sig = {0};
  const ir_callable_sig_t *sig = NULL;
  if (value.symbol_ref.kind == PSX_GVAR_SYMBOL_REF_STRING_LITERAL) {
    name = value.symbol_ref.symbol;
    name_len = name ? (int)strlen(name) : 0;
  } else if (ps_gvar_symbol_ref_named_function_in(
          ctx->lowering->semantic_context,
          value.symbol_ref, &name, &name_len)) {
    kind = IR_DATA_RELOC_FUNCTION;
    ir_abi_type_context_t abi = {
        .semantic_types = ctx->lowering->semantic_types,
        .record_layouts = ctx->lowering->record_layouts,
        .target = ctx->lowering->target,
    };
    if (ir_abi_callable_sig_from_type_id(
            &abi, callable_type_id, &callable_sig))
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
                            psx_type_id_t value_type_id) {
  int target_size = type_size_id(ctx->lowering, value_type_id);
  if (target_size <= 0) return 0;
  value.size = target_size;
  if (value.kind == PSX_GVAR_INIT_VALUE_SYMBOL) {
    return lower_symbol_reloc(ctx, offset, value, value_type_id);
  }
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
  const psx_initializer_scalar_leaf_t *leaf =
      ctx->leaves && index < ctx->leaves->count
          ? &ctx->leaves->items[index]
          : NULL;
  int offset = leaf ? leaf->relative_offset : index * layout->elem_size;
  if (leaf) {
    int size = type_size_id(ctx->lowering, leaf->type_id);
    if (size > 0) value.size = size;
  }
  if (!lower_init_value(ctx, offset, value,
                        leaf ? leaf->type_id
                             : ps_gvar_decl_type_id(ctx->global))) {
    ctx->lowering->failed = 1;
    return 0;
  }
  return 1;
}

static void lower_aggregate_scalar(
    void *user, const psx_record_member_decl_t *member,
    psx_type_id_t value_type_id, int slot, long long offset) {
  global_data_lowering_t *ctx = user;
  psx_gvar_init_member_value_t value =
      ps_gvar_init_member_value(
          ctx->global, slot, member,
          type_size_id(ctx->lowering, value_type_id));
  if (offset < 0 || offset > INT32_MAX ||
      !lower_init_value(ctx, (int)offset, value, value_type_id))
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
    void *user, const psx_record_member_decl_t *member,
    const psx_record_member_layout_t *layout,
    psx_type_id_t value_type_id, int slot,
    long long offset) {
  global_data_lowering_t *ctx = user;
  unsigned long long packed = ps_gvar_init_slot_bitfield_bits(
      ctx->global, slot, member->bit_width, layout->bit_offset);
  int size = type_size_id(ctx->lowering, value_type_id);
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
  return ps_gvar_walk_resolved_aggregate_initializer(
      ctx->lowering->semantic_types, ctx->lowering->record_decls,
      ctx->lowering->record_layouts, ctx->lowering->target,
      ps_gvar_decl_type_id(ctx->global), ctx->global, 0,
      &aggregate_lowering_ops, ctx);
}

static int lower_global_slots(
    void *user, const psx_gvar_init_slots_layout_t *layout,
    const psx_gvar_initializer_class_t *init_class) {
  (void)init_class;
  global_data_lowering_t *ctx = user;
  psx_initializer_scalar_leaf_list_t leaves = {0};
  psx_type_id_t type_id = ps_gvar_decl_type_id(ctx->global);
  if (!psx_collect_initializer_scalar_leaves_with_records(
          ctx->lowering->semantic_types,
          ctx->lowering->record_decls, ctx->lowering->record_layouts,
          ctx->lowering->target,
          type_id, 0, &leaves)) {
    return 0;
  }
  ctx->leaves = &leaves;
  int result = ps_gvar_walk_init_slot_values(
      ctx->global, layout, leaves.count, lower_init_slot, ctx);
  ctx->leaves = NULL;
  psx_initializer_scalar_leaf_list_dispose(&leaves);
  return result;
}

static int lower_global_scalar(
    void *user, psx_gvar_init_scalar_value_t value,
    const psx_gvar_initializer_class_t *init_class) {
  global_data_lowering_t *ctx = user;
  if (!init_class->has_payload) return 1;
  return lower_init_value(
      ctx, 0, value, ps_gvar_decl_type_id(ctx->global));
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
  const psx_type_t *type = ps_gvar_get_decl_type(global);
  int storage_size = type_size_id(
      lowering, ps_gvar_decl_type_id(global));
  int is_extern = ps_gvar_is_extern_decl(global) ? 1 : 0;
  if (storage_size <= 0 && is_extern && type &&
      type->kind == PSX_TYPE_ARRAY) {
    storage_size = type_size_id(
        lowering, psx_semantic_type_table_base(
                      lowering->semantic_types,
                      ps_gvar_decl_type_id(global)).type_id);
  }
  int alignment = type_alignment_id(
      lowering, ps_gvar_decl_type_id(global));
  ir_data_object_t *object = ir_data_module_add_object(
      lowering->module, name, name_len, IR_DATA_OBJECT);
  if (!object || storage_size <= 0 || alignment <= 0) {
    lowering->failed = 1;
    return;
  }
  object->byte_size = storage_size;
  object->alignment = alignment;
  object->is_extern = is_extern;
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
  psx_type_id_t scalar_element_id = scalar_element_type_id(
      lowering, ps_gvar_decl_type_id(global));
  int slot_element_size = type_size_id(lowering, scalar_element_id);
  int slot_element_count =
      slot_element_size > 0 ? storage_size / slot_element_size : 0;
  if (!ps_gvar_visit_initializer_classified(
          global, &init_class, storage_size,
          slot_element_size, slot_element_count,
          &global_lowering_ops, &global_lowering))
    lowering->failed = 1;
}

static ir_data_module_t *lower_ir_translation_unit_data_in_registry(
    psx_semantic_context_t *semantic_context,
    const psx_semantic_type_table_t *semantic_types,
    const ag_target_info_t *target,
    psx_global_registry_t *registry) {
  if (!semantic_context || !semantic_types || !target || !registry)
    return NULL;
  ir_data_module_t *module = ir_data_module_new();
  if (!module) return NULL;
  translation_unit_data_lowering_t lowering = {
      .module = module,
      .semantic_context = semantic_context,
      .semantic_types = semantic_types,
      .record_decls = ps_ctx_record_decl_table_in(semantic_context),
      .record_layouts = ps_ctx_record_layout_table_in(semantic_context),
      .target = target,
  };
  ps_iter_string_literals_in(registry, lower_string_literal, &lowering);
  ps_iter_float_literals_in(registry, lower_float_literal, &lowering);
  ps_iter_globals_in(registry, lower_global_object, &lowering);
  if (lowering.failed) {
    ir_data_module_free(module);
    return NULL;
  }
  return module;
}

ir_data_module_t *lower_ir_translation_unit_data_in_session(
    const ag_compilation_session_t *session) {
  if (!ag_compilation_session_is_complete(session)) return NULL;
  return lower_ir_translation_unit_data_in_registry(
      ag_compilation_session_semantic_context(session),
      ps_ctx_semantic_type_table_in(
          ag_compilation_session_semantic_context(session)),
      ag_compilation_session_target(session),
      ag_compilation_session_global_registry(session));
}
