#include "ir_symbol_lowering.h"

#include "abi_lowering.h"
#include "../parser/parser_public.h"

typedef struct {
  ir_symbol_t *symbol;
  global_var_t *global;
} ir_symbol_func_ref_lowering_t;

static int symbol_alignment(int size) {
  if (size >= 8) return 8;
  if (size >= 4) return 4;
  if (size >= 2) return 2;
  return 1;
}

static void lower_func_ref(ir_symbol_t *symbol, int offset,
                           psx_gvar_init_value_t value,
                           const psx_type_t *type) {
  char *name = NULL;
  int name_len = 0;
  if (!ps_gvar_init_value_named_function(value, &name, &name_len)) return;
  ir_callable_sig_t callable_sig = {0};
  const ir_callable_sig_t *sig = NULL;
  if (ir_abi_callable_sig_from_type(type, &callable_sig)) sig = &callable_sig;
  ir_symbol_add_func_ref(symbol, offset, name, name_len, sig);
}

static void lower_aggregate_scalar(void *user, const tag_member_info_t *member,
                                   int slot, long long offset) {
  ir_symbol_func_ref_lowering_t *ctx = user;
  psx_gvar_init_value_t value =
      ps_gvar_init_member_value(ctx->global, slot, member);
  lower_func_ref(ctx->symbol, (int)offset, value,
                 ps_tag_member_decl_type(member));
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
                                   int slot, long long base_offset) {
  (void)user;
  (void)member;
  (void)slot;
  (void)base_offset;
}

static const psx_gvar_aggregate_walk_ops_t func_ref_walk_ops = {
    .scalar = lower_aggregate_scalar,
    .bitfield_unit = ignore_bitfield_unit,
    .bitfield_member = ignore_bitfield_member,
};

ir_symbol_t *lower_ir_global_symbol(ir_module_t *module,
                                    const char *name, int name_len) {
  if (!module || !name || name_len <= 0) return NULL;
  ir_symbol_t *symbol = ir_module_find_symbol(module, name, name_len);
  if (symbol) return symbol;

  global_var_t *global = ps_find_global_var((char *)name, name_len);
  if (!global) return NULL;
  symbol = ir_module_add_symbol(module, name, name_len);
  if (!symbol) return NULL;
  symbol->byte_size = ps_gvar_storage_size(global, 8);
  symbol->alignment = symbol_alignment(symbol->byte_size);
  symbol->is_extern = ps_gvar_is_extern_decl(global) ? 1 : 0;
  symbol->is_static = ps_gvar_is_static_storage(global) ? 1 : 0;
  symbol->is_thread_local = ps_gvar_is_thread_local(global) ? 1 : 0;

  psx_gvar_init_value_t scalar =
      ps_gvar_init_scalar_value(global, symbol->byte_size);
  lower_func_ref(symbol, 0, scalar, ps_gvar_get_decl_type(global));

  ir_symbol_func_ref_lowering_t ctx = {
      .symbol = symbol,
      .global = global,
  };
  if (ps_gvar_has_aggregate_initializer(global)) {
    ps_gvar_walk_aggregate_initializer(global, 0, &func_ref_walk_ops, &ctx);
  }
  return symbol;
}
