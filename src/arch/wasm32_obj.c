#include "wasm32_obj.h"
#include "../diag/diag.h"
#include "../parser/parser_public.h"
#include "../parser/semantic_ctx.h"
#include "../tokenizer/escape.h"
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

  WASM_SYM_FUNCTION = 0,
  WASM_SYM_DATA = 1,

  WASM_SYMBOL_BINDING_LOCAL = 0x2,
  WASM_SYMBOL_UNDEFINED = 0x10,
  WASM_SYMBOL_EXPLICIT_NAME = 0x40,

  WASM_SEGMENT_INFO = 5,
  WASM_SYMBOL_TABLE = 8,
};

typedef struct {
  unsigned char *data;
  size_t len;
  size_t cap;
} wb_t;

typedef struct {
  unsigned char *body;
  int target_sym;
  int target_is_data;
  size_t body_off;
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
  FILE *out;
  obj_func_t *funcs;
  int func_count;
  int func_cap;
  obj_data_t *data;
  int data_count;
  int data_cap;
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
  if (b->len + add <= b->cap) return;
  size_t ncap = b->cap ? b->cap * 2 : 128;
  while (ncap < b->len + add) ncap *= 2;
  b->data = xrealloc(b->data, ncap);
  b->cap = ncap;
}

static void wb_u8(wb_t *b, unsigned v) {
  wb_reserve(b, 1);
  b->data[b->len++] = (unsigned char)v;
}

static void wb_bytes(wb_t *b, const void *p, size_t n) {
  if (n == 0) return;
  wb_reserve(b, n);
  memcpy(b->data + b->len, p, n);
  b->len += n;
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
    v >>= 7;
    if ((v == 0 && !sign) || (v == -1 && sign)) more = 0;
    else byte |= 0x80;
    wb_u8(b, byte);
  }
}

static size_t wb_uleb5(wb_t *b, uint32_t v) {
  size_t off = b->len;
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

static void wb_int_le(wb_t *b, uint64_t value, int size) {
  if (size < 0) obj_unsupported_msg("negative data size in Wasm object mode");
  for (int i = 0; i < size; i++) wb_u8(b, (unsigned)((value >> (8 * i)) & 0xff));
}

static void wb_zero(wb_t *b, int size) {
  if (size < 0) obj_unsupported_msg("negative data size in Wasm object mode");
  wb_reserve(b, (size_t)size);
  memset(b->data + b->len, 0, (size_t)size);
  b->len += (size_t)size;
}

static obj_data_t *data_for_symbol(char *sym, int sym_len, int *out_addend) {
  if (!sym) return NULL;
  if (psx_ctx_has_function_name(sym, sym_len >= 0 ? sym_len : (int)strlen(sym))) {
    obj_unsupported_msg("function address relocation in Wasm object mode");
  }
  int name_len = sym_len >= 0 ? sym_len : (int)strlen(sym);
  global_var_t *gv = sym_len >= 0 ? psx_find_global_var(sym, sym_len) : NULL;
  int is_undefined = gv && gv->is_extern_decl;
  if (out_addend) *out_addend = 0;
  return intern_data(sym, name_len, 2, gv ? gv->is_static : 0, is_undefined);
}

static int sig_equal(const obj_sig_t *a, const obj_sig_t *b) {
  if (a->nparams != b->nparams || wasm_ir_type(a->result) != wasm_ir_type(b->result)) return 0;
  for (int i = 0; i < a->nparams; i++) {
    if (wasm_ir_type(a->params[i]) != wasm_ir_type(b->params[i])) return 0;
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

static int data_index(obj_data_t *d) {
  return (int)(d - g_obj.data);
}

static void func_add_reloc(obj_func_t *f, int type, size_t body_off, int target_sym,
                           int target_is_data, int addend) {
  if (f->reloc_count == f->reloc_cap) {
    int ncap = f->reloc_cap ? f->reloc_cap * 2 : 8;
    f->relocs = xrealloc(f->relocs, (size_t)ncap * sizeof(*f->relocs));
    f->reloc_cap = ncap;
  }
  obj_reloc_t *r = &f->relocs[f->reloc_count++];
  r->body = f->body.data;
  r->body_off = body_off;
  r->type = type;
  r->target_sym = target_sym;
  r->target_is_data = target_is_data;
  r->addend = addend;
}

static void data_add_reloc(obj_data_t *d, int type, size_t body_off, int target_sym,
                           int target_is_data, int addend) {
  if (d->reloc_count == d->reloc_cap) {
    int ncap = d->reloc_cap ? d->reloc_cap * 2 : 8;
    d->relocs = xrealloc(d->relocs, (size_t)ncap * sizeof(*d->relocs));
    d->reloc_cap = ncap;
  }
  obj_reloc_t *r = &d->relocs[d->reloc_count++];
  r->body = d->bytes.data;
  r->body_off = body_off;
  r->type = type;
  r->target_sym = target_sym;
  r->target_is_data = target_is_data;
  r->addend = addend;
}

static void add_global_reloc(obj_reloc_t **arr, int *count, int *cap, int type,
                             size_t off, int target_sym, int addend) {
  if (*count == *cap) {
    int ncap = *cap ? *cap * 2 : 16;
    *arr = xrealloc(*arr, (size_t)ncap * sizeof(**arr));
    *cap = ncap;
  }
  obj_reloc_t *r = &(*arr)[(*count)++];
  r->body = NULL;
  r->body_off = off;
  r->type = type;
  r->target_sym = target_sym;
  r->addend = addend;
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

static void note_vreg_type(ir_type_t *types, int ntypes, ir_val_t v) {
  if (v.id >= 0 && v.id < ntypes) types[v.id] = wasm_ir_type(v.type);
}

static void collect_local_types(ir_func_t *f, ir_type_t *types, int ntypes) {
  for (int v = 0; v < ntypes; v++) types[v] = IR_TY_I32;
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      note_vreg_type(types, ntypes, i->dst);
      note_vreg_type(types, ntypes, i->src1);
      note_vreg_type(types, ntypes, i->src2);
      note_vreg_type(types, ntypes, i->src3);
      note_vreg_type(types, ntypes, i->callee);
      note_vreg_type(types, ntypes, i->ret_struct_area);
      for (int a = 0; a < i->nargs; a++) note_vreg_type(types, ntypes, i->args[a]);
    }
  }
}

static void emit_local_get(wb_t *b, int idx) {
  wb_u8(b, 0x20);
  wb_uleb(b, (uint32_t)idx);
}

static void emit_local_set(wb_t *b, int idx) {
  wb_u8(b, 0x21);
  wb_uleb(b, (uint32_t)idx);
}

static void emit_const(wb_t *b, ir_type_t type, long long value) {
  type = wasm_ir_type(type);
  if (type == IR_TY_I64) {
    wb_u8(b, 0x42);
    wb_sleb(b, value);
  } else if (type == IR_TY_I32) {
    wb_u8(b, 0x41);
    wb_sleb(b, value);
  } else {
    obj_unsupported_msg("floating-point immediates in Wasm object mode");
  }
}

static void emit_val(wb_t *b, ir_val_t v, ir_type_t want, int param_count) {
  want = wasm_ir_type(want);
  if (v.id == IR_VAL_IMM) {
    emit_const(b, want, v.imm);
    return;
  }
  if (v.id < 0) obj_unsupported_msg("missing Wasm object value");
  ir_type_t got = wasm_ir_type(v.type);
  emit_local_get(b, local_index(param_count, v.id));
  if (got == want) return;
  if (got == IR_TY_I32 && want == IR_TY_I64) {
    wb_u8(b, 0xad); /* i64.extend_i32_u */
  } else if (got == IR_TY_I64 && want == IR_TY_I32) {
    wb_u8(b, 0xa7); /* i32.wrap_i64 */
  } else {
    obj_unsupported_msg("unsupported Wasm object value cast");
  }
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

static void emit_memarg(wb_t *b, ir_type_t ty) {
  wb_uleb(b, (uint32_t)mem_align_log2(ty));
  wb_uleb(b, 0);
}

static int collect_param_count(ir_func_t *f) {
  int max_idx = -1;
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      if (i->op == IR_PARAM && i->src1.id == IR_VAL_IMM && i->src1.imm >= 0) {
        if ((int)i->src1.imm > max_idx) max_idx = (int)i->src1.imm;
      }
    }
  }
  return max_idx + 1;
}

static void collect_func_sig(ir_func_t *f, obj_sig_t *sig) {
  if (f->ret_struct_size > 0 || f->ret_area_vreg >= 0 || f->ret_complex_half > 0) {
    obj_unsupported_msg("aggregate or complex return in Wasm object mode");
  }
  memset(sig, 0, sizeof(*sig));
  sig->nparams = collect_param_count(f);
  sig->result = f->ret_type == IR_TY_VOID ? IR_TY_VOID : wasm_ir_type(f->ret_type);
  if (sig->nparams > 0) {
    sig->params = xrealloc(NULL, (size_t)sig->nparams * sizeof(ir_type_t));
    for (int p = 0; p < sig->nparams; p++) sig->params[p] = IR_TY_I64;
    for (ir_block_t *b = f->entry; b; b = b->next) {
      for (ir_inst_t *i = b->head; i; i = i->next) {
        if (i->op == IR_PARAM && i->src1.id == IR_VAL_IMM && i->src1.imm >= 0 &&
            i->src1.imm < sig->nparams) {
          sig->params[i->src1.imm] = wasm_ir_type(i->dst.type);
        }
      }
    }
  }
}

static obj_sig_t call_sig_from_inst(ir_inst_t *i) {
  obj_sig_t sig = {0};
  if (i->ret_struct_size > 0 || i->ret_struct_area.id != IR_VAL_NONE || i->ret_complex_half > 0) {
    obj_unsupported_msg("aggregate or complex call in Wasm object mode");
  }
  if (i->is_variadic_call) obj_unsupported_msg("variadic call in Wasm object mode");
  sig.nparams = i->is_variadic_call ? i->nargs_fixed : i->nargs;
  if (sig.nparams > 0) {
    sig.params = xrealloc(NULL, (size_t)sig.nparams * sizeof(ir_type_t));
    for (int a = 0; a < sig.nparams; a++) {
      ir_type_t ty = wasm_ir_type(i->args[a].type);
      int pcat = i->sym ? psx_ctx_get_function_param_category(i->sym, i->sym_len, a) : PSX_PCAT_UNSET;
      int int_size = i->sym ? psx_ctx_get_function_param_int_size(i->sym, i->sym_len, a) : 0;
      if (pcat == PSX_PCAT_PTR) ty = IR_TY_I32;
      else if (int_size == 8 || pcat == PSX_PCAT_INT8) ty = IR_TY_I64;
      sig.params[a] = ty;
    }
  }
  sig.result = (i->is_void_call || i->dst.id == IR_VAL_NONE || i->dst.type == IR_TY_VOID)
                 ? IR_TY_VOID
                 : wasm_ir_type(i->dst.type);
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
    case IR_LE: return is64 ? 0x55 : 0x4c;
    case IR_ULT: return is64 ? 0x54 : 0x49;
    case IR_ULE: return is64 ? 0x56 : 0x4d;
    default: obj_unsupported_op(op);
  }
  return 0;
}

static void gen_func_body(obj_func_t *of, ir_func_t *f) {
  int of_index = (int)(of - g_obj.funcs);
  int param_count = of->sig.nparams;
  int nlocals = f->next_vreg_id;
  wb_t body = {0};
  ir_type_t *local_types = NULL;
  if (nlocals > 0) {
    local_types = xrealloc(NULL, (size_t)nlocals * sizeof(ir_type_t));
    collect_local_types(f, local_types, nlocals);
  }

  wb_uleb(&body, (uint32_t)nlocals);
  for (int v = 0; v < nlocals; v++) {
    wb_uleb(&body, 1);
    wb_u8(&body, wasm_valtype(local_types[v]));
  }

  for (ir_block_t *blk = f->entry; blk; blk = blk->next) {
    for (ir_inst_t *i = blk->head; i; i = i->next) {
      switch (i->op) {
        case IR_NOP:
        case IR_LABEL:
          break;
        case IR_PARAM:
          if (i->src1.id != IR_VAL_IMM || i->src1.imm < 0) obj_unsupported_op(i->op);
          emit_local_get(&body, (int)i->src1.imm);
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        case IR_LOAD_IMM:
          emit_const(&body, i->dst.type, i->src1.imm);
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        case IR_LOAD_STR:
        case IR_LOAD_SYM: {
          if (!i->sym) obj_unsupported_op(i->op);
          if (i->op == IR_LOAD_SYM && psx_ctx_has_function_name(i->sym, i->sym_len)) {
            obj_func_t *target = intern_func(i->sym, i->sym_len);
            of = &g_obj.funcs[of_index];
            wb_u8(&body, 0x41);
            size_t imm_off = wb_uleb5(&body, 0);
            func_add_reloc(of, R_WASM_TABLE_INDEX_SLEB, imm_off,
                           (int)(target - g_obj.funcs), 0, 0);
            emit_local_set(&body, local_index(param_count, i->dst.id));
            break;
          }
          int addend = 0;
          obj_data_t *d = data_for_symbol(i->sym, i->op == IR_LOAD_STR ? -1 : i->sym_len, &addend);
          wb_u8(&body, 0x41);
          size_t imm_off = wb_uleb5(&body, 0);
          func_add_reloc(of, R_WASM_MEMORY_ADDR_LEB, imm_off, data_index(d), 1, addend);
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        }
        case IR_ZEXT:
        case IR_SEXT:
        case IR_TRUNC:
          emit_val(&body, i->src1, i->dst.type, param_count);
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        case IR_LOAD:
          emit_val(&body, i->src1, IR_TY_PTR, param_count);
          wb_u8(&body, load_opcode(i->dst.type, i->is_unsigned));
          emit_memarg(&body, i->dst.type);
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        case IR_STORE:
          emit_val(&body, i->src1, IR_TY_PTR, param_count);
          emit_val(&body, i->src2, i->src2.type, param_count);
          wb_u8(&body, store_opcode(i->src2.type));
          emit_memarg(&body, i->src2.type);
          break;
        case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: case IR_MOD:
        case IR_UDIV: case IR_UMOD: case IR_AND: case IR_OR: case IR_XOR:
        case IR_SHL: case IR_SHR: case IR_LSR:
        case IR_EQ: case IR_NE: case IR_LT: case IR_LE: case IR_ULT: case IR_ULE: {
          ir_type_t op_ty = wasm_ir_type(i->src1.type == IR_TY_I64 || i->src2.type == IR_TY_I64
                                         ? IR_TY_I64 : i->src1.type);
          emit_val(&body, i->src1, op_ty, param_count);
          emit_val(&body, i->src2, op_ty, param_count);
          wb_u8(&body, int_binop_opcode(i->op, op_ty));
          emit_local_set(&body, local_index(param_count, i->dst.id));
          break;
        }
        case IR_CALL: {
          if (i->callee.id != IR_VAL_NONE) {
            obj_sig_t csig = call_sig_from_inst(i);
            int type_index = intern_type(&csig);
            g_obj.has_indirect_call = 1;
            for (int a = 0; a < csig.nparams; a++) {
              emit_val(&body, i->args[a], csig.params[a], param_count);
            }
            emit_val(&body, i->callee, IR_TY_I32, param_count);
            wb_u8(&body, 0x11);
            wb_uleb(&body, (uint32_t)type_index);
            wb_uleb(&body, 0);
            if (csig.result != IR_TY_VOID && i->dst.id >= 0) {
              emit_local_set(&body, local_index(param_count, i->dst.id));
            }
            free(csig.params);
            break;
          }
          if (!i->sym) obj_unsupported_op(i->op);
          obj_func_t *target = intern_func(i->sym, i->sym_len);
          of = &g_obj.funcs[of_index];
          obj_sig_t csig = call_sig_from_inst(i);
          if (target->sig.nparams == 0 && target->sig.result == IR_TY_VOID && !target->defined) {
            target->sig = csig;
          } else if (!sig_equal(&target->sig, &csig)) {
            obj_unsupported_msg("conflicting Wasm object function signature");
          } else {
            free(csig.params);
          }
          for (int a = 0; a < target->sig.nparams; a++) {
            emit_val(&body, i->args[a], target->sig.params[a], param_count);
          }
          wb_u8(&body, 0x10);
          size_t imm_off = wb_uleb5(&body, 0);
          func_add_reloc(of, R_WASM_FUNCTION_INDEX_LEB, imm_off, -1, 0, 0);
          of->relocs[of->reloc_count - 1].target_sym = target - g_obj.funcs;
          if (target->sig.result != IR_TY_VOID && i->dst.id >= 0) {
            emit_local_set(&body, local_index(param_count, i->dst.id));
          }
          break;
        }
        case IR_RET:
          if (i->src1.id != IR_VAL_NONE) emit_val(&body, i->src1, of->sig.result, param_count);
          wb_u8(&body, 0x0f);
          break;
        default:
          obj_unsupported_op(i->op);
      }
    }
  }
  wb_u8(&body, 0x0b);
  of->body = body;
  free(local_types);
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

  int sym = 0;
  for (int i = 0; i < g_obj.func_count; i++) g_obj.funcs[i].symbol_index = sym++;
  for (int i = 0; i < g_obj.data_count; i++) g_obj.data[i].symbol_index = sym++;
  g_obj.symbol_count = sym;

  for (int i = 0; i < g_obj.func_count; i++) {
    obj_func_t *f = &g_obj.funcs[i];
    for (int r = 0; r < f->reloc_count; r++) {
      if (f->relocs[r].target_is_data) {
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
    size_t body_payload_start = p.len + body_size.len;
    wb_bytes(&p, body_size.data, body_size.len);
    wb_bytes(&p, f->body.data, f->body.len);
    for (int r = 0; r < f->reloc_count; r++) {
      add_global_reloc(&g_obj.code_relocs, &g_obj.code_reloc_count, &g_obj.code_reloc_cap,
                       f->relocs[r].type, body_payload_start + f->relocs[r].body_off,
                       f->relocs[r].target_sym, f->relocs[r].addend);
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
    size_t data_start = p.len;
    wb_bytes(&p, d->bytes.data, d->bytes.len);
    for (int r = 0; r < d->reloc_count; r++) {
      int sym = d->relocs[r].target_is_data
                  ? g_obj.data[d->relocs[r].target_sym].symbol_index
                  : g_obj.funcs[d->relocs[r].target_sym].symbol_index;
      add_global_reloc(&g_obj.data_relocs, &g_obj.data_reloc_count, &g_obj.data_reloc_cap,
                       d->relocs[r].type, data_start + d->relocs[r].body_off,
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
  wb_uleb(p, (uint32_t)d->bytes.len);
}

static void emit_linking_section(wb_t *out) {
  wb_t payload = {0};
  wb_uleb(&payload, 2);

  wb_t symtab = {0};
  wb_uleb(&symtab, (uint32_t)g_obj.symbol_count);
  for (int i = 0; i < g_obj.func_count; i++) emit_symbol_entry(&symtab, &g_obj.funcs[i]);
  for (int i = 0; i < g_obj.data_count; i++) emit_data_symbol_entry(&symtab, &g_obj.data[i]);
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

void wasm32_obj_begin(void) {
  FILE *out = g_obj.out;
  memset(&g_obj, 0, sizeof(g_obj));
  g_obj.out = out;
}

void wasm32_obj_gen_ir_module(ir_module_t *m) {
  for (ir_func_t *f = m->funcs; f; f = f->next) {
    obj_func_t *of = intern_func(f->name, f->name_len);
    if (of->defined) obj_unsupported_msg("duplicate function in Wasm object mode");
    collect_func_sig(f, &of->sig);
    of->defined = 1;
    of->is_static = f->is_static;
    gen_func_body(of, f);
  }
}

static void emit_obj_string_literal(string_lit_t *lit, void *user) {
  (void)user;
  int name_len = lit->label ? (int)strlen(lit->label) : 0;
  if (!lit->label || name_len == 0) obj_unsupported_msg("string literal label in Wasm object mode");
  int cw = lit->char_width > 0 ? (int)lit->char_width : TK_CHAR_WIDTH_CHAR;
  if (cw != TK_CHAR_WIDTH_CHAR) obj_unsupported_msg("wide string literal in Wasm object mode");
  obj_data_t *d = intern_data(lit->label, name_len, 0, 1, 0);
  if (d->is_emitted) return;
  int i = 0;
  while (i < lit->len) {
    uint32_t v = 0;
    if (lit->str[i] == '\\') {
      tk_parse_escape_value(lit->str, lit->len, &i, &v);
    } else {
      v = (unsigned char)lit->str[i++];
    }
    if (v > 0xff) obj_unsupported_msg("non-byte string literal in Wasm object mode");
    wb_u8(&d->bytes, (unsigned)v);
  }
  wb_u8(&d->bytes, 0);
  d->is_emitted = 1;
}

static void data_write_symbol_addr(obj_data_t *d, char *sym, int sym_len,
                                   long long addend, int size) {
  if (sym && sym_len >= 0 && psx_ctx_has_function_name(sym, sym_len)) {
    if (addend != 0) obj_unsupported_msg("function address addend in Wasm object mode");
    obj_func_t *target = find_func(sym, sym_len);
    if (!target) obj_unsupported_msg("unresolved function address in Wasm object mode");
    size_t off = d->bytes.len;
    wb_int_le(&d->bytes, 0, size);
    data_add_reloc(d, R_WASM_TABLE_INDEX_I32, off, (int)(target - g_obj.funcs), 0, 0);
    return;
  }
  int reloc_addend = 0;
  obj_data_t *target = data_for_symbol(sym, sym_len, &reloc_addend);
  size_t off = d->bytes.len;
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
    if (!target) obj_unsupported_msg("unresolved function address in Wasm object mode");
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
                                    tk_float_kind_t fp_kind) {
  if (idx < 0) return;
  char *sym = (idx < gv->init_count && gv->init_value_symbols) ? gv->init_value_symbols[idx] : NULL;
  int sym_len = (idx < gv->init_count && gv->init_value_symbol_lens)
                  ? gv->init_value_symbol_lens[idx] : 0;
  long long value = (idx < gv->init_count && gv->init_values) ? gv->init_values[idx] : 0;
  if (sym) {
    data_write_symbol_addr_at(d, off, sym, sym_len, value, size);
    return;
  }
  if (sym_len == -2 || sym_len == -3) {
    double fv = (idx < gv->init_count && gv->init_fvalues) ? gv->init_fvalues[idx] : 0.0;
    data_write_fp_at(d, off, sym_len == -2 ? TK_FLOAT_KIND_FLOAT : TK_FLOAT_KIND_DOUBLE, fv);
    return;
  }
  if (fp_kind != TK_FLOAT_KIND_NONE) {
    double fv = (idx < gv->init_count && gv->init_fvalues) ? gv->init_fvalues[idx] : 0.0;
    data_write_fp_at(d, off, fp_kind, fv);
    return;
  }
  if (normalize_bool) value = value != 0;
  data_write_int_le_at(d, off, (uint64_t)value, size);
}

static void emit_obj_global_union_member_data(token_kind_t tk, char *tn, int tl,
                                              obj_data_t *d, global_var_t *gv,
                                              int *val_idx, size_t base_off);

static void emit_obj_global_bitfield_unit_data(token_kind_t tk, char *tn, int tl,
                                               int *member_idx, obj_data_t *d,
                                               global_var_t *gv, int *val_idx,
                                               size_t base_off) {
  tag_member_info_t first = {0};
  if (!psx_ctx_get_tag_member_info(tk, tn, tl, *member_idx, &first) || first.bit_width <= 0) {
    obj_unsupported_msg("global bitfield initializer in Wasm object mode");
  }
  int unit_off = first.offset;
  int unit_size = first.type_size;
  int n_members = psx_ctx_get_tag_member_count(tk, tn, tl);
  uint64_t packed = 0;
  int m = *member_idx;
  while (m < n_members && *val_idx < gv->init_count) {
    tag_member_info_t mi = {0};
    if (!psx_ctx_get_tag_member_info(tk, tn, tl, m, &mi)) break;
    if (mi.bit_width <= 0 || mi.offset != unit_off) break;
    uint64_t mask = mi.bit_width >= 64 ? UINT64_MAX : ((UINT64_C(1) << mi.bit_width) - 1);
    uint64_t value = (uint64_t)((*val_idx < gv->init_count && gv->init_values)
                                  ? gv->init_values[*val_idx] : 0);
    packed |= (value & mask) << mi.bit_offset;
    (*val_idx)++;
    m++;
  }
  data_write_int_le_at(d, base_off + (size_t)unit_off, packed, unit_size);
  *member_idx = m - 1;
}

static void emit_obj_global_bitfield_member_data(obj_data_t *d, global_var_t *gv, int idx,
                                                 size_t base_off,
                                                 const tag_member_info_t *mi) {
  if (!mi || mi->bit_width <= 0) obj_unsupported_msg("global bitfield initializer in Wasm object mode");
  uint64_t mask = mi->bit_width >= 64 ? UINT64_MAX : ((UINT64_C(1) << mi->bit_width) - 1);
  uint64_t value = (uint64_t)((idx < gv->init_count && gv->init_values) ? gv->init_values[idx] : 0);
  uint64_t packed = (value & mask) << mi->bit_offset;
  data_write_int_le_at(d, base_off + (size_t)mi->offset, packed, mi->type_size);
}

static int union_init_slot_fp_size(global_var_t *gv, int idx) {
  char *sym = (idx < gv->init_count && gv->init_value_symbols) ? gv->init_value_symbols[idx] : NULL;
  int sym_len = (idx < gv->init_count && gv->init_value_symbol_lens)
                  ? gv->init_value_symbol_lens[idx] : 0;
  if (sym) return 0;
  if (sym_len == -2) return 4;
  if (sym_len == -3) return 8;
  return 0;
}

static void select_union_member_for_init_slot(token_kind_t tk, char *tn, int tl,
                                              global_var_t *gv, int idx,
                                              tag_member_info_t *mi) {
  int init_fp_size = union_init_slot_fp_size(gv, idx);
  int selected_fp_size = mi->fp_kind == TK_FLOAT_KIND_FLOAT ? 4
                       : mi->fp_kind >= TK_FLOAT_KIND_DOUBLE ? 8 : 0;
  if (init_fp_size == selected_fp_size) return;
  if (init_fp_size == 0 && selected_fp_size == 0) return;

  int n = psx_ctx_get_tag_member_count(tk, tn, tl);
  for (int i = 0; i < n; i++) {
    tag_member_info_t cand = {0};
    if (!psx_ctx_get_tag_member_info(tk, tn, tl, i, &cand)) break;
    int cand_fp_size = cand.fp_kind == TK_FLOAT_KIND_FLOAT ? 4
                       : cand.fp_kind >= TK_FLOAT_KIND_DOUBLE ? 8 : 0;
    if ((init_fp_size > 0 && cand_fp_size == init_fp_size) ||
        (init_fp_size == 0 && cand_fp_size == 0)) {
      *mi = cand;
      return;
    }
  }
}

static void emit_obj_global_struct_members_data_rec(token_kind_t tk, char *tn, int tl,
                                                    obj_data_t *d, global_var_t *gv,
                                                    int *val_idx, size_t base_off) {
  int n_members = psx_ctx_get_tag_member_count(tk, tn, tl);
  for (int m = 0; m < n_members && *val_idx < gv->init_count; m++) {
    tag_member_info_t mi = {0};
    if (!psx_ctx_get_tag_member_info(tk, tn, tl, m, &mi)) break;
    if (mi.bit_width > 0) {
      emit_obj_global_bitfield_unit_data(tk, tn, tl, &m, d, gv, val_idx, base_off);
      continue;
    }
    if (mi.array_len > 0) {
      if ((mi.tag_kind == TK_STRUCT || mi.tag_kind == TK_UNION) && !mi.is_tag_pointer) {
        for (int k = 0; k < mi.array_len && *val_idx < gv->init_count; k++) {
          size_t elem_off = base_off + (size_t)mi.offset + (size_t)k * (size_t)mi.type_size;
          if (mi.tag_kind == TK_UNION) {
            emit_obj_global_union_member_data(mi.tag_kind, mi.tag_name, mi.tag_len, d, gv,
                                              val_idx, elem_off);
          } else {
            emit_obj_global_struct_members_data_rec(mi.tag_kind, mi.tag_name, mi.tag_len, d, gv,
                                                    val_idx, elem_off);
          }
        }
      } else {
        for (int k = 0; k < mi.array_len && *val_idx < gv->init_count; k++) {
          int slot = (*val_idx)++;
          data_write_init_slot_at(d, gv, slot,
                                  base_off + (size_t)mi.offset + (size_t)k * (size_t)mi.type_size,
                                  mi.type_size, mi.is_bool, mi.fp_kind);
        }
      }
      continue;
    }
    if (mi.tag_kind == TK_STRUCT && !mi.is_tag_pointer) {
      emit_obj_global_struct_members_data_rec(mi.tag_kind, mi.tag_name, mi.tag_len, d, gv,
                                              val_idx, base_off + (size_t)mi.offset);
      continue;
    }
    if (mi.tag_kind == TK_UNION && !mi.is_tag_pointer) {
      emit_obj_global_union_member_data(mi.tag_kind, mi.tag_name, mi.tag_len, d, gv, val_idx,
                                        base_off + (size_t)mi.offset);
      continue;
    }
    int slot = (*val_idx)++;
    data_write_init_slot_at(d, gv, slot, base_off + (size_t)mi.offset, mi.type_size,
                            mi.is_bool, mi.fp_kind);
  }
}

static void emit_obj_global_union_member_data(token_kind_t tk, char *tn, int tl,
                                              obj_data_t *d, global_var_t *gv,
                                              int *val_idx, size_t base_off) {
  if (*val_idx >= gv->init_count) return;
  tag_member_info_t mi = {0};
  int ord = gv->union_init_ordinal;
  if (gv->init_union_ordinals && gv->init_union_ordinals[*val_idx] >= 0) {
    ord = gv->init_union_ordinals[*val_idx];
  }
  if (!psx_ctx_get_tag_member_info(tk, tn, tl, ord, &mi)) {
    obj_unsupported_msg("global union initializer in Wasm object mode");
  }
  select_union_member_for_init_slot(tk, tn, tl, gv, *val_idx, &mi);
  if (mi.bit_width > 0) {
    emit_obj_global_bitfield_member_data(d, gv, (*val_idx)++, base_off, &mi);
    return;
  }
  if ((mi.tag_kind == TK_STRUCT || mi.tag_kind == TK_UNION) && !mi.is_tag_pointer) {
    if (mi.tag_kind == TK_STRUCT) {
      emit_obj_global_struct_members_data_rec(mi.tag_kind, mi.tag_name, mi.tag_len, d, gv,
                                              val_idx, base_off);
    } else {
      emit_obj_global_union_member_data(mi.tag_kind, mi.tag_name, mi.tag_len, d, gv,
                                        val_idx, base_off);
    }
    return;
  }
  int slot = (*val_idx)++;
  data_write_init_slot_at(d, gv, slot, base_off, mi.type_size, mi.is_bool, mi.fp_kind);
}

static void emit_obj_global_aggregate_data(obj_data_t *d, global_var_t *gv, int size) {
  wb_zero(&d->bytes, size);
  if (gv->init_count <= 0) return;
  if (gv->is_tag_pointer || (gv->tag_kind != TK_STRUCT && gv->tag_kind != TK_UNION)) {
    obj_unsupported_msg("global aggregate initializer in Wasm object mode");
  }
  int val_idx = 0;
  if (gv->tag_kind == TK_UNION) {
    if (gv->is_array) {
      int elem_size = gv->deref_size > 0 ? gv->deref_size : 0;
      int total = elem_size > 0 ? size / elem_size : 0;
      for (int e = 0; e < total && val_idx < gv->init_count; e++) {
        emit_obj_global_union_member_data(gv->tag_kind, gv->tag_name, gv->tag_len, d, gv,
                                          &val_idx, (size_t)e * (size_t)elem_size);
      }
    } else {
      emit_obj_global_union_member_data(gv->tag_kind, gv->tag_name, gv->tag_len, d, gv,
                                        &val_idx, 0);
    }
    return;
  }
  if (gv->is_array) {
    int elem_size = gv->deref_size > 0 ? gv->deref_size : 0;
    int total = elem_size > 0 ? size / elem_size : 0;
    for (int e = 0; e < total && val_idx < gv->init_count; e++) {
      emit_obj_global_struct_members_data_rec(gv->tag_kind, gv->tag_name, gv->tag_len, d, gv,
                                              &val_idx, (size_t)e * (size_t)elem_size);
    }
  } else {
    emit_obj_global_struct_members_data_rec(gv->tag_kind, gv->tag_name, gv->tag_len, d, gv,
                                            &val_idx, 0);
  }
}

static void emit_obj_global(global_var_t *gv, void *user) {
  (void)user;
  if (gv->is_thread_local) obj_unsupported_msg("TLS global in Wasm object mode");
  if (gv->is_extern_decl) {
    intern_data(gv->name, gv->name_len, 2, gv->is_static, 1);
    return;
  }

  int size = gv->type_size > 0 ? gv->type_size : 4;
  obj_data_t *d = intern_data(gv->name, gv->name_len, align_log2_for_size(size), gv->is_static, 0);
  if (d->is_emitted) return;

  if ((gv->tag_kind == TK_STRUCT || gv->tag_kind == TK_UNION) && !gv->is_tag_pointer) {
    emit_obj_global_aggregate_data(d, gv, size);
  } else if (gv->init_symbol) {
    data_write_symbol_addr(d, gv->init_symbol, gv->init_symbol_len, gv->init_symbol_offset, size);
  } else if (gv->init_count > 0) {
    int elem = gv->is_array && gv->deref_size > 0 ? gv->deref_size : size;
    if (elem != 1 && elem != 2 && elem != 4 && elem != 8) {
      obj_unsupported_msg("global array element size in Wasm object mode");
    }
    int total = elem > 0 ? (size + elem - 1) / elem : 0;
    for (int i = 0; i < total; i++) {
      char *sym = (i < gv->init_count && gv->init_value_symbols) ? gv->init_value_symbols[i] : NULL;
      int sym_len = (i < gv->init_count && gv->init_value_symbol_lens)
                      ? gv->init_value_symbol_lens[i] : 0;
      if (sym) {
        long long off = (i < gv->init_count && gv->init_values) ? gv->init_values[i] : 0;
        data_write_symbol_addr(d, sym, sym_len, off, elem);
      } else {
        uint64_t value = (uint64_t)((i < gv->init_count && gv->init_values) ? gv->init_values[i] : 0);
        data_write_scalar(d, value, elem);
      }
    }
  } else if (gv->fp_kind == TK_FLOAT_KIND_FLOAT) {
    float f = gv->has_init ? (float)gv->fval : 0.0f;
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    data_write_scalar(d, bits, 4);
  } else if (gv->fp_kind >= TK_FLOAT_KIND_DOUBLE) {
    double f = gv->has_init ? gv->fval : 0.0;
    uint64_t bits;
    memcpy(&bits, &f, sizeof(bits));
    data_write_scalar(d, bits, 8);
  } else {
    if (!gv->has_init || gv->init_val == 0) wb_zero(&d->bytes, size);
    else data_write_scalar(d, (uint64_t)gv->init_val, size);
  }
  d->is_emitted = 1;
}

void wasm32_obj_emit_data_segments(void) {
  ps_iter_string_literals(emit_obj_string_literal, NULL);
  ps_iter_globals(emit_obj_global, NULL);
}

void wasm32_obj_end(void) {
  assign_indices();

  int has_imports = 0;
  int has_defs = 0;
  for (int i = 0; i < g_obj.func_count; i++) {
    if (g_obj.funcs[i].imported) has_imports = 1;
    if (g_obj.funcs[i].defined) has_defs = 1;
  }
  if (g_obj.has_indirect_call) has_imports = 1;
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

  if (!g_obj.out || fwrite(out.data, 1, out.len, g_obj.out) != out.len) {
    diag_emit_internalf(DIAG_ERR_INTERNAL_USAGE, "%s", "failed to write Wasm object output");
  }
  free(out.data);
}
