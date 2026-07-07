#include "wasm32_obj.h"
#include "../diag/diag.h"
#include "../parser/parser_public.h"
#include "../tokenizer/literals.h"
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
  unsigned char *data;
  uint32_t len;
  uint32_t cap;
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

typedef struct {
  ir_type_t *params;
  int nparams;
  ir_type_t result;
} obj_sig_t;

typedef struct {
  char *name;
  int name_len;
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
} obj_ctx_t;

static obj_ctx_t g_obj;
static wb_t g_obj_capture;

static const char STACK_POINTER_NAME[] = "__stack_pointer";
static const char VA_ARG_AREA_NAME[] = "__ag_va_arg_area";

static void obj_unsupported_op(ir_op_t op) {
  diag_emit_internalf(DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP,
                      diag_message_for(DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP),
                      ir_op_name(op));
}

static void obj_unsupported_msg(const char *msg) {
  diag_emit_internalf(DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP,
                      diag_message_for(DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP),
                      msg);
}

static void *xrealloc(void *p, size_t n) {
  void *q = realloc(p, n);
  if (!q) diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
  return q;
}

static char *dup_name(const char *s, int len) {
  char *p = malloc((size_t)len + 1);
  if (!p) diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
  memcpy(p, s, (size_t)len);
  p[len] = '\0';
  return p;
}

static int name_eq(const char *a, int alen, const char *b, int blen) {
  return alen == blen && a && b && memcmp(a, b, (size_t)alen) == 0;
}

static void wb_reserve(wb_t *b, size_t add) {
  uint32_t need = b->len + (uint32_t)add;
  if (need <= b->cap) return;
  uint32_t ncap = b->cap ? b->cap * 2 : 128;
  while (ncap < need) ncap *= 2;
  b->data = xrealloc(b->data, ncap);
  b->cap = ncap;
}

static void wb_u8(wb_t *b, unsigned v) {
  wb_reserve(b, 1);
  b->data[b->len++] = (unsigned char)v;
}

static void obj_emit_string_literal_byte(unsigned char byte, void *user) {
  wb_u8((wb_t *)user, byte);
}

static void wb_bytes(wb_t *b, const void *p, size_t n) {
  if (n == 0) return;
  wb_reserve(b, n);
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
  wb_t sec = {0};
  wb_str(&sec, name, (int)strlen(name));
  wb_bytes(&sec, payload->data, payload->len);
  emit_section(out, 0, &sec);
  free(sec.data);
}

static unsigned wasm_valtype(ir_type_t t) {
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
      obj_unsupported_msg("unsupported Wasm object value type");
  }
  return 0;
}

static ir_type_t wasm_ir_type(ir_type_t t) {
  if (t == IR_TY_I8 || t == IR_TY_I16 || t == IR_TY_PTR) return IR_TY_I32;
  return t;
}

static obj_data_t *intern_data(const char *name, int name_len, int align_log2,
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

static int align_to(int v, int align) {
  if (align <= 1) return v;
  return (v + align - 1) & ~(align - 1);
}

static void wb_int_le(wb_t *b, uint64_t value, int size) {
  if (size < 0) obj_unsupported_msg("negative data size in Wasm object mode");
  for (int i = 0; i < size; i++) wb_u8(b, (unsigned)((value >> (8 * i)) & 0xff));
}

static void wb_zero(wb_t *b, int size) {
  if (size < 0) obj_unsupported_msg("negative data size in Wasm object mode");
  wb_reserve(b, (size_t)size);
  memset(b->data + b->len, 0, (size_t)size);
  b->len += (uint32_t)size;
}

static obj_data_t *data_for_symbol(char *sym, int sym_len, int *out_addend) {
  if (!sym) return NULL;
  if (psx_ctx_has_function_name(sym, sym_len >= 0 ? sym_len : (int)strlen(sym))) {
    obj_unsupported_msg("function address relocation in Wasm object mode");
  }
  int name_len = sym_len >= 0 ? sym_len : (int)strlen(sym);
  int is_undefined = sym_len >= 0 ? psx_gvar_is_extern_decl_by_name(sym, sym_len) : 0;
  int is_static = sym_len >= 0 ? psx_gvar_is_static_storage_by_name(sym, sym_len) : 0;
  if (out_addend) *out_addend = 0;
  return intern_data(sym, name_len, 2, is_static, is_undefined);
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

static obj_func_t *find_func(const char *name, int name_len) {
  for (int i = 0; i < g_obj.func_count; i++) {
    if (name_eq(g_obj.funcs[i].name, g_obj.funcs[i].name_len, name, name_len)) return &g_obj.funcs[i];
  }
  return NULL;
}

static obj_func_t *intern_func(const char *name, int name_len) {
  obj_func_t *f = find_func(name, name_len);
  if (f) return f;
  if (g_obj.func_count == g_obj.func_cap) {
    int ncap = g_obj.func_cap ? g_obj.func_cap * 2 : 32;
    g_obj.funcs = xrealloc(g_obj.funcs, (size_t)ncap * sizeof(*g_obj.funcs));
    memset(g_obj.funcs + g_obj.func_cap, 0, (size_t)(ncap - g_obj.func_cap) * sizeof(*g_obj.funcs));
    g_obj.func_cap = ncap;
  }
  f = &g_obj.funcs[g_obj.func_count++];
  f->name = dup_name(name, name_len);
  f->name_len = name_len;
  f->func_index = -1;
  f->symbol_index = -1;
  f->type_index = -1;
  return f;
}

static obj_data_t *find_data(const char *name, int name_len) {
  for (int i = 0; i < g_obj.data_count; i++) {
    if (name_eq(g_obj.data[i].name, g_obj.data[i].name_len, name, name_len)) return &g_obj.data[i];
  }
  return NULL;
}

static obj_data_t *intern_data(const char *name, int name_len, int align_log2,
                               int is_static, int is_undefined) {
  obj_data_t *d = find_data(name, name_len);
  if (d) {
    if (!is_undefined) d->is_undefined = 0;
    if (align_log2 > d->align) d->align = align_log2;
    if (is_static) d->is_static = 1;
    return d;
  }
  if (g_obj.data_count == g_obj.data_cap) {
    int ncap = g_obj.data_cap ? g_obj.data_cap * 2 : 32;
    g_obj.data = xrealloc(g_obj.data, (size_t)ncap * sizeof(*g_obj.data));
    memset(g_obj.data + g_obj.data_cap, 0, (size_t)(ncap - g_obj.data_cap) * sizeof(*g_obj.data));
    g_obj.data_cap = ncap;
  }
  d = &g_obj.data[g_obj.data_count++];
  d->name = dup_name(name, name_len);
  d->name_len = name_len;
  d->align = align_log2;
  d->segment_index = -1;
  d->symbol_index = -1;
  d->is_static = is_static;
  d->is_undefined = is_undefined;
  return d;
}

static void reserve_data_capacity(int min_cap) {
  if (min_cap <= g_obj.data_cap) return;
  int ncap = g_obj.data_cap ? g_obj.data_cap : 32;
  while (ncap < min_cap) ncap *= 2;
  g_obj.data = xrealloc(g_obj.data, (size_t)ncap * sizeof(*g_obj.data));
  memset(g_obj.data + g_obj.data_cap, 0,
         (size_t)(ncap - g_obj.data_cap) * sizeof(*g_obj.data));
  g_obj.data_cap = ncap;
}

static void data_note_alloc_size(obj_data_t *d, size_t size) {
  if (d && size > d->alloc_size) d->alloc_size = size;
}

static int data_index(obj_data_t *d) {
  return (int)(d - g_obj.data);
}

static obj_global_t *find_global_symbol(const char *name, int name_len) {
  for (int i = 0; i < g_obj.global_count; i++) {
    if (name_eq(g_obj.globals[i].name, g_obj.globals[i].name_len, name, name_len)) {
      return &g_obj.globals[i];
    }
  }
  return NULL;
}

static obj_global_t *intern_global_symbol(const char *name, int name_len) {
  obj_global_t *g = find_global_symbol(name, name_len);
  if (g) return g;
  if (g_obj.global_count == g_obj.global_cap) {
    int ncap = g_obj.global_cap ? g_obj.global_cap * 2 : 4;
    g_obj.globals = xrealloc(g_obj.globals, (size_t)ncap * sizeof(*g_obj.globals));
    memset(g_obj.globals + g_obj.global_cap, 0,
           (size_t)(ncap - g_obj.global_cap) * sizeof(*g_obj.globals));
    g_obj.global_cap = ncap;
  }
  g = &g_obj.globals[g_obj.global_count++];
  g->name = dup_name(name, name_len);
  g->name_len = name_len;
  g->global_index = -1;
  g->symbol_index = -1;
  return g;
}

static obj_global_t *intern_stack_pointer_global(void) {
  return intern_global_symbol(STACK_POINTER_NAME, (int)strlen(STACK_POINTER_NAME));
}

static obj_global_t *intern_va_arg_area_global(void) {
  return intern_global_symbol(VA_ARG_AREA_NAME, (int)strlen(VA_ARG_AREA_NAME));
}

static void func_add_reloc(obj_func_t *f, int type, uint32_t body_off, int target_sym,
                           int target_is_data, int addend) {
  if (f->reloc_count == f->reloc_cap) {
    int ncap = f->reloc_cap ? f->reloc_cap * 2 : 8;
    f->relocs = xrealloc(f->relocs, (size_t)ncap * sizeof(*f->relocs));
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

static void func_add_call_reloc(obj_func_t *f, uint32_t body_off, int target_sym) {
  if (f->reloc_count == f->reloc_cap) {
    int ncap = f->reloc_cap ? f->reloc_cap * 2 : 8;
    f->relocs = xrealloc(f->relocs, (size_t)ncap * sizeof(*f->relocs));
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

static void func_add_global_reloc(obj_func_t *f, int type, uint32_t body_off, int target_sym) {
  func_add_reloc(f, type, body_off, target_sym, 0, 0);
  f->relocs[f->reloc_count - 1].target_is_global = 1;
}

static void func_add_type_reloc(obj_func_t *f, uint32_t body_off, int type_index) {
  func_add_reloc(f, R_WASM_TYPE_INDEX_LEB, body_off, type_index, 0, 0);
  f->relocs[f->reloc_count - 1].target_is_type = 1;
}

static void data_add_reloc(obj_data_t *d, int type, uint32_t body_off, int target_sym,
                           int target_is_data, int addend) {
  if (d->reloc_count == d->reloc_cap) {
    int ncap = d->reloc_cap ? d->reloc_cap * 2 : 8;
    d->relocs = xrealloc(d->relocs, (size_t)ncap * sizeof(*d->relocs));
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

static void add_global_reloc(obj_reloc_t **arr, int *count, int *cap, int type,
                             uint32_t off, int target_sym, int addend) {
  if (*count == *cap) {
    int ncap = *cap ? *cap * 2 : 16;
    *arr = xrealloc(*arr, (size_t)ncap * sizeof(**arr));
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

static void add_code_reloc(uint32_t off, obj_reloc_t *src) {
  if (g_obj.code_reloc_count == g_obj.code_reloc_cap) {
    int ncap = g_obj.code_reloc_cap ? g_obj.code_reloc_cap * 2 : 16;
    g_obj.code_relocs = xrealloc(g_obj.code_relocs, (size_t)ncap * sizeof(*g_obj.code_relocs));
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

static int intern_type(const obj_sig_t *sig) {
  for (int i = 0; i < g_obj.type_count; i++) {
    if (sig_equal(&g_obj.types[i], sig)) return i;
  }
  if (g_obj.type_count == g_obj.type_cap) {
    int ncap = g_obj.type_cap ? g_obj.type_cap * 2 : 16;
    g_obj.types = xrealloc(g_obj.types, (size_t)ncap * sizeof(*g_obj.types));
    memset(g_obj.types + g_obj.type_cap, 0, (size_t)(ncap - g_obj.type_cap) * sizeof(*g_obj.types));
    g_obj.type_cap = ncap;
  }
  obj_sig_t *dst = &g_obj.types[g_obj.type_count];
  dst->nparams = sig->nparams;
  dst->result = wasm_ir_type(sig->result);
  if (sig->nparams > 0) {
    dst->params = xrealloc(NULL, (size_t)sig->nparams * sizeof(ir_type_t));
    for (int i = 0; i < sig->nparams; i++) dst->params[i] = wasm_ir_type(sig->params[i]);
  }
  return g_obj.type_count++;
}

static int local_index(int param_count, int vreg) {
  return param_count + vreg;
}

static ir_type_t *g_emit_local_types;
static unsigned char *g_emit_local_unsigned;
static int g_emit_local_count;

static ir_type_t actual_vreg_type(ir_val_t v) {
  if (v.id >= 0 && v.id < g_emit_local_count && g_emit_local_types) return g_emit_local_types[v.id];
  return wasm_ir_type(v.type);
}

static int actual_vreg_unsigned(ir_val_t v) {
  return v.id >= 0 && v.id < g_emit_local_count && g_emit_local_unsigned &&
         g_emit_local_unsigned[v.id];
}

static ir_type_t memory_access_type(ir_type_t raw, ir_type_t actual) {
  if (raw == IR_TY_I8 || raw == IR_TY_I16) return raw;
  if (raw == IR_TY_PTR) return actual == IR_TY_I64 ? IR_TY_I64 : IR_TY_I32;
  if (actual == IR_TY_I32 && raw != IR_TY_I32) return IR_TY_I32;
  return raw;
}

static void note_vreg_type(ir_type_t *types, int ntypes, ir_val_t v) {
  if (v.id >= 0 && v.id < ntypes && types[v.id] == IR_TY_I32) {
    types[v.id] = wasm_ir_type(v.type);
  }
}

static void note_vreg_unsigned(unsigned char *is_unsigned, int ntypes, ir_val_t v, int flag) {
  if (v.id >= 0 && v.id < ntypes) is_unsigned[v.id] = flag ? 1 : 0;
}

static int force_vreg_i32(ir_type_t *types, unsigned char *forced_i32, int ntypes, ir_val_t v) {
  if (v.id < 0 || v.id >= ntypes) return 0;
  int changed = types[v.id] != IR_TY_I32 || !forced_i32[v.id];
  types[v.id] = IR_TY_I32;
  forced_i32[v.id] = 1;
  return changed;
}

static ir_type_t func_param_type_from_decl(ir_func_t *f, int idx, ir_type_t raw);
static ir_type_t func_result_type_from_decl(const char *name, int name_len, ir_type_t raw);
static obj_sig_t call_sig_from_inst(ir_inst_t *i);

static int func_param_ordinal_for_inst(ir_func_t *f, ir_inst_t *target) {
  int ordinal = 0;
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      if (i->op != IR_PARAM || i->src1.id != IR_VAL_IMM || i->src1.imm < 0) continue;
      if (i == target) return ordinal;
      ordinal++;
    }
  }
  return -1;
}

static void collect_local_types(ir_func_t *f, ir_type_t *types, unsigned char *is_unsigned,
                                int ntypes) {
  for (int v = 0; v < ntypes; v++) types[v] = IR_TY_I32;
  unsigned char *forced_i32 = calloc((size_t)ntypes, 1);
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      note_vreg_type(types, ntypes, i->dst);
      note_vreg_unsigned(is_unsigned, ntypes, i->dst, i->is_unsigned);
      if (i->op == IR_LOAD_IMM && i->dst.type == IR_TY_I32 && i->src1.id == IR_VAL_IMM &&
          i->src1.imm > INT32_MAX) {
        note_vreg_unsigned(is_unsigned, ntypes, i->dst, 1);
      }
      note_vreg_type(types, ntypes, i->src1);
      note_vreg_type(types, ntypes, i->src2);
      note_vreg_type(types, ntypes, i->src3);
      note_vreg_type(types, ntypes, i->callee);
      note_vreg_type(types, ntypes, i->ret_struct_area);
      for (int a = 0; a < i->nargs; a++) note_vreg_type(types, ntypes, i->args[a]);
    }
  }
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      if (i->op != IR_PARAM || i->dst.id < 0 || i->dst.id >= ntypes ||
          i->src1.id != IR_VAL_IMM) {
        continue;
      }
      if (i->src1.imm < 0) {
        force_vreg_i32(types, forced_i32, ntypes, i->dst);
      } else {
        int ordinal = func_param_ordinal_for_inst(f, i);
        ir_type_t pty = func_param_type_from_decl(f, ordinal, i->dst.type);
        types[i->dst.id] = wasm_ir_type(pty);
        if (pty == IR_TY_PTR) {
          force_vreg_i32(types, forced_i32, ntypes, i->dst);
        }
      }
    }
  }
  int changed = 1;
  while (changed) {
    changed = 0;
    for (ir_block_t *b = f->entry; b; b = b->next) {
      for (ir_inst_t *i = b->head; i; i = i->next) {
        switch (i->op) {
          case IR_ALLOCA:
          case IR_LOAD_STR:
          case IR_LOAD_SYM:
          case IR_LOAD_TLV_ADDR:
            changed |= force_vreg_i32(types, forced_i32, ntypes, i->dst);
            break;
          case IR_LOAD:
            changed |= force_vreg_i32(types, forced_i32, ntypes, i->src1);
            break;
          case IR_STORE:
            changed |= force_vreg_i32(types, forced_i32, ntypes, i->src1);
            break;
          case IR_ATOMIC:
            changed |= force_vreg_i32(types, forced_i32, ntypes, i->src1);
            if (i->atomic_kind == IR_ATOMIC_CAS) {
              changed |= force_vreg_i32(types, forced_i32, ntypes, i->src2);
            }
            break;
          case IR_MEMCPY:
            changed |= force_vreg_i32(types, forced_i32, ntypes, i->src1);
            changed |= force_vreg_i32(types, forced_i32, ntypes, i->src2);
            break;
          case IR_VLA_ALLOC:
            changed |= force_vreg_i32(types, forced_i32, ntypes, i->dst);
            changed |= force_vreg_i32(types, forced_i32, ntypes, i->src1);
            break;
          case IR_VA_ARG_AREA:
            changed |= force_vreg_i32(types, forced_i32, ntypes, i->dst);
            break;
          case IR_LEA:
            changed |= force_vreg_i32(types, forced_i32, ntypes, i->dst);
            changed |= force_vreg_i32(types, forced_i32, ntypes, i->src1);
            break;
          case IR_ALIGN_PTR:
            changed |= force_vreg_i32(types, forced_i32, ntypes, i->dst);
            changed |= force_vreg_i32(types, forced_i32, ntypes, i->src1);
            break;
          case IR_CALL:
            changed |= force_vreg_i32(types, forced_i32, ntypes, i->callee);
            if (i->dst.id >= 0 && i->dst.id < ntypes) {
              obj_sig_t csig = call_sig_from_inst(i);
              if (csig.result != IR_TY_VOID) {
                psx_function_ret_info_t ret =
                    i->sym ? psx_ctx_get_function_ret_info(i->sym, i->sym_len)
                           : (psx_function_ret_info_t){0};
                int force_result_i32 = i->dst.type == IR_TY_PTR ||
                                       ret.is_pointer || ret.is_funcptr;
                if (force_result_i32 || forced_i32[i->dst.id]) {
                  changed |= force_vreg_i32(types, forced_i32, ntypes, i->dst);
                } else if (types[i->dst.id] != csig.result) {
                  types[i->dst.id] = csig.result;
                  changed = 1;
                }
              }
              free(csig.params);
            }
            break;
          case IR_ADD:
          case IR_SUB:
            if (i->dst.id >= 0 && i->dst.id < ntypes && forced_i32[i->dst.id]) {
              changed |= force_vreg_i32(types, forced_i32, ntypes, i->src1);
              changed |= force_vreg_i32(types, forced_i32, ntypes, i->src2);
            }
            if ((i->src1.id >= 0 && i->src1.id < ntypes && forced_i32[i->src1.id]) ||
                (i->src2.id >= 0 && i->src2.id < ntypes && forced_i32[i->src2.id])) {
              changed |= force_vreg_i32(types, forced_i32, ntypes, i->dst);
            }
            break;
          default:
            break;
        }
      }
    }
  }
  free(forced_i32);
}

static int collect_frame_size(ir_func_t *f) {
  int frame_size = 0;
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      if (i->op != IR_ALLOCA) continue;
      int align = i->alloca_align > 0 ? i->alloca_align : 4;
      frame_size = align_to(frame_size, align);
      frame_size += i->alloca_size;
    }
  }
  return align_to(frame_size, 16);
}

static int func_has_vla_alloc(ir_func_t *f) {
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      if (i->op == IR_VLA_ALLOC) return 1;
    }
  }
  return 0;
}

static int func_has_variadic_varargs(ir_func_t *f) {
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      if (i->op == IR_CALL && i->is_variadic_call && i->nargs > i->nargs_fixed) {
        return 1;
      }
    }
  }
  return 0;
}

static int func_has_control_flow(ir_func_t *f) {
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      if (i->op == IR_LABEL || i->op == IR_BR || i->op == IR_BR_COND) return 1;
    }
  }
  return 0;
}

static int func_has_atomic_cas_width(ir_func_t *f, int width64) {
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      if (i->op != IR_ATOMIC || i->atomic_kind != IR_ATOMIC_CAS) continue;
      if ((i->atomic_width == 8) == width64) return 1;
    }
  }
  return 0;
}

static int block_has_terminator(ir_block_t *b) {
  ir_inst_t *tail = b ? b->tail : NULL;
  return tail && (tail->op == IR_BR || tail->op == IR_BR_COND || tail->op == IR_RET);
}

static int alloca_offset(ir_func_t *f, int vreg) {
  int frame_size = 0;
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      if (i->op != IR_ALLOCA) continue;
      int align = i->alloca_align > 0 ? i->alloca_align : 4;
      frame_size = align_to(frame_size, align);
      if (i->dst.id == vreg) return frame_size;
      frame_size += i->alloca_size;
    }
  }
  return -1;
}

static void emit_local_get(wb_t *b, int idx) {
  wb_u8(b, 0x20);
  wb_uleb(b, (uint32_t)idx);
}

static void emit_local_set(wb_t *b, int idx) {
  wb_u8(b, 0x21);
  wb_uleb(b, (uint32_t)idx);
}

static void emit_stack_global_get(wb_t *b, obj_func_t *of, obj_global_t *sp) {
  wb_u8(b, 0x23);
  uint32_t imm_off = wb_uleb5(b, 0);
  func_add_global_reloc(of, R_WASM_GLOBAL_INDEX_LEB, imm_off, (int)(sp - g_obj.globals));
}

static void emit_stack_global_set(wb_t *b, obj_func_t *of, obj_global_t *sp) {
  wb_u8(b, 0x24);
  uint32_t imm_off = wb_uleb5(b, 0);
  func_add_global_reloc(of, R_WASM_GLOBAL_INDEX_LEB, imm_off, (int)(sp - g_obj.globals));
}

static void emit_fp_const(wb_t *b, ir_type_t type, double value);

static void emit_const(wb_t *b, ir_type_t type, long long value) {
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
    emit_fp_const(b, type, (double)value);
  } else {
    char msg[96];
    snprintf(msg, sizeof(msg), "unsupported immediate type in Wasm object mode: %d", (int)type);
    obj_unsupported_msg(msg);
  }
}

static void emit_fp_const(wb_t *b, ir_type_t type, double value) {
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
    obj_unsupported_msg("floating-point immediate type in Wasm object mode");
  }
}

static unsigned i2f_opcode(ir_type_t dst, ir_type_t src, int is_unsigned);
static unsigned f2i_opcode(ir_type_t dst, ir_type_t src, int is_unsigned);

static void emit_val(wb_t *b, ir_val_t v, ir_type_t want, int param_count) {
  want = wasm_ir_type(want);
  if (v.id == IR_VAL_IMM) {
    if (want == IR_TY_F32 || want == IR_TY_F64) emit_fp_const(b, want, v.fp_imm);
    else emit_const(b, want, v.imm);
    return;
  }
  if (v.id < 0) obj_unsupported_msg("missing Wasm object value");
  ir_type_t got = actual_vreg_type(v);
  emit_local_get(b, local_index(param_count, v.id));
  if (got == want) return;
  if (got == IR_TY_I32 && want == IR_TY_I64) {
    wb_u8(b, actual_vreg_unsigned(v) ? 0xad : 0xac); /* i64.extend_i32_{u,s} */
  } else if (got == IR_TY_I64 && want == IR_TY_I32) {
    wb_u8(b, 0xa7); /* i32.wrap_i64 */
  } else if ((got == IR_TY_I32 || got == IR_TY_I64) &&
             (want == IR_TY_F32 || want == IR_TY_F64)) {
    wb_u8(b, i2f_opcode(want, got, 0));
  } else if ((got == IR_TY_F32 || got == IR_TY_F64) &&
             (want == IR_TY_I32 || want == IR_TY_I64)) {
    wb_u8(b, f2i_opcode(want, got, 0));
  } else {
    obj_unsupported_msg("unsupported Wasm object value cast");
  }
}

static void emit_stack_cast(wb_t *b, ir_type_t got, ir_type_t want, int is_unsigned) {
  got = wasm_ir_type(got);
  want = wasm_ir_type(want);
  if (got == want) return;
  if (got == IR_TY_I32 && want == IR_TY_I64) {
    wb_u8(b, is_unsigned ? 0xad : 0xac);
  } else if (got == IR_TY_I64 && want == IR_TY_I32) {
    wb_u8(b, 0xa7);
  } else if ((got == IR_TY_I32 || got == IR_TY_I64) &&
             (want == IR_TY_F32 || want == IR_TY_F64)) {
    wb_u8(b, i2f_opcode(want, got, is_unsigned));
  } else if ((got == IR_TY_F32 || got == IR_TY_F64) &&
             (want == IR_TY_I32 || want == IR_TY_I64)) {
    wb_u8(b, f2i_opcode(want, got, is_unsigned));
  } else {
    obj_unsupported_msg("unsupported Wasm object parameter cast");
  }
}

static void emit_addr_val(wb_t *b, ir_val_t v, int param_count) {
  if (v.id == IR_VAL_IMM) {
    emit_const(b, IR_TY_I32, v.imm);
    return;
  }
  if (v.id < 0) obj_unsupported_msg("missing Wasm object address");
  emit_local_get(b, local_index(param_count, v.id));
  emit_stack_cast(b, actual_vreg_type(v), IR_TY_I32, actual_vreg_unsigned(v));
}

static int mem_align_log2(ir_type_t ty) {
  switch (ty) {
    case IR_TY_I8: return 0;
    case IR_TY_I16: return 1;
    case IR_TY_I32:
    case IR_TY_PTR:
    case IR_TY_F32: return 2;
    case IR_TY_I64:
    case IR_TY_F64: return 3;
    default: obj_unsupported_msg("unsupported Wasm object memory type");
  }
  return 0;
}

static unsigned load_opcode(ir_type_t ty, int is_unsigned) {
  switch (ty) {
    case IR_TY_I8: return is_unsigned ? 0x2d : 0x2c;
    case IR_TY_I16: return is_unsigned ? 0x2f : 0x2e;
    case IR_TY_I32:
    case IR_TY_PTR: return 0x28;
    case IR_TY_I64: return 0x29;
    case IR_TY_F32: return 0x2a;
    case IR_TY_F64: return 0x2b;
    default: obj_unsupported_msg("unsupported Wasm object load type");
  }
  return 0;
}

static unsigned store_opcode(ir_type_t ty) {
  switch (ty) {
    case IR_TY_I8: return 0x3a;
    case IR_TY_I16: return 0x3b;
    case IR_TY_I32:
    case IR_TY_PTR: return 0x36;
    case IR_TY_I64: return 0x37;
    case IR_TY_F32: return 0x38;
    case IR_TY_F64: return 0x39;
    default: obj_unsupported_msg("unsupported Wasm object store type");
  }
  return 0;
}

static ir_type_t atomic_width_type(ir_inst_t *i) {
  switch (i->atomic_width ? i->atomic_width : 4) {
    case 1: return IR_TY_I8;
    case 2: return IR_TY_I16;
    case 4: return IR_TY_I32;
    case 8: return IR_TY_I64;
    default: obj_unsupported_op(i->op);
  }
  return IR_TY_I32;
}

static ir_type_t atomic_value_type(ir_inst_t *i) {
  return atomic_width_type(i) == IR_TY_I64 ? IR_TY_I64 : IR_TY_I32;
}

static unsigned atomic_rmw_opcode(ir_inst_t *i) {
  int is64 = atomic_value_type(i) == IR_TY_I64;
  switch (i->atomic_rmw_op) {
    case IR_ARMW_ADD: return is64 ? 0x7c : 0x6a;
    case IR_ARMW_SUB: return is64 ? 0x7d : 0x6b;
    case IR_ARMW_OR: return is64 ? 0x84 : 0x72;
    case IR_ARMW_AND: return is64 ? 0x83 : 0x71;
    case IR_ARMW_XOR: return is64 ? 0x85 : 0x73;
    default: obj_unsupported_op(i->op);
  }
  return 0;
}

static void emit_memarg(wb_t *b, ir_type_t ty) {
  wb_uleb(b, (uint32_t)mem_align_log2(ty));
  wb_uleb(b, 0);
}

static int collect_param_count(ir_func_t *f) {
  int count = 0;
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      if (i->op == IR_PARAM && i->src1.id == IR_VAL_IMM && i->src1.imm >= 0) {
        count++;
      }
    }
  }
  int declared = f ? psx_ctx_get_function_nargs_fixed(f->name, f->name_len) : 0;
  if (declared > count) {
    count = declared;
  }
  return count;
}

static int func_has_ret_area(ir_func_t *f) {
  return f && (f->ret_struct_size > 0 || f->ret_area_vreg >= 0 || f->ret_complex_half > 0);
}

static ir_type_t func_param_type_from_decl(ir_func_t *f, int idx, ir_type_t raw) {
  if (f && idx >= 0 && idx < f->param_abi_count && idx < 32) {
    return f->param_abi_types[idx];
  }
  int pcat = psx_ctx_get_function_param_category(f->name, f->name_len, idx);
  if (raw == IR_TY_PTR) return IR_TY_PTR;
  if (pcat == PSX_PCAT_PTR || pcat == PSX_PCAT_STRUCT) return IR_TY_PTR;
  tk_float_kind_t fp = psx_ctx_get_function_param_fp_kind(f->name, f->name_len, idx);
  if (fp == TK_FLOAT_KIND_FLOAT) return IR_TY_F32;
  if (fp >= TK_FLOAT_KIND_DOUBLE) return IR_TY_F64;
  int int_size = psx_ctx_get_function_param_int_size(f->name, f->name_len, idx);
  if (raw != IR_TY_PTR && int_size > 0) {
    return int_size == 8 ? IR_TY_I64 : IR_TY_I32;
  }
  return raw;
}

static ir_type_t func_result_type_from_decl(const char *name, int name_len, ir_type_t raw) {
  if (raw == IR_TY_VOID) return IR_TY_VOID;
  psx_function_ret_info_t ret = psx_ctx_get_function_ret_info((char *)name, name_len);
  if (ret.is_pointer || ret.is_funcptr) {
    return IR_TY_PTR;
  }
  if (ret.struct_size > 0) {
    return ret.struct_size == 8 ? IR_TY_I64 : IR_TY_I32;
  }
  if (ret.fp_kind == TK_FLOAT_KIND_FLOAT) return IR_TY_F32;
  if (ret.fp_kind >= TK_FLOAT_KIND_DOUBLE) return IR_TY_F64;
  if (ret.token_kind == TK_VOID) return IR_TY_VOID;
  if (ret.token_kind == TK_LONG) return IR_TY_I64;
  if (ret.token_kind != TK_EOF) return IR_TY_I32;
  return raw;
}

static obj_sig_t func_sig_from_ctx(const char *name, int name_len) {
  obj_sig_t sig = {0};
  psx_function_ret_info_t ret = psx_ctx_get_function_ret_info((char *)name, name_len);
  if (ret.is_pointer || ret.is_funcptr) {
    sig.result = IR_TY_I32;
  } else if (ret.struct_size > 0) {
    sig.result = ret.struct_size == 8
                   ? IR_TY_I64
                   : IR_TY_I32;
  } else if (ret.fp_kind == TK_FLOAT_KIND_FLOAT) {
    sig.result = IR_TY_F32;
  } else if (ret.fp_kind >= TK_FLOAT_KIND_DOUBLE) {
    sig.result = IR_TY_F64;
  } else if (ret.token_kind == TK_VOID) {
    sig.result = IR_TY_VOID;
  } else if (ret.token_kind == TK_LONG) {
    sig.result = IR_TY_I64;
  } else {
    sig.result = IR_TY_I32;
  }

  int nparams = 0;
  for (; nparams < 32; nparams++) {
    int pcat = psx_ctx_get_function_param_category((char *)name, name_len, nparams);
    tk_float_kind_t fp = psx_ctx_get_function_param_fp_kind((char *)name, name_len, nparams);
    int int_size = psx_ctx_get_function_param_int_size((char *)name, name_len, nparams);
    if (pcat == PSX_PCAT_UNSET && fp == TK_FLOAT_KIND_NONE && int_size <= 0) break;
  }
  sig.nparams = nparams;
  if (nparams > 0) {
    sig.params = xrealloc(NULL, (size_t)nparams * sizeof(ir_type_t));
    for (int p = 0; p < nparams; p++) {
      int pcat = psx_ctx_get_function_param_category((char *)name, name_len, p);
      tk_float_kind_t fp = psx_ctx_get_function_param_fp_kind((char *)name, name_len, p);
      if (pcat == PSX_PCAT_PTR || pcat == PSX_PCAT_STRUCT) sig.params[p] = IR_TY_I32;
      else if (fp == TK_FLOAT_KIND_FLOAT) sig.params[p] = IR_TY_F32;
      else if (fp >= TK_FLOAT_KIND_DOUBLE) sig.params[p] = IR_TY_F64;
      else sig.params[p] = IR_TY_I64;
    }
  }
  return sig;
}

static int funcptr_mask_param_count(unsigned short fp_mask, unsigned short int_mask) {
  int n = 0;
  for (int p = 0; p < 8; p++) {
    if (((fp_mask >> (2 * p)) & 3u) || ((int_mask >> (2 * p)) & 3u)) n = p + 1;
  }
  return n;
}

static ir_type_t funcptr_mask_param_type(unsigned fp, unsigned iw) {
  if (fp == TK_FLOAT_KIND_FLOAT) return IR_TY_F32;
  if (fp >= TK_FLOAT_KIND_DOUBLE) return IR_TY_F64;
  if (iw != 0) {
    if (iw == 3) return IR_TY_I32;
    return IR_TY_I64;
  }
  return IR_TY_I64;
}

static obj_sig_t func_sig_from_funcptr_sig(psx_decl_funcptr_sig_t fs) {
  obj_sig_t sig = {0};
  if (fs.ret_is_void) sig.result = IR_TY_VOID;
  else if (fs.ret_is_data_pointer) sig.result = IR_TY_I32;
  else if (fs.ret_fp_kind == TK_FLOAT_KIND_FLOAT) sig.result = IR_TY_F32;
  else if (fs.ret_fp_kind >= TK_FLOAT_KIND_DOUBLE) sig.result = IR_TY_F64;
  else sig.result = fs.ret_int_width == 8 ? IR_TY_I64 : IR_TY_I32;

  int nparams = fs.is_variadic
                    ? fs.nargs_fixed
                    : funcptr_mask_param_count(fs.param_fp_mask, fs.param_int_mask);
  sig.nparams = nparams;
  if (nparams > 0) {
    sig.params = xrealloc(NULL, (size_t)nparams * sizeof(ir_type_t));
    for (int p = 0; p < nparams; p++) {
      unsigned fp = (fs.param_fp_mask >> (2 * p)) & 3u;
      unsigned iw = (fs.param_int_mask >> (2 * p)) & 3u;
      sig.params[p] = funcptr_mask_param_type(fp, iw);
    }
  }
  return sig;
}

static obj_sig_t func_sig_from_global_funcptr(global_var_t *gv, const char *name, int name_len) {
  if (!gv) return func_sig_from_ctx(name, name_len);
  return func_sig_from_funcptr_sig(psx_gvar_funcptr_sig(gv));
}

static obj_sig_t func_sig_from_member_funcptr(const tag_member_info_t *mi,
                                              const char *name, int name_len) {
  if (!mi) return func_sig_from_ctx(name, name_len);
  return func_sig_from_funcptr_sig(psx_ctx_tag_member_funcptr_sig(mi));
}

static obj_sig_t func_sig_from_ir_funcptr(const ir_inst_t *inst, const char *name, int name_len) {
  if (!inst || !inst->has_funcptr_sig) return func_sig_from_ctx(name, name_len);
  return func_sig_from_funcptr_sig(inst->funcptr_sig);
}

static void ensure_func_sig_for_address(char *sym, int sym_len, obj_sig_t sig) {
  obj_func_t *target = find_func(sym, sym_len);
  if (!target) {
    target = intern_func(sym, sym_len);
    target->sig = sig;
    return;
  }
  if (!target->defined && target->sig.nparams == 0 && target->sig.result == IR_TY_VOID) {
    target->sig = sig;
  } else {
    free(sig.params);
  }
}

static int call_has_ret_area(ir_inst_t *i) {
  return i && (i->ret_struct_size > 0 || i->ret_struct_area.id != IR_VAL_NONE ||
               i->ret_complex_half > 0);
}

static ir_val_t call_ret_area(ir_inst_t *i) {
  if (i->ret_complex_half > 0) return i->dst;
  return i->ret_struct_area;
}

static void collect_func_sig(ir_func_t *f, obj_sig_t *sig) {
  int has_ret_area = func_has_ret_area(f);
  memset(sig, 0, sizeof(*sig));
  sig->nparams = collect_param_count(f) + (has_ret_area ? 1 : 0);
  sig->result = has_ret_area || f->ret_type == IR_TY_VOID
                  ? IR_TY_VOID
                  : wasm_ir_type(func_result_type_from_decl(f->name, f->name_len, f->ret_type));
  if (sig->nparams > 0) {
    sig->params = xrealloc(NULL, (size_t)sig->nparams * sizeof(ir_type_t));
    for (int p = 0; p < sig->nparams; p++) sig->params[p] = IR_TY_I64;
    if (has_ret_area) sig->params[0] = IR_TY_I32;
    for (ir_block_t *b = f->entry; b; b = b->next) {
      for (ir_inst_t *i = b->head; i; i = i->next) {
        if (i->op == IR_PARAM && i->src1.id == IR_VAL_IMM && i->src1.imm >= 0 &&
            i->src1.imm + (has_ret_area ? 1 : 0) < sig->nparams) {
          int ordinal = func_param_ordinal_for_inst(f, i);
          if (ordinal >= 0 && ordinal + (has_ret_area ? 1 : 0) < sig->nparams) {
            ir_type_t pty = func_param_type_from_decl(f, ordinal, i->dst.type);
            sig->params[ordinal + (has_ret_area ? 1 : 0)] = wasm_ir_type(pty);
          }
        }
      }
    }
  }
  if (!has_ret_area && psx_ctx_has_function_name(f->name, f->name_len)) {
    obj_sig_t ctx_sig = func_sig_from_ctx(f->name, f->name_len);
    if (ctx_sig.nparams == sig->nparams && ctx_sig.nparams > 0) {
      free(sig->params);
      *sig = ctx_sig;
    } else {
      free(ctx_sig.params);
    }
  }
}

static obj_sig_t call_sig_from_inst(ir_inst_t *i) {
  obj_sig_t sig = {0};
  int has_ret_area = call_has_ret_area(i);
  if (has_ret_area && call_ret_area(i).id == IR_VAL_NONE) {
    obj_unsupported_msg("aggregate call without return area in Wasm object mode");
  }
  if (!has_ret_area && i->callee.id != IR_VAL_NONE && i->has_funcptr_sig) {
    sig = func_sig_from_ir_funcptr(i, i->sym, i->sym_len);
    if (i->is_void_call || i->dst.id == IR_VAL_NONE || i->dst.type == IR_TY_VOID) {
      sig.result = IR_TY_VOID;
    }
    int call_nargs = i->is_variadic_call ? i->nargs_fixed : i->nargs;
    if (sig.nparams < call_nargs) {
      int old_nparams = sig.nparams;
      sig.params = xrealloc(sig.params, (size_t)call_nargs * sizeof(ir_type_t));
      for (int a = old_nparams; a < call_nargs; a++) {
        ir_type_t arg_ty = i->args[a].type;
        ir_type_t ty = wasm_ir_type(arg_ty);
        int null_ptr_pair_arg =
            a == 0 && call_nargs >= 2 && i->args[1].type == IR_TY_PTR;
        if (arg_ty == IR_TY_PTR || null_ptr_pair_arg) ty = IR_TY_I32;
        else if (arg_ty != IR_TY_F32 && arg_ty != IR_TY_F64) ty = IR_TY_I64;
        sig.params[a] = ty;
      }
      sig.nparams = call_nargs;
    }
    for (int a = 0; a < call_nargs && a < sig.nparams; a++) {
      ir_type_t arg_ty = i->args[a].type;
      int null_ptr_pair_arg =
          a == 0 && call_nargs >= 2 && i->args[1].type == IR_TY_PTR;
      psx_decl_funcptr_sig_t fs = i->funcptr_sig;
      unsigned iw = a < 8 ? ((fs.param_int_mask >> (2 * a)) & 3u) : 0;
      int funcptr_pointer_param = iw == 3;
      if (!fs.is_variadic && !i->is_variadic_call &&
          sig.params[a] == IR_TY_I32 && arg_ty != IR_TY_PTR &&
          arg_ty != IR_TY_F32 && arg_ty != IR_TY_F64 && !null_ptr_pair_arg &&
          !funcptr_pointer_param) {
        sig.params[a] = IR_TY_I64;
      }
    }
    if (!i->is_void_call && i->dst.id != IR_VAL_NONE) {
      if (i->dst.type == IR_TY_PTR ||
          (i->dst.type == IR_TY_I32 && (sig.result == IR_TY_F32 || sig.result == IR_TY_F64))) {
        sig.result = IR_TY_I32;
      } else if (i->dst.type == IR_TY_I64 && sig.result == IR_TY_I32 &&
                 !i->funcptr_sig.ret_is_data_pointer) {
        sig.result = IR_TY_I64;
      }
    }
    return sig;
  }
  sig.nparams = (i->is_variadic_call ? i->nargs_fixed : i->nargs) + (has_ret_area ? 1 : 0);
  if (sig.nparams > 0) {
    sig.params = xrealloc(NULL, (size_t)sig.nparams * sizeof(ir_type_t));
    if (has_ret_area) sig.params[0] = IR_TY_I32;
    int call_nargs = i->is_variadic_call ? i->nargs_fixed : i->nargs;
    for (int a = 0; a < call_nargs; a++) {
      ir_type_t arg_ty = i->args[a].type;
      ir_type_t ty = wasm_ir_type(arg_ty);
      int pcat = i->sym ? psx_ctx_get_function_param_category(i->sym, i->sym_len, a) : PSX_PCAT_UNSET;
      if (pcat == PSX_PCAT_PTR || arg_ty == IR_TY_PTR) ty = IR_TY_I32;
      else if (arg_ty != IR_TY_F32 && arg_ty != IR_TY_F64) ty = IR_TY_I64;
      sig.params[a + (has_ret_area ? 1 : 0)] = ty;
    }
  }
  if (has_ret_area || i->is_void_call || i->dst.id == IR_VAL_NONE || i->dst.type == IR_TY_VOID) {
    sig.result = IR_TY_VOID;
  } else {
    ir_type_t ret_ty = i->sym ? func_result_type_from_decl(i->sym, i->sym_len, i->dst.type)
                              : i->dst.type;
    sig.result = wasm_ir_type(ret_ty);
  }
  return sig;
}

static unsigned int_binop_opcode(ir_op_t op, ir_type_t ty) {
  int is64 = wasm_ir_type(ty) == IR_TY_I64;
  switch (op) {
    case IR_ADD: return is64 ? 0x7c : 0x6a;
    case IR_SUB: return is64 ? 0x7d : 0x6b;
    case IR_MUL: return is64 ? 0x7e : 0x6c;
    case IR_DIV: return is64 ? 0x7f : 0x6d;
    case IR_UDIV: return is64 ? 0x80 : 0x6e;
    case IR_MOD: return is64 ? 0x81 : 0x6f;
    case IR_UMOD: return is64 ? 0x82 : 0x70;
    case IR_AND: return is64 ? 0x83 : 0x71;
    case IR_OR: return is64 ? 0x84 : 0x72;
    case IR_XOR: return is64 ? 0x85 : 0x73;
    case IR_SHL: return is64 ? 0x86 : 0x74;
    case IR_SHR: return is64 ? 0x87 : 0x75;
    case IR_LSR: return is64 ? 0x88 : 0x76;
    case IR_EQ: return is64 ? 0x51 : 0x46;
    case IR_NE: return is64 ? 0x52 : 0x47;
    case IR_LT: return is64 ? 0x53 : 0x48;
    case IR_LE: return is64 ? 0x57 : 0x4c;
    case IR_ULT: return is64 ? 0x54 : 0x49;
    case IR_ULE: return is64 ? 0x58 : 0x4d;
    default: obj_unsupported_op(op);
  }
  return 0;
}

static unsigned fp_binop_opcode(ir_op_t op, ir_type_t ty) {
  int is64 = ty == IR_TY_F64;
  if (ty != IR_TY_F32 && ty != IR_TY_F64) obj_unsupported_op(op);
  switch (op) {
    case IR_FADD: return is64 ? 0xa0 : 0x92;
    case IR_FSUB: return is64 ? 0xa1 : 0x93;
    case IR_FMUL: return is64 ? 0xa2 : 0x94;
    case IR_FDIV: return is64 ? 0xa3 : 0x95;
    case IR_FEQ: return is64 ? 0x61 : 0x5b;
    case IR_FNE: return is64 ? 0x62 : 0x5c;
    case IR_FLT: return is64 ? 0x63 : 0x5d;
    case IR_FLE: return is64 ? 0x65 : 0x5f;
    default: obj_unsupported_op(op);
  }
  return 0;
}

static unsigned i2f_opcode(ir_type_t dst, ir_type_t src, int is_unsigned) {
  dst = wasm_ir_type(dst);
  src = wasm_ir_type(src);
  if (dst == IR_TY_F32 && src == IR_TY_I32) return is_unsigned ? 0xb3 : 0xb2;
  if (dst == IR_TY_F32 && src == IR_TY_I64) return is_unsigned ? 0xb5 : 0xb4;
  if (dst == IR_TY_F64 && src == IR_TY_I32) return is_unsigned ? 0xb8 : 0xb7;
  if (dst == IR_TY_F64 && src == IR_TY_I64) return is_unsigned ? 0xba : 0xb9;
  obj_unsupported_op(IR_I2F);
  return 0;
}

static unsigned f2i_opcode(ir_type_t dst, ir_type_t src, int is_unsigned) {
  dst = wasm_ir_type(dst);
  src = wasm_ir_type(src);
  if (dst == IR_TY_I32 && src == IR_TY_F32) return is_unsigned ? 0xa9 : 0xa8;
  if (dst == IR_TY_I32 && src == IR_TY_F64) return is_unsigned ? 0xab : 0xaa;
  if (dst == IR_TY_I64 && src == IR_TY_F32) return is_unsigned ? 0xaf : 0xae;
  if (dst == IR_TY_I64 && src == IR_TY_F64) return is_unsigned ? 0xb1 : 0xb0;
  obj_unsupported_op(IR_F2I);
  return 0;
}

static void emit_addr_plus_const(wb_t *b, ir_val_t v, int off, int param_count) {
  emit_addr_val(b, v, param_count);
  if (off == 0) return;
  emit_const(b, IR_TY_I32, off);
  wb_u8(b, 0x6a);
}

static void emit_copy_chunk(wb_t *b, ir_val_t dst, ir_val_t src, int off, ir_type_t ty,
                            int param_count) {
  emit_addr_plus_const(b, dst, off, param_count);
  emit_addr_plus_const(b, src, off, param_count);
  wb_u8(b, load_opcode(ty, 1));
  emit_memarg(b, ty);
  wb_u8(b, store_opcode(ty));
  emit_memarg(b, ty);
}

static void emit_memcpy_inline(wb_t *b, ir_inst_t *i, int param_count) {
  int n = i->alloca_size;
  if (n < 0) obj_unsupported_op(i->op);
  int off = 0;
  for (; off + 8 <= n; off += 8) emit_copy_chunk(b, i->src1, i->src2, off, IR_TY_I64, param_count);
  for (; off + 4 <= n; off += 4) emit_copy_chunk(b, i->src1, i->src2, off, IR_TY_I32, param_count);
  for (; off + 2 <= n; off += 2) emit_copy_chunk(b, i->src1, i->src2, off, IR_TY_I16, param_count);
  for (; off < n; off++) emit_copy_chunk(b, i->src1, i->src2, off, IR_TY_I8, param_count);
}

static int emit_variadic_arg_area_prepare(wb_t *b, obj_func_t *of, obj_global_t *stack_pointer,
                                          obj_global_t **va_arg_area, int old_va_arg_area_local,
                                          ir_inst_t *i, int param_count) {
  if (!i->is_variadic_call) return 0;
  int nargs_var = i->nargs - i->nargs_fixed;
  if (nargs_var <= 0) return 0;
  if (!stack_pointer) obj_unsupported_msg("variadic call without stack pointer in Wasm object mode");
  if (!*va_arg_area) *va_arg_area = intern_va_arg_area_global();
  int bytes = align_to(nargs_var * 8, 16);

  emit_stack_global_get(b, of, *va_arg_area);
  emit_local_set(b, old_va_arg_area_local);

  emit_stack_global_get(b, of, stack_pointer);
  emit_const(b, IR_TY_I32, bytes);
  wb_u8(b, 0x6b);
  emit_stack_global_set(b, of, stack_pointer);

  emit_stack_global_get(b, of, stack_pointer);
  emit_stack_global_set(b, of, *va_arg_area);

  for (int a = i->nargs_fixed; a < i->nargs; a++) {
    int off = (a - i->nargs_fixed) * 8;
    emit_stack_global_get(b, of, *va_arg_area);
    emit_const(b, IR_TY_I32, off);
    wb_u8(b, 0x6a);
    ir_type_t arg_ty = wasm_ir_type(i->args[a].type);
    if (arg_ty == IR_TY_F64) {
      emit_val(b, i->args[a], IR_TY_F64, param_count);
      wb_u8(b, store_opcode(IR_TY_F64));
      emit_memarg(b, IR_TY_F64);
    } else if (arg_ty == IR_TY_F32) {
      emit_val(b, i->args[a], IR_TY_F32, param_count);
      wb_u8(b, 0xbb); /* f64.promote_f32 */
      wb_u8(b, store_opcode(IR_TY_F64));
      emit_memarg(b, IR_TY_F64);
    } else {
      emit_val(b, i->args[a], IR_TY_I64, param_count);
      wb_u8(b, store_opcode(IR_TY_I64));
      emit_memarg(b, IR_TY_I64);
    }
  }
  return bytes;
}

static void emit_variadic_arg_area_restore(wb_t *b, obj_func_t *of, obj_global_t *stack_pointer,
                                           obj_global_t *va_arg_area,
                                           int old_va_arg_area_local, int bytes) {
  if (bytes <= 0) return;
  emit_stack_global_get(b, of, stack_pointer);
  emit_const(b, IR_TY_I32, bytes);
  wb_u8(b, 0x6a);
  emit_stack_global_set(b, of, stack_pointer);

  emit_local_get(b, old_va_arg_area_local);
  emit_stack_global_set(b, of, va_arg_area);
}

static void emit_complex_ret_copy(wb_t *b, ir_inst_t *i, int param_count) {
  ir_type_t ty = i->ret_complex_half == 4 ? IR_TY_F32 : IR_TY_F64;
  if (i->ret_complex_half != 4 && i->ret_complex_half != 8) obj_unsupported_op(i->op);
  emit_local_get(b, 0);
  emit_addr_val(b, i->src1, param_count);
  wb_u8(b, load_opcode(ty, 0));
  emit_memarg(b, ty);
  wb_u8(b, store_opcode(ty));
  emit_memarg(b, ty);
  emit_local_get(b, 0);
  emit_const(b, IR_TY_I32, i->ret_complex_half);
  wb_u8(b, 0x6a);
  emit_addr_plus_const(b, i->src1, i->ret_complex_half, param_count);
  wb_u8(b, load_opcode(ty, 0));
  emit_memarg(b, ty);
  wb_u8(b, store_opcode(ty));
  emit_memarg(b, ty);
}

static void gen_func_body(obj_func_t *of, ir_func_t *f) {
  int of_index = (int)(of - g_obj.funcs);
  int param_count = of->sig.nparams;
  int has_ret_area = func_has_ret_area(f);
  int nlocals = f->next_vreg_id;
  int frame_size = collect_frame_size(f);
  int has_variadic_varargs = func_has_variadic_varargs(f);
  int has_stack_restore = frame_size > 0 || func_has_vla_alloc(f) || has_variadic_varargs;
  int has_control_flow = func_has_control_flow(f);
  int has_atomic_cas32 = func_has_atomic_cas_width(f, 0);
  int has_atomic_cas64 = func_has_atomic_cas_width(f, 1);
  int extra_base = local_index(param_count, nlocals);
  int extra_count = 0;
  int fp_local = extra_base + extra_count;
  int old_sp_local = fp_local + 1;
  if (has_stack_restore) extra_count += 2;
  int old_va_arg_area_local = extra_base + extra_count;
  if (has_variadic_varargs) extra_count++;
  int pc_local = extra_base + extra_count;
  if (has_control_flow) extra_count++;
  int atomic_tmp32_local = extra_base + extra_count;
  int atomic_exp32_local = atomic_tmp32_local + 1;
  if (has_atomic_cas32) extra_count += 2;
  int atomic_tmp64_local = extra_base + extra_count;
  int atomic_exp64_local = atomic_tmp64_local + 1;
  if (has_atomic_cas64) extra_count += 2;
  obj_global_t *stack_pointer = has_stack_restore ? intern_stack_pointer_global() : NULL;
  obj_global_t *va_arg_area = NULL;
  wb_t body = {0};
  ir_type_t *local_types = NULL;
  unsigned char *local_unsigned = NULL;
  if (nlocals > 0) {
    local_types = xrealloc(NULL, (size_t)nlocals * sizeof(ir_type_t));
    local_unsigned = xrealloc(NULL, (size_t)nlocals);
    memset(local_unsigned, 0, (size_t)nlocals);
    collect_local_types(f, local_types, local_unsigned, nlocals);
  }
  g_emit_local_types = local_types;
  g_emit_local_unsigned = local_unsigned;
  g_emit_local_count = nlocals;

  wb_uleb(&body, (uint32_t)(nlocals + extra_count));
  for (int v = 0; v < nlocals; v++) {
    wb_uleb(&body, 1);
    wb_u8(&body, wasm_valtype(local_types[v]));
  }
  if (has_stack_restore) {
    wb_uleb(&body, 1);
    wb_u8(&body, wasm_valtype(IR_TY_I32));
    wb_uleb(&body, 1);
    wb_u8(&body, wasm_valtype(IR_TY_I32));
  }
  if (has_variadic_varargs) {
    wb_uleb(&body, 1);
    wb_u8(&body, wasm_valtype(IR_TY_I32));
  }
  if (has_control_flow) {
    wb_uleb(&body, 1);
    wb_u8(&body, wasm_valtype(IR_TY_I32));
  }
  if (has_atomic_cas32) {
    wb_uleb(&body, 1);
    wb_u8(&body, wasm_valtype(IR_TY_I32));
    wb_uleb(&body, 1);
    wb_u8(&body, wasm_valtype(IR_TY_I32));
  }
  if (has_atomic_cas64) {
    wb_uleb(&body, 1);
    wb_u8(&body, wasm_valtype(IR_TY_I64));
    wb_uleb(&body, 1);
    wb_u8(&body, wasm_valtype(IR_TY_I64));
  }
  if (has_stack_restore) {
    emit_stack_global_get(&body, of, stack_pointer);
    emit_local_set(&body, old_sp_local);
  }
  if (frame_size > 0) {
    emit_stack_global_get(&body, of, stack_pointer);
    emit_const(&body, IR_TY_I32, frame_size);
    wb_u8(&body, 0x6b);
    emit_local_set(&body, fp_local);
    emit_local_get(&body, fp_local);
    emit_stack_global_set(&body, of, stack_pointer);
  }
  if (has_control_flow) {
    emit_const(&body, IR_TY_I32, f->entry ? f->entry->id : 0);
    emit_local_set(&body, pc_local);
    wb_u8(&body, 0x02);
    wb_u8(&body, 0x40);
    wb_u8(&body, 0x03);
    wb_u8(&body, 0x40);
  }

  for (ir_block_t *blk = f->entry; blk; blk = blk->next) {
    if (has_control_flow) {
      emit_local_get(&body, pc_local);
      emit_const(&body, IR_TY_I32, blk->id);
      wb_u8(&body, 0x46);
      wb_u8(&body, 0x04);
      wb_u8(&body, 0x40);
    }
    for (ir_inst_t *i = blk->head; i; i = i->next) {
      switch (i->op) {
        case IR_NOP:
        case IR_LABEL:
          break;
        case IR_PARAM:
          if (i->src1.id != IR_VAL_IMM) obj_unsupported_op(i->op);
          int param_slot = 0;
          if (i->src1.imm < 0) {
            if (!has_ret_area) obj_unsupported_op(i->op);
            param_slot = 0;
          } else {
            int ordinal = func_param_ordinal_for_inst(f, i);
            if (ordinal < 0) obj_unsupported_op(i->op);
            param_slot = ordinal + (has_ret_area ? 1 : 0);
          }
          emit_local_get(&body, param_slot);
          if (param_slot >= 0 && param_slot < of->sig.nparams) {
            emit_stack_cast(&body, of->sig.params[param_slot], actual_vreg_type(i->dst),
                            actual_vreg_unsigned(i->dst));
          }
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        case IR_ALLOCA: {
          int off = alloca_offset(f, i->dst.id);
          if (off < 0 || frame_size <= 0) obj_unsupported_op(i->op);
          emit_local_get(&body, fp_local);
          emit_const(&body, IR_TY_I32, off);
          wb_u8(&body, 0x6a);
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        }
        case IR_LOAD_IMM:
          if (actual_vreg_type(i->dst) == IR_TY_F32 || actual_vreg_type(i->dst) == IR_TY_F64) {
            emit_fp_const(&body, actual_vreg_type(i->dst), i->src1.fp_imm);
          } else {
            emit_const(&body, actual_vreg_type(i->dst), i->src1.imm);
          }
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        case IR_LOAD_FP_IMM:
          emit_fp_const(&body, i->dst.type, i->src1.fp_imm);
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        case IR_LOAD_STR:
        case IR_LOAD_SYM:
        case IR_LOAD_TLV_ADDR: {
          if (!i->sym) obj_unsupported_op(i->op);
          if (i->op == IR_LOAD_SYM && psx_ctx_has_function_name(i->sym, i->sym_len)) {
            obj_func_t *target = intern_func(i->sym, i->sym_len);
            if (!target->defined && target->sig.nparams == 0 && target->sig.result == IR_TY_VOID) {
              target->sig = func_sig_from_ir_funcptr(i, i->sym, i->sym_len);
            }
            of = &g_obj.funcs[of_index];
            wb_u8(&body, 0x41);
            uint32_t imm_off = wb_uleb5(&body, 0);
            func_add_reloc(of, R_WASM_TABLE_INDEX_SLEB, imm_off,
                           (int)(target - g_obj.funcs), 0, 0);
            emit_local_set(&body, local_index(param_count, i->dst.id));
            break;
          }
          int addend = 0;
          obj_data_t *d = data_for_symbol(i->sym, i->op == IR_LOAD_STR ? -1 : i->sym_len, &addend);
          wb_u8(&body, 0x41);
          uint32_t imm_off = wb_uleb5(&body, 0);
          func_add_reloc(of, R_WASM_MEMORY_ADDR_LEB, imm_off, data_index(d), 1, addend);
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        }
        case IR_ZEXT:
        case IR_SEXT:
        case IR_TRUNC:
          emit_val(&body, i->src1, actual_vreg_type(i->dst), param_count);
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        case IR_I2F: {
          ir_type_t src_ty = wasm_ir_type(i->src1.type);
          emit_val(&body, i->src1, src_ty, param_count);
          wb_u8(&body, i2f_opcode(i->dst.type, src_ty, i->is_unsigned));
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        }
        case IR_F2I: {
          ir_type_t src_ty = wasm_ir_type(i->src1.type);
          emit_val(&body, i->src1, src_ty, param_count);
          wb_u8(&body, f2i_opcode(i->dst.type, src_ty, i->is_unsigned));
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        }
        case IR_F2F: {
          ir_type_t src_ty = wasm_ir_type(i->src1.type);
          ir_type_t dst_ty = wasm_ir_type(i->dst.type);
          emit_val(&body, i->src1, src_ty, param_count);
          if (dst_ty == IR_TY_F64 && src_ty == IR_TY_F32) {
            wb_u8(&body, 0xbb);
          } else if (dst_ty == IR_TY_F32 && src_ty == IR_TY_F64) {
            wb_u8(&body, 0xb6);
          } else if (dst_ty != src_ty) {
            obj_unsupported_op(i->op);
          }
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        }
        case IR_LOAD: {
          ir_type_t load_ty = memory_access_type(i->dst.type, actual_vreg_type(i->dst));
          emit_addr_val(&body, i->src1, param_count);
          wb_u8(&body, load_opcode(load_ty, i->is_unsigned));
          emit_memarg(&body, load_ty);
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        }
        case IR_STORE: {
          ir_type_t store_ty = memory_access_type(i->src2.type, actual_vreg_type(i->src2));
          emit_addr_val(&body, i->src1, param_count);
          emit_val(&body, i->src2, store_ty, param_count);
          wb_u8(&body, store_opcode(store_ty));
          emit_memarg(&body, store_ty);
          break;
        }
        case IR_ATOMIC: {
          if (i->atomic_kind == IR_ATOMIC_FENCE) {
            wb_u8(&body, 0x01);
            break;
          }
          ir_type_t width_ty = atomic_width_type(i);
          ir_type_t value_ty = atomic_value_type(i);
          if (i->atomic_kind == IR_ATOMIC_LOAD) {
            emit_addr_val(&body, i->src1, param_count);
            wb_u8(&body, load_opcode(width_ty, i->is_unsigned));
            emit_memarg(&body, width_ty);
            emit_local_set(&body, local_index(param_count, i->dst.id));
            break;
          }
          if (i->atomic_kind == IR_ATOMIC_STORE) {
            emit_addr_val(&body, i->src1, param_count);
            emit_val(&body, i->src2, value_ty, param_count);
            wb_u8(&body, store_opcode(width_ty));
            emit_memarg(&body, width_ty);
            break;
          }
          if (i->atomic_kind == IR_ATOMIC_RMW) {
            emit_addr_val(&body, i->src1, param_count);
            wb_u8(&body, load_opcode(width_ty, i->is_unsigned));
            emit_memarg(&body, width_ty);
            emit_local_set(&body, local_index(param_count, i->dst.id));

            emit_addr_val(&body, i->src1, param_count);
            if (i->atomic_rmw_op == IR_ARMW_XCHG) {
              emit_val(&body, i->src2, value_ty, param_count);
            } else {
              emit_val(&body, i->dst, value_ty, param_count);
              emit_val(&body, i->src2, value_ty, param_count);
              wb_u8(&body, atomic_rmw_opcode(i));
            }
            wb_u8(&body, store_opcode(width_ty));
            emit_memarg(&body, width_ty);
            break;
          }
          if (i->atomic_kind == IR_ATOMIC_CAS) {
            int tmp_local = value_ty == IR_TY_I64 ? atomic_tmp64_local : atomic_tmp32_local;
            int exp_local = value_ty == IR_TY_I64 ? atomic_exp64_local : atomic_exp32_local;
            emit_addr_val(&body, i->src1, param_count);
            wb_u8(&body, load_opcode(width_ty, i->is_unsigned));
            emit_memarg(&body, width_ty);
            emit_local_set(&body, tmp_local);
            emit_addr_val(&body, i->src2, param_count);
            wb_u8(&body, load_opcode(width_ty, i->is_unsigned));
            emit_memarg(&body, width_ty);
            emit_local_set(&body, exp_local);
            emit_local_get(&body, tmp_local);
            emit_local_get(&body, exp_local);
            wb_u8(&body, value_ty == IR_TY_I64 ? 0x51 : 0x46);
            emit_local_set(&body, local_index(param_count, i->dst.id));
            emit_local_get(&body, local_index(param_count, i->dst.id));
            wb_u8(&body, 0x04);
            wb_u8(&body, 0x40);
            emit_addr_val(&body, i->src1, param_count);
            emit_val(&body, i->src3, value_ty, param_count);
            wb_u8(&body, store_opcode(width_ty));
            emit_memarg(&body, width_ty);
            wb_u8(&body, 0x0b);
            emit_addr_val(&body, i->src2, param_count);
            emit_local_get(&body, tmp_local);
            wb_u8(&body, store_opcode(width_ty));
            emit_memarg(&body, width_ty);
            break;
          }
          obj_unsupported_op(i->op);
          break;
        }
        case IR_MEMCPY:
          emit_memcpy_inline(&body, i, param_count);
          break;
        case IR_VLA_ALLOC:
          emit_stack_global_get(&body, of, stack_pointer);
          emit_val(&body, i->src1, IR_TY_I32, param_count);
          emit_const(&body, IR_TY_I32, 15);
          wb_u8(&body, 0x6a);
          emit_const(&body, IR_TY_I32, -16);
          wb_u8(&body, 0x71);
          wb_u8(&body, 0x6b);
          emit_local_set(&body, local_index(param_count, i->dst.id));
          emit_local_get(&body, local_index(param_count, i->dst.id));
          emit_stack_global_set(&body, of, stack_pointer);
          break;
        case IR_VA_ARG_AREA:
          if (!va_arg_area) va_arg_area = intern_va_arg_area_global();
          of = &g_obj.funcs[of_index];
          emit_stack_global_get(&body, of, va_arg_area);
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        case IR_LEA:
          emit_addr_val(&body, i->src1, param_count);
          emit_val(&body, i->src2, IR_TY_I32, param_count);
          wb_u8(&body, 0x6a);
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        case IR_ALIGN_PTR: {
          int align = i->alloca_align > 0 ? i->alloca_align : 16;
          emit_addr_val(&body, i->src1, param_count);
          emit_const(&body, IR_TY_I32, align - 1);
          wb_u8(&body, 0x6a);
          emit_const(&body, IR_TY_I32, -align);
          wb_u8(&body, 0x71);
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        }
        case IR_NEG: {
          ir_type_t ty = wasm_ir_type(i->dst.type);
          if (ty == IR_TY_F32 || ty == IR_TY_F64) {
            emit_val(&body, i->src1, ty, param_count);
            wb_u8(&body, ty == IR_TY_F64 ? 0x9a : 0x8c);
          } else {
            emit_const(&body, ty, 0);
            emit_val(&body, i->src1, ty, param_count);
            wb_u8(&body, ty == IR_TY_I64 ? 0x7d : 0x6b);
          }
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        }
        case IR_NOT: {
          ir_type_t ty = wasm_ir_type(i->dst.type);
          emit_val(&body, i->src1, ty, param_count);
          emit_const(&body, ty, -1);
          wb_u8(&body, ty == IR_TY_I64 ? 0x83 : 0x73);
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        }
        case IR_FNEG: {
          ir_type_t ty = wasm_ir_type(i->src1.type);
          if (ty != IR_TY_F32 && ty != IR_TY_F64) obj_unsupported_op(i->op);
          emit_val(&body, i->src1, ty, param_count);
          wb_u8(&body, ty == IR_TY_F64 ? 0x9a : 0x8c);
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        }
        case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: case IR_MOD:
        case IR_UDIV: case IR_UMOD: case IR_AND: case IR_OR: case IR_XOR:
        case IR_SHL: case IR_SHR: case IR_LSR:
        case IR_EQ: case IR_NE: case IR_LT: case IR_LE: case IR_ULT: case IR_ULE: {
          ir_type_t lhs_ty = actual_vreg_type(i->src1);
          ir_type_t rhs_ty = actual_vreg_type(i->src2);
          ir_type_t dst_ty = actual_vreg_type(i->dst);
          ir_type_t op_ty;
          if (i->op == IR_EQ || i->op == IR_NE || i->op == IR_LT || i->op == IR_LE ||
              i->op == IR_ULT || i->op == IR_ULE) {
            op_ty = lhs_ty == IR_TY_I64 || rhs_ty == IR_TY_I64 ? IR_TY_I64 : IR_TY_I32;
          } else if (i->op == IR_SHL || i->op == IR_SHR || i->op == IR_LSR) {
            op_ty = lhs_ty == IR_TY_I64 ? IR_TY_I64 : IR_TY_I32;
          } else {
            op_ty = dst_ty == IR_TY_I64 ? IR_TY_I64 : IR_TY_I32;
          }
          if (i->op == IR_MOD || i->op == IR_UMOD) {
            emit_val(&body, i->src2, op_ty, param_count);
            wb_u8(&body, op_ty == IR_TY_I64 ? 0x50 : 0x45);
            wb_u8(&body, 0x04);
            wb_u8(&body, wasm_valtype(op_ty));
            emit_val(&body, i->src1, op_ty, param_count);
            wb_u8(&body, 0x05);
            emit_val(&body, i->src1, op_ty, param_count);
            emit_val(&body, i->src2, op_ty, param_count);
            wb_u8(&body, int_binop_opcode(i->op, op_ty));
            wb_u8(&body, 0x0b);
            emit_local_set(&body, local_index(param_count, i->dst.id));
            break;
          }
          emit_val(&body, i->src1, op_ty, param_count);
          emit_val(&body, i->src2, op_ty, param_count);
          wb_u8(&body, int_binop_opcode(i->op, op_ty));
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        }
        case IR_FADD: case IR_FSUB: case IR_FMUL: case IR_FDIV:
        case IR_FEQ: case IR_FNE: case IR_FLT: case IR_FLE: {
          ir_type_t op_ty = wasm_ir_type(i->src1.type);
          emit_val(&body, i->src1, op_ty, param_count);
          emit_val(&body, i->src2, op_ty, param_count);
          wb_u8(&body, fp_binop_opcode(i->op, op_ty));
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        }
        case IR_CALL: {
          int vararg_area_bytes =
              emit_variadic_arg_area_prepare(&body, of, stack_pointer, &va_arg_area,
                                             old_va_arg_area_local, i, param_count);
          if (i->callee.id != IR_VAL_NONE) {
            obj_sig_t csig = call_sig_from_inst(i);
            int type_index = intern_type(&csig);
            g_obj.has_indirect_call = 1;
            if (call_has_ret_area(i)) emit_addr_val(&body, call_ret_area(i), param_count);
            for (int a = 0; a < csig.nparams; a++) {
              int p = a + (call_has_ret_area(i) ? 1 : 0);
              if (p >= csig.nparams) break;
              emit_val(&body, i->args[a], csig.params[p], param_count);
            }
            emit_addr_val(&body, i->callee, param_count);
            wb_u8(&body, 0x11);
            uint32_t type_imm_off = wb_uleb5(&body, (uint32_t)type_index);
            func_add_type_reloc(of, type_imm_off, type_index);
            wb_uleb(&body, 0);
            if (csig.result != IR_TY_VOID && i->dst.id >= 0) {
              emit_local_set(&body, local_index(param_count, i->dst.id));
            }
            emit_variadic_arg_area_restore(&body, of, stack_pointer, va_arg_area,
                                           old_va_arg_area_local, vararg_area_bytes);
            free(csig.params);
            break;
          }
          if (!i->sym) obj_unsupported_op(i->op);
          obj_func_t *target = intern_func(i->sym, i->sym_len);
          of = &g_obj.funcs[of_index];
          obj_sig_t csig = call_sig_from_inst(i);
          obj_sig_t *emit_sig = &target->sig;
          if (target->sig.nparams == 0 && target->sig.result == IR_TY_VOID && !target->defined) {
            target->sig = csig;
            emit_sig = &target->sig;
          } else if (target->defined) {
            free(csig.params);
          } else if (!sig_equal(&target->sig, &csig) &&
                     !sig_integer_width_compatible(&target->sig, &csig)) {
            char msg[160];
            snprintf(msg, sizeof(msg), "conflicting Wasm object function signature: %.*s",
                     i->sym_len, i->sym);
            obj_unsupported_msg(msg);
          } else {
            free(csig.params);
          }
          int has_call_ret_area = call_has_ret_area(i);
          if (has_call_ret_area) emit_addr_val(&body, call_ret_area(i), param_count);
          for (int a = 0; a < i->nargs; a++) {
            int p = a + (has_call_ret_area ? 1 : 0);
            if (p >= emit_sig->nparams) break;
            emit_val(&body, i->args[a], emit_sig->params[p], param_count);
          }
          wb_u8(&body, 0x10);
          uint32_t imm_off = wb_uleb5(&body, 0);
          func_add_call_reloc(of, imm_off, (int)(target - g_obj.funcs));
          if (emit_sig->result != IR_TY_VOID && i->dst.id >= 0) {
            emit_local_set(&body, local_index(param_count, i->dst.id));
          }
          emit_variadic_arg_area_restore(&body, of, stack_pointer, va_arg_area,
                                         old_va_arg_area_local, vararg_area_bytes);
          break;
        }
        case IR_BR:
          if (!has_control_flow) obj_unsupported_op(i->op);
          emit_const(&body, IR_TY_I32, i->label_id);
          emit_local_set(&body, pc_local);
          wb_u8(&body, 0x0c);
          wb_uleb(&body, 1);
          break;
        case IR_BR_COND:
          if (!has_control_flow) obj_unsupported_op(i->op);
          emit_val(&body, i->src1, IR_TY_I32, param_count);
          wb_u8(&body, 0x04);
          wb_u8(&body, 0x40);
          emit_const(&body, IR_TY_I32, i->label_id);
          emit_local_set(&body, pc_local);
          wb_u8(&body, 0x05);
          emit_const(&body, IR_TY_I32, i->else_label_id);
          emit_local_set(&body, pc_local);
          wb_u8(&body, 0x0b);
          wb_u8(&body, 0x0c);
          wb_uleb(&body, 1);
          break;
        case IR_RET:
          if (i->ret_complex_half > 0) {
            emit_complex_ret_copy(&body, i, param_count);
          } else if (i->src1.id != IR_VAL_NONE) {
            emit_val(&body, i->src1, of->sig.result, param_count);
          }
          if (has_stack_restore) {
            emit_local_get(&body, old_sp_local);
            emit_stack_global_set(&body, of, stack_pointer);
          }
          wb_u8(&body, 0x0f);
          break;
        default:
          obj_unsupported_op(i->op);
      }
    }
    if (has_control_flow && !block_has_terminator(blk)) {
      if (blk->next) {
        emit_const(&body, IR_TY_I32, blk->next->id);
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
    emit_stack_global_set(&body, of, stack_pointer);
  }
  wb_u8(&body, 0x0b);
  of->body = body;
  free(local_types);
  free(local_unsigned);
  g_emit_local_types = NULL;
  g_emit_local_unsigned = NULL;
  g_emit_local_count = 0;
}

static void assign_indices(void) {
  for (int i = 0; i < g_obj.func_count; i++) {
    g_obj.funcs[i].type_index = intern_type(&g_obj.funcs[i].sig);
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

static int defined_data_count(void) {
  int n = 0;
  for (int i = 0; i < g_obj.data_count; i++) {
    if (!g_obj.data[i].is_undefined) n++;
  }
  return n;
}

static void emit_type_section(wb_t *out) {
  wb_t p = {0};
  wb_uleb(&p, (uint32_t)g_obj.type_count);
  for (int i = 0; i < g_obj.type_count; i++) {
    obj_sig_t *s = &g_obj.types[i];
    wb_u8(&p, 0x60);
    wb_uleb(&p, (uint32_t)s->nparams);
    for (int a = 0; a < s->nparams; a++) wb_u8(&p, wasm_valtype(s->params[a]));
    if (s->result == IR_TY_VOID) {
      wb_uleb(&p, 0);
    } else {
      wb_uleb(&p, 1);
      wb_u8(&p, wasm_valtype(s->result));
    }
  }
  emit_section(out, WASM_SEC_TYPE, &p);
  free(p.data);
}

static void emit_import_section(wb_t *out) {
  int nimports = 0;
  for (int i = 0; i < g_obj.func_count; i++) if (g_obj.funcs[i].imported) nimports++;
  if (g_obj.has_indirect_call) nimports++;
  nimports++;
  nimports += g_obj.global_count;
  if (nimports == 0) return;
  wb_t p = {0};
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
    wb_u8(&p, wasm_valtype(IR_TY_I32));
    wb_u8(&p, 0x01);
  }
  emit_section(out, WASM_SEC_IMPORT, &p);
  free(p.data);
}

static void emit_function_section(wb_t *out) {
  int ndefs = 0;
  for (int i = 0; i < g_obj.func_count; i++) if (g_obj.funcs[i].defined) ndefs++;
  if (ndefs == 0) return;
  wb_t p = {0};
  wb_uleb(&p, (uint32_t)ndefs);
  for (int i = 0; i < g_obj.func_count; i++) {
    if (g_obj.funcs[i].defined) wb_uleb(&p, (uint32_t)g_obj.funcs[i].type_index);
  }
  emit_section(out, WASM_SEC_FUNCTION, &p);
  free(p.data);
}

static void emit_datacount_section(wb_t *out) {
  int ndata = defined_data_count();
  if (ndata == 0) return;
  wb_t p = {0};
  wb_uleb(&p, (uint32_t)ndata);
  emit_section(out, WASM_SEC_DATACOUNT, &p);
  free(p.data);
}

static void emit_code_section(wb_t *out) {
  int ndefs = 0;
  for (int i = 0; i < g_obj.func_count; i++) if (g_obj.funcs[i].defined) ndefs++;
  if (ndefs == 0) return;
  wb_t p = {0};
  wb_uleb(&p, (uint32_t)ndefs);
  for (int i = 0; i < g_obj.func_count; i++) {
    obj_func_t *f = &g_obj.funcs[i];
    if (!f->defined) continue;
    wb_t body_size = {0};
    wb_uleb(&body_size, (uint32_t)f->body.len);
    uint32_t body_payload_start = p.len + body_size.len;
    wb_bytes(&p, body_size.data, body_size.len);
    wb_bytes(&p, f->body.data, f->body.len);
    for (int r = 0; r < f->reloc_count; r++) {
      add_code_reloc(body_payload_start + f->relocs[r].body_off, &f->relocs[r]);
    }
    free(body_size.data);
  }
  emit_section(out, WASM_SEC_CODE, &p);
  free(p.data);
}

static void emit_data_section(wb_t *out) {
  int ndata = defined_data_count();
  if (ndata == 0) return;
  wb_t p = {0};
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
      add_global_reloc(&g_obj.data_relocs, &g_obj.data_reloc_count, &g_obj.data_reloc_cap,
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

static void emit_linking_section(wb_t *out) {
  wb_t payload = {0};
  wb_uleb(&payload, 2);

  wb_t symtab = {0};
  wb_uleb(&symtab, (uint32_t)g_obj.symbol_count);
  for (int i = 0; i < g_obj.func_count; i++) emit_symbol_entry(&symtab, &g_obj.funcs[i]);
  for (int i = 0; i < g_obj.data_count; i++) emit_data_symbol_entry(&symtab, &g_obj.data[i]);
  for (int i = 0; i < g_obj.global_count; i++) emit_global_symbol_entry(&symtab, &g_obj.globals[i]);
  wb_u8(&payload, WASM_SYMBOL_TABLE);
  wb_uleb(&payload, (uint32_t)symtab.len);
  wb_bytes(&payload, symtab.data, symtab.len);

  int ndata = defined_data_count();
  if (ndata > 0) {
    wb_t segs = {0};
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

static void emit_reloc_section(wb_t *out, const char *name, int target_section,
                               obj_reloc_t *relocs, int reloc_count) {
  if (reloc_count == 0) return;
  wb_t p = {0};
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

void wasm32_obj_set_output_file(FILE *out) {
  g_obj.out = out;
}

void wasm32_obj_capture_output(int enabled) {
  g_obj.capture_output = enabled;
}

unsigned char *wasm32_obj_take_output(size_t *out_len) {
  unsigned char *data = g_obj_capture.data;
  if (out_len) *out_len = g_obj_capture.len;
  g_obj_capture.data = NULL;
  g_obj_capture.len = 0;
  g_obj_capture.cap = 0;
  return data;
}

void wasm32_obj_begin(void) {
  FILE *out = g_obj.out;
  int capture_output = g_obj.capture_output;
  memset(&g_obj, 0, sizeof(g_obj));
  g_obj.out = out;
  g_obj.capture_output = capture_output;
}

void wasm32_obj_gen_ir_module(ir_module_t *m) {
  for (ir_func_t *f = m->funcs; f; f = f->next) {
    obj_func_t *of = intern_func(f->name, f->name_len);
    if (of->defined) obj_unsupported_msg("duplicate function in Wasm object mode");
    obj_sig_t def_sig = {0};
    collect_func_sig(f, &def_sig);
    if (of->sig.nparams > 0 || of->sig.result != IR_TY_VOID) {
      if (!sig_equal(&of->sig, &def_sig) &&
          !sig_integer_width_compatible(&of->sig, &def_sig)) {
        char msg[160];
        snprintf(msg, sizeof(msg), "conflicting Wasm object function signature: %.*s",
                 f->name_len, f->name);
        obj_unsupported_msg(msg);
      }
      free(def_sig.params);
    } else {
      of->sig = def_sig;
    }
    of->defined = 1;
    of->is_static = f->is_static;
    gen_func_body(of, f);
  }
}

static void emit_obj_string_literal(string_lit_t *lit, void *user) {
  (void)user;
  psx_string_lit_view_t view = psx_string_lit_view(lit);
  int name_len = view.label ? (int)strlen(view.label) : 0;
  if (!view.label || name_len == 0) obj_unsupported_msg("string literal label in Wasm object mode");
  obj_data_t *d = intern_data(view.label, name_len, 0, 1, 0);
  if (d->is_emitted) return;
  tk_emit_string_literal_bytes(view.str, view.len, (int)view.char_width, true,
                               obj_emit_string_literal_byte, &d->bytes);
  d->is_emitted = 1;
}

static void data_write_symbol_addr(obj_data_t *d, char *sym, int sym_len,
                                   long long addend, int size) {
  if (sym && sym_len >= 0 && psx_ctx_has_function_name(sym, sym_len)) {
    if (addend != 0) obj_unsupported_msg("function address addend in Wasm object mode");
    obj_func_t *target = find_func(sym, sym_len);
    if (!target) {
      target = intern_func(sym, sym_len);
      target->sig = func_sig_from_ctx(sym, sym_len);
    }
    uint32_t off = d->bytes.len;
    wb_int_le(&d->bytes, 0, size);
    data_add_reloc(d, R_WASM_TABLE_INDEX_I32, off, (int)(target - g_obj.funcs), 0, 0);
    return;
  }
  int reloc_addend = 0;
  obj_data_t *target = data_for_symbol(sym, sym_len, &reloc_addend);
  uint32_t off = d->bytes.len;
  wb_int_le(&d->bytes, 0, size);
  data_add_reloc(d, R_WASM_MEMORY_ADDR_I32, off, data_index(target), 1,
                 reloc_addend + (int)addend);
}

static void data_write_int_le_at(obj_data_t *d, size_t off, uint64_t value, int size) {
  if (size != 1 && size != 2 && size != 4 && size != 8) {
    obj_unsupported_msg("global data slot size in Wasm object mode");
  }
  if (off + (size_t)size > d->bytes.len) {
    obj_unsupported_msg("global data write out of range in Wasm object mode");
  }
  for (int i = 0; i < size; i++) {
    d->bytes.data[off + (size_t)i] = (unsigned char)((value >> (8 * i)) & 0xff);
  }
}

static void data_write_symbol_addr_at(obj_data_t *d, size_t off, char *sym, int sym_len,
                                      long long addend, int size) {
  if (sym && sym_len >= 0 && psx_ctx_has_function_name(sym, sym_len)) {
    if (addend != 0) obj_unsupported_msg("function address addend in Wasm object mode");
    obj_func_t *target = find_func(sym, sym_len);
    if (!target) {
      target = intern_func(sym, sym_len);
      target->sig = func_sig_from_ctx(sym, sym_len);
    }
    data_write_int_le_at(d, off, 0, size);
    data_add_reloc(d, R_WASM_TABLE_INDEX_I32, off, (int)(target - g_obj.funcs), 0, 0);
    return;
  }
  int reloc_addend = 0;
  obj_data_t *target = data_for_symbol(sym, sym_len, &reloc_addend);
  data_write_int_le_at(d, off, 0, size);
  data_add_reloc(d, R_WASM_MEMORY_ADDR_I32, off, data_index(target), 1,
                 reloc_addend + (int)addend);
}

static void data_write_scalar(obj_data_t *d, uint64_t value, int size) {
  if (size != 1 && size != 2 && size != 4 && size != 8) {
    obj_unsupported_msg("global scalar size in Wasm object mode");
  }
  wb_int_le(&d->bytes, value, size);
}

static void data_write_fp_at(obj_data_t *d, size_t off, tk_float_kind_t fp_kind, double value) {
  if (fp_kind == TK_FLOAT_KIND_FLOAT) {
    float f = (float)value;
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    data_write_int_le_at(d, off, bits, 4);
  } else if (fp_kind >= TK_FLOAT_KIND_DOUBLE) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    data_write_int_le_at(d, off, bits, 8);
  } else {
    obj_unsupported_msg("global floating slot in Wasm object mode");
  }
}

static void data_write_init_slot_at(obj_data_t *d, global_var_t *gv, int idx,
                                    size_t off, int size, int normalize_bool,
                                    tk_float_kind_t fp_kind,
                                    const tag_member_info_t *member_info) {
  if (idx < 0) return;
  psx_gvar_init_slot_t slot = psx_gvar_init_slot_view(gv, idx);
  if (slot.symbol) {
    if (psx_ctx_has_function_name(slot.symbol, slot.symbol_len) && member_info) {
      ensure_func_sig_for_address(slot.symbol, slot.symbol_len,
                                  func_sig_from_member_funcptr(member_info, slot.symbol,
                                                              slot.symbol_len));
    }
    data_write_symbol_addr_at(d, off, slot.symbol, slot.symbol_len, slot.value, size);
    return;
  }
  if (slot.fp_sentinel_kind != TK_FLOAT_KIND_NONE) {
    data_write_fp_at(d, off, slot.fp_sentinel_kind, slot.fvalue);
    return;
  }
  if (fp_kind != TK_FLOAT_KIND_NONE) {
    data_write_fp_at(d, off, fp_kind, slot.fvalue);
    return;
  }
  long long value = slot.value;
  if (normalize_bool) value = value != 0;
  data_write_int_le_at(d, off, (uint64_t)value, size);
}

static void emit_obj_global_union_member_data(token_kind_t tk, char *tn, int tl,
                                              obj_data_t *d, global_var_t *gv,
                                              psx_gvar_init_cursor_t *cur, size_t base_off);

static void emit_obj_global_bitfield_unit_data(token_kind_t tk, char *tn, int tl,
                                               int *member_idx, obj_data_t *d,
                                               psx_gvar_init_cursor_t *cur,
                                               size_t base_off) {
  psx_gvar_bitfield_unit_t unit = {0};
  if (!psx_gvar_init_cursor_pack_bitfield_unit(tk, tn, tl, *member_idx, cur, &unit)) {
    obj_unsupported_msg("global bitfield initializer in Wasm object mode");
  }
  data_write_int_le_at(d, base_off + (size_t)unit.offset, unit.packed, unit.size);
  *member_idx = unit.last_member_index;
}

static void emit_obj_global_bitfield_member_data(obj_data_t *d, global_var_t *gv, int idx,
                                                 size_t base_off,
                                                 const tag_member_info_t *mi) {
  if (!mi || mi->bit_width <= 0) obj_unsupported_msg("global bitfield initializer in Wasm object mode");
  unsigned long long packed = psx_gvar_init_slot_bitfield_bits(gv, idx,
                                                               mi->bit_width, mi->bit_offset);
  data_write_int_le_at(d, base_off + (size_t)mi->offset, packed, mi->type_size);
}

static void consume_trailing_zero_union_padding(psx_gvar_init_cursor_t *cur, int start_idx,
                                                int target_slots) {
  psx_gvar_init_cursor_consume_plain_zero_padding(cur, start_idx, target_slots);
}

static void emit_obj_global_struct_members_data_rec(token_kind_t tk, char *tn, int tl,
                                                    obj_data_t *d, global_var_t *gv,
                                                    psx_gvar_init_cursor_t *cur,
                                                    size_t base_off) {
  int n_members = psx_ctx_get_tag_member_count(tk, tn, tl);
  psx_tag_flat_cover_state_t cover_state;
  psx_tag_flat_cover_state_init(&cover_state);
  for (int m = 0; m < n_members && psx_gvar_init_cursor_has(cur); m++) {
    tag_member_info_t mi = {0};
    if (!psx_ctx_get_tag_member_info(tk, tn, tl, m, &mi)) break;
    if (psx_tag_member_is_unnamed_struct(&mi)) continue;
    if (psx_tag_flat_cover_state_covers(&cover_state, &mi)) continue;
    if (mi.bit_width > 0) {
      emit_obj_global_bitfield_unit_data(tk, tn, tl, &m, d, cur, base_off);
      continue;
    }
    if (mi.array_len > 0) {
      if (psx_tag_member_is_tag_aggregate(&mi)) {
        for (int k = 0; k < mi.array_len && psx_gvar_init_cursor_has(cur); k++) {
          size_t elem_off = base_off + (size_t)mi.offset + (size_t)k * (size_t)mi.type_size;
          if (psx_tag_member_is_union_aggregate(&mi)) {
            emit_obj_global_union_member_data(mi.tag_kind, mi.tag_name, mi.tag_len, d, gv,
                                              cur, elem_off);
          } else {
            emit_obj_global_struct_members_data_rec(mi.tag_kind, mi.tag_name, mi.tag_len, d, gv,
                                                    cur, elem_off);
          }
        }
      } else {
        for (int k = 0; k < mi.array_len && psx_gvar_init_cursor_has(cur); k++) {
          int slot = psx_gvar_init_cursor_advance(cur);
          data_write_init_slot_at(d, gv, slot,
                                  base_off + (size_t)mi.offset + (size_t)k * (size_t)mi.type_size,
                                  mi.type_size, mi.is_bool, mi.fp_kind, &mi);
        }
      }
      psx_tag_flat_cover_state_note(&cover_state, tk, tn, tl, &mi);
      continue;
    }
    if (psx_tag_member_is_struct_aggregate(&mi)) {
      emit_obj_global_struct_members_data_rec(mi.tag_kind, mi.tag_name, mi.tag_len, d, gv,
                                              cur, base_off + (size_t)mi.offset);
      psx_tag_flat_cover_state_note(&cover_state, tk, tn, tl, &mi);
      continue;
    }
    if (psx_tag_member_is_union_aggregate(&mi)) {
      emit_obj_global_union_member_data(mi.tag_kind, mi.tag_name, mi.tag_len, d, gv, cur,
                                        base_off + (size_t)mi.offset);
      psx_tag_flat_cover_state_note(&cover_state, tk, tn, tl, &mi);
      continue;
    }
    int slot = psx_gvar_init_cursor_advance(cur);
    data_write_init_slot_at(d, gv, slot, base_off + (size_t)mi.offset, mi.type_size,
                            mi.is_bool, mi.fp_kind, &mi);
    psx_tag_flat_cover_state_note(&cover_state, tk, tn, tl, &mi);
  }
}

static void emit_obj_global_union_member_data(token_kind_t tk, char *tn, int tl,
                                              obj_data_t *d, global_var_t *gv,
                                              psx_gvar_init_cursor_t *cur, size_t base_off) {
  if (!psx_gvar_init_cursor_has(cur)) return;
  int start_idx = psx_gvar_init_cursor_index(cur);
  tag_member_info_t mi = {0};
  if (!psx_tag_union_init_member_for_slot(tk, tn, tl, gv,
                                          psx_gvar_init_cursor_index(cur), &mi)) {
    obj_unsupported_msg("global union initializer in Wasm object mode");
  }
  if (mi.bit_width > 0) {
    emit_obj_global_bitfield_member_data(d, gv, psx_gvar_init_cursor_advance(cur), base_off, &mi);
    consume_trailing_zero_union_padding(cur, start_idx,
                                        psx_tag_flat_slot_count(tk, tn, tl));
    return;
  }
  if (mi.array_len > 0) {
    if (psx_tag_member_is_tag_aggregate(&mi)) {
      for (int k = 0; k < mi.array_len && psx_gvar_init_cursor_has(cur); k++) {
        size_t elem_off = base_off + (size_t)mi.offset + (size_t)k * (size_t)mi.type_size;
        if (psx_tag_member_is_struct_aggregate(&mi)) {
          emit_obj_global_struct_members_data_rec(mi.tag_kind, mi.tag_name, mi.tag_len, d, gv,
                                                  cur, elem_off);
        } else {
          emit_obj_global_union_member_data(mi.tag_kind, mi.tag_name, mi.tag_len, d, gv,
                                            cur, elem_off);
        }
      }
    } else {
      for (int k = 0; k < mi.array_len && psx_gvar_init_cursor_has(cur); k++) {
        int slot = psx_gvar_init_cursor_advance(cur);
        data_write_init_slot_at(d, gv, slot,
                                base_off + (size_t)mi.offset + (size_t)k * (size_t)mi.type_size,
                                mi.type_size, mi.is_bool, mi.fp_kind, &mi);
      }
    }
    return;
  }
  if (psx_tag_member_is_tag_aggregate(&mi)) {
    if (psx_tag_member_is_struct_aggregate(&mi)) {
      emit_obj_global_struct_members_data_rec(mi.tag_kind, mi.tag_name, mi.tag_len, d, gv,
                                              cur, base_off);
    } else {
      emit_obj_global_union_member_data(mi.tag_kind, mi.tag_name, mi.tag_len, d, gv,
                                        cur, base_off);
    }
    consume_trailing_zero_union_padding(cur, start_idx,
                                        psx_tag_flat_slot_count(tk, tn, tl));
    return;
  }
  int slot = psx_gvar_init_cursor_advance(cur);
  data_write_init_slot_at(d, gv, slot, base_off, mi.type_size, mi.is_bool, mi.fp_kind, &mi);
  consume_trailing_zero_union_padding(cur, start_idx,
                                      psx_tag_flat_slot_count(tk, tn, tl));
}

static int global_has_object_payload(global_var_t *gv) {
  if (!gv) return 0;
  psx_gvar_view_t view = psx_gvar_view(gv);
  if (psx_gvar_is_tag_aggregate(gv)) {
    return view.init_count > 0;
  }
  return view.init_symbol || view.init_count > 0 ||
         view.fp_kind != TK_FLOAT_KIND_NONE || view.has_init;
}

static void emit_obj_global_aggregate_data(obj_data_t *d, global_var_t *gv, int size) {
  psx_gvar_view_t view = psx_gvar_view(gv);
  psx_gvar_aggregate_layout_t layout = psx_gvar_aggregate_layout(gv);
  wb_zero(&d->bytes, size);
  if (view.init_count <= 0) return;
  if (!psx_gvar_is_tag_aggregate(gv)) {
    obj_unsupported_msg("global aggregate initializer in Wasm object mode");
  }
  psx_gvar_init_cursor_t cur = psx_gvar_init_cursor(gv);
  if (layout.is_union) {
    if (layout.is_array) {
      for (int e = 0; e < layout.elem_count && psx_gvar_init_cursor_has(&cur); e++) {
        emit_obj_global_union_member_data(layout.tag_kind, layout.tag_name, layout.tag_len, d, gv,
                                          &cur, (size_t)e * (size_t)layout.elem_size);
      }
    } else {
      emit_obj_global_union_member_data(layout.tag_kind, layout.tag_name, layout.tag_len, d, gv,
                                        &cur, 0);
    }
    return;
  }
  if (layout.is_array) {
    for (int e = 0; e < layout.elem_count && psx_gvar_init_cursor_has(&cur); e++) {
      emit_obj_global_struct_members_data_rec(layout.tag_kind, layout.tag_name, layout.tag_len,
                                              d, gv, &cur,
                                              (size_t)e * (size_t)layout.elem_size);
    }
  } else {
    emit_obj_global_struct_members_data_rec(layout.tag_kind, layout.tag_name, layout.tag_len,
                                            d, gv, &cur, 0);
  }
}

static void emit_obj_global(global_var_t *gv, void *user) {
  (void)user;
  psx_gvar_view_t view = psx_gvar_view(gv);
  if (view.is_extern_decl) {
    intern_data(view.name, view.name_len, 2, view.is_static, 1);
    return;
  }

  int size = psx_gvar_storage_size(gv, 4);
  obj_data_t *d = intern_data(view.name, view.name_len, align_log2_for_size(size),
                              view.is_static, 0);
  if (d->is_emitted) {
    if (!global_has_object_payload(gv) || d->bytes.len != 0) return;
    d->bytes.len = 0;
    d->reloc_count = 0;
    d->is_emitted = 0;
  }
  data_note_alloc_size(d, (size_t)size);

  if (psx_gvar_is_tag_aggregate(gv)) {
    emit_obj_global_aggregate_data(d, gv, size);
  } else if (view.init_symbol) {
    if (psx_ctx_has_function_name(view.init_symbol, view.init_symbol_len)) {
      ensure_func_sig_for_address(view.init_symbol, view.init_symbol_len,
                                  func_sig_from_global_funcptr(gv, view.init_symbol,
                                                               view.init_symbol_len));
    }
    data_write_symbol_addr(d, view.init_symbol, view.init_symbol_len,
                           view.init_symbol_offset, size);
  } else if (view.init_count > 0) {
    int elem = psx_gvar_initializer_element_size(gv, size);
    if (elem != 1 && elem != 2 && elem != 4 && elem != 8) {
      obj_unsupported_msg("global array element size in Wasm object mode");
    }
    int is_fp_array = view.has_init_fvalues &&
                      (view.fp_kind == TK_FLOAT_KIND_FLOAT ||
                       view.fp_kind >= TK_FLOAT_KIND_DOUBLE);
    int total = elem > 0 ? (size + elem - 1) / elem : 0;
    for (int i = 0; i < total; i++) {
      psx_gvar_init_slot_t slot = psx_gvar_init_slot_view(gv, i);
      if (slot.symbol) {
        if (psx_ctx_has_function_name(slot.symbol, slot.symbol_len)) {
          ensure_func_sig_for_address(slot.symbol, slot.symbol_len,
                                      func_sig_from_global_funcptr(gv, slot.symbol,
                                                                   slot.symbol_len));
        }
        data_write_symbol_addr(d, slot.symbol, slot.symbol_len, slot.value, elem);
      } else {
        uint64_t value = (uint64_t)slot.value;
        if (is_fp_array) {
          if (view.fp_kind == TK_FLOAT_KIND_FLOAT) {
            float f = (float)slot.fvalue;
            uint32_t bits;
            memcpy(&bits, &f, sizeof(bits));
            value = bits;
          } else {
            uint64_t bits;
            memcpy(&bits, &slot.fvalue, sizeof(bits));
            value = bits;
          }
        }
        data_write_scalar(d, value, elem);
      }
    }
  } else if (view.fp_kind == TK_FLOAT_KIND_FLOAT) {
    float f = view.has_init ? (float)view.fval : 0.0f;
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    data_write_scalar(d, bits, 4);
  } else if (view.fp_kind >= TK_FLOAT_KIND_DOUBLE) {
    double f = view.has_init ? view.fval : 0.0;
    uint64_t bits;
    memcpy(&bits, &f, sizeof(bits));
    data_write_scalar(d, bits, 8);
  } else {
    if (!view.has_init) {
      /* Leave BSS-like globals out of the object payload; linear memory starts zeroed. */
    } else if (view.init_val == 0) {
      wb_zero(&d->bytes, size);
    } else {
      data_write_scalar(d, (uint64_t)view.init_val, size);
    }
  }
  d->is_emitted = 1;
}

static void count_obj_global(global_var_t *gv, void *user) {
  (void)gv;
  int *count = (int *)user;
  (*count)++;
}

void wasm32_obj_emit_data_segments(void) {
  ps_iter_string_literals(emit_obj_string_literal, NULL);
  int global_count = 0;
  ps_iter_globals(count_obj_global, &global_count);
  reserve_data_capacity(g_obj.data_count + global_count + 8);
  ps_iter_globals(emit_obj_global, NULL);
}

void wasm32_obj_end(void) {
  assign_indices();

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
  int ndata = defined_data_count();
  section_index++; /* Type */
  if (has_imports) section_index++;
  if (has_defs) section_index++; /* Function */
  if (ndata > 0) section_index++; /* DataCount */
  if (has_defs) code_section_index = section_index++;
  if (ndata > 0) data_section_index = section_index++;

  wb_t out = {0};
  wb_u32le(&out, 0x6d736100);
  wb_u32le(&out, 1);
  emit_type_section(&out);
  emit_import_section(&out);
  emit_function_section(&out);
  emit_datacount_section(&out);
  emit_code_section(&out);
  emit_data_section(&out);
  emit_linking_section(&out);
  emit_reloc_section(&out, "reloc.CODE", code_section_index, g_obj.code_relocs, g_obj.code_reloc_count);
  emit_reloc_section(&out, "reloc.DATA", data_section_index, g_obj.data_relocs, g_obj.data_reloc_count);

  if (g_obj.out && fwrite(out.data, 1, out.len, g_obj.out) != out.len) {
    diag_emit_internalf(DIAG_ERR_INTERNAL_USAGE, "%s", "failed to write Wasm object output");
  }
  if (g_obj.capture_output) {
    free(g_obj_capture.data);
    g_obj_capture = out;
  } else {
    if (!g_obj.out) {
      diag_emit_internalf(DIAG_ERR_INTERNAL_USAGE, "%s", "missing Wasm object output sink");
    }
    free(out.data);
  }
}
