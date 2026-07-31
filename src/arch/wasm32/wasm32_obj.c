#include "wasm32_obj.h"
#include "wasm32_machine_abi.h"
#include "wasm32_machine_function.h"
#include "wasm32_machine_ir.h"
#include "wasm32_obj_internal.h"
#include "../../diag/diag.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void wasm32_obj_clear_module(obj_ctx_t *obj) {
  if (!obj) return;
  for (int i = 0; i < obj->func_count; i++) {
    free(obj->funcs[i].name);
    free(obj->funcs[i].c_signature);
    free(obj->funcs[i].abi_layout_signature);
    free(obj->funcs[i].sig.params);
    free(obj->funcs[i].body.data);
    free(obj->funcs[i].relocs);
  }
  for (int i = 0; i < obj->data_count; i++) {
    free(obj->data[i].name);
    free(obj->data[i].c_signature);
    free(obj->data[i].abi_layout_signature);
    free(obj->data[i].bytes.data);
    free(obj->data[i].relocs);
  }
  for (int i = 0; i < obj->global_count; i++)
    free(obj->globals[i].name);
  for (int i = 0; i < obj->type_count; i++)
    free(obj->types[i].params);
  free(obj->funcs);
  free(obj->data);
  free(obj->globals);
  free(obj->types);
  free(obj->code_relocs);
  free(obj->data_relocs);
  free(obj->continuation_entry);
  free(obj->continuation_condition);
  free(obj->continuation_step);
  free(obj->continuation_start);
  free(obj->continuation_resume);
  free(obj->continuation_status);
  free(obj->continuation_result);
  memset(obj, 0, sizeof(*obj));
}

wasm32_obj_context_t *wasm32_obj_context_create(
    ag_diagnostic_context_t *diagnostic_context) {
  wasm32_obj_context_t *context = calloc(1, sizeof(*context));
  if (context) {
    context->diagnostic_context = diagnostic_context;
    context->capture.diagnostic_context = diagnostic_context;
  }
  return context;
}

void wasm32_obj_context_destroy(wasm32_obj_context_t *ctx) {
  if (!ctx) return;
  wasm32_obj_clear_module(&ctx->obj);
  free(ctx->capture.data);
  free(ctx);
}

#define g_obj (context->obj)
#define g_obj_capture (context->capture)
#define g_obj_capture_limit (context->capture_limit)
#define g_obj_capture_limit_exceeded \
  (context->capture_limit_exceeded)
#define g_emit_local_types (context->emit_local_types)
#define g_emit_local_unsigned \
  (context->emit_local_unsigned)
#define g_emit_local_count (context->emit_local_count)
#define g_obj_machine_primitives \
  (context->primitives)

static ag_diagnostic_context_t *wasm32_obj_diagnostics(
    wasm32_obj_context_t *context) {
  return context->diagnostic_context;
}

static const char STACK_POINTER_NAME[] = "__stack_pointer";
static const char VA_ARG_AREA_NAME[] = "__ag_va_arg_area";

static void obj_unsupported_inst(
    wasm32_obj_context_t *context,
    const wasm32_machine_inst_t *instruction) {
  diag_emit_internalf_in(wasm32_obj_diagnostics(context), DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP,
                      diag_message_for_in(wasm32_obj_diagnostics(context), DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP),
                      wasm32_machine_inst_kind_name(
                          instruction ? instruction->kind
                                      : WASM32_MACHINE_INST_NONE));
}

static void obj_unsupported_msg(
    wasm32_obj_context_t *context, const char *msg) {
  diag_emit_internalf_in(wasm32_obj_diagnostics(context), DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP,
                      diag_message_for_in(wasm32_obj_diagnostics(context), DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP),
                      msg);
}

static unsigned machine_opcode_binary_or_unsupported(
    wasm32_obj_context_t *context,
    wasm32_machine_opcode_t opcode) {
  unsigned binary = wasm32_machine_opcode_binary(opcode);
  if (!binary)
    obj_unsupported_msg(
        context, "missing preselected Wasm Machine opcode");
  return binary;
}

static unsigned machine_binary_binary_or_unsupported(
    wasm32_obj_context_t *context,
    const wasm32_machine_binary_t *binary) {
  if (!binary)
    obj_unsupported_msg(
        context, "missing preselected Wasm Machine binary operation");
  return machine_opcode_binary_or_unsupported(
      context, binary->opcode);
}

static void *xrealloc(
    ag_diagnostic_context_t *diagnostics, void *p, size_t n) {
  void *q = realloc(p, n);
  if (!q) diag_emit_internalf_in(diagnostics, DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for_in(diagnostics, DIAG_ERR_INTERNAL_OOM));
  return q;
}

static char *dup_name(
    ag_diagnostic_context_t *diagnostics, const char *s, int len) {
  char *p = malloc((size_t)len + 1);
  if (!p) diag_emit_internalf_in(diagnostics, DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for_in(diagnostics, DIAG_ERR_INTERNAL_OOM));
  memcpy(p, s, (size_t)len);
  p[len] = '\0';
  return p;
}

static int name_eq(const char *a, int alen, const char *b, int blen) {
  return alen == blen && a && b && memcmp(a, b, (size_t)alen) == 0;
}

static unsigned wasm_valtype(
    wasm32_obj_context_t *context, ir_type_t t) {
  switch (t) {
    case IR_TY_I8:
    case IR_TY_I16:
    case IR_TY_I32:
    case IR_TY_PTR:
      return 0x7f;
    case IR_TY_I64:
      return 0x7e;
    case IR_TY_F32:
      return 0x7d;
    case IR_TY_F64:
      return 0x7c;
    default:
      obj_unsupported_msg(context, "unsupported Wasm object value type");
  }
  return 0;
}

static ir_type_t wasm_ir_type(ir_type_t t) {
  return wasm32_machine_value_type(t);
}

static obj_data_t *intern_data(
                               wasm32_obj_context_t *context,
                               const char *name, int name_len, int align_log2,
                               int is_static, int is_undefined);

static int align_log2_for_size(int size) {
  int a = 0;
  int v = 1;
  while (v < size) {
    v <<= 1;
    a++;
  }
  return a;
}

static obj_data_t *data_for_machine_inst(
                                    wasm32_obj_context_t *context,
                                    const wasm32_machine_inst_t *inst,
                                    int *out_addend) {
  if (!inst || !inst->sym || inst->sym_len <= 0) return NULL;
  if (out_addend) *out_addend = 0;
  if (inst->kind == WASM32_MACHINE_INST_STRING_ADDRESS)
    return intern_data(context, inst->sym, inst->sym_len, 0, 1, 0);
  const wasm32_machine_symbol_t *symbol = inst->resolved_symbol;
  if (!symbol)
    obj_unsupported_msg(context, "missing resolved IR global symbol in Wasm object mode");
  return intern_data(context, symbol->name, symbol->name_len,
                     align_log2_for_size(symbol->alignment),
                     symbol->is_static, symbol->is_extern);
}

static int sig_equal(const obj_sig_t *a, const obj_sig_t *b) {
  if (a->nparams != b->nparams || wasm_ir_type(a->result) != wasm_ir_type(b->result)) return 0;
  for (int i = 0; i < a->nparams; i++) {
    if (wasm_ir_type(a->params[i]) != wasm_ir_type(b->params[i])) return 0;
  }
  return 1;
}

static int wasm_valtype_is_int(ir_type_t ty) {
  ir_type_t w = wasm_ir_type(ty);
  return w == IR_TY_I32 || w == IR_TY_I64;
}

static int sig_integer_width_compatible(const obj_sig_t *a, const obj_sig_t *b) {
  if (a->nparams != b->nparams) return 0;
  if (wasm_ir_type(a->result) != wasm_ir_type(b->result) &&
      !(wasm_valtype_is_int(a->result) && wasm_valtype_is_int(b->result))) {
    return 0;
  }
  for (int i = 0; i < a->nparams; i++) {
    if (wasm_ir_type(a->params[i]) == wasm_ir_type(b->params[i])) continue;
    if (wasm_valtype_is_int(a->params[i]) && wasm_valtype_is_int(b->params[i])) continue;
    return 0;
  }
  return 1;
}

static int sig_result_equal(
    const obj_sig_t *left, const obj_sig_t *right) {
  return left && right &&
         wasm_ir_type(left->result) ==
             wasm_ir_type(right->result) &&
         left->has_hidden_result ==
             right->has_hidden_result &&
         left->has_direct_aggregate_result ==
             right->has_direct_aggregate_result;
}

static obj_sig_t copy_signature(
    wasm32_obj_context_t *context, const obj_sig_t *source) {
  obj_sig_t copy = *source;
  copy.params = NULL;
  copy.abi_layout_signature = NULL;
  copy.abi_layout_signature_len = 0;
  if (source->nparams > 0) {
    copy.params = xrealloc(
        context->diagnostic_context, NULL,
        (size_t)source->nparams * sizeof(*copy.params));
    memcpy(
        copy.params, source->params,
        (size_t)source->nparams * sizeof(*copy.params));
  }
  return copy;
}

static obj_func_t *find_func(
    wasm32_obj_context_t *context, const char *name, int name_len) {
  for (int i = 0; i < g_obj.func_count; i++) {
    if (name_eq(g_obj.funcs[i].name, g_obj.funcs[i].name_len, name, name_len)) return &g_obj.funcs[i];
  }
  return NULL;
}

static obj_func_t *intern_func(
    wasm32_obj_context_t *context, const char *name, int name_len) {
  obj_func_t *f = find_func(context, name, name_len);
  if (f) return f;
  if (g_obj.func_count == g_obj.func_cap) {
    int ncap = g_obj.func_cap ? g_obj.func_cap * 2 : 32;
    g_obj.funcs = xrealloc(
        context->diagnostic_context, g_obj.funcs,
        (size_t)ncap * sizeof(*g_obj.funcs));
    memset(g_obj.funcs + g_obj.func_cap, 0, (size_t)(ncap - g_obj.func_cap) * sizeof(*g_obj.funcs));
    g_obj.func_cap = ncap;
  }
  f = &g_obj.funcs[g_obj.func_count++];
  f->name = dup_name(context->diagnostic_context, name, name_len);
  f->body.diagnostic_context = context->diagnostic_context;
  f->name_len = name_len;
  f->func_index = -1;
  f->symbol_index = -1;
  f->type_index = -1;
  return f;
}

static obj_data_t *find_data(
    wasm32_obj_context_t *context, const char *name, int name_len) {
  for (int i = 0; i < g_obj.data_count; i++) {
    if (name_eq(g_obj.data[i].name, g_obj.data[i].name_len, name, name_len)) return &g_obj.data[i];
  }
  return NULL;
}

static obj_data_t *intern_data(
                               wasm32_obj_context_t *context,
                               const char *name, int name_len, int align_log2,
                               int is_static, int is_undefined) {
  obj_data_t *d = find_data(context, name, name_len);
  if (d) {
    if (!is_undefined) d->is_undefined = 0;
    if (align_log2 > d->align) d->align = align_log2;
    if (is_static) d->is_static = 1;
    return d;
  }
  if (g_obj.data_count == g_obj.data_cap) {
    int ncap = g_obj.data_cap ? g_obj.data_cap * 2 : 32;
    g_obj.data = xrealloc(
        context->diagnostic_context, g_obj.data,
        (size_t)ncap * sizeof(*g_obj.data));
    memset(g_obj.data + g_obj.data_cap, 0, (size_t)(ncap - g_obj.data_cap) * sizeof(*g_obj.data));
    g_obj.data_cap = ncap;
  }
  d = &g_obj.data[g_obj.data_count++];
  d->name = dup_name(context->diagnostic_context, name, name_len);
  d->bytes.diagnostic_context = context->diagnostic_context;
  d->name_len = name_len;
  d->align = align_log2;
  d->segment_index = -1;
  d->symbol_index = -1;
  d->is_static = is_static;
  d->is_undefined = is_undefined;
  return d;
}

static void reserve_data_capacity(
    wasm32_obj_context_t *context, int min_cap) {
  if (min_cap <= g_obj.data_cap) return;
  int ncap = g_obj.data_cap ? g_obj.data_cap : 32;
  while (ncap < min_cap) ncap *= 2;
  g_obj.data = xrealloc(
      context->diagnostic_context, g_obj.data,
      (size_t)ncap * sizeof(*g_obj.data));
  memset(g_obj.data + g_obj.data_cap, 0,
         (size_t)(ncap - g_obj.data_cap) * sizeof(*g_obj.data));
  g_obj.data_cap = ncap;
}

static void data_note_alloc_size(obj_data_t *d, size_t size) {
  if (d && size > d->alloc_size) d->alloc_size = size;
}

static int data_index(
    wasm32_obj_context_t *context, obj_data_t *d) {
  return (int)(d - g_obj.data);
}

static obj_global_t *find_global_symbol(
    wasm32_obj_context_t *context, const char *name, int name_len) {
  for (int i = 0; i < g_obj.global_count; i++) {
    if (name_eq(g_obj.globals[i].name, g_obj.globals[i].name_len, name, name_len)) {
      return &g_obj.globals[i];
    }
  }
  return NULL;
}

static obj_global_t *intern_global_symbol(
    wasm32_obj_context_t *context, const char *name, int name_len) {
  obj_global_t *g = find_global_symbol(context, name, name_len);
  if (g) return g;
  if (g_obj.global_count == g_obj.global_cap) {
    int ncap = g_obj.global_cap ? g_obj.global_cap * 2 : 4;
    g_obj.globals = xrealloc(
        context->diagnostic_context, g_obj.globals,
        (size_t)ncap * sizeof(*g_obj.globals));
    memset(g_obj.globals + g_obj.global_cap, 0,
           (size_t)(ncap - g_obj.global_cap) * sizeof(*g_obj.globals));
    g_obj.global_cap = ncap;
  }
  g = &g_obj.globals[g_obj.global_count++];
  g->name = dup_name(context->diagnostic_context, name, name_len);
  g->name_len = name_len;
  g->global_index = -1;
  g->symbol_index = -1;
  return g;
}

static obj_global_t *intern_stack_pointer_global(
    wasm32_obj_context_t *context) {
  return intern_global_symbol(
      context, STACK_POINTER_NAME, (int)strlen(STACK_POINTER_NAME));
}

static obj_global_t *intern_va_arg_area_global(
    wasm32_obj_context_t *context) {
  return intern_global_symbol(
      context, VA_ARG_AREA_NAME, (int)strlen(VA_ARG_AREA_NAME));
}

static void func_add_reloc(
                           wasm32_obj_context_t *context,
                           obj_func_t *f, int type, uint32_t body_off, int target_sym,
                           int target_is_data, int addend) {
  if (f->reloc_count == f->reloc_cap) {
    int ncap = f->reloc_cap ? f->reloc_cap * 2 : 8;
    f->relocs = xrealloc(
        context->diagnostic_context, f->relocs,
        (size_t)ncap * sizeof(*f->relocs));
    f->reloc_cap = ncap;
  }
  obj_reloc_t *r = &f->relocs[f->reloc_count++];
  r->body_off = (uint32_t)body_off;
  r->type = type;
  r->target_sym = target_sym;
  r->target_is_data = target_is_data;
  r->target_is_global = 0;
  r->target_is_type = 0;
  r->addend = addend;
}

static void func_add_call_reloc(
    wasm32_obj_context_t *context,
    obj_func_t *f, uint32_t body_off, int target_sym) {
  if (f->reloc_count == f->reloc_cap) {
    int ncap = f->reloc_cap ? f->reloc_cap * 2 : 8;
    f->relocs = xrealloc(
        context->diagnostic_context, f->relocs,
        (size_t)ncap * sizeof(*f->relocs));
    f->reloc_cap = ncap;
  }
  obj_reloc_t *r = &f->relocs[f->reloc_count++];
  r->body_off = body_off;
  r->type = R_WASM_FUNCTION_INDEX_LEB;
  r->target_sym = target_sym;
  r->target_is_data = 0;
  r->target_is_global = 0;
  r->target_is_type = 0;
  r->addend = 0;
}

static void func_add_global_reloc(
    wasm32_obj_context_t *context,
    obj_func_t *f, int type, uint32_t body_off, int target_sym) {
  func_add_reloc(context, f, type, body_off, target_sym, 0, 0);
  f->relocs[f->reloc_count - 1].target_is_global = 1;
}

static void func_add_type_reloc(
    wasm32_obj_context_t *context,
    obj_func_t *f, uint32_t body_off, int type_index) {
  func_add_reloc(
      context, f, R_WASM_TYPE_INDEX_LEB, body_off, type_index, 0, 0);
  f->relocs[f->reloc_count - 1].target_is_type = 1;
}

static void data_add_reloc(
                           wasm32_obj_context_t *context,
                           obj_data_t *d, int type, uint32_t body_off, int target_sym,
                           int target_is_data, int addend) {
  if (d->reloc_count == d->reloc_cap) {
    int ncap = d->reloc_cap ? d->reloc_cap * 2 : 8;
    d->relocs = xrealloc(
        context->diagnostic_context, d->relocs,
        (size_t)ncap * sizeof(*d->relocs));
    d->reloc_cap = ncap;
  }
  obj_reloc_t *r = &d->relocs[d->reloc_count++];
  r->body_off = (uint32_t)body_off;
  r->type = type;
  r->target_sym = target_sym;
  r->target_is_data = target_is_data;
  r->target_is_global = 0;
  r->target_is_type = 0;
  r->addend = addend;
}

static int intern_type(
    wasm32_obj_context_t *context, const obj_sig_t *sig) {
  for (int i = 0; i < g_obj.type_count; i++) {
    if (sig_equal(&g_obj.types[i], sig)) return i;
  }
  if (g_obj.type_count == g_obj.type_cap) {
    int ncap = g_obj.type_cap ? g_obj.type_cap * 2 : 16;
    g_obj.types = xrealloc(
        context->diagnostic_context, g_obj.types,
        (size_t)ncap * sizeof(*g_obj.types));
    memset(g_obj.types + g_obj.type_cap, 0, (size_t)(ncap - g_obj.type_cap) * sizeof(*g_obj.types));
    g_obj.type_cap = ncap;
  }
  obj_sig_t *dst = &g_obj.types[g_obj.type_count];
  dst->nparams = sig->nparams;
  dst->result = wasm_ir_type(sig->result);
  if (sig->nparams > 0) {
    dst->params = xrealloc(
        context->diagnostic_context, NULL,
        (size_t)sig->nparams * sizeof(ir_type_t));
    for (int i = 0; i < sig->nparams; i++) dst->params[i] = wasm_ir_type(sig->params[i]);
  }
  return g_obj.type_count++;
}

static int local_index(int param_count, int vreg) {
  return param_count + vreg;
}

static ir_type_t actual_vreg_type(
    wasm32_obj_context_t *context, ir_val_t v) {
  if (v.id >= 0 && v.id < g_emit_local_count && g_emit_local_types) return g_emit_local_types[v.id];
  return wasm_ir_type(v.type);
}

static int actual_vreg_unsigned(
    wasm32_obj_context_t *context, ir_val_t v) {
  return v.id >= 0 && v.id < g_emit_local_count && g_emit_local_unsigned &&
         g_emit_local_unsigned[v.id];
}

static void emit_local_get(wb_t *b, int idx) {
  wb_u8(b, 0x20);
  wb_uleb(b, (uint32_t)idx);
}

static void emit_local_set(wb_t *b, int idx) {
  wb_u8(b, 0x21);
  wb_uleb(b, (uint32_t)idx);
}

static void emit_const(
    wasm32_obj_context_t *context,
    wb_t *b, ir_type_t type, long long value);
static void emit_memarg(
    wasm32_obj_context_t *context, wb_t *b, ir_type_t ty);
static void emit_selected_memarg(
    wb_t *b, const wasm32_machine_memory_t *selected);
static unsigned load_opcode(
    wasm32_obj_context_t *context, ir_type_t ty, int is_unsigned);
static unsigned store_opcode(
    wasm32_obj_context_t *context, ir_type_t ty);

static void emit_data_address(
                              wasm32_obj_context_t *context,
                              wb_t *b, obj_func_t *of,
                              int data_idx, int addend) {
  wb_u8(b, 0x41);
  uint32_t imm_off = wb_uleb5(b, 0);
  func_add_reloc(context, of, R_WASM_MEMORY_ADDR_LEB, imm_off,
                 data_idx, 1, addend);
}

static void emit_continuation_data_load(
                                        wasm32_obj_context_t *context,
                                        wb_t *b, obj_func_t *of,
                                        int data_idx) {
  emit_data_address(context, b, of, data_idx, 0);
  wb_u8(b, load_opcode(context, IR_TY_I32, 1));
  emit_memarg(context, b, IR_TY_I32);
}

static void emit_continuation_data_store_const(
                                               wasm32_obj_context_t *context,
                                               wb_t *b, obj_func_t *of,
                                               int data_idx, int value) {
  emit_data_address(context, b, of, data_idx, 0);
  emit_const(context, b, IR_TY_I32, value);
  wb_u8(b, store_opcode(context, IR_TY_I32));
  emit_memarg(context, b, IR_TY_I32);
}

static void emit_stack_global_get(
    wasm32_obj_context_t *context,
    wb_t *b, obj_func_t *of, obj_global_t *sp) {
  wb_u8(b, 0x23);
  uint32_t imm_off = wb_uleb5(b, 0);
  func_add_global_reloc(
      context, of, R_WASM_GLOBAL_INDEX_LEB, imm_off,
      (int)(sp - g_obj.globals));
}

static void emit_stack_global_set(
    wasm32_obj_context_t *context,
    wb_t *b, obj_func_t *of, obj_global_t *sp) {
  wb_u8(b, 0x24);
  uint32_t imm_off = wb_uleb5(b, 0);
  func_add_global_reloc(
      context, of, R_WASM_GLOBAL_INDEX_LEB, imm_off,
      (int)(sp - g_obj.globals));
}

static void emit_fp_const(
    wasm32_obj_context_t *context,
    wb_t *b, ir_type_t type, double value);

static void emit_const(
    wasm32_obj_context_t *context,
    wb_t *b, ir_type_t type, long long value) {
  type = wasm_ir_type(type);
  if (type == IR_TY_VOID) type = IR_TY_I32;
  if (type == IR_TY_I64) {
    wb_u8(b, 0x42);
    wb_sleb(b, value);
  } else if (type == IR_TY_I32) {
    uint32_t bits = (uint32_t)value;
    int64_t signed_bits = (bits & 0x80000000u) ? (int64_t)bits - 0x100000000LL : (int64_t)bits;
    wb_u8(b, 0x41);
    wb_sleb(b, signed_bits);
  } else if (type == IR_TY_F32 || type == IR_TY_F64) {
    emit_fp_const(context, b, type, (double)value);
  } else {
    char msg[96];
    snprintf(msg, sizeof(msg), "unsupported immediate type in Wasm object mode: %d", (int)type);
    obj_unsupported_msg(context, msg);
  }
}

static void emit_fp_const(
    wasm32_obj_context_t *context,
    wb_t *b, ir_type_t type, double value) {
  if (type == IR_TY_F32) {
    float f = (float)value;
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    wb_u8(b, 0x43);
    wb_u32le(b, bits);
  } else if (type == IR_TY_F64) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    wb_u8(b, 0x44);
    wb_u32le(b, (uint32_t)(bits & 0xffffffffu));
    wb_u32le(b, (uint32_t)(bits >> 32));
  } else {
    obj_unsupported_msg(
        context, "floating-point immediate type in Wasm object mode");
  }
}

static void emit_conversion_opcode(
    wasm32_obj_context_t *context,
    wb_t *b, ir_type_t source_type, ir_type_t result_type,
    int is_unsigned) {
  const wasm32_machine_conversion_t *selected =
      wasm32_machine_planned_conversion(
          g_obj_machine_primitives,
          wasm_ir_type(source_type), wasm_ir_type(result_type),
          is_unsigned);
  if (!selected)
    obj_unsupported_msg(context, "unsupported Wasm object value conversion");
  if (selected->opcode == WASM32_MI_COPY) return;
  unsigned opcode = wasm32_machine_opcode_binary(selected->opcode);
  if (!opcode)
    obj_unsupported_msg(context, "missing Wasm object conversion opcode");
  wb_u8(b, opcode);
}

static void emit_val(
    wasm32_obj_context_t *context,
    wb_t *b, ir_val_t v, ir_type_t want, int param_count) {
  want = wasm_ir_type(want);
  if (v.id == IR_VAL_IMM) {
    if (want == IR_TY_F32 || want == IR_TY_F64)
      emit_fp_const(context, b, want, v.fp_imm);
    else
      emit_const(context, b, want, v.imm);
    return;
  }
  if (v.id < 0) obj_unsupported_msg(context, "missing Wasm object value");
  ir_type_t got = actual_vreg_type(context, v);
  emit_local_get(b, local_index(param_count, v.id));
  emit_conversion_opcode(
      context, b, got, want, actual_vreg_unsigned(context, v));
}

static void emit_stack_cast(
    wasm32_obj_context_t *context,
    wb_t *b, ir_type_t got, ir_type_t want, int is_unsigned) {
  got = wasm_ir_type(got);
  want = wasm_ir_type(want);
  emit_conversion_opcode(context, b, got, want, is_unsigned);
}

static void emit_addr_val(
    wasm32_obj_context_t *context,
    wb_t *b, ir_val_t v, int param_count) {
  if (v.id == IR_VAL_IMM) {
    emit_const(context, b, IR_TY_I32, v.imm);
    return;
  }
  if (v.id < 0) obj_unsupported_msg(context, "missing Wasm object address");
  emit_local_get(b, local_index(param_count, v.id));
  emit_stack_cast(
      context, b, actual_vreg_type(context, v), IR_TY_I32,
      actual_vreg_unsigned(context, v));
}

static wasm32_machine_memory_t planned_load_or_unsupported(
    wasm32_obj_context_t *context,
    ir_type_t memory_type, int is_unsigned) {
  const wasm32_machine_memory_t *selected =
      wasm32_machine_planned_load(
          g_obj_machine_primitives, memory_type, is_unsigned);
  if (!selected)
    obj_unsupported_msg(context, "unsupported Wasm object load type");
  return *selected;
}

static wasm32_machine_memory_t planned_store_or_unsupported(
    wasm32_obj_context_t *context,
    ir_type_t memory_type) {
  const wasm32_machine_memory_t *selected =
      wasm32_machine_planned_store(
          g_obj_machine_primitives, memory_type);
  if (!selected)
    obj_unsupported_msg(context, "unsupported Wasm object store type");
  return *selected;
}

static unsigned memory_binary_or_unsupported(
    wasm32_obj_context_t *context,
    wasm32_machine_memory_t selected) {
  unsigned opcode = wasm32_machine_opcode_binary(selected.opcode);
  if (!opcode) obj_unsupported_msg(context, "missing Wasm object memory opcode");
  return opcode;
}

static unsigned conversion_binary_or_unsupported(
    wasm32_obj_context_t *context,
    wasm32_machine_conversion_t selected) {
  unsigned opcode = wasm32_machine_opcode_binary(selected.opcode);
  if (!opcode)
    obj_unsupported_msg(context, "missing Wasm object conversion opcode");
  return opcode;
}

static int mem_align_log2(
    wasm32_obj_context_t *context, ir_type_t ty) {
  return (int)planned_store_or_unsupported(context, ty).alignment_log2;
}

static unsigned load_opcode(
    wasm32_obj_context_t *context, ir_type_t ty, int is_unsigned) {
  return memory_binary_or_unsupported(
      context, planned_load_or_unsupported(context, ty, is_unsigned));
}

static void emit_abi_argument(
    wasm32_obj_context_t *context,
    wb_t *b, const wasm32_machine_argument_t *argument,
    ir_type_t want, int param_count) {
  if (!argument) obj_unsupported_msg(context, "missing lowered call argument");
  if (argument->access == WASM32_MACHINE_ARGUMENT_DIRECT) {
    emit_val(context, b, argument->source, want, param_count);
    return;
  }
  if (argument->access != WASM32_MACHINE_ARGUMENT_LOAD ||
      argument->source.type != IR_TY_PTR)
    obj_unsupported_msg(context, "unsupported lowered call argument access");
  emit_addr_val(context, b, argument->source, param_count);
  if (argument->byte_offset != 0) {
    emit_const(context, b, IR_TY_I32, argument->byte_offset);
    wb_u8(
        b, machine_binary_binary_or_unsupported(
               context, &g_obj_machine_primitives->i32_add));
  }
  wb_u8(b, memory_binary_or_unsupported(context, argument->load));
  emit_selected_memarg(b, &argument->load);
  emit_stack_cast(context, b, argument->load.value_type, want, 1);
}

static unsigned store_opcode(
    wasm32_obj_context_t *context, ir_type_t ty) {
  return memory_binary_or_unsupported(
      context, planned_store_or_unsupported(context, ty));
}

static void emit_memarg(
    wasm32_obj_context_t *context, wb_t *b, ir_type_t ty) {
  wb_uleb(b, (uint32_t)mem_align_log2(context, ty));
  wb_uleb(b, 0);
}

static void emit_selected_memarg(
    wb_t *b, const wasm32_machine_memory_t *selected) {
  wb_uleb(b, selected->alignment_log2);
  wb_uleb(b, 0);
}

static obj_sig_t func_sig_from_machine_callable(
    wasm32_obj_context_t *context,
    const wasm32_machine_inst_t *inst, const char *name, int name_len) {
  (void)name;
  (void)name_len;
  if (!inst || !inst->has_reference_signature)
    obj_unsupported_msg(
        context, "missing function-reference ABI in Wasm object mode");
  return copy_signature(context, &inst->reference_signature);
}

typedef struct {
  char kind;
  const char *tag;
  int tag_len;
  int end;
  int has_shape;
  int is_complete;
  int body_start;
  int body_len;
} obj_canonical_record_t;

typedef struct {
  uint32_t bound;
  int element_start;
} obj_canonical_array_t;

static int obj_canonical_decimal(
    const char *signature, int signature_len,
    int *offset, uint32_t *value) {
  if (!signature || !offset || !value ||
      *offset < 0 || *offset >= signature_len ||
      signature[*offset] < '0' ||
      signature[*offset] > '9')
    return 0;
  uint32_t result = 0;
  do {
    uint32_t digit =
        (uint32_t)(signature[*offset] - '0');
    if (result > (UINT32_MAX - digit) / 10u)
      return 0;
    result = result * 10u + digit;
    (*offset)++;
  } while (
      *offset < signature_len &&
      signature[*offset] >= '0' &&
      signature[*offset] <= '9');
  *value = result;
  return 1;
}

static int obj_canonical_array_at(
    const char *signature, int signature_len,
    int offset, obj_canonical_array_t *parsed) {
  if (!signature || !parsed || offset < 0 ||
      offset >= signature_len ||
      signature[offset] != 'a')
    return 0;
  obj_canonical_array_t result = {0};
  int index = offset + 1;
  if (!obj_canonical_decimal(
          signature, signature_len,
          &index, &result.bound) ||
      index >= signature_len ||
      signature[index++] != '<')
    return 0;
  result.element_start = index;
  *parsed = result;
  return 1;
}

static int obj_canonical_record_at(
    const char *signature, int signature_len,
    int offset, obj_canonical_record_t *parsed) {
  if (!signature || !parsed || offset < 0 ||
      offset + 2 > signature_len ||
      (signature[offset] != 's' &&
       signature[offset] != 'u') ||
      signature[offset + 1] != '{')
    return 0;
  obj_canonical_record_t result = {
      .kind = signature[offset],
  };
  int index = offset + 2;
  uint32_t tag_len = 0;
  if (!obj_canonical_decimal(
          signature, signature_len,
          &index, &tag_len) ||
      index >= signature_len ||
      signature[index++] != ':' ||
      tag_len > (uint32_t)(signature_len - index))
    return 0;
  result.tag = signature + index;
  result.tag_len = (int)tag_len;
  index += result.tag_len;
  if (index >= signature_len ||
      signature[index++] != '}')
    return 0;
  result.end = index;
  if (index >= signature_len ||
      signature[index] != '[') {
    *parsed = result;
    return 1;
  }

  int body_start = index + 1;
  int header_index = body_start;
  uint32_t complete = 0;
  uint32_t member_count = 0;
  if (!obj_canonical_decimal(
          signature, signature_len,
          &header_index, &complete) ||
      complete > 1 ||
      header_index >= signature_len ||
      signature[header_index++] != ':' ||
      !obj_canonical_decimal(
          signature, signature_len,
          &header_index, &member_count))
    return 0;
  (void)member_count;

  int depth = 1;
  int body_end = -1;
  for (int cursor = body_start;
       cursor < signature_len; cursor++) {
    if (signature[cursor] == '[') {
      depth++;
    } else if (signature[cursor] == ']') {
      depth--;
      if (depth == 0) {
        body_end = cursor;
        break;
      }
    }
  }
  if (body_end < 0) return 0;
  result.has_shape = 1;
  result.is_complete = complete != 0;
  result.body_start = body_start;
  result.body_len = body_end - body_start;
  result.end = body_end + 1;
  *parsed = result;
  return 1;
}

static int update_type_refinement(
    int *refinement, int candidate) {
  if (!refinement || candidate == 0) return 0;
  if (*refinement != 0 && *refinement != candidate)
    return 0;
  *refinement = candidate;
  return 1;
}

static int obj_canonical_matching_parenthesis(
    const char *signature, int signature_len,
    int opening_offset, int *closing_offset) {
  if (!signature || opening_offset < 0 ||
      opening_offset >= signature_len ||
      signature[opening_offset] != '(')
    return 0;
  int depth = 0;
  for (int index = opening_offset;
       index < signature_len; index++) {
    if (signature[index] == '(') {
      depth++;
    } else if (signature[index] == ')') {
      depth--;
      if (depth == 0) {
        if (closing_offset) *closing_offset = index;
        return 1;
      }
      if (depth < 0) return 0;
    }
  }
  return 0;
}

static int obj_canonical_parameter_unchanged_by_default_promotions(
    const char *parameter, int length) {
  int index = 0;
  int is_atomic = 0;
  while (index < length &&
         (parameter[index] == 'k' || parameter[index] == 'V' ||
          parameter[index] == 'A' || parameter[index] == 'R')) {
    if (parameter[index] == 'A') is_atomic = 1;
    index++;
  }
  if (index >= length ||
      (length - index == 3 &&
       memcmp(parameter + index, "...", 3) == 0))
    return 0;
  if (is_atomic) return 1;
  char kind = parameter[index++];
  if (kind == 'b' || kind == 'c') return 0;
  if (kind != 'f' && kind != 'i' && kind != 'u') return 1;
  if (kind == 'u' && index < length &&
      (parameter[index] == '{' || parameter[index] == '[' ||
       parameter[index] == 'l'))
    return 1;
  int bits = 0;
  int has_bits = 0;
  while (index < length &&
         parameter[index] >= '0' && parameter[index] <= '9') {
    has_bits = 1;
    bits = bits * 10 + parameter[index++] - '0';
  }
  if (!has_bits) return 1;
  return kind == 'f' ? bits >= 64 : bits >= 32;
}

static int obj_canonical_parameters_unchanged_by_default_promotions(
    const char *signature, int opening_offset,
    int closing_offset) {
  if (!signature || opening_offset < 0 ||
      closing_offset <= opening_offset + 1)
    return 0;
  int parameter_start = opening_offset + 1;
  int depth = 0;
  for (int index = parameter_start;
       index <= closing_offset; index++) {
    if (index == closing_offset) {
      if (depth != 0) return 0;
      return obj_canonical_parameter_unchanged_by_default_promotions(
          signature + parameter_start,
          index - parameter_start);
    }
    char ch = signature[index];
    if (ch == '(' || ch == '<' || ch == '{' || ch == '[') {
      depth++;
    } else if (ch == ')' || ch == '>' || ch == '}' || ch == ']') {
      if (depth == 0) return 0;
      depth--;
    } else if (ch == ',' && depth == 0) {
      if (!obj_canonical_parameter_unchanged_by_default_promotions(
              signature + parameter_start,
              index - parameter_start))
        return 0;
      parameter_start = index + 1;
    }
  }
  return 0;
}

static int canonical_signatures_allow_type_refinement(
    const char *left, int left_len,
    const char *right, int right_len,
    int *refinement) {
  int left_offset = 0;
  int right_offset = 0;
  while (left_offset < left_len &&
         right_offset < right_len) {
    if (left[left_offset] == '(' &&
        right[right_offset] == '(') {
      int left_empty =
          left_offset + 1 < left_len &&
          left[left_offset + 1] == ')';
      int right_empty =
          right_offset + 1 < right_len &&
          right[right_offset + 1] == ')';
      if (left_empty != right_empty) {
        const char *specified = left_empty ? right : left;
        int specified_len = left_empty ? right_len : left_len;
        int specified_open =
            left_empty ? right_offset : left_offset;
        int specified_close = 0;
        if (!obj_canonical_matching_parenthesis(
                specified, specified_len,
                specified_open, &specified_close) ||
            !obj_canonical_parameters_unchanged_by_default_promotions(
                specified, specified_open, specified_close) ||
            !update_type_refinement(
                refinement, left_empty ? 1 : -1))
          return 0;
        if (left_empty) {
          left_offset += 2;
          right_offset = specified_close + 1;
        } else {
          left_offset = specified_close + 1;
          right_offset += 2;
        }
        continue;
      }
    }
    obj_canonical_record_t left_record = {0};
    obj_canonical_record_t right_record = {0};
    int left_is_record = obj_canonical_record_at(
        left, left_len, left_offset, &left_record);
    int right_is_record = obj_canonical_record_at(
        right, right_len, right_offset, &right_record);
    if (left_is_record || right_is_record) {
      if (!left_is_record || !right_is_record ||
          left_record.kind != right_record.kind ||
          !name_eq(
              left_record.tag, left_record.tag_len,
              right_record.tag, right_record.tag_len))
        return 0;
      if (left_record.has_shape &&
          right_record.has_shape &&
          left_record.is_complete &&
          right_record.is_complete) {
        if (!canonical_signatures_allow_type_refinement(
                left + left_record.body_start,
                left_record.body_len,
                right + right_record.body_start,
                right_record.body_len,
                refinement))
          return 0;
      } else if (left_record.is_complete !=
                 right_record.is_complete) {
        if (!update_type_refinement(
                refinement,
                right_record.is_complete ? 1 : -1))
          return 0;
      }
      left_offset = left_record.end;
      right_offset = right_record.end;
      continue;
    }
    obj_canonical_array_t left_array = {0};
    obj_canonical_array_t right_array = {0};
    int left_is_array = obj_canonical_array_at(
        left, left_len, left_offset, &left_array);
    int right_is_array = obj_canonical_array_at(
        right, right_len, right_offset, &right_array);
    if (left_is_array || right_is_array) {
      if (!left_is_array || !right_is_array ||
          (left_array.bound != 0 &&
           right_array.bound != 0 &&
           left_array.bound != right_array.bound))
        return 0;
      if (left_array.bound != right_array.bound &&
          !update_type_refinement(
              refinement,
              right_array.bound != 0 ? 1 : -1))
        return 0;
      left_offset = left_array.element_start;
      right_offset = right_array.element_start;
      continue;
    }
    if (left[left_offset] != right[right_offset])
      return 0;
    left_offset++;
    right_offset++;
  }
  return left_offset == left_len &&
         right_offset == right_len;
}

static int canonical_function_signature_parameter_list(
    const char *signature, int signature_len,
    int *parameter_list_offset, int *is_empty) {
  if (!signature || signature_len < 2 ||
      signature[signature_len - 1] != ')')
    return 0;
  int depth = 0;
  for (int index = signature_len - 1; index >= 0; index--) {
    if (signature[index] == ')') {
      depth++;
    } else if (signature[index] == '(') {
      depth--;
      if (depth == 0) {
        if (parameter_list_offset)
          *parameter_list_offset = index;
        if (is_empty)
          *is_empty = index + 1 == signature_len - 1;
        return 1;
      }
      if (depth < 0) return 0;
    }
  }
  return 0;
}

static int canonical_function_signatures_allow_unspecified_parameters(
    const char *left, int left_len,
    const char *right, int right_len,
    int *left_is_empty, int *right_is_empty) {
  int left_offset = 0;
  int right_offset = 0;
  int left_empty = 0;
  int right_empty = 0;
  if (!canonical_function_signature_parameter_list(
          left, left_len, &left_offset, &left_empty) ||
      !canonical_function_signature_parameter_list(
          right, right_len, &right_offset, &right_empty) ||
      (!left_empty && !right_empty) ||
      left_offset != right_offset ||
      memcmp(left, right, (size_t)left_offset) != 0)
    return 0;
  if (left_is_empty) *left_is_empty = left_empty;
  if (right_is_empty) *right_is_empty = right_empty;
  return 1;
}

static void replace_func_c_signature(
    wasm32_obj_context_t *context, obj_func_t *function,
    const char *signature, int signature_len) {
  function->c_signature = xrealloc(
      context->diagnostic_context, function->c_signature,
      (size_t)signature_len + 1);
  memcpy(
      function->c_signature, signature,
      (size_t)signature_len);
  function->c_signature[signature_len] = '\0';
  function->c_signature_len = signature_len;
}

static void set_func_c_signature(
    wasm32_obj_context_t *context, obj_func_t *function,
    const char *signature, int signature_len) {
  if (!function || !signature || signature_len <= 0) {
    obj_unsupported_msg(
        context, "missing function-reference C signature");
    return;
  }
  if (function->c_signature) {
    if (function->c_signature_len == signature_len &&
        memcmp(
            function->c_signature, signature,
            (size_t)signature_len) == 0)
      return;
    int type_refinement = 0;
    if (canonical_signatures_allow_type_refinement(
            function->c_signature, function->c_signature_len,
            signature, signature_len,
            &type_refinement)) {
      if (type_refinement > 0)
        replace_func_c_signature(
            context, function, signature, signature_len);
      return;
    }
    int existing_is_empty = 0;
    int incoming_is_empty = 0;
    if (!canonical_function_signatures_allow_unspecified_parameters(
            function->c_signature, function->c_signature_len,
            signature, signature_len,
            &existing_is_empty, &incoming_is_empty))
      obj_unsupported_msg(
          context, "conflicting Wasm object C function signature");
    if (existing_is_empty && !incoming_is_empty)
      replace_func_c_signature(
          context, function, signature, signature_len);
    return;
  }
  replace_func_c_signature(
      context, function, signature, signature_len);
}

static int abi_layout_signature_parameter_offset(
    const char *signature, int signature_len) {
  if (!signature || signature_len < 4 ||
      signature[0] != 'F' ||
      signature[signature_len - 1] != ')')
    return -1;
  for (int index = 1; index < signature_len - 1; index++) {
    if (signature[index] == '(') return index;
  }
  return -1;
}

static int abi_layout_signature_has_wildcard(
    const char *signature, int signature_len) {
  int offset = abi_layout_signature_parameter_offset(
      signature, signature_len);
  return offset >= 0 &&
         offset + 2 == signature_len - 1 &&
         signature[offset + 1] == '?';
}

static int abi_layout_signature_has_any_wildcard(
    const char *signature, int signature_len) {
  return signature && signature_len > 0 &&
         memchr(signature, '?', (size_t)signature_len) != NULL;
}

static int abi_layout_signatures_allow_array_refinement(
    const char *left, int left_len,
    const char *right, int right_len,
    int *right_refinement) {
  if (!left || !right || left_len <= 0 || right_len <= 0)
    return 0;
  int left_offset = 0;
  int right_offset = 0;
  int refinement = 0;
  while (left_offset < left_len &&
         right_offset < right_len) {
    if (left[left_offset] == 'a' &&
        right[right_offset] == 'a') {
      left_offset++;
      right_offset++;
      int left_bound_start = left_offset;
      int right_bound_start = right_offset;
      while (left_offset < left_len &&
             left[left_offset] >= '0' &&
             left[left_offset] <= '9')
        left_offset++;
      while (right_offset < right_len &&
             right[right_offset] >= '0' &&
             right[right_offset] <= '9')
        right_offset++;
      int left_bound_len = left_offset - left_bound_start;
      int right_bound_len = right_offset - right_bound_start;
      if (left_bound_len <= 0 || right_bound_len <= 0)
        return 0;
      int bounds_equal =
          left_bound_len == right_bound_len &&
          memcmp(
              left + left_bound_start,
              right + right_bound_start,
              (size_t)left_bound_len) == 0;
      if (!bounds_equal) {
        int left_incomplete =
            left_bound_len == 1 &&
            left[left_bound_start] == '0';
        int right_incomplete =
            right_bound_len == 1 &&
            right[right_bound_start] == '0';
        if (!left_incomplete && !right_incomplete)
          return 0;
        refinement += left_incomplete ? 1 : -1;
      }
      continue;
    }
    if (left[left_offset++] != right[right_offset++])
      return 0;
  }
  if (left_offset != left_len ||
      right_offset != right_len)
    return 0;
  if (right_refinement)
    *right_refinement = refinement;
  return 1;
}

static int abi_layout_signatures_compatible(
    const char *left, int left_len,
    const char *right, int right_len,
    int *right_refinement) {
  if (right_refinement) *right_refinement = 0;
  if (!left || !right || left_len <= 0 || right_len <= 0)
    return 0;
  if (left_len == right_len &&
      memcmp(left, right, (size_t)left_len) == 0)
    return 1;
  if (abi_layout_signature_has_any_wildcard(
          left, left_len) ||
      abi_layout_signature_has_any_wildcard(
          right, right_len))
    return 1;
  if (abi_layout_signatures_allow_array_refinement(
          left, left_len, right, right_len,
          right_refinement))
    return 1;
  int left_offset = abi_layout_signature_parameter_offset(
      left, left_len);
  int right_offset = abi_layout_signature_parameter_offset(
      right, right_len);
  if (left_offset < 0 || right_offset < 0 ||
      left_offset != right_offset ||
      memcmp(left, right, (size_t)left_offset) != 0)
    return 0;
  return abi_layout_signature_has_wildcard(left, left_len) ||
         abi_layout_signature_has_wildcard(right, right_len);
}

static void replace_func_abi_layout_signature(
    wasm32_obj_context_t *context, obj_func_t *function,
    const char *signature, int signature_len) {
  function->abi_layout_signature = xrealloc(
      context->diagnostic_context,
      function->abi_layout_signature,
      (size_t)signature_len + 1);
  memcpy(
      function->abi_layout_signature, signature,
      (size_t)signature_len);
  function->abi_layout_signature[signature_len] = '\0';
  function->abi_layout_signature_len = signature_len;
}

static void set_func_abi_layout_signature(
    wasm32_obj_context_t *context, obj_func_t *function,
    const char *signature, int signature_len) {
  if (!function || !signature || signature_len <= 0) return;
  if (!function->abi_layout_signature) {
    replace_func_abi_layout_signature(
        context, function, signature, signature_len);
    return;
  }
  int right_refinement = 0;
  if (!abi_layout_signatures_compatible(
          function->abi_layout_signature,
          function->abi_layout_signature_len,
          signature, signature_len,
          &right_refinement))
    obj_unsupported_msg(
        context,
        "conflicting Wasm object ABI layout signature");
  if (abi_layout_signature_has_any_wildcard(
          function->abi_layout_signature,
          function->abi_layout_signature_len) &&
      !abi_layout_signature_has_any_wildcard(
          signature, signature_len))
    replace_func_abi_layout_signature(
        context, function, signature, signature_len);
  else if (right_refinement > 0)
    replace_func_abi_layout_signature(
        context, function, signature, signature_len);
}

static void ensure_func_sig_for_address(
    wasm32_obj_context_t *context,
    char *sym, int sym_len, obj_sig_t sig,
    int parameters_unspecified) {
  obj_func_t *target = find_func(context, sym, sym_len);
  if (!target) {
    target = intern_func(context, sym, sym_len);
    target->sig = sig;
    target->signature_parameters_unspecified =
        parameters_unspecified;
    target->signature_refined_from_unspecified_call = 0;
    return;
  }
  if (!target->defined && target->sig.nparams == 0 && target->sig.result == IR_TY_VOID) {
    target->sig = sig;
    target->signature_parameters_unspecified =
        parameters_unspecified;
    target->signature_refined_from_unspecified_call = 0;
  } else if (!target->defined &&
             target->signature_parameters_unspecified &&
             !parameters_unspecified &&
             sig_result_equal(&target->sig, &sig)) {
    free(target->sig.params);
    target->sig = sig;
    target->signature_parameters_unspecified = 0;
    target->signature_refined_from_unspecified_call = 0;
  } else {
    free(sig.params);
  }
}

static void collect_func_sig(
    wasm32_obj_context_t *context,
    const wasm32_machine_function_t *machine, obj_sig_t *sig) {
  if (!machine)
    obj_unsupported_msg(context, "function without ABI lowering result");
  *sig = copy_signature(context, &machine->signature);
}

static unsigned selected_opcode_or_unsupported(
    wasm32_obj_context_t *context,
    const wasm32_machine_inst_t *selected) {
  unsigned opcode = wasm32_machine_opcode_binary(selected->binary.opcode);
  if (!opcode) obj_unsupported_inst(context, selected);
  return opcode;
}

static void emit_selected_unary(
    wasm32_obj_context_t *context,
    wb_t *body, const wasm32_machine_inst_t *instruction,
    const wasm32_machine_inst_t *machine, int param_count) {
  const wasm32_machine_unary_t *selected = &machine->unary;
  if (selected->form == WASM32_MI_UNARY_ZERO_THEN_OPERAND) {
    emit_const(context, body, selected->operand_type, 0);
    emit_val(
        context, body, instruction->src1, selected->operand_type, param_count);
  } else if (selected->form == WASM32_MI_UNARY_OPERAND_THEN_NEG_ONE) {
    emit_val(
        context, body, instruction->src1, selected->operand_type, param_count);
    emit_const(context, body, selected->operand_type, -1);
  } else {
    emit_val(
        context, body, instruction->src1, selected->operand_type, param_count);
  }
  unsigned opcode = wasm32_machine_opcode_binary(selected->opcode);
  if (!opcode) obj_unsupported_inst(context, instruction);
  wb_u8(body, opcode);
  emit_local_set(
      body, local_index(param_count, instruction->dst.id));
}

static void emit_addr_plus_const(
    wasm32_obj_context_t *context,
    wb_t *b, ir_val_t v, int off, int param_count) {
  emit_addr_val(context, b, v, param_count);
  if (off == 0) return;
  emit_const(context, b, IR_TY_I32, off);
  wb_u8(
      b, machine_binary_binary_or_unsupported(
             context, &g_obj_machine_primitives->i32_add));
}

static void emit_copy_chunk(
    wasm32_obj_context_t *context,
    wb_t *b, ir_val_t dst, ir_val_t src,
    const wasm32_machine_copy_chunk_t *chunk, int param_count) {
  emit_addr_plus_const(context, b, dst, chunk->offset, param_count);
  emit_addr_plus_const(context, b, src, chunk->offset, param_count);
  wb_u8(b, memory_binary_or_unsupported(context, chunk->load));
  emit_selected_memarg(b, &chunk->load);
  wb_u8(b, memory_binary_or_unsupported(context, chunk->store));
  emit_selected_memarg(b, &chunk->store);
}

static void emit_memcpy_inline(
    wasm32_obj_context_t *context,
    wb_t *b, const wasm32_machine_inst_t *i, int param_count) {
  for (int chunk = 0; chunk < i->copy.chunk_count; chunk++)
    emit_copy_chunk(
        context, b, i->src1, i->src2,
        &i->copy.chunks[chunk], param_count);
}

static void emit_parameter_copy_chunk(
    wasm32_obj_context_t *context,
    wb_t *b, ir_val_t destination, int parameter_slot,
    const wasm32_machine_copy_chunk_t *chunk, int param_count) {
  emit_addr_plus_const(
      context, b, destination, chunk->offset, param_count);
  emit_local_get(b, parameter_slot);
  if (chunk->offset != 0) {
    emit_const(context, b, IR_TY_I32, chunk->offset);
    wb_u8(
        b, machine_binary_binary_or_unsupported(
               context, &g_obj_machine_primitives->i32_add));
  }
  wb_u8(b, memory_binary_or_unsupported(context, chunk->load));
  emit_selected_memarg(b, &chunk->load);
  wb_u8(b, memory_binary_or_unsupported(context, chunk->store));
  emit_selected_memarg(b, &chunk->store);
}

static void emit_parameter_copy(
    wasm32_obj_context_t *context,
    wb_t *b, ir_val_t destination, int parameter_slot,
    const wasm32_machine_copy_plan_t *plan, int param_count) {
  for (int index = 0; index < plan->chunk_count; index++)
    emit_parameter_copy_chunk(
        context, b, destination, parameter_slot, &plan->chunks[index],
        param_count);
}

static void emit_return_copy_chunk(
    wasm32_obj_context_t *context,
    wb_t *body, ir_val_t source,
    const wasm32_machine_copy_chunk_t *chunk, int param_count) {
  emit_local_get(body, 0);
  if (chunk->offset != 0) {
    emit_const(context, body, IR_TY_I32, chunk->offset);
    wb_u8(
        body, machine_binary_binary_or_unsupported(
                  context, &g_obj_machine_primitives->i32_add));
  }
  emit_addr_plus_const(
      context, body, source, chunk->offset, param_count);
  wb_u8(body, memory_binary_or_unsupported(context, chunk->load));
  emit_selected_memarg(body, &chunk->load);
  wb_u8(body, memory_binary_or_unsupported(context, chunk->store));
  emit_selected_memarg(body, &chunk->store);
}

static void emit_indirect_return_copy(
    wasm32_obj_context_t *context,
    wb_t *body, ir_val_t source,
    const wasm32_machine_copy_plan_t *plan, int param_count) {
  for (int index = 0; index < plan->chunk_count; index++)
    emit_return_copy_chunk(
        context, body, source, &plan->chunks[index], param_count);
}

static void emit_parameter_bind(
    wasm32_obj_context_t *context,
    wb_t *body, const obj_func_t *object_function,
    const wasm32_machine_inst_t *instruction,
    const wasm32_machine_inst_t *selected,
    int param_count) {
  if (!selected ||
      selected->kind != WASM32_MACHINE_INST_PARAMETER_BIND)
    obj_unsupported_inst(context, instruction);
  const wasm32_machine_parameter_bind_t *binding =
      &selected->parameter_bind;
  if (!binding->pieces || binding->piece_count == 0 ||
      instruction->src1.type != IR_TY_PTR)
    obj_unsupported_inst(context, instruction);
  for (int i = 0; i < binding->piece_count; i++) {
    int parameter_slot = binding->physical_index + i;
    if (parameter_slot < 0 ||
        parameter_slot >= object_function->sig.nparams)
      obj_unsupported_inst(context, instruction);
    if (binding->pieces[i].kind ==
        WASM32_MACHINE_PARAMETER_INDIRECT) {
      emit_parameter_copy(
          context, body, instruction->src1, parameter_slot,
          &binding->copy_plans[i], param_count);
      continue;
    }
    emit_addr_plus_const(
        context, body, instruction->src1,
        binding->pieces[i].byte_offset, param_count);
    emit_local_get(body, parameter_slot);
    emit_stack_cast(
        context, body, object_function->sig.params[parameter_slot],
        wasm_ir_type(binding->pieces[i].value_type), 1);
    wb_u8(body, memory_binary_or_unsupported(context, binding->stores[i]));
    emit_selected_memarg(body, &binding->stores[i]);
  }
}

typedef struct {
  wasm32_obj_context_t *context;
  wb_t *body;
  obj_func_t *function;
  obj_global_t *stack_pointer;
  obj_global_t **va_arg_area;
  const wasm32_machine_call_t *call;
  int old_va_arg_area_local;
  int param_count;
} wasm_obj_vararg_visitor_t;

static int emit_obj_vararg_action(
    void *user, const wasm32_machine_vararg_action_t *action) {
  wasm_obj_vararg_visitor_t *visitor = user;
  wasm32_obj_context_t *context = visitor->context;
  wb_t *body = visitor->body;
  obj_func_t *function = visitor->function;
  if (!visitor->stack_pointer)
    obj_unsupported_msg(
        context, "variadic call without stack pointer in Wasm object mode");
  if (!*visitor->va_arg_area)
    *visitor->va_arg_area = intern_va_arg_area_global(context);
  switch (action->kind) {
    case WASM32_MACHINE_VARARG_SAVE_AREA:
      emit_stack_global_get(
          context, body, function, *visitor->va_arg_area);
      emit_local_set(body, visitor->old_va_arg_area_local);
      return 1;
    case WASM32_MACHINE_VARARG_RESERVE_STACK:
      emit_stack_global_get(
          context, body, function, visitor->stack_pointer);
      emit_const(context, body, IR_TY_I32, action->byte_count);
      wb_u8(
          body, machine_binary_binary_or_unsupported(
                    context,
                    &g_obj_machine_primitives->i32_subtract));
      emit_stack_global_set(
          context, body, function, visitor->stack_pointer);
      return 1;
    case WASM32_MACHINE_VARARG_SET_AREA_FROM_STACK:
      emit_stack_global_get(
          context, body, function, visitor->stack_pointer);
      emit_stack_global_set(
          context, body, function, *visitor->va_arg_area);
      return 1;
    case WASM32_MACHINE_VARARG_STORE_ARGUMENT: {
      const wasm32_machine_variadic_argument_t *variadic =
          action->argument;
      if (!variadic || variadic->argument_index < 0 ||
          variadic->argument_index >= visitor->call->argument_count)
        return 0;
      emit_stack_global_get(
          context, body, function, *visitor->va_arg_area);
      emit_const(context, body, IR_TY_I32, variadic->byte_offset);
      wb_u8(
          body, machine_binary_binary_or_unsupported(
                    context, &g_obj_machine_primitives->i32_add));
      emit_abi_argument(
          context, body,
          &visitor->call->arguments[variadic->argument_index],
          variadic->argument_type, visitor->param_count);
      if (variadic->conversion.opcode != WASM32_MI_COPY)
        wb_u8(
            body, conversion_binary_or_unsupported(
                      context, variadic->conversion));
      wb_u8(
          body, memory_binary_or_unsupported(
                    context, variadic->store));
      emit_selected_memarg(body, &variadic->store);
      return 1;
    }
    case WASM32_MACHINE_VARARG_RELEASE_STACK:
      emit_stack_global_get(
          context, body, function, visitor->stack_pointer);
      emit_const(context, body, IR_TY_I32, action->byte_count);
      wb_u8(
          body, machine_binary_binary_or_unsupported(
                    context, &g_obj_machine_primitives->i32_add));
      emit_stack_global_set(
          context, body, function, visitor->stack_pointer);
      return 1;
    case WASM32_MACHINE_VARARG_RESTORE_AREA:
      emit_local_get(body, visitor->old_va_arg_area_local);
      emit_stack_global_set(
          context, body, function, *visitor->va_arg_area);
      return 1;
  }
  return 0;
}

static void emit_variadic_arg_area_prepare(
    wasm32_obj_context_t *context, wb_t *body, obj_func_t *function,
    obj_global_t *stack_pointer, obj_global_t **va_arg_area,
    int old_va_arg_area_local, const wasm32_machine_call_t *call,
    int param_count) {
  if (!call || call->variadic_area_size <= 0) return;
  wasm_obj_vararg_visitor_t visitor = {
      .context = context,
      .body = body,
      .function = function,
      .stack_pointer = stack_pointer,
      .va_arg_area = va_arg_area,
      .call = call,
      .old_va_arg_area_local = old_va_arg_area_local,
      .param_count = param_count,
  };
  if (!wasm32_machine_call_visit_variadic_prepare(
          call, emit_obj_vararg_action, &visitor))
    obj_unsupported_msg(
        context, "invalid Wasm variadic argument preparation plan");
}

static void emit_variadic_arg_area_restore(
    wasm32_obj_context_t *context, wb_t *body, obj_func_t *function,
    obj_global_t *stack_pointer, obj_global_t **va_arg_area,
    int old_va_arg_area_local, const wasm32_machine_call_t *call,
    int param_count) {
  if (!call || call->variadic_area_size <= 0) return;
  wasm_obj_vararg_visitor_t visitor = {
      .context = context,
      .body = body,
      .function = function,
      .stack_pointer = stack_pointer,
      .va_arg_area = va_arg_area,
      .call = call,
      .old_va_arg_area_local = old_va_arg_area_local,
      .param_count = param_count,
  };
  if (!wasm32_machine_call_visit_variadic_restore(
          call, emit_obj_vararg_action, &visitor))
    obj_unsupported_msg(
        context, "invalid Wasm variadic argument restore plan");
}

static void emit_direct_aggregate_call_result(
    wasm32_obj_context_t *context,
    wb_t *body, const wasm32_machine_inst_t *instruction,
    const wasm32_machine_call_t *call,
    int result_local_i32, int result_local_i64,
    int param_count) {
  if (call->result_area.id == IR_VAL_NONE)
    obj_unsupported_inst(context, instruction);
  ir_type_t result_type = call->direct_result_type;
  ir_type_t type = wasm_ir_type(result_type);
  int temporary = type == IR_TY_I64
                      ? result_local_i64 : result_local_i32;
  emit_local_set(body, temporary);
  emit_addr_val(context, body, call->result_area, param_count);
  emit_local_get(body, temporary);
  wb_u8(
      body, memory_binary_or_unsupported(context, call->direct_result_store));
  emit_selected_memarg(body, &call->direct_result_store);
}

static void gen_func_body(
                          wasm32_obj_context_t *context,
                          obj_func_t *of,
                          const wasm32_machine_function_t *planned) {
  if (!planned)
    obj_unsupported_msg(context, "failed to build Wasm machine function");
  wasm32_machine_function_t machine_function = *planned;
  int of_index = (int)(of - g_obj.funcs);
  int param_count = of->sig.nparams;
  int nlocals = machine_function.vreg_count;
  int frame_size = machine_function.stack.fixed_frame_size;
  int has_variadic_varargs =
      machine_function.stack.has_variadic_call_area;
  int has_persistent_continuation_frame =
      machine_function.stack.has_persistent_frame;
  int has_stack_restore =
      machine_function.stack.restores_stack_pointer;
  int has_control_flow = machine_function.has_control_flow;
  int has_atomic_cas32 = machine_function.has_atomic_cas32;
  int has_atomic_cas64 = machine_function.has_atomic_cas64;
  int extra_base = local_index(param_count, nlocals);
  int extra_count = 0;
  int fp_local = -1;
  if (frame_size > 0) fp_local = extra_base + extra_count++;
  int old_sp_local = -1;
  if (has_stack_restore) old_sp_local = extra_base + extra_count++;
  int old_va_arg_area_local = extra_base + extra_count;
  if (has_variadic_varargs) extra_count++;
  int pc_local = extra_base + extra_count;
  if (has_control_flow) extra_count++;
  int resumed_local = extra_base + extra_count;
  if (machine_function.is_continuation_entry) extra_count++;
  int call_result_i32_local = extra_base + extra_count++;
  int call_result_i64_local = extra_base + extra_count++;
  int atomic_tmp32_local = extra_base + extra_count;
  int atomic_exp32_local = atomic_tmp32_local + 1;
  if (has_atomic_cas32) extra_count += 2;
  int atomic_tmp64_local = extra_base + extra_count;
  int atomic_exp64_local = atomic_tmp64_local + 1;
  if (has_atomic_cas64) extra_count += 2;
  obj_global_t *stack_pointer =
      machine_function.stack.uses_stack_pointer
          ? intern_stack_pointer_global(context)
          : NULL;
  int continuation_frame_data = -1;
  int continuation_status_data = -1;
  int continuation_result_data = -1;
  if (machine_function.is_continuation_entry) {
    char name[320];
    int n = snprintf(name, sizeof(name), "__agc_cont_frame_%s",
                     machine_function.continuation_entry_name);
    if (n < 0 || n >= (int)sizeof(name))
      obj_unsupported_msg(context, "continuation data symbol name too long");
    if (has_persistent_continuation_frame) {
      obj_data_t *frame = intern_data(context, name, n, 4, 1, 0);
      continuation_frame_data = data_index(context, frame);
      data_note_alloc_size(frame, (size_t)(frame_size > 0 ? frame_size : 16));
      frame->is_emitted = 1;
    }
    n = snprintf(name, sizeof(name), "__agc_cont_status_%s",
                 machine_function.continuation_entry_name);
    obj_data_t *status = intern_data(context, name, n, 2, 1, 0);
    continuation_status_data = data_index(context, status);
    data_note_alloc_size(status, 4);
    status->is_emitted = 1;
    n = snprintf(name, sizeof(name), "__agc_cont_result_%s",
                 machine_function.continuation_entry_name);
    obj_data_t *result = intern_data(context, name, n, 2, 1, 0);
    continuation_result_data = data_index(context, result);
    data_note_alloc_size(result, 4);
    result->is_emitted = 1;
    of = &g_obj.funcs[of_index];
  }
  obj_global_t *va_arg_area = NULL;
  wb_t body = {.diagnostic_context = context->diagnostic_context};
  ir_type_t *local_types = machine_function.vreg_types;
  unsigned char *local_unsigned = machine_function.vreg_unsigned;
  g_emit_local_types = local_types;
  g_emit_local_unsigned = local_unsigned;
  g_emit_local_count = nlocals;

  wb_uleb(&body, (uint32_t)(nlocals + extra_count));
  for (int v = 0; v < nlocals; v++) {
    wb_uleb(&body, 1);
    wb_u8(&body, wasm_valtype(context, local_types[v]));
  }
  if (frame_size > 0) {
    wb_uleb(&body, 1);
    wb_u8(&body, wasm_valtype(context, IR_TY_I32));
  }
  if (has_stack_restore) {
    wb_uleb(&body, 1);
    wb_u8(&body, wasm_valtype(context, IR_TY_I32));
  }
  if (has_variadic_varargs) {
    wb_uleb(&body, 1);
    wb_u8(&body, wasm_valtype(context, IR_TY_I32));
  }
  if (has_control_flow) {
    wb_uleb(&body, 1);
    wb_u8(&body, wasm_valtype(context, IR_TY_I32));
  }
  if (machine_function.is_continuation_entry) {
    wb_uleb(&body, 1);
    wb_u8(&body, wasm_valtype(context, IR_TY_I32));
  }
  wb_uleb(&body, 1);
  wb_u8(&body, wasm_valtype(context, IR_TY_I32));
  wb_uleb(&body, 1);
  wb_u8(&body, wasm_valtype(context, IR_TY_I64));
  if (has_atomic_cas32) {
    wb_uleb(&body, 1);
    wb_u8(&body, wasm_valtype(context, IR_TY_I32));
    wb_uleb(&body, 1);
    wb_u8(&body, wasm_valtype(context, IR_TY_I32));
  }
  if (has_atomic_cas64) {
    wb_uleb(&body, 1);
    wb_u8(&body, wasm_valtype(context, IR_TY_I64));
    wb_uleb(&body, 1);
    wb_u8(&body, wasm_valtype(context, IR_TY_I64));
  }
  if (has_stack_restore) {
    emit_stack_global_get(context, &body, of, stack_pointer);
    emit_local_set(&body, old_sp_local);
  }
  if (frame_size > 0 && has_persistent_continuation_frame) {
    emit_data_address(context, &body, of, continuation_frame_data, 0);
    emit_local_set(&body, fp_local);
  } else if (frame_size > 0) {
    emit_stack_global_get(context, &body, of, stack_pointer);
    emit_const(context, &body, IR_TY_I32, frame_size);
    wb_u8(
        &body, machine_binary_binary_or_unsupported(
                   context,
                   &g_obj_machine_primitives->i32_subtract));
    emit_local_set(&body, fp_local);
    emit_local_get(&body, fp_local);
    emit_stack_global_set(context, &body, of, stack_pointer);
  }
  if (machine_function.is_continuation_entry) {
    /* command=-1 is start; all other values resume the pending condition. */
    emit_local_get(&body, 0);
    emit_const(context, &body, IR_TY_I32, -1);
    wb_u8(
        &body, machine_binary_binary_or_unsupported(
                   context, &g_obj_machine_primitives->i32_equal));
    wb_u8(&body, 0x04); wb_u8(&body, 0x40); /* if */
    emit_continuation_data_load(
        context, &body, of, continuation_status_data);
    wb_u8(
        &body, machine_opcode_binary_or_unsupported(
                   context,
                   g_obj_machine_primitives->i32_zero_test.opcode));
    wb_u8(&body, 0x04); wb_u8(&body, 0x40);
    wb_u8(&body, 0x05); /* else: invalid double start */
    emit_const(context, &body, IR_TY_I32, -1); wb_u8(&body, 0x0f);
    wb_u8(&body, 0x0b);
    wb_u8(&body, 0x05); /* resume */
    emit_continuation_data_load(
        context, &body, of, continuation_status_data);
    emit_const(context, &body, IR_TY_I32, 2);
    wb_u8(
        &body, machine_binary_binary_or_unsupported(
                   context, &g_obj_machine_primitives->i32_equal));
    wb_u8(&body, 0x04); wb_u8(&body, 0x40);
    wb_u8(&body, 0x05); /* else: resume without suspension */
    emit_const(context, &body, IR_TY_I32, -1); wb_u8(&body, 0x0f);
    wb_u8(&body, 0x0b);
    wb_u8(&body, 0x0b);
    emit_continuation_data_store_const(
        context, &body, of, continuation_status_data, 1);
    emit_local_get(&body, 0);
    emit_const(context, &body, IR_TY_I32, -1);
    wb_u8(
        &body, machine_binary_binary_or_unsupported(
                   context,
                   &g_obj_machine_primitives->i32_not_equal));
    emit_local_set(&body, resumed_local);

    if (has_persistent_continuation_frame) {
      /* Every invocation reconstructs ALLOCA pointer vregs from the same frame. */
      for (int index = 0;
           index < machine_function.instruction_count; index++) {
        const wasm32_machine_inst_t *instruction =
            &machine_function.instructions[index];
        if (instruction->kind != WASM32_MACHINE_INST_ALLOCA) continue;
        const wasm32_machine_alloca_t *slot =
            wasm32_machine_function_alloca(
                &machine_function, instruction->dst.id);
        int off = slot ? slot->offset : -1;
        emit_local_get(&body, fp_local);
        emit_const(context, &body, IR_TY_I32, off);
        wb_u8(
            &body, machine_binary_binary_or_unsupported(
                       context, &g_obj_machine_primitives->i32_add));
        emit_local_set(
            &body, local_index(param_count, instruction->dst.id));
      }
      for (int index = 0;
           index < machine_function.instruction_count; index++) {
        const wasm32_machine_inst_t *instruction =
            &machine_function.instructions[index];
        if (instruction->kind != WASM32_MACHINE_INST_ALIGN_POINTER)
          continue;
        emit_addr_val(context, &body, instruction->src1, param_count);
        emit_const(
            context, &body, IR_TY_I32,
            instruction->alignment.addend);
        wb_u8(
            &body, machine_binary_binary_or_unsupported(
                       context, &g_obj_machine_primitives->i32_add));
        emit_const(
            context, &body, IR_TY_I32,
            instruction->alignment.mask);
        wb_u8(
            &body, machine_binary_binary_or_unsupported(
                       context, &g_obj_machine_primitives->i32_and));
        emit_local_set(
            &body, local_index(param_count, instruction->dst.id));
      }
    }
  }
  if (has_control_flow) {
    int entry_id = machine_function.block_count > 0
                       ? machine_function.blocks[0].id
                       : 0;
    emit_const(context, &body, IR_TY_I32, entry_id);
    emit_local_set(&body, pc_local);
    if (machine_function.is_continuation_entry) {
      emit_local_get(&body, resumed_local);
      wb_u8(&body, 0x04); wb_u8(&body, 0x40);
      emit_const(
          context, &body, IR_TY_I32,
          machine_function.continuation_condition_block_id);
      emit_local_set(&body, pc_local);
      wb_u8(&body, 0x0b);
    }
    wb_u8(&body, 0x02);
    wb_u8(&body, 0x40);
    wb_u8(&body, 0x03);
    wb_u8(&body, 0x40);
  }

  for (int block_index = 0;
       block_index < machine_function.block_count; block_index++) {
    const wasm32_machine_block_t *block =
        &machine_function.blocks[block_index];
    if (has_control_flow) {
      emit_local_get(&body, pc_local);
      emit_const(context, &body, IR_TY_I32, block->id);
      wb_u8(
          &body, machine_binary_binary_or_unsupported(
                     context, &g_obj_machine_primitives->i32_equal));
      wb_u8(&body, 0x04);
      wb_u8(&body, 0x40);
    }
    for (int instruction_index = 0;
         instruction_index < block->instruction_count;
         instruction_index++) {
      const wasm32_machine_inst_t *planned =
          &machine_function.instructions[
              block->first_instruction + instruction_index];
      const wasm32_machine_inst_t *i = planned;
      switch (planned->kind) {
        case WASM32_MACHINE_INST_NOP:
          break;
        case WASM32_MACHINE_INST_PARAMETER_BIND:
          emit_parameter_bind(
              context, &body, of, i, planned,
              param_count);
          break;
        case WASM32_MACHINE_INST_ALLOCA: {
          if (has_persistent_continuation_frame) break;
          const wasm32_machine_alloca_t *slot =
              wasm32_machine_function_alloca(
                  &machine_function, i->dst.id);
          int off = slot ? slot->offset : -1;
          if (off < 0 || frame_size <= 0)
            obj_unsupported_inst(context, i);
          emit_local_get(&body, fp_local);
          emit_const(context, &body, IR_TY_I32, off);
          wb_u8(
              &body, machine_binary_binary_or_unsupported(
                         context, &g_obj_machine_primitives->i32_add));
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        }
        case WASM32_MACHINE_INST_INTEGER_CONSTANT:
          if (actual_vreg_type(context, i->dst) == IR_TY_F32 ||
              actual_vreg_type(context, i->dst) == IR_TY_F64) {
            emit_fp_const(
                context, &body, actual_vreg_type(context, i->dst),
                i->src1.fp_imm);
          } else {
            emit_const(
                context, &body, actual_vreg_type(context, i->dst),
                i->src1.imm);
          }
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        case WASM32_MACHINE_INST_FLOAT_CONSTANT:
          emit_fp_const(context, &body, i->dst.type, i->src1.fp_imm);
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        case WASM32_MACHINE_INST_STRING_ADDRESS:
        case WASM32_MACHINE_INST_SYMBOL_ADDRESS:
        case WASM32_MACHINE_INST_TLS_ADDRESS: {
          if (!i->sym) obj_unsupported_inst(context, i);
          if (planned->kind == WASM32_MACHINE_INST_SYMBOL_ADDRESS &&
              i->is_function_symbol) {
            ensure_func_sig_for_address(
                context, i->sym, i->sym_len,
                func_sig_from_machine_callable(
                    context, i, i->sym, i->sym_len),
                i->reference_parameters_unspecified);
            obj_func_t *target =
                find_func(context, i->sym, i->sym_len);
            if (!target)
              obj_unsupported_msg(
                  context,
                  "missing function-reference target");
            set_func_c_signature(
                context, target, i->reference_c_signature,
                i->reference_c_signature_len);
            set_func_abi_layout_signature(
                context, target,
                i->reference_signature.abi_layout_signature,
                i->reference_signature.abi_layout_signature_len);
            of = &g_obj.funcs[of_index];
            wb_u8(&body, 0x41);
            uint32_t imm_off = wb_uleb5(&body, 0);
            func_add_reloc(context, of, R_WASM_TABLE_INDEX_SLEB, imm_off,
                           (int)(target - g_obj.funcs), 0, 0);
            emit_local_set(&body, local_index(param_count, i->dst.id));
            break;
          }
          int addend = 0;
          obj_data_t *d = data_for_machine_inst(context, i, &addend);
          if (!d)
            obj_unsupported_msg(
                context, "missing IR data symbol in Wasm object mode");
          wb_u8(&body, 0x41);
          uint32_t imm_off = wb_uleb5(&body, 0);
          func_add_reloc(
              context, of, R_WASM_MEMORY_ADDR_LEB, imm_off,
              data_index(context, d), 1, addend);
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        }
        case WASM32_MACHINE_INST_CONVERSION: {
          const wasm32_machine_inst_t *selected = planned;
          emit_val(
              context, &body, i->src1, selected->conversion.source_type,
              param_count);
          if (selected->conversion.has_immediate) {
            wb_u8(&body, 0x41);
            wb_sleb(&body, selected->conversion.immediate);
          }
          if (selected->conversion.opcode != WASM32_MI_COPY) {
            unsigned opcode = wasm32_machine_opcode_binary(
                selected->conversion.opcode);
            if (!opcode) obj_unsupported_inst(context, i);
            wb_u8(&body, opcode);
          }
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        }
        case WASM32_MACHINE_INST_LOAD: {
          const wasm32_machine_inst_t *selected = planned;
          emit_addr_val(context, &body, i->src1, param_count);
          wb_u8(
              &body,
              memory_binary_or_unsupported(context, selected->load));
          emit_selected_memarg(&body, &selected->load);
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        }
        case WASM32_MACHINE_INST_STORE: {
          const wasm32_machine_inst_t *selected = planned;
          emit_addr_val(context, &body, i->src1, param_count);
          emit_val(
              context, &body, i->src2, selected->store.value_type,
              param_count);
          wb_u8(
              &body,
              memory_binary_or_unsupported(context, selected->store));
          emit_selected_memarg(&body, &selected->store);
          break;
        }
        case WASM32_MACHINE_INST_ATOMIC: {
          const wasm32_machine_inst_t *selected = planned;
          if (selected->atomic.kind == WASM32_MACHINE_ATOMIC_FENCE) {
            wb_u8(&body, 0x01);
            break;
          }
          ir_type_t value_ty =
              selected->atomic.load.opcode != WASM32_MI_INVALID
                  ? selected->atomic.load.value_type
                  : selected->atomic.store.value_type;
          if (selected->atomic.kind == WASM32_MACHINE_ATOMIC_LOAD) {
            emit_addr_val(context, &body, i->src1, param_count);
            wb_u8(
                &body, memory_binary_or_unsupported(
                           context, selected->atomic.load));
            emit_selected_memarg(&body, &selected->atomic.load);
            emit_local_set(&body, local_index(param_count, i->dst.id));
            break;
          }
          if (selected->atomic.kind == WASM32_MACHINE_ATOMIC_STORE) {
            emit_addr_val(context, &body, i->src1, param_count);
            emit_val(context, &body, i->src2, value_ty, param_count);
            wb_u8(
                &body, memory_binary_or_unsupported(
                           context, selected->atomic.store));
            emit_selected_memarg(&body, &selected->atomic.store);
            break;
          }
          if (selected->atomic.kind == WASM32_MACHINE_ATOMIC_EXCHANGE ||
              selected->atomic.kind == WASM32_MACHINE_ATOMIC_RMW) {
            emit_addr_val(context, &body, i->src1, param_count);
            wb_u8(
                &body, memory_binary_or_unsupported(
                           context, selected->atomic.load));
            emit_selected_memarg(&body, &selected->atomic.load);
            emit_local_set(&body, local_index(param_count, i->dst.id));

            emit_addr_val(context, &body, i->src1, param_count);
            if (selected->atomic.kind == WASM32_MACHINE_ATOMIC_EXCHANGE) {
              emit_val(context, &body, i->src2, value_ty, param_count);
            } else {
              emit_val(context, &body, i->dst, value_ty, param_count);
              emit_val(context, &body, i->src2, value_ty, param_count);
              unsigned opcode = wasm32_machine_opcode_binary(
                  selected->atomic.binary.opcode);
              if (!opcode) obj_unsupported_inst(context, i);
              wb_u8(&body, opcode);
            }
            wb_u8(
                &body, memory_binary_or_unsupported(
                           context, selected->atomic.store));
            emit_selected_memarg(&body, &selected->atomic.store);
            break;
          }
          if (selected->atomic.kind ==
              WASM32_MACHINE_ATOMIC_COMPARE_EXCHANGE) {
            int tmp_local = value_ty == IR_TY_I64 ? atomic_tmp64_local : atomic_tmp32_local;
            int exp_local = value_ty == IR_TY_I64 ? atomic_exp64_local : atomic_exp32_local;
            emit_addr_val(context, &body, i->src1, param_count);
            wb_u8(
                &body, memory_binary_or_unsupported(
                           context, selected->atomic.load));
            emit_selected_memarg(&body, &selected->atomic.load);
            emit_local_set(&body, tmp_local);
            emit_addr_val(context, &body, i->src2, param_count);
            wb_u8(
                &body, memory_binary_or_unsupported(
                           context, selected->atomic.load));
            emit_selected_memarg(&body, &selected->atomic.load);
            emit_local_set(&body, exp_local);
            emit_local_get(&body, tmp_local);
            emit_local_get(&body, exp_local);
            unsigned comparison_opcode = wasm32_machine_opcode_binary(
                selected->atomic.comparison.opcode);
            if (!comparison_opcode) obj_unsupported_inst(context, i);
            wb_u8(&body, comparison_opcode);
            emit_local_set(&body, local_index(param_count, i->dst.id));
            emit_local_get(&body, local_index(param_count, i->dst.id));
            wb_u8(&body, 0x04);
            wb_u8(&body, 0x40);
            emit_addr_val(context, &body, i->src1, param_count);
            emit_val(context, &body, i->src3, value_ty, param_count);
            wb_u8(
                &body, memory_binary_or_unsupported(
                           context, selected->atomic.store));
            emit_selected_memarg(&body, &selected->atomic.store);
            wb_u8(&body, 0x0b);
            emit_addr_val(context, &body, i->src2, param_count);
            emit_local_get(&body, tmp_local);
            wb_u8(
                &body, memory_binary_or_unsupported(
                           context, selected->atomic.store));
            emit_selected_memarg(&body, &selected->atomic.store);
            break;
          }
          obj_unsupported_inst(context, i);
          break;
        }
        case WASM32_MACHINE_INST_MEMORY_COPY:
          emit_memcpy_inline(context, &body, i, param_count);
          break;
        case WASM32_MACHINE_INST_DYNAMIC_ALLOCA:
          emit_stack_global_get(context, &body, of, stack_pointer);
          emit_val(context, &body, i->src1, IR_TY_I32, param_count);
          emit_const(
              context, &body, IR_TY_I32, i->alignment.addend);
          wb_u8(
              &body, machine_binary_binary_or_unsupported(
                         context, &g_obj_machine_primitives->i32_add));
          emit_const(
              context, &body, IR_TY_I32, i->alignment.mask);
          wb_u8(
              &body, machine_binary_binary_or_unsupported(
                         context, &g_obj_machine_primitives->i32_and));
          wb_u8(
              &body, machine_binary_binary_or_unsupported(
                         context,
                         &g_obj_machine_primitives->i32_subtract));
          emit_local_set(&body, local_index(param_count, i->dst.id));
          emit_local_get(&body, local_index(param_count, i->dst.id));
          emit_stack_global_set(context, &body, of, stack_pointer);
          break;
        case WASM32_MACHINE_INST_STACK_SAVE:
          emit_stack_global_get(context, &body, of, stack_pointer);
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        case WASM32_MACHINE_INST_STACK_RESTORE:
          emit_addr_val(context, &body, i->src1, param_count);
          emit_stack_global_set(context, &body, of, stack_pointer);
          break;
        case WASM32_MACHINE_INST_VARARG_AREA:
          if (!va_arg_area)
            va_arg_area = intern_va_arg_area_global(context);
          of = &g_obj.funcs[of_index];
          emit_stack_global_get(context, &body, of, va_arg_area);
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        case WASM32_MACHINE_INST_ADDRESS_ADD:
          emit_addr_val(context, &body, i->src1, param_count);
          emit_val(context, &body, i->src2, IR_TY_I32, param_count);
          wb_u8(
              &body, machine_binary_binary_or_unsupported(
                         context, &g_obj_machine_primitives->i32_add));
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        case WASM32_MACHINE_INST_ALIGN_POINTER: {
          if (machine_function.is_continuation_entry) break;
          emit_addr_val(context, &body, i->src1, param_count);
          emit_const(
              context, &body, IR_TY_I32, i->alignment.addend);
          wb_u8(
              &body, machine_binary_binary_or_unsupported(
                         context, &g_obj_machine_primitives->i32_add));
          emit_const(
              context, &body, IR_TY_I32, i->alignment.mask);
          wb_u8(
              &body, machine_binary_binary_or_unsupported(
                         context, &g_obj_machine_primitives->i32_and));
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        }
        case WASM32_MACHINE_INST_UNARY:
          emit_selected_unary(
              context, &body, i, planned,
              param_count);
          break;
        case WASM32_MACHINE_INST_BINARY: {
          const wasm32_machine_inst_t *selected = planned;
          ir_type_t op_ty = selected->binary.operand_type;
          if (selected->binary.guard_zero_divisor) {
            emit_val(context, &body, i->src2, op_ty, param_count);
            wb_u8(
                &body, machine_opcode_binary_or_unsupported(
                           context, selected->binary.zero_test.opcode));
            wb_u8(&body, 0x04);
            wb_u8(&body, wasm_valtype(context, op_ty));
            emit_val(context, &body, i->src1, op_ty, param_count);
            wb_u8(&body, 0x05);
            emit_val(context, &body, i->src1, op_ty, param_count);
            emit_val(context, &body, i->src2, op_ty, param_count);
            wb_u8(
                &body, selected_opcode_or_unsupported(context, selected));
            wb_u8(&body, 0x0b);
            emit_local_set(&body, local_index(param_count, i->dst.id));
            break;
          }
          emit_val(context, &body, i->src1, op_ty, param_count);
          emit_val(context, &body, i->src2, op_ty, param_count);
          wb_u8(
              &body, selected_opcode_or_unsupported(context, selected));
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        }
        case WASM32_MACHINE_INST_CALL: {
          const wasm32_machine_inst_t *selected = planned;
          const wasm32_machine_call_t *call = &selected->call;
          const obj_sig_t *csig = &call->signature;
          int argument_count = call->argument_count;
          const wasm32_machine_argument_t *arguments = call->arguments;
          emit_variadic_arg_area_prepare(
              context, &body, of, stack_pointer, &va_arg_area,
              old_va_arg_area_local, call, param_count);
          if (i->callee.id != IR_VAL_NONE) {
            int type_index = intern_type(context, csig);
            g_obj.has_indirect_call = 1;
            if (csig->has_hidden_result)
              emit_addr_val(
                  context, &body, call->result_area, param_count);
            for (int a = 0; a < csig->nparams; a++) {
              int p = a + (csig->has_hidden_result ? 1 : 0);
              if (p >= csig->nparams) break;
              if (a >= argument_count)
                obj_unsupported_msg(
                    context,
                    "indirect call has too few lowered arguments");
              emit_abi_argument(
                  context, &body, &arguments[a], csig->params[p],
                  param_count);
            }
            emit_addr_val(context, &body, i->callee, param_count);
            wb_u8(&body, 0x11);
            uint32_t type_imm_off = wb_uleb5(&body, (uint32_t)type_index);
            func_add_type_reloc(context, of, type_imm_off, type_index);
            wb_uleb(&body, 0);
            if (csig->has_direct_aggregate_result) {
              emit_direct_aggregate_call_result(
                  context, &body, i, call,
                  call_result_i32_local,
                  call_result_i64_local, param_count);
            } else if (csig->result != IR_TY_VOID && i->dst.id >= 0) {
              emit_local_set(&body, local_index(param_count, i->dst.id));
            }
            emit_variadic_arg_area_restore(
                context, &body, of, stack_pointer, &va_arg_area,
                old_va_arg_area_local, call, param_count);
            break;
          }
          if (!i->sym) obj_unsupported_inst(context, i);
          obj_func_t *target = intern_func(context, i->sym, i->sym_len);
          set_func_c_signature(
              context, target, i->reference_c_signature,
              i->reference_c_signature_len);
          set_func_abi_layout_signature(
              context, target,
              csig->abi_layout_signature,
              csig->abi_layout_signature_len);
          of = &g_obj.funcs[of_index];
          obj_sig_t *emit_sig = &target->sig;
          if (target->sig.nparams == 0 && target->sig.result == IR_TY_VOID && !target->defined) {
            target->sig = copy_signature(context, csig);
            target->signature_parameters_unspecified =
                i->reference_parameters_unspecified;
            target->signature_refined_from_unspecified_call =
                i->reference_parameters_unspecified;
            emit_sig = &target->sig;
          } else if (!target->defined &&
                     target->signature_parameters_unspecified &&
                     !target->signature_refined_from_unspecified_call &&
                     sig_result_equal(&target->sig, csig)) {
            free(target->sig.params);
            target->sig = copy_signature(context, csig);
            target->signature_refined_from_unspecified_call = 1;
            emit_sig = &target->sig;
          } else if (!target->defined &&
                     !sig_equal(&target->sig, csig) &&
                     !sig_integer_width_compatible(&target->sig, csig)) {
            char msg[160];
            snprintf(msg, sizeof(msg), "conflicting Wasm object function signature: %.*s",
                     i->sym_len, i->sym);
            obj_unsupported_msg(context, msg);
          }
          int has_call_ret_area = csig->has_hidden_result;
          if (has_call_ret_area)
            emit_addr_val(context, &body, call->result_area, param_count);
          for (int a = 0; a < argument_count; a++) {
            int p = a + (has_call_ret_area ? 1 : 0);
            if (p >= emit_sig->nparams) break;
            emit_abi_argument(
                context, &body, &arguments[a], emit_sig->params[p],
                param_count);
          }
          wb_u8(&body, 0x10);
          uint32_t imm_off = wb_uleb5(&body, 0);
          func_add_call_reloc(
              context, of, imm_off, (int)(target - g_obj.funcs));
          if (csig->has_direct_aggregate_result) {
            emit_direct_aggregate_call_result(
                context, &body, i, call,
                call_result_i32_local,
                call_result_i64_local, param_count);
          } else if (emit_sig->result != IR_TY_VOID && i->dst.id >= 0) {
            emit_local_set(&body, local_index(param_count, i->dst.id));
          }
          emit_variadic_arg_area_restore(
              context, &body, of, stack_pointer, &va_arg_area,
              old_va_arg_area_local, call, param_count);
          break;
        }
        case WASM32_MACHINE_INST_CONTROL:
          switch (planned->control.kind) {
            case WASM32_MACHINE_CONTROL_LABEL:
              if (!has_control_flow) obj_unsupported_inst(context, i);
              break;
            case WASM32_MACHINE_CONTROL_BRANCH:
              if (!has_control_flow) obj_unsupported_inst(context, i);
              emit_const(
                  context, &body, IR_TY_I32,
                  planned->control.target_block_id);
              emit_local_set(&body, pc_local);
              wb_u8(&body, 0x0c);
              wb_uleb(&body, 1);
              break;
            case WASM32_MACHINE_CONTROL_BRANCH_CONDITIONAL:
              if (!has_control_flow) obj_unsupported_inst(context, i);
              emit_val(
                  context, &body, planned->control.value,
                  IR_TY_I32, param_count);
              wb_u8(&body, 0x04);
              wb_u8(&body, 0x40);
              emit_const(
                  context, &body, IR_TY_I32,
                  planned->control.target_block_id);
              emit_local_set(&body, pc_local);
              wb_u8(&body, 0x05);
              emit_const(
                  context, &body, IR_TY_I32,
                  planned->control.else_block_id);
              emit_local_set(&body, pc_local);
              wb_u8(&body, 0x0b);
              wb_u8(&body, 0x0c);
              wb_uleb(&body, 1);
              break;
            case WASM32_MACHINE_CONTROL_SUSPEND:
              if (!machine_function.is_continuation_entry ||
                  !has_control_flow)
                obj_unsupported_inst(context, i);
              emit_local_get(&body, resumed_local);
              wb_u8(&body, 0x04); wb_u8(&body, 0x40);
              emit_const(context, &body, IR_TY_I32, 0);
              emit_local_set(&body, resumed_local);
              emit_local_get(&body, 0);
              wb_u8(&body, 0x04); wb_u8(&body, 0x40);
              emit_const(
                  context, &body, IR_TY_I32,
                  planned->control.target_block_id);
              emit_local_set(&body, pc_local);
              wb_u8(&body, 0x05);
              emit_const(
                  context, &body, IR_TY_I32,
                  planned->control.else_block_id);
              emit_local_set(&body, pc_local);
              wb_u8(&body, 0x0b);
              wb_u8(&body, 0x05);
              emit_continuation_data_store_const(
                  context, &body, of, continuation_status_data, 2);
              emit_const(context, &body, IR_TY_I32, 2);
              wb_u8(&body, 0x0f);
              wb_u8(&body, 0x0b);
              wb_u8(&body, 0x0c);
              wb_uleb(&body, 1);
              break;
            case WASM32_MACHINE_CONTROL_RETURN: {
              ir_val_t result = planned->control.value;
              if (machine_function.is_continuation_entry) {
                emit_data_address(
                    context, &body, of, continuation_result_data, 0);
                if (result.id != IR_VAL_NONE)
                  emit_val(
                      context, &body, result, IR_TY_I32, param_count);
                else
                  emit_const(context, &body, IR_TY_I32, 0);
                wb_u8(&body, store_opcode(context, IR_TY_I32));
                emit_memarg(context, &body, IR_TY_I32);
                emit_continuation_data_store_const(
                    context, &body, of, continuation_status_data, 3);
                if (has_stack_restore) {
                  emit_local_get(&body, old_sp_local);
                  emit_stack_global_set(
                      context, &body, of, stack_pointer);
                }
                emit_const(context, &body, IR_TY_I32, 3);
              } else {
                if (machine_function.signature.has_hidden_result) {
                  if (result.type != IR_TY_PTR ||
                      result.id == IR_VAL_NONE)
                    obj_unsupported_inst(context, i);
                  emit_indirect_return_copy(
                      context, &body, result,
                      &machine_function.result_copy,
                      param_count);
                } else if (
                    machine_function.signature
                        .has_direct_aggregate_result) {
                  if (result.type != IR_TY_PTR ||
                      result.id == IR_VAL_NONE)
                    obj_unsupported_inst(context, i);
                  emit_addr_val(context, &body, result, param_count);
                  wb_u8(
                      &body, memory_binary_or_unsupported(
                                 context,
                                 machine_function.direct_result_load));
                  emit_selected_memarg(
                      &body, &machine_function.direct_result_load);
                } else if (result.id != IR_VAL_NONE) {
                  emit_val(
                      context, &body, result, of->sig.result,
                      param_count);
                }
                if (has_stack_restore) {
                  emit_local_get(&body, old_sp_local);
                  emit_stack_global_set(
                      context, &body, of, stack_pointer);
                }
              }
              wb_u8(&body, 0x0f);
              break;
            }
            default:
              obj_unsupported_inst(context, i);
          }
          break;
        default:
          obj_unsupported_inst(context, i);
      }
    }
    if (has_control_flow && !block->has_terminator) {
      if (block->next_block_id >= 0) {
        emit_const(context, &body, IR_TY_I32, block->next_block_id);
        emit_local_set(&body, pc_local);
        wb_u8(&body, 0x0c);
        wb_uleb(&body, 1);
      } else {
        wb_u8(&body, 0x0c);
        wb_uleb(&body, 2);
      }
    }
    if (has_control_flow) wb_u8(&body, 0x0b);
  }
  if (has_control_flow) {
    wb_u8(&body, 0x0c);
    wb_uleb(&body, 1);
    wb_u8(&body, 0x0b);
    wb_u8(&body, 0x0b);
    wb_u8(&body, 0x00);
  } else if (has_stack_restore) {
    emit_local_get(&body, old_sp_local);
    emit_stack_global_set(context, &body, of, stack_pointer);
  }
  wb_u8(&body, 0x0b);
  of->body = body;
  g_emit_local_types = NULL;
  g_emit_local_unsigned = NULL;
  g_emit_local_count = 0;
}

static void assign_indices(wasm32_obj_context_t *context) {
  for (int i = 0; i < g_obj.func_count; i++) {
    g_obj.funcs[i].type_index = intern_type(context, &g_obj.funcs[i].sig);
  }

  int func_index = 0;
  for (int i = 0; i < g_obj.func_count; i++) {
    if (!g_obj.funcs[i].defined) {
      g_obj.funcs[i].imported = 1;
      g_obj.funcs[i].func_index = func_index++;
    }
  }
  for (int i = 0; i < g_obj.func_count; i++) {
    if (g_obj.funcs[i].defined) g_obj.funcs[i].func_index = func_index++;
  }
  for (int i = 0; i < g_obj.global_count; i++) {
    g_obj.globals[i].global_index = i;
  }

  int sym = 0;
  for (int i = 0; i < g_obj.func_count; i++) g_obj.funcs[i].symbol_index = sym++;
  for (int i = 0; i < g_obj.data_count; i++) g_obj.data[i].symbol_index = sym++;
  for (int i = 0; i < g_obj.global_count; i++) g_obj.globals[i].symbol_index = sym++;
  g_obj.symbol_count = sym;

  for (int i = 0; i < g_obj.func_count; i++) {
    obj_func_t *f = &g_obj.funcs[i];
    for (int r = 0; r < f->reloc_count; r++) {
      if (f->relocs[r].target_is_global) {
        obj_global_t *target = &g_obj.globals[f->relocs[r].target_sym];
        wb_patch_uleb5(f->body.data + f->relocs[r].body_off, (uint32_t)target->global_index);
        f->relocs[r].target_sym = target->symbol_index;
      } else if (f->relocs[r].target_is_type) {
        wb_patch_uleb5(f->body.data + f->relocs[r].body_off,
                       (uint32_t)f->relocs[r].target_sym);
      } else if (f->relocs[r].target_is_data) {
        obj_data_t *target = &g_obj.data[f->relocs[r].target_sym];
        wb_patch_uleb5(f->body.data + f->relocs[r].body_off, 0);
        f->relocs[r].target_sym = target->symbol_index;
      } else {
        obj_func_t *target = &g_obj.funcs[f->relocs[r].target_sym];
        uint32_t value = f->relocs[r].type == R_WASM_TABLE_INDEX_SLEB
                           ? 0 : (uint32_t)target->func_index;
        wb_patch_uleb5(f->body.data + f->relocs[r].body_off, value);
        f->relocs[r].target_sym = target->symbol_index;
      }
    }
  }
}

static obj_func_t *define_continuation_helper(
    wasm32_obj_context_t *context,
    const char *name, int param_count) {
  obj_func_t *helper = intern_func(context, name, (int)strlen(name));
  if (helper->defined)
    obj_unsupported_msg(
        context, "continuation export conflicts with a C function");
  helper->defined = 1;
  helper->sig.result = IR_TY_I32;
  helper->sig.nparams = param_count;
  if (param_count > 0) {
    helper->sig.params = xrealloc(
        context->diagnostic_context, helper->sig.params,
        (size_t)param_count * sizeof(ir_type_t));
    for (int i = 0; i < param_count; i++)
      helper->sig.params[i] = IR_TY_I32;
  }
  const char *signature =
      param_count ? "i32(i32)" : "i32()";
  set_func_c_signature(
      context, helper, signature, (int)strlen(signature));
  return helper;
}

static void synthesize_continuation_helpers(
    wasm32_obj_context_t *context,
    const wasm32_machine_function_t *function) {
  obj_func_t *step = find_func(
      context, function->name, function->name_len);
  if (!step || !step->defined)
    obj_unsupported_msg(context, "missing continuation step function");
  int step_index = (int)(step - g_obj.funcs);
  char data_name[320];
  int n = snprintf(data_name, sizeof(data_name), "__agc_cont_status_%s",
                   function->continuation_entry_name);
  obj_data_t *status = find_data(context, data_name, n);
  n = snprintf(data_name, sizeof(data_name), "__agc_cont_result_%s",
               function->continuation_entry_name);
  obj_data_t *result = find_data(context, data_name, n);
  if (!status || !result)
    obj_unsupported_msg(context, "missing continuation state data");
  int status_index = data_index(context, status);
  int result_index = data_index(context, result);

  obj_func_t *start = define_continuation_helper(
      context, function->continuation_start_export, 0);
  int start_index = (int)(start - g_obj.funcs);
  wb_uleb(&start->body, 0);
  emit_const(context, &start->body, IR_TY_I32, -1);
  wb_u8(&start->body, 0x10);
  uint32_t call_off = wb_uleb5(&start->body, 0);
  func_add_call_reloc(context, start, call_off, step_index);
  wb_u8(&start->body, 0x0b);

  obj_func_t *resume = define_continuation_helper(
      context, function->continuation_resume_export, 1);
  int resume_index = (int)(resume - g_obj.funcs);
  wb_uleb(&resume->body, 0);
  emit_local_get(&resume->body, 0);
  wb_u8(&resume->body, 0x10);
  call_off = wb_uleb5(&resume->body, 0);
  func_add_call_reloc(context, resume, call_off, step_index);
  wb_u8(&resume->body, 0x0b);

  obj_func_t *status_fn = define_continuation_helper(
      context, function->continuation_status_export, 0);
  int status_fn_index = (int)(status_fn - g_obj.funcs);
  wb_uleb(&status_fn->body, 0);
  emit_continuation_data_load(
      context, &status_fn->body, status_fn, status_index);
  wb_u8(&status_fn->body, 0x0b);

  obj_func_t *result_fn = define_continuation_helper(
      context, function->continuation_result_export, 0);
  int result_fn_index = (int)(result_fn - g_obj.funcs);
  wb_uleb(&result_fn->body, 0);
  emit_continuation_data_load(
      context, &result_fn->body, result_fn, result_index);
  wb_u8(&result_fn->body, 0x0b);

  /* intern_func may move the array; reloc targets remain stable indices. */
  (void)start_index;
  (void)resume_index;
  (void)status_fn_index;
  (void)result_fn_index;
}

void wasm32_obj_set_output_file_in(wasm32_obj_context_t *ctx, FILE *out) {
  if (!ctx) abort();
  ctx->obj.out = out;
}

void wasm32_obj_capture_output_in(
    wasm32_obj_context_t *ctx, int enabled) {
  if (!ctx) abort();
  ctx->obj.capture_output = enabled;
}

void wasm32_obj_set_capture_limit_in(
    wasm32_obj_context_t *ctx, size_t max_bytes) {
  if (!ctx) abort();
  ctx->capture_limit =
      max_bytes > UINT32_MAX ? UINT32_MAX : (uint32_t)max_bytes;
  ctx->capture_limit_exceeded = 0;
}

int wasm32_obj_capture_limit_exceeded_in(wasm32_obj_context_t *ctx) {
  if (!ctx) abort();
  return ctx->capture_limit_exceeded;
}

unsigned char *wasm32_obj_take_output_in(
    wasm32_obj_context_t *ctx, size_t *out_len) {
  if (!ctx) abort();
  unsigned char *data = ctx->capture.data;
  if (out_len) *out_len = ctx->capture.len;
  ctx->capture = (wb_t){
      .diagnostic_context = ctx->diagnostic_context};
  return data;
}

void wasm32_obj_begin_in(wasm32_obj_context_t *ctx) {
  if (!ctx) abort();
  wasm32_obj_context_t *context = ctx;
  FILE *out = g_obj.out;
  int capture_output = g_obj.capture_output;
  wasm32_obj_clear_module(&g_obj);
  g_obj.out = out;
  g_obj.capture_output = capture_output;
  g_obj_machine_primitives = NULL;
}

void wasm32_obj_gen_machine_module_in(
    wasm32_obj_context_t *ctx,
    const wasm32_machine_module_t *machine_module) {
  if (!ctx) abort();
  wasm32_obj_context_t *context = ctx;
  if (!machine_module)
    obj_unsupported_msg(context, "failed to build Wasm machine module");
  g_obj_machine_primitives =
      wasm32_machine_module_primitives(machine_module);
  if (!g_obj_machine_primitives)
    obj_unsupported_msg(
        context, "Wasm machine module has no primitive plan");
  for (size_t function_index = 0;
       function_index < machine_module->function_count;
       function_index++) {
    const wasm32_machine_function_t *function =
        wasm32_machine_module_function(
            machine_module, function_index);
    if (!function)
      obj_unsupported_msg(context, "incomplete Wasm machine module");
    obj_func_t *of = intern_func(
        context, function->name, function->name_len);
    if (of->defined)
      obj_unsupported_msg(context, "duplicate function in Wasm object mode");
    obj_sig_t def_sig = {0};
    collect_func_sig(context, function, &def_sig);
    if (of->sig.nparams > 0 || of->sig.result != IR_TY_VOID ||
        of->signature_parameters_unspecified) {
      if (of->signature_parameters_unspecified) {
        if (!sig_result_equal(&of->sig, &def_sig) ||
            (of->signature_refined_from_unspecified_call &&
             !sig_equal(&of->sig, &def_sig) &&
             !sig_integer_width_compatible(&of->sig, &def_sig))) {
          char msg[160];
          snprintf(msg, sizeof(msg), "conflicting Wasm object function signature: %.*s",
                   function->name_len, function->name);
          obj_unsupported_msg(context, msg);
        }
        free(of->sig.params);
        of->sig = def_sig;
        of->signature_parameters_unspecified = 0;
        of->signature_refined_from_unspecified_call = 0;
      } else if (!sig_equal(&of->sig, &def_sig) &&
          !sig_integer_width_compatible(&of->sig, &def_sig)) {
        char msg[160];
        snprintf(msg, sizeof(msg), "conflicting Wasm object function signature: %.*s",
                 function->name_len, function->name);
        obj_unsupported_msg(context, msg);
      } else {
        free(def_sig.params);
      }
    } else {
      of->sig = def_sig;
    }
    if (function->c_signature && function->c_signature_len > 0)
      set_func_c_signature(
          context, of, function->c_signature,
          function->c_signature_len);
    set_func_abi_layout_signature(
        context, of,
        function->signature.abi_layout_signature,
        function->signature.abi_layout_signature_len);
    of->defined = 1;
    of->is_static = function->is_static;
    gen_func_body(context, of, function);
    if (function->is_continuation_entry) {
      if (g_obj.continuation_entry)
        obj_unsupported_msg(
            context, "multiple continuation entries in one object");
      g_obj.continuation_entry = dup_name(
          context->diagnostic_context,
          function->continuation_entry_name,
          (int)strlen(function->continuation_entry_name));
      g_obj.continuation_condition = dup_name(
          context->diagnostic_context,
          function->continuation_condition_name,
          (int)strlen(function->continuation_condition_name));
      g_obj.continuation_step = dup_name(
          context->diagnostic_context,
          function->name, function->name_len);
      g_obj.continuation_start = dup_name(
          context->diagnostic_context,
          function->continuation_start_export,
          (int)strlen(function->continuation_start_export));
      g_obj.continuation_resume = dup_name(
          context->diagnostic_context,
          function->continuation_resume_export,
          (int)strlen(function->continuation_resume_export));
      g_obj.continuation_status = dup_name(
          context->diagnostic_context,
          function->continuation_status_export,
          (int)strlen(function->continuation_status_export));
      g_obj.continuation_result = dup_name(
          context->diagnostic_context,
          function->continuation_result_export,
          (int)strlen(function->continuation_result_export));
      synthesize_continuation_helpers(context, function);
    }
  }
  g_obj_machine_primitives = NULL;
}

static void emit_obj_string_literal(
    wasm32_obj_context_t *context,
    const wasm32_machine_data_object_t *object) {
  obj_data_t *d = intern_data(
      context,
      object->name, object->name_len,
      align_log2_for_size(object->alignment), 1, 0);
  if (d->is_emitted) return;
  wb_bytes(&d->bytes, object->bytes, (size_t)object->byte_size);
  data_note_alloc_size(d, (size_t)object->byte_size);
  d->is_emitted = 1;
}

static obj_data_t *intern_lowered_data_object(
    wasm32_obj_context_t *context,
    const wasm32_machine_data_object_t *object) {
  if (!object || object->kind == WASM32_MACHINE_DATA_FLOAT) return NULL;
  int is_string = object->kind == WASM32_MACHINE_DATA_STRING;
  obj_data_t *data = intern_data(
      context,
      object->name, object->name_len,
      align_log2_for_size(object->alignment),
      is_string ? 1 : object->is_static,
      is_string ? 0 : object->is_extern);
  if (!data || is_string) return data;
  data->is_thread_local = object->is_thread_local ? 1 : 0;
  data->requested_alignment =
      object->requested_alignment;
  if (!object->c_signature ||
      object->c_signature_len <= 0 ||
      !object->abi_layout_signature ||
      object->abi_layout_signature_len <= 0)
    obj_unsupported_msg(
        context, "missing lowered data type signature");
  if (!data->c_signature) {
    data->c_signature = dup_name(
        context->diagnostic_context,
        object->c_signature, object->c_signature_len);
    data->c_signature_len = object->c_signature_len;
  } else if (data->c_signature_len !=
                 object->c_signature_len ||
             memcmp(
                 data->c_signature, object->c_signature,
                 (size_t)object->c_signature_len) != 0) {
    obj_unsupported_msg(
        context, "conflicting Wasm object data C signature");
  }
  if (!data->abi_layout_signature) {
    data->abi_layout_signature = dup_name(
        context->diagnostic_context,
        object->abi_layout_signature,
        object->abi_layout_signature_len);
    data->abi_layout_signature_len =
        object->abi_layout_signature_len;
  } else if (data->abi_layout_signature_len !=
                 object->abi_layout_signature_len ||
             memcmp(
                 data->abi_layout_signature,
                 object->abi_layout_signature,
                 (size_t)object->abi_layout_signature_len) != 0) {
    obj_unsupported_msg(
        context, "conflicting Wasm object data ABI layout signature");
  }
  return data;
}

static void emit_obj_data_reloc(
    wasm32_obj_context_t *context,
    obj_data_t *data,
    const wasm32_machine_data_reloc_t *reloc) {
  if (reloc->offset < 0 || reloc->width <= 0 ||
      (size_t)reloc->offset + (size_t)reloc->width > data->bytes.len)
    obj_unsupported_msg(
        context, "lowered data relocation range in Wasm object mode");
  if (reloc->kind == WASM32_MACHINE_DATA_RELOC_FUNCTION) {
    if (reloc->addend != 0)
      obj_unsupported_msg(
          context, "function address addend in Wasm object mode");
    if (!reloc->has_function_signature)
      obj_unsupported_msg(
          context, "missing function relocation Machine signature");
    ensure_func_sig_for_address(
        context,
        reloc->target, reloc->target_len,
        copy_signature(context, &reloc->function_signature),
        reloc->reference_parameters_unspecified);
    obj_func_t *target = find_func(
        context, reloc->target, reloc->target_len);
    if (!target)
      obj_unsupported_msg(context, "missing function relocation target");
    set_func_c_signature(
        context, target, reloc->reference_c_signature,
        reloc->reference_c_signature_len);
    set_func_abi_layout_signature(
        context, target,
        reloc->function_signature.abi_layout_signature,
        reloc->function_signature.abi_layout_signature_len);
    data_add_reloc(
                   context,
                   data, R_WASM_TABLE_INDEX_I32, (uint32_t)reloc->offset,
                   (int)(target - g_obj.funcs), 0, 0);
    return;
  }
  obj_data_t *target = intern_lowered_data_object(
      context, reloc->resolved_target);
  if (!target)
    obj_unsupported_msg(context, "missing data relocation target");
  data_add_reloc(
      context, data, R_WASM_MEMORY_ADDR_I32, (uint32_t)reloc->offset,
      data_index(context, target), 1, (int)reloc->addend);
}

static void emit_obj_global(
    wasm32_obj_context_t *context,
    const wasm32_machine_data_object_t *object) {
  obj_data_t *data = intern_lowered_data_object(context, object);
  if (!data || object->is_extern) return;
  data_note_alloc_size(data, (size_t)object->byte_size);
  if (data->is_emitted) return;
  if (object->has_explicit_initializer) {
    if (!object->bytes)
      obj_unsupported_msg(
          context, "missing lowered global bytes in Wasm object mode");
    wb_bytes(&data->bytes, object->bytes, (size_t)object->byte_size);
    for (int index = 0; index < object->relocation_count; index++)
      emit_obj_data_reloc(
          context, data, &object->relocations[index]);
  }
  data->is_emitted = 1;
}

void wasm32_obj_emit_machine_data_segments_in(
    wasm32_obj_context_t *ctx,
    const wasm32_machine_module_t *machine_module) {
  if (!ctx) abort();
  wasm32_obj_context_t *context = ctx;
  if (!machine_module)
    obj_unsupported_msg(context, "missing Wasm machine data module");
  int object_count = 0;
  for (size_t index = 0; index < machine_module->data_object_count; index++)
    if (machine_module->data_objects[index].kind !=
        WASM32_MACHINE_DATA_FLOAT)
      object_count++;
  reserve_data_capacity(context, g_obj.data_count + object_count + 8);
  for (size_t index = 0; index < machine_module->data_object_count; index++)
    intern_lowered_data_object(
        context, &machine_module->data_objects[index]);
  for (size_t index = 0; index < machine_module->data_object_count;
       index++) {
    const wasm32_machine_data_object_t *object =
        &machine_module->data_objects[index];
    if (object->kind == WASM32_MACHINE_DATA_STRING)
      emit_obj_string_literal(context, object);
    else if (object->kind == WASM32_MACHINE_DATA_OBJECT)
      emit_obj_global(context, object);
  }
}

static void wasm32_obj_end_context(wasm32_obj_context_t *context) {
  assign_indices(context);

  wb_t out = {.diagnostic_context = context->diagnostic_context};
  if (g_obj.capture_output) out.max_len = g_obj_capture_limit;
  wb_u32le(&out, 0x6d736100);
  wb_u32le(&out, 1);
  wasm32_obj_serialize_sections(context, &out);

  if (out.overflow) {
    free(out.data);
    if (g_obj.capture_output) {
      free(g_obj_capture.data);
      g_obj_capture = (wb_t){
          .diagnostic_context = context->diagnostic_context};
      g_obj_capture_limit_exceeded = 1;
      return;
    }
    diag_error_id_t id =
        DIAG_ERR_CODEGEN_WASM_OBJECT_ADDRESSABLE_SIZE_EXCEEDED;
    diag_emit_internalf_in(
        wasm32_obj_diagnostics(context), id, "%s",
        diag_message_for_in(wasm32_obj_diagnostics(context), id));
  }

  if (g_obj.out && fwrite(out.data, 1, out.len, g_obj.out) != out.len) {
    diag_error_id_t id = DIAG_ERR_CODEGEN_WASM_OBJECT_WRITE_FAILED;
    diag_emit_internalf_in(
        wasm32_obj_diagnostics(context), id, "%s",
        diag_message_for_in(wasm32_obj_diagnostics(context), id));
  }
  if (g_obj.capture_output) {
    free(g_obj_capture.data);
    g_obj_capture = out;
  } else {
    if (!g_obj.out) {
      diag_error_id_t id =
          DIAG_ERR_CODEGEN_WASM_OBJECT_OUTPUT_SINK_MISSING;
      diag_emit_internalf_in(
          wasm32_obj_diagnostics(context), id, "%s",
          diag_message_for_in(wasm32_obj_diagnostics(context), id));
    }
    free(out.data);
  }
}

void wasm32_obj_end_in(wasm32_obj_context_t *ctx) {
  if (!ctx) abort();
  wasm32_obj_end_context(ctx);
}
