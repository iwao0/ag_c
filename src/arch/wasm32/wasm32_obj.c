#include "wasm32_obj.h"
#include "wasm32_machine_abi.h"
#include "wasm32_machine_function.h"
#include "wasm32_machine_ir.h"
#include "../../diag/diag.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  WASM_SEC_TYPE = 1,
  WASM_SEC_IMPORT = 2,
  WASM_SEC_FUNCTION = 3,
  WASM_SEC_CODE = 10,
  WASM_SEC_DATA = 11,
  WASM_SEC_DATACOUNT = 12,

  R_WASM_FUNCTION_INDEX_LEB = 0,
  R_WASM_TABLE_INDEX_SLEB = 1,
  R_WASM_TABLE_INDEX_I32 = 2,
  R_WASM_MEMORY_ADDR_LEB = 3,
  R_WASM_MEMORY_ADDR_I32 = 5,
  R_WASM_TYPE_INDEX_LEB = 6,
  R_WASM_GLOBAL_INDEX_LEB = 7,

  WASM_SYM_FUNCTION = 0,
  WASM_SYM_DATA = 1,
  WASM_SYM_GLOBAL = 2,

  WASM_SYMBOL_BINDING_LOCAL = 0x2,
  WASM_SYMBOL_UNDEFINED = 0x10,
  WASM_SYMBOL_EXPLICIT_NAME = 0x40,

  WASM_SEGMENT_INFO = 5,
  WASM_SYMBOL_TABLE = 8,
};

typedef struct {
  ag_diagnostic_context_t *diagnostic_context;
  unsigned char *data;
  uint32_t len;
  uint32_t cap;
  uint32_t max_len;
  int overflow;
} wb_t;

typedef struct {
  int target_sym;
  int target_is_data;
  int target_is_global;
  int target_is_type;
  uint32_t body_off;
  int type;
  int addend;
} obj_reloc_t;

typedef wasm32_machine_signature_t obj_sig_t;

typedef struct {
  char *name;
  int name_len;
  char *c_signature;
  int c_signature_len;
  obj_sig_t sig;
  int imported;
  int defined;
  int is_static;
  int type_index;
  int func_index;
  int symbol_index;
  wb_t body;
  obj_reloc_t *relocs;
  int reloc_count;
  int reloc_cap;
} obj_func_t;

typedef struct {
  char *name;
  int name_len;
  wb_t bytes;
  size_t alloc_size;
  int align;
  int segment_index;
  int symbol_index;
  int is_static;
  int is_undefined;
  int is_emitted;
  obj_reloc_t *relocs;
  int reloc_count;
  int reloc_cap;
} obj_data_t;

typedef struct {
  char *name;
  int name_len;
  int global_index;
  int symbol_index;
} obj_global_t;

typedef struct {
  FILE *out;
  int capture_output;
  obj_func_t *funcs;
  int func_count;
  int func_cap;
  obj_data_t *data;
  int data_count;
  int data_cap;
  obj_global_t *globals;
  int global_count;
  int global_cap;
  obj_sig_t *types;
  int type_count;
  int type_cap;
  obj_reloc_t *code_relocs;
  int code_reloc_count;
  int code_reloc_cap;
  obj_reloc_t *data_relocs;
  int data_reloc_count;
  int data_reloc_cap;
  int symbol_count;
  int has_indirect_call;
  char *continuation_entry;
  char *continuation_condition;
  char *continuation_step;
  char *continuation_start;
  char *continuation_resume;
  char *continuation_status;
  char *continuation_result;
} obj_ctx_t;

struct wasm32_obj_context_t {
  ag_diagnostic_context_t *diagnostic_context;
  obj_ctx_t obj;
  wb_t capture;
  uint32_t capture_limit;
  int capture_limit_exceeded;
  ir_type_t *emit_local_types;
  unsigned char *emit_local_unsigned;
  int emit_local_count;
  const wasm32_machine_primitive_plan_t *primitives;
};

static void wasm32_obj_clear_module(obj_ctx_t *obj) {
  if (!obj) return;
  for (int i = 0; i < obj->func_count; i++) {
    free(obj->funcs[i].name);
    free(obj->funcs[i].c_signature);
    free(obj->funcs[i].sig.params);
    free(obj->funcs[i].body.data);
    free(obj->funcs[i].relocs);
  }
  for (int i = 0; i < obj->data_count; i++) {
    free(obj->data[i].name);
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

static int wb_reserve(wb_t *b, size_t add) {
  if (add > UINT32_MAX || b->len > UINT32_MAX - (uint32_t)add) {
    b->overflow = 1;
    return 0;
  }
  uint32_t need = b->len + (uint32_t)add;
  if (b->max_len && need > b->max_len) {
    b->overflow = 1;
    return 0;
  }
  if (need <= b->cap) return 1;
  uint32_t ncap = b->cap
      ? (b->cap > UINT32_MAX / 2 ? UINT32_MAX : b->cap * 2)
      : 128;
  while (ncap < need && ncap <= UINT32_MAX / 2) ncap *= 2;
  if (ncap < need) ncap = need;
  if (b->max_len && ncap > b->max_len) ncap = b->max_len;
  b->data = xrealloc(b->diagnostic_context, b->data, ncap);
  b->cap = ncap;
  return 1;
}

static void wb_u8(wb_t *b, unsigned v) {
  if (!wb_reserve(b, 1)) return;
  b->data[b->len++] = (unsigned char)v;
}

static void wb_bytes(wb_t *b, const void *p, size_t n) {
  if (n == 0) return;
  if (!wb_reserve(b, n)) return;
  memcpy(b->data + b->len, p, n);
  b->len += (uint32_t)n;
}

static void wb_u32le(wb_t *b, uint32_t v) {
  wb_u8(b, v & 0xff);
  wb_u8(b, (v >> 8) & 0xff);
  wb_u8(b, (v >> 16) & 0xff);
  wb_u8(b, (v >> 24) & 0xff);
}

static void wb_uleb(wb_t *b, uint32_t v) {
  do {
    unsigned char byte = (unsigned char)(v & 0x7f);
    v >>= 7;
    if (v) byte |= 0x80;
    wb_u8(b, byte);
  } while (v);
}

static void wb_sleb(wb_t *b, int64_t v) {
  int more = 1;
  while (more) {
    unsigned char byte = (unsigned char)(v & 0x7f);
    int sign = byte & 0x40;
    uint64_t shifted = (uint64_t)v >> 7;
    if (v < 0) shifted |= (~(uint64_t)0) << (64 - 7);
    v = (int64_t)shifted;
    if ((v == 0 && !sign) || (v == -1 && sign)) more = 0;
    else byte |= 0x80;
    wb_u8(b, byte);
  }
}

static uint32_t wb_uleb5(wb_t *b, uint32_t v) {
  uint32_t off = b->len;
  for (int i = 0; i < 5; i++) {
    unsigned char byte = (unsigned char)(v & 0x7f);
    v >>= 7;
    if (i != 4) byte |= 0x80;
    wb_u8(b, byte);
  }
  return off;
}

static void wb_patch_uleb5(unsigned char *p, uint32_t v) {
  for (int i = 0; i < 5; i++) {
    unsigned char byte = (unsigned char)(v & 0x7f);
    v >>= 7;
    if (i != 4) byte |= 0x80;
    p[i] = byte;
  }
}

static void wb_str(wb_t *b, const char *s, int len) {
  wb_uleb(b, (uint32_t)len);
  wb_bytes(b, s, (size_t)len);
}

static void emit_section(wb_t *out, int id, wb_t *payload) {
  wb_u8(out, (unsigned)id);
  wb_uleb(out, (uint32_t)payload->len);
  wb_bytes(out, payload->data, payload->len);
}

static void emit_custom_section(wb_t *out, const char *name, wb_t *payload) {
  wb_t sec = {.diagnostic_context = out->diagnostic_context};
  wb_str(&sec, name, (int)strlen(name));
  wb_bytes(&sec, payload->data, payload->len);
  emit_section(out, 0, &sec);
  free(sec.data);
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
  while (v < size && v < 8) {
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

static obj_sig_t copy_signature(
    wasm32_obj_context_t *context, const obj_sig_t *source) {
  obj_sig_t copy = *source;
  copy.params = NULL;
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

static void add_global_reloc(
                             wasm32_obj_context_t *context,
                             obj_reloc_t **arr, int *count, int *cap, int type,
                             uint32_t off, int target_sym, int addend) {
  if (*count == *cap) {
    int ncap = *cap ? *cap * 2 : 16;
    *arr = xrealloc(
        context->diagnostic_context, *arr,
        (size_t)ncap * sizeof(**arr));
    *cap = ncap;
  }
  obj_reloc_t *r = &(*arr)[(*count)++];
  r->body_off = (uint32_t)off;
  r->type = type;
  r->target_sym = target_sym;
  r->target_is_data = 0;
  r->target_is_global = 0;
  r->target_is_type = 0;
  r->addend = addend;
}

static void add_code_reloc(
    wasm32_obj_context_t *context, uint32_t off, obj_reloc_t *src) {
  if (g_obj.code_reloc_count == g_obj.code_reloc_cap) {
    int ncap = g_obj.code_reloc_cap ? g_obj.code_reloc_cap * 2 : 16;
    g_obj.code_relocs = xrealloc(
        context->diagnostic_context, g_obj.code_relocs,
        (size_t)ncap * sizeof(*g_obj.code_relocs));
    g_obj.code_reloc_cap = ncap;
  }
  obj_reloc_t *r = &g_obj.code_relocs[g_obj.code_reloc_count++];
  r->body_off = off;
  r->type = src->type;
  r->target_sym = src->target_sym;
  r->target_is_data = 0;
  r->target_is_global = 0;
  r->target_is_type = 0;
  r->addend = src->addend;
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
    wb_u8(b, 0x6a); /* i32.add */
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

static void ensure_func_sig_for_address(
    wasm32_obj_context_t *context,
    char *sym, int sym_len, obj_sig_t sig) {
  obj_func_t *target = find_func(context, sym, sym_len);
  if (!target) {
    target = intern_func(context, sym, sym_len);
    target->sig = sig;
    return;
  }
  if (!target->defined && target->sig.nparams == 0 && target->sig.result == IR_TY_VOID) {
    target->sig = sig;
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
  wb_u8(b, 0x6a);
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
    wb_u8(b, 0x6a);
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
    wb_u8(body, 0x6a);
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

static int emit_variadic_arg_area_prepare(
                                          wasm32_obj_context_t *context,
                                          wb_t *b, obj_func_t *of, obj_global_t *stack_pointer,
                                          obj_global_t **va_arg_area, int old_va_arg_area_local,
                                          const wasm32_machine_call_t *call,
                                          int param_count) {
  if (!call->is_variadic) return 0;
  const wasm32_machine_argument_t *arguments = call->arguments;
  int bytes = call->variadic_area_size;
  if (bytes <= 0) return 0;
  if (!stack_pointer)
    obj_unsupported_msg(
        context, "variadic call without stack pointer in Wasm object mode");
  if (!*va_arg_area) *va_arg_area = intern_va_arg_area_global(context);

  emit_stack_global_get(context, b, of, *va_arg_area);
  emit_local_set(b, old_va_arg_area_local);

  emit_stack_global_get(context, b, of, stack_pointer);
  emit_const(context, b, IR_TY_I32, bytes);
  wb_u8(b, 0x6b);
  emit_stack_global_set(context, b, of, stack_pointer);

  emit_stack_global_get(context, b, of, stack_pointer);
  emit_stack_global_set(context, b, of, *va_arg_area);

  for (int i = 0; i < call->variadic_argument_count; i++) {
    const wasm32_machine_variadic_argument_t *variadic =
        &call->variadic_arguments[i];
    emit_stack_global_get(context, b, of, *va_arg_area);
    emit_const(context, b, IR_TY_I32, variadic->byte_offset);
    wb_u8(b, 0x6a);
    emit_abi_argument(
        context, b, &arguments[variadic->argument_index],
        variadic->argument_type, param_count);
    if (variadic->conversion.opcode != WASM32_MI_COPY)
      wb_u8(
          b, conversion_binary_or_unsupported(
                 context, variadic->conversion));
    wb_u8(b, memory_binary_or_unsupported(context, variadic->store));
    emit_selected_memarg(b, &variadic->store);
  }
  return bytes;
}

static void emit_variadic_arg_area_restore(
                                           wasm32_obj_context_t *context,
                                           wb_t *b, obj_func_t *of, obj_global_t *stack_pointer,
                                           obj_global_t *va_arg_area,
                                           int old_va_arg_area_local, int bytes) {
  if (bytes <= 0) return;
  emit_stack_global_get(context, b, of, stack_pointer);
  emit_const(context, b, IR_TY_I32, bytes);
  wb_u8(b, 0x6a);
  emit_stack_global_set(context, b, of, stack_pointer);

  emit_local_get(b, old_va_arg_area_local);
  emit_stack_global_set(context, b, of, va_arg_area);
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
  int frame_size = machine_function.frame_size;
  int has_variadic_varargs = machine_function.has_variadic_varargs;
  int has_persistent_continuation_frame =
      machine_function.is_continuation_entry &&
      machine_function.continuation_has_suspend;
  int has_stack_restore = !has_persistent_continuation_frame &&
      (frame_size > 0 || machine_function.has_vla_alloc ||
       has_variadic_varargs);
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
      (has_stack_restore || has_variadic_varargs)
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
    wb_u8(&body, 0x6b);
    emit_local_set(&body, fp_local);
    emit_local_get(&body, fp_local);
    emit_stack_global_set(context, &body, of, stack_pointer);
  }
  if (machine_function.is_continuation_entry) {
    /* command=-1 is start; all other values resume the pending condition. */
    emit_local_get(&body, 0);
    emit_const(context, &body, IR_TY_I32, -1);
    wb_u8(&body, 0x46); /* i32.eq */
    wb_u8(&body, 0x04); wb_u8(&body, 0x40); /* if */
    emit_continuation_data_load(
        context, &body, of, continuation_status_data);
    wb_u8(&body, 0x45); /* i32.eqz */
    wb_u8(&body, 0x04); wb_u8(&body, 0x40);
    wb_u8(&body, 0x05); /* else: invalid double start */
    emit_const(context, &body, IR_TY_I32, -1); wb_u8(&body, 0x0f);
    wb_u8(&body, 0x0b);
    wb_u8(&body, 0x05); /* resume */
    emit_continuation_data_load(
        context, &body, of, continuation_status_data);
    emit_const(context, &body, IR_TY_I32, 2);
    wb_u8(&body, 0x46);
    wb_u8(&body, 0x04); wb_u8(&body, 0x40);
    wb_u8(&body, 0x05); /* else: resume without suspension */
    emit_const(context, &body, IR_TY_I32, -1); wb_u8(&body, 0x0f);
    wb_u8(&body, 0x0b);
    wb_u8(&body, 0x0b);
    emit_continuation_data_store_const(
        context, &body, of, continuation_status_data, 1);
    emit_local_get(&body, 0);
    emit_const(context, &body, IR_TY_I32, -1);
    wb_u8(&body, 0x47); /* i32.ne */
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
        wb_u8(&body, 0x6a);
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
        wb_u8(&body, 0x6a);
        emit_const(
            context, &body, IR_TY_I32,
            instruction->alignment.mask);
        wb_u8(&body, 0x71);
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
      wb_u8(&body, 0x46);
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
          wb_u8(&body, 0x6a);
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
            obj_func_t *target = intern_func(context, i->sym, i->sym_len);
            if (!target->defined && target->sig.nparams == 0 && target->sig.result == IR_TY_VOID) {
              target->sig = func_sig_from_machine_callable(
                  context, i, i->sym, i->sym_len);
            }
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
          wb_u8(&body, 0x6a);
          emit_const(
              context, &body, IR_TY_I32, i->alignment.mask);
          wb_u8(&body, 0x71);
          wb_u8(&body, 0x6b);
          emit_local_set(&body, local_index(param_count, i->dst.id));
          emit_local_get(&body, local_index(param_count, i->dst.id));
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
          wb_u8(&body, 0x6a);
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        case WASM32_MACHINE_INST_ALIGN_POINTER: {
          if (machine_function.is_continuation_entry) break;
          emit_addr_val(context, &body, i->src1, param_count);
          emit_const(
              context, &body, IR_TY_I32, i->alignment.addend);
          wb_u8(&body, 0x6a);
          emit_const(
              context, &body, IR_TY_I32, i->alignment.mask);
          wb_u8(&body, 0x71);
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
            wb_u8(&body, op_ty == IR_TY_I64 ? 0x50 : 0x45);
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
          int vararg_area_bytes =
              emit_variadic_arg_area_prepare(context, &body, of, stack_pointer, &va_arg_area,
                                             old_va_arg_area_local, call,
                                             param_count);
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
            emit_variadic_arg_area_restore(context, &body, of, stack_pointer, va_arg_area,
                                           old_va_arg_area_local, vararg_area_bytes);
            break;
          }
          if (!i->sym) obj_unsupported_inst(context, i);
          obj_func_t *target = intern_func(context, i->sym, i->sym_len);
          of = &g_obj.funcs[of_index];
          obj_sig_t *emit_sig = &target->sig;
          if (target->sig.nparams == 0 && target->sig.result == IR_TY_VOID && !target->defined) {
            target->sig = copy_signature(context, csig);
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
          emit_variadic_arg_area_restore(context, &body, of, stack_pointer, va_arg_area,
                                         old_va_arg_area_local, vararg_area_bytes);
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

static void set_helper_c_signature(
    wasm32_obj_context_t *context,
    obj_func_t *helper, const char *signature) {
  int len = (int)strlen(signature);
  helper->c_signature = xrealloc(
      context->diagnostic_context, helper->c_signature, (size_t)len + 1);
  memcpy(helper->c_signature, signature, (size_t)len + 1);
  helper->c_signature_len = len;
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
  set_helper_c_signature(
      context, helper, param_count ? "i32(i32)" : "i32()");
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

static int defined_data_count(wasm32_obj_context_t *context) {
  int n = 0;
  for (int i = 0; i < g_obj.data_count; i++) {
    if (!g_obj.data[i].is_undefined) n++;
  }
  return n;
}

static void emit_type_section(
    wasm32_obj_context_t *context, wb_t *out) {
  wb_t p = {.diagnostic_context = context->diagnostic_context};
  wb_uleb(&p, (uint32_t)g_obj.type_count);
  for (int i = 0; i < g_obj.type_count; i++) {
    obj_sig_t *s = &g_obj.types[i];
    wb_u8(&p, 0x60);
    wb_uleb(&p, (uint32_t)s->nparams);
    for (int a = 0; a < s->nparams; a++)
      wb_u8(&p, wasm_valtype(context, s->params[a]));
    if (s->result == IR_TY_VOID) {
      wb_uleb(&p, 0);
    } else {
      wb_uleb(&p, 1);
      wb_u8(&p, wasm_valtype(context, s->result));
    }
  }
  emit_section(out, WASM_SEC_TYPE, &p);
  free(p.data);
}

static void emit_import_section(
    wasm32_obj_context_t *context, wb_t *out) {
  int nimports = 0;
  for (int i = 0; i < g_obj.func_count; i++) if (g_obj.funcs[i].imported) nimports++;
  if (g_obj.has_indirect_call) nimports++;
  nimports++;
  nimports += g_obj.global_count;
  if (nimports == 0) return;
  wb_t p = {.diagnostic_context = context->diagnostic_context};
  wb_uleb(&p, (uint32_t)nimports);
  for (int i = 0; i < g_obj.func_count; i++) {
    obj_func_t *f = &g_obj.funcs[i];
    if (!f->imported) continue;
    wb_str(&p, "env", 3);
    wb_str(&p, f->name, f->name_len);
    wb_u8(&p, 0x00);
    wb_uleb(&p, (uint32_t)f->type_index);
  }
  if (g_obj.has_indirect_call) {
    wb_str(&p, "env", 3);
    wb_str(&p, "__indirect_function_table", 25);
    wb_u8(&p, 0x01);
    wb_u8(&p, 0x70);
    wb_u8(&p, 0x00);
    wb_uleb(&p, 0);
  }
  wb_str(&p, "env", 3);
  wb_str(&p, "__linear_memory", 15);
  wb_u8(&p, 0x02);
  wb_u8(&p, 0x00);
  wb_uleb(&p, 0);
  for (int i = 0; i < g_obj.global_count; i++) {
    obj_global_t *g = &g_obj.globals[i];
    wb_str(&p, "env", 3);
    wb_str(&p, g->name, g->name_len);
    wb_u8(&p, 0x03);
    wb_u8(&p, wasm_valtype(context, IR_TY_I32));
    wb_u8(&p, 0x01);
  }
  emit_section(out, WASM_SEC_IMPORT, &p);
  free(p.data);
}

static void emit_function_section(
    wasm32_obj_context_t *context, wb_t *out) {
  int ndefs = 0;
  for (int i = 0; i < g_obj.func_count; i++) if (g_obj.funcs[i].defined) ndefs++;
  if (ndefs == 0) return;
  wb_t p = {.diagnostic_context = context->diagnostic_context};
  wb_uleb(&p, (uint32_t)ndefs);
  for (int i = 0; i < g_obj.func_count; i++) {
    if (g_obj.funcs[i].defined) wb_uleb(&p, (uint32_t)g_obj.funcs[i].type_index);
  }
  emit_section(out, WASM_SEC_FUNCTION, &p);
  free(p.data);
}

static void emit_datacount_section(
    wasm32_obj_context_t *context, wb_t *out) {
  int ndata = defined_data_count(context);
  if (ndata == 0) return;
  wb_t p = {.diagnostic_context = context->diagnostic_context};
  wb_uleb(&p, (uint32_t)ndata);
  emit_section(out, WASM_SEC_DATACOUNT, &p);
  free(p.data);
}

static void emit_code_section(
    wasm32_obj_context_t *context, wb_t *out) {
  int ndefs = 0;
  for (int i = 0; i < g_obj.func_count; i++) if (g_obj.funcs[i].defined) ndefs++;
  if (ndefs == 0) return;
  wb_t p = {.diagnostic_context = context->diagnostic_context};
  wb_uleb(&p, (uint32_t)ndefs);
  for (int i = 0; i < g_obj.func_count; i++) {
    obj_func_t *f = &g_obj.funcs[i];
    if (!f->defined) continue;
    wb_t body_size = {
        .diagnostic_context = context->diagnostic_context};
    wb_uleb(&body_size, (uint32_t)f->body.len);
    uint32_t body_payload_start = p.len + body_size.len;
    wb_bytes(&p, body_size.data, body_size.len);
    wb_bytes(&p, f->body.data, f->body.len);
    for (int r = 0; r < f->reloc_count; r++) {
      add_code_reloc(
          context, body_payload_start + f->relocs[r].body_off,
          &f->relocs[r]);
    }
    free(body_size.data);
  }
  emit_section(out, WASM_SEC_CODE, &p);
  free(p.data);
}

static void emit_data_section(
    wasm32_obj_context_t *context, wb_t *out) {
  int ndata = defined_data_count(context);
  if (ndata == 0) return;
  wb_t p = {.diagnostic_context = context->diagnostic_context};
  wb_uleb(&p, (uint32_t)ndata);
  int seg_index = 0;
  for (int i = 0; i < g_obj.data_count; i++) {
    obj_data_t *d = &g_obj.data[i];
    if (d->is_undefined) continue;
    d->segment_index = seg_index++;
    wb_u8(&p, 0x00);
    wb_u8(&p, 0x41);
    wb_uleb(&p, 0);
    wb_u8(&p, 0x0b);
    wb_uleb(&p, (uint32_t)d->bytes.len);
    uint32_t data_start = p.len;
    wb_bytes(&p, d->bytes.data, d->bytes.len);
    for (int r = 0; r < d->reloc_count; r++) {
      int sym = d->relocs[r].target_is_data
                  ? g_obj.data[d->relocs[r].target_sym].symbol_index
                  : g_obj.funcs[d->relocs[r].target_sym].symbol_index;
      add_global_reloc(
                       context,
                       &g_obj.data_relocs, &g_obj.data_reloc_count, &g_obj.data_reloc_cap,
                       d->relocs[r].type, (uint32_t)data_start + d->relocs[r].body_off,
                       sym, d->relocs[r].addend);
    }
  }
  emit_section(out, WASM_SEC_DATA, &p);
  free(p.data);
}

static void emit_symbol_entry(wb_t *p, obj_func_t *f) {
  uint32_t flags = (f->is_static ? WASM_SYMBOL_BINDING_LOCAL : 0) | WASM_SYMBOL_EXPLICIT_NAME;
  if (f->imported) flags |= WASM_SYMBOL_UNDEFINED;
  wb_u8(p, WASM_SYM_FUNCTION);
  wb_uleb(p, flags);
  wb_uleb(p, (uint32_t)f->func_index);
  wb_str(p, f->name, f->name_len);
}

static void emit_data_symbol_entry(wb_t *p, obj_data_t *d) {
  uint32_t flags = d->is_static ? WASM_SYMBOL_BINDING_LOCAL : 0;
  if (d->is_undefined) flags |= WASM_SYMBOL_UNDEFINED;
  wb_u8(p, WASM_SYM_DATA);
  wb_uleb(p, flags);
  wb_str(p, d->name, d->name_len);
  if (d->is_undefined) return;
  wb_uleb(p, (uint32_t)d->segment_index);
  wb_uleb(p, 0);
  wb_uleb(p, (uint32_t)(d->alloc_size > d->bytes.len ? d->alloc_size : d->bytes.len));
}

static void emit_global_symbol_entry(wb_t *p, obj_global_t *g) {
  uint32_t flags = WASM_SYMBOL_UNDEFINED | WASM_SYMBOL_EXPLICIT_NAME;
  wb_u8(p, WASM_SYM_GLOBAL);
  wb_uleb(p, flags);
  wb_uleb(p, (uint32_t)g->global_index);
  wb_str(p, g->name, g->name_len);
}

static void emit_linking_section(
    wasm32_obj_context_t *context, wb_t *out) {
  wb_t payload = {.diagnostic_context = context->diagnostic_context};
  wb_uleb(&payload, 2);

  wb_t symtab = {.diagnostic_context = context->diagnostic_context};
  wb_uleb(&symtab, (uint32_t)g_obj.symbol_count);
  for (int i = 0; i < g_obj.func_count; i++) emit_symbol_entry(&symtab, &g_obj.funcs[i]);
  for (int i = 0; i < g_obj.data_count; i++) emit_data_symbol_entry(&symtab, &g_obj.data[i]);
  for (int i = 0; i < g_obj.global_count; i++) emit_global_symbol_entry(&symtab, &g_obj.globals[i]);
  wb_u8(&payload, WASM_SYMBOL_TABLE);
  wb_uleb(&payload, (uint32_t)symtab.len);
  wb_bytes(&payload, symtab.data, symtab.len);

  int ndata = defined_data_count(context);
  if (ndata > 0) {
    wb_t segs = {.diagnostic_context = context->diagnostic_context};
    wb_uleb(&segs, (uint32_t)ndata);
    for (int i = 0; i < g_obj.data_count; i++) {
      if (g_obj.data[i].is_undefined) continue;
      wb_str(&segs, g_obj.data[i].name, g_obj.data[i].name_len);
      wb_uleb(&segs, (uint32_t)g_obj.data[i].align);
      wb_uleb(&segs, 0);
    }
    wb_u8(&payload, WASM_SEGMENT_INFO);
    wb_uleb(&payload, (uint32_t)segs.len);
    wb_bytes(&payload, segs.data, segs.len);
    free(segs.data);
  }

  emit_custom_section(out, "linking", &payload);
  free(symtab.data);
  free(payload.data);
}

static void emit_c_signature_section(
    wasm32_obj_context_t *context, wb_t *out) {
  int count = 0;
  for (int i = 0; i < g_obj.func_count; i++) {
    if (g_obj.funcs[i].c_signature) count++;
  }
  if (count == 0) return;

  wb_t payload = {.diagnostic_context = context->diagnostic_context};
  wb_uleb(&payload, 1); /* format version */
  wb_uleb(&payload, (uint32_t)count);
  for (int i = 0; i < g_obj.func_count; i++) {
    obj_func_t *f = &g_obj.funcs[i];
    if (!f->c_signature) continue;
    wb_str(&payload, f->name, f->name_len);
    wb_str(&payload, f->c_signature, f->c_signature_len);
  }
  emit_custom_section(out, "agc.c_signature", &payload);
  free(payload.data);
}

static void emit_continuation_section(
    wasm32_obj_context_t *context, wb_t *out) {
  if (!g_obj.continuation_entry) return;
  wb_t payload = {.diagnostic_context = context->diagnostic_context};
  wb_uleb(&payload, 1); /* metadata version */
  wb_str(&payload, g_obj.continuation_entry,
         (int)strlen(g_obj.continuation_entry));
  wb_str(&payload, g_obj.continuation_condition,
         (int)strlen(g_obj.continuation_condition));
  wb_str(&payload, g_obj.continuation_step,
         (int)strlen(g_obj.continuation_step));
  wb_str(&payload, g_obj.continuation_start,
         (int)strlen(g_obj.continuation_start));
  wb_str(&payload, g_obj.continuation_resume,
         (int)strlen(g_obj.continuation_resume));
  wb_str(&payload, g_obj.continuation_status,
         (int)strlen(g_obj.continuation_status));
  wb_str(&payload, g_obj.continuation_result,
         (int)strlen(g_obj.continuation_result));
  emit_custom_section(out, "agc.continuation", &payload);
  free(payload.data);
}

static void emit_reloc_section(
    wasm32_obj_context_t *context,
    wb_t *out, const char *name, int target_section,
    obj_reloc_t *relocs, int reloc_count) {
  if (reloc_count == 0) return;
  wb_t p = {.diagnostic_context = context->diagnostic_context};
  wb_uleb(&p, (uint32_t)target_section);
  wb_uleb(&p, (uint32_t)reloc_count);
  for (int i = 0; i < reloc_count; i++) {
    wb_uleb(&p, (uint32_t)relocs[i].type);
    wb_uleb(&p, (uint32_t)relocs[i].body_off);
    wb_uleb(&p, (uint32_t)relocs[i].target_sym);
    if (relocs[i].type == R_WASM_MEMORY_ADDR_LEB ||
        relocs[i].type == R_WASM_MEMORY_ADDR_I32) {
      wb_sleb(&p, relocs[i].addend);
    }
  }
  emit_custom_section(out, name, &p);
  free(p.data);
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
    if (of->sig.nparams > 0 || of->sig.result != IR_TY_VOID) {
      if (!sig_equal(&of->sig, &def_sig) &&
          !sig_integer_width_compatible(&of->sig, &def_sig)) {
        char msg[160];
        snprintf(msg, sizeof(msg), "conflicting Wasm object function signature: %.*s",
                 function->name_len, function->name);
        obj_unsupported_msg(context, msg);
      }
      free(def_sig.params);
    } else {
      of->sig = def_sig;
    }
    if (function->c_signature && function->c_signature_len > 0) {
      of->c_signature = xrealloc(
          context->diagnostic_context, of->c_signature,
          (size_t)function->c_signature_len + 1);
      memcpy(of->c_signature, function->c_signature,
             (size_t)function->c_signature_len + 1);
      of->c_signature_len = function->c_signature_len;
    }
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
  return intern_data(
      context,
      object->name, object->name_len,
      align_log2_for_size(object->alignment),
      is_string ? 1 : object->is_static,
      is_string ? 0 : object->is_extern);
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
        copy_signature(context, &reloc->function_signature));
    obj_func_t *target = find_func(
        context, reloc->target, reloc->target_len);
    if (!target)
      obj_unsupported_msg(context, "missing function relocation target");
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

  int has_imports = 1; /* __linear_memory is always imported. */
  int has_defs = 0;
  for (int i = 0; i < g_obj.func_count; i++) {
    if (g_obj.funcs[i].imported) has_imports = 1;
    if (g_obj.funcs[i].defined) has_defs = 1;
  }
  if (g_obj.has_indirect_call) has_imports = 1;
  if (g_obj.global_count > 0) has_imports = 1;
  int section_index = 0;
  int code_section_index = -1;
  int data_section_index = -1;
  int ndata = defined_data_count(context);
  section_index++; /* Type */
  if (has_imports) section_index++;
  if (has_defs) section_index++; /* Function */
  if (ndata > 0) section_index++; /* DataCount */
  if (has_defs) code_section_index = section_index++;
  if (ndata > 0) data_section_index = section_index++;

  wb_t out = {.diagnostic_context = context->diagnostic_context};
  if (g_obj.capture_output) out.max_len = g_obj_capture_limit;
  wb_u32le(&out, 0x6d736100);
  wb_u32le(&out, 1);
  emit_type_section(context, &out);
  emit_import_section(context, &out);
  emit_function_section(context, &out);
  emit_datacount_section(context, &out);
  emit_code_section(context, &out);
  emit_data_section(context, &out);
  emit_c_signature_section(context, &out);
  emit_continuation_section(context, &out);
  emit_linking_section(context, &out);
  emit_reloc_section(
      context, &out, "reloc.CODE", code_section_index,
      g_obj.code_relocs, g_obj.code_reloc_count);
  emit_reloc_section(
      context, &out, "reloc.DATA", data_section_index,
      g_obj.data_relocs, g_obj.data_reloc_count);

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
