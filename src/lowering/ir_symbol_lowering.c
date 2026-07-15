#include "ir_symbol_lowering.h"

#include "abi_lowering.h"
#include "../parser/gvar_public.h"
#include "../parser/symtab.h"
#include "../target_info.h"
#include "../type_layout.h"

typedef struct {
  ir_symbol_t *symbol;
  global_var_t *global;
  const psx_semantic_type_table_t *semantic_types;
  const psx_record_layout_table_t *record_layouts;
  const ag_target_info_t *target;
} ir_symbol_func_ref_lowering_t;

static void lower_func_ref(
    ir_symbol_func_ref_lowering_t *ctx, int offset,
    psx_gvar_init_value_t value, psx_type_id_t type_id) {
  if (!ctx) return;
  if (value.kind != PSX_GVAR_INIT_VALUE_SYMBOL) return;
  char *name = NULL;
  int name_len = 0;
  if (!ps_gvar_symbol_ref_named(value.symbol_ref, &name, &name_len)) return;
  ir_callable_sig_t callable_sig = {0};
  ir_abi_type_context_t abi = {
      .semantic_types = ctx->semantic_types,
      .record_layouts = ctx->record_layouts,
      .target = ctx->target,
  };
  if (!ir_abi_callable_sig_from_type_id(
          &abi, type_id, &callable_sig)) return;
  ir_symbol_add_func_ref(
      ctx->symbol, offset, name, name_len, &callable_sig);
}

static void lower_aggregate_scalar(void *user, const tag_member_info_t *member,
                                   psx_type_id_t value_type_id,
                                   int slot, long long offset) {
  ir_symbol_func_ref_lowering_t *ctx = user;
  int value_size = ps_type_sizeof_id_with_records(
      ctx->semantic_types, ctx->record_layouts, value_type_id,
      ctx->target);
  psx_gvar_init_value_t value =
      ps_gvar_init_member_value(
          ctx->global, slot, member, value_size);
  lower_func_ref(ctx, (int)offset, value, value_type_id);
}

static void ignore_bitfield_unit(void *user,
                                 const psx_gvar_bitfield_unit_t *unit,
                                 long long base_offset) {
  (void)user;
  (void)unit;
  (void)base_offset;
}

static void ignore_bitfield_member(void *user,
                                   const tag_member_info_t *member,
                                   psx_type_id_t value_type_id,
                                   int slot, long long offset) {
  (void)user;
  (void)member;
  (void)value_type_id;
  (void)slot;
  (void)offset;
}

static const psx_gvar_aggregate_walk_ops_t func_ref_walk_ops = {
    .scalar = lower_aggregate_scalar,
    .bitfield_unit = ignore_bitfield_unit,
    .bitfield_member = ignore_bitfield_member,
};

ir_symbol_t *lower_ir_global_symbol(
    ir_module_t *module, global_var_t *global,
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target) {
  if (!module || !global || !semantic_types || !record_decls ||
      !record_layouts || !target || !global->name || global->name_len <= 0)
    return NULL;
  const char *name = global->name;
  int name_len = global->name_len;
  ir_symbol_t *symbol = ir_module_find_symbol(module, name, name_len);
  if (symbol) return symbol;

  psx_type_id_t type_id = ps_gvar_decl_type_id(global);
  if (!psx_semantic_type_table_lookup(semantic_types, type_id)) return NULL;
  int storage_size = ps_type_sizeof_id_with_records(
      semantic_types, record_layouts, type_id, target);
  if (storage_size <= 0 && ps_gvar_is_extern_decl(global)) {
    psx_type_id_t base_type_id = psx_semantic_type_table_base(
        semantic_types, type_id).type_id;
    storage_size = ps_type_sizeof_id_with_records(
        semantic_types, record_layouts, base_type_id, target);
  }
  int alignment = ps_type_alignof_id_with_records(
      semantic_types, record_layouts, type_id, target);
  if (storage_size <= 0 || alignment <= 0) return NULL;

  symbol = ir_module_add_symbol(module, name, name_len);
  if (!symbol) return NULL;
  symbol->byte_size = storage_size;
  symbol->alignment = alignment;
  symbol->is_extern = ps_gvar_is_extern_decl(global) ? 1 : 0;
  symbol->is_static = ps_gvar_is_static_storage(global) ? 1 : 0;
  symbol->is_thread_local = ps_gvar_is_thread_local(global) ? 1 : 0;

  ir_symbol_func_ref_lowering_t ctx = {
      .symbol = symbol,
      .global = global,
      .semantic_types = semantic_types,
      .record_layouts = record_layouts,
      .target = target,
  };
  psx_gvar_init_value_t scalar =
      ps_gvar_init_scalar_value(global, symbol->byte_size);
  lower_func_ref(&ctx, 0, scalar, type_id);
  if (ps_gvar_has_aggregate_initializer(global)) {
    ps_gvar_walk_resolved_aggregate_initializer(
        semantic_types, record_decls, record_layouts, target, type_id,
        global, 0, &func_ref_walk_ops, &ctx);
  }
  return symbol;
}
