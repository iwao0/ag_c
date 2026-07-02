#include "wasm32_ir.h"
#include "../codegen_emit.h"
#include "../diag/diag.h"
#include "../parser/parser_public.h"
#include "../parser/semantic_ctx.h"
#include "../tokenizer/escape.h"
#include "../tokenizer/literals.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WASM_PAGE_SIZE 65536
#define WASM_STATIC_BASE 1024
#define WASM_STACK_BASE WASM_PAGE_SIZE

typedef struct {
  int vreg;
  int offset;
  int size;
  ir_type_t value_type;
  char *func_ref_name;
  int func_ref_name_len;
} wasm_alloca_slot_t;

typedef struct {
  global_var_t *gv;
  int offset;
  char *func_ref_name;
  int func_ref_name_len;
  int is_set;
} wasm_global_func_state_t;

typedef struct {
  ir_func_t *f;
  wasm_alloca_slot_t *allocas;
  int alloca_count;
  int alloca_cap;
  wasm_global_func_state_t *global_func_states;
  int global_func_state_count;
  int global_func_state_cap;
  int frame_size;
  int *vreg_type_seen;
  ir_type_t *vreg_types;
  unsigned char *vreg_unsigned;
  char **vreg_func_ref_names;
  int *vreg_func_ref_name_lens;
  global_var_t **vreg_global_refs;
  int *vreg_global_ref_offsets;
  unsigned char *vreg_const_known;
  long long *vreg_const_values;
  int has_control_flow;
} wasm_func_ctx_t;

typedef struct {
  char *name;
  int name_len;
  int addr;
  int size;
} wasm_data_symbol_t;

typedef struct {
  int next_data_off;
  wasm_data_symbol_t *symbols;
  int symbol_count;
  int symbol_cap;
} wasm_data_ctx_t;

typedef struct {
  char *name;
  int name_len;
} wasm_func_ref_t;

typedef struct {
  wasm_func_ref_t *refs;
  int ref_count;
  int ref_cap;
  int needs_table;
} wasm_func_table_ctx_t;

static wasm_data_ctx_t g_data = {WASM_STATIC_BASE, NULL, 0, 0};
static wasm_func_table_ctx_t g_func_table = {NULL, 0, 0, 0};
static const char k_wasm_indent_spaces[] = "                                ";

static void wasm_emit_indent(int spaces) {
  int chunk = (int)sizeof(k_wasm_indent_spaces) - 1;
  while (spaces > chunk) {
    cg_emitf("%s", k_wasm_indent_spaces);
    spaces -= chunk;
  }
  if (spaces > 0) cg_emitf("%.*s", spaces, k_wasm_indent_spaces);
}

#define wasm_emitf(spaces, ...)       \
  do {                                \
    wasm_emit_indent((spaces));       \
    cg_emitf(__VA_ARGS__);            \
  } while (0)

static const char *wasm_type(ir_type_t t) {
  switch (t) {
    case IR_TY_I8:
    case IR_TY_I16:
    case IR_TY_I32:
    case IR_TY_PTR:
      return "i32";
    case IR_TY_I64:
      return "i64";
    case IR_TY_F32:
      return "f32";
    case IR_TY_F64:
      return "f64";
    default:
      return NULL;
  }
}

static void wasm_unsupported_msg(const char *msg);
static int has_undefined_function(const char *name, int len);

static int is_fp_type(ir_type_t t) {
  return t == IR_TY_F32 || t == IR_TY_F64;
}

static const char *wasm_fp_type_or_unsupported(ir_type_t t) {
  const char *ty = wasm_type(t);
  if (!ty || !is_fp_type(t)) wasm_unsupported_msg("unsupported Wasm floating-point type");
  return ty;
}

static const char *wasm_int_type_or_unsupported(ir_type_t t) {
  const char *ty = wasm_type(t);
  if (!ty || is_fp_type(t)) wasm_unsupported_msg("unsupported Wasm integer type");
  return ty;
}

static const char *wasm_any_type_or_unsupported(ir_type_t t) {
  const char *ty = wasm_type(t);
  if (!ty) wasm_unsupported_msg("unsupported Wasm value type");
  return ty;
}

static int align_to(int value, int align) {
  if (align <= 1) return value;
  int mask = align - 1;
  return (value + mask) & ~mask;
}

static void wasm_unsupported_op(ir_op_t op) {
  diag_emit_internalf(DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP,
                      diag_message_for(DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP),
                      ir_op_name(op));
}

static void wasm_unsupported_msg(const char *msg) {
  diag_emit_internalf(DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP,
                      diag_message_for(DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP),
                      msg);
}

static int name_eq(const char *a, int alen, const char *b, int blen) {
  return alen == blen && a && b && memcmp(a, b, (size_t)alen) == 0;
}

static wasm_data_symbol_t *find_data_symbol(const char *name, int name_len) {
  for (int i = 0; i < g_data.symbol_count; i++) {
    if (name_eq(g_data.symbols[i].name, g_data.symbols[i].name_len, name, name_len)) {
      return &g_data.symbols[i];
    }
  }
  return NULL;
}

static wasm_data_symbol_t *intern_data_symbol(const char *name, int name_len, int size, int align) {
  wasm_data_symbol_t *existing = find_data_symbol(name, name_len);
  if (existing) return existing;
  if (g_data.symbol_count == g_data.symbol_cap) {
    int ncap = g_data.symbol_cap ? g_data.symbol_cap * 2 : 32;
    wasm_data_symbol_t *n = realloc(g_data.symbols, (size_t)ncap * sizeof(*n));
    if (!n) diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
    g_data.symbols = n;
    g_data.symbol_cap = ncap;
  }
  g_data.next_data_off = align_to(g_data.next_data_off, align > 0 ? align : 1);
  wasm_data_symbol_t *s = &g_data.symbols[g_data.symbol_count++];
  s->name = (char *)name;
  s->name_len = name_len;
  s->addr = g_data.next_data_off;
  s->size = size > 0 ? size : 1;
  g_data.next_data_off += s->size;
  return s;
}

typedef struct {
  const char *label;
  int label_len;
  string_lit_t *found;
} string_find_ctx_t;

static void find_string_lit_cb(string_lit_t *lit, void *user) {
  string_find_ctx_t *ctx = user;
  if (!ctx->found && lit->label &&
      name_eq(lit->label, (int)strlen(lit->label), ctx->label, ctx->label_len)) {
    ctx->found = lit;
  }
}

static int narrow_string_encoded_size(string_lit_t *lit) {
  if (!lit) return 1;
  int cw = lit->char_width > 0 ? (int)lit->char_width : TK_CHAR_WIDTH_CHAR;
  if (cw != TK_CHAR_WIDTH_CHAR) {
    int bytes = cw;  /* trailing NUL code unit */
    int i = 0;
    while (i < lit->len) {
      uint32_t units[2];
      int nu = tk_next_string_code_units(lit->str, lit->len, &i, cw, units);
      bytes += nu * cw;
    }
    return bytes;
  }
  int bytes = 1;  /* trailing NUL */
  int i = 0;
  while (i < lit->len) {
    uint32_t v = 0;
    if (lit->str[i] == '\\') tk_parse_escape_value(lit->str, lit->len, &i, &v);
    else v = (unsigned char)lit->str[i++];
    bytes += (v < 0x80) ? 1 : (v < 0x800) ? 2 : (v < 0x10000) ? 3 : 4;
  }
  return bytes;
}

static int data_addr_for_string_label(const char *sym) {
  if (!sym) return -1;
  string_find_ctx_t ctx = {sym, (int)strlen(sym), NULL};
  ps_iter_string_literals(find_string_lit_cb, &ctx);
  int size = narrow_string_encoded_size(ctx.found);
  return intern_data_symbol(sym, ctx.label_len, size, 1)->addr;
}

typedef struct {
  const char *name;
  int name_len;
  global_var_t *found;
} global_find_ctx_t;

static void find_global_cb(global_var_t *gv, void *user) {
  global_find_ctx_t *ctx = user;
  if (!ctx->found && name_eq(gv->name, gv->name_len, ctx->name, ctx->name_len)) ctx->found = gv;
}

static int data_addr_for_global(const char *sym, int sym_len) {
  global_find_ctx_t ctx = {sym, sym_len, NULL};
  ps_iter_globals(find_global_cb, &ctx);
  int size = (ctx.found && ctx.found->type_size > 0) ? ctx.found->type_size : 8;
  int align = size >= 8 ? 8 : size >= 4 ? 4 : size >= 2 ? 2 : 1;
  return intern_data_symbol(sym, sym_len, size, align)->addr;
}

static int intern_function_table_ref(char *name, int name_len) {
  if (!name || name_len <= 0) return -1;
  for (int i = 0; i < g_func_table.ref_count; i++) {
    if (name_eq(g_func_table.refs[i].name, g_func_table.refs[i].name_len, name, name_len)) return i;
  }
  if (g_func_table.ref_count == g_func_table.ref_cap) {
    int ncap = g_func_table.ref_cap ? g_func_table.ref_cap * 2 : 16;
    wasm_func_ref_t *n = realloc(g_func_table.refs, (size_t)ncap * sizeof(*n));
    if (!n) diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
    g_func_table.refs = n;
    g_func_table.ref_cap = ncap;
  }
  int idx = g_func_table.ref_count++;
  g_func_table.refs[idx].name = name;
  g_func_table.refs[idx].name_len = name_len;
  return idx;
}

static int function_table_index_or_unsupported(char *name, int name_len) {
  if (!psx_ctx_has_function_name(name, name_len)) return -1;
  return intern_function_table_ref(name, name_len);
}

static ir_type_t funcptr_int_mask_type(unsigned iw) {
  if (iw == 3) return IR_TY_I32;
  return IR_TY_I64;
}

static ir_type_t funcptr_param_type_from_inst(const ir_inst_t *i, int idx, ir_type_t fallback) {
  if (!i || !i->has_funcptr_sig || idx < 0 || idx >= 8) return fallback;
  unsigned fp = (i->funcptr_param_fp_mask >> (2 * idx)) & 3u;
  unsigned iw = (i->funcptr_param_int_mask >> (2 * idx)) & 3u;
  if (fp == TK_FLOAT_KIND_FLOAT) return IR_TY_F32;
  if (fp >= TK_FLOAT_KIND_DOUBLE) return IR_TY_F64;
  if (iw != 0) {
    ir_type_t ty = funcptr_int_mask_type(iw);
    if (ty == IR_TY_I32 && fallback != IR_TY_PTR && !is_fp_type(fallback)) return IR_TY_I64;
    return ty;
  }
  return fallback;
}

static int has_minimal_libc_stub_function(char *name, int name_len) {
  return (name_len == 6 && memcmp(name, "printf", 6) == 0) ||
         (name_len == 7 && memcmp(name, "fprintf", 7) == 0) ||
         (name_len == 4 && memcmp(name, "puts", 4) == 0) ||
         (name_len == 7 && memcmp(name, "sprintf", 7) == 0) ||
         (name_len == 8 && memcmp(name, "snprintf", 8) == 0);
}

static void emit_function_table(void) {
  if (g_func_table.ref_count <= 0) {
    if (g_func_table.needs_table) wasm_emitf(2, "(table 1 funcref)\n");
    return;
  }
  for (int i = 0; i < g_func_table.ref_count; i++) {
    char *name = g_func_table.refs[i].name;
    int name_len = g_func_table.refs[i].name_len;
    if (!psx_ctx_is_function_defined(name, name_len) &&
        !(has_undefined_function(name, name_len) && has_minimal_libc_stub_function(name, name_len))) {
      wasm_unsupported_msg("external function pointer in Wasm backend");
    }
  }
  wasm_emitf(2, "(table %d funcref)\n", g_func_table.ref_count);
  wasm_emitf(2, "(elem (i32.const 0)");
  for (int i = 0; i < g_func_table.ref_count; i++) {
    cg_emitf(" $%.*s", g_func_table.refs[i].name_len, g_func_table.refs[i].name);
  }
  cg_emitf(")\n");
}

static ir_type_t func_param_type_from_decl(ir_func_t *f, int idx, ir_type_t raw) {
  if (psx_ctx_get_function_param_category(f->name, f->name_len, idx) == PSX_PCAT_STRUCT) {
    return IR_TY_PTR;
  }
  if (raw != IR_TY_PTR && psx_ctx_get_function_param_int_size(f->name, f->name_len, idx) == 8) {
    return IR_TY_I64;
  }
  return raw;
}

static int func_has_hidden_ret_area(ir_func_t *f) {
  return f && (f->ret_struct_size > 0 || f->ret_complex_half > 0);
}

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

static ir_inst_t *func_param_inst_at_ordinal(ir_func_t *f, int ordinal) {
  if (ordinal < 0) return NULL;
  int cur = 0;
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      if (i->op != IR_PARAM || i->src1.id != IR_VAL_IMM || i->src1.imm < 0) continue;
      if (cur == ordinal) return i;
      cur++;
    }
  }
  return NULL;
}

static void collect_vreg_type_as(wasm_func_ctx_t *ctx, ir_val_t v, ir_type_t type) {
  if (v.id < 0 || v.id >= ctx->f->next_vreg_id) return;
  if (!wasm_type(type)) return;
  if (ctx->vreg_type_seen[v.id]) return;
  ctx->vreg_type_seen[v.id] = 1;
  ctx->vreg_types[v.id] = type;
}

static void collect_vreg_type(wasm_func_ctx_t *ctx, ir_val_t v) {
  collect_vreg_type_as(ctx, v, v.type);
}

static void collect_inst_vregs(wasm_func_ctx_t *ctx, ir_inst_t *i) {
  if (i->op == IR_PARAM && i->src1.id == IR_VAL_IMM) {
    int ordinal = func_param_ordinal_for_inst(ctx->f, i);
    ir_type_t ty = (i->src1.imm < 0) ? IR_TY_PTR
                                     : func_param_type_from_decl(ctx->f, ordinal, i->dst.type);
    collect_vreg_type_as(ctx, i->dst, ty);
    return;
  }
  collect_vreg_type(ctx, i->dst);
  collect_vreg_type(ctx, i->src1);
  collect_vreg_type(ctx, i->src2);
  collect_vreg_type(ctx, i->src3);
  collect_vreg_type(ctx, i->ret_struct_area);
  collect_vreg_type(ctx, i->callee);
  for (int a = 0; a < i->nargs; a++) collect_vreg_type(ctx, i->args[a]);
}

static void add_alloca_slot(wasm_func_ctx_t *ctx, ir_inst_t *i) {
  if (ctx->alloca_count == ctx->alloca_cap) {
    int ncap = ctx->alloca_cap ? ctx->alloca_cap * 2 : 16;
    wasm_alloca_slot_t *n = realloc(ctx->allocas, (size_t)ncap * sizeof(*n));
    if (!n) diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
    ctx->allocas = n;
    ctx->alloca_cap = ncap;
  }
  int align = i->alloca_align > 0 ? i->alloca_align : 4;
  ctx->frame_size = align_to(ctx->frame_size, align);
  ctx->allocas[ctx->alloca_count].vreg = i->dst.id;
  ctx->allocas[ctx->alloca_count].offset = ctx->frame_size;
  ctx->allocas[ctx->alloca_count].size = i->alloca_size;
  ctx->allocas[ctx->alloca_count].value_type = IR_TY_VOID;
  ctx->alloca_count++;
  ctx->frame_size += i->alloca_size;
}

static wasm_alloca_slot_t *find_alloca_slot(wasm_func_ctx_t *ctx, int vreg) {
  for (int i = 0; i < ctx->alloca_count; i++) {
    if (ctx->allocas[i].vreg == vreg) return &ctx->allocas[i];
  }
  return NULL;
}

static int find_alloca_offset(wasm_func_ctx_t *ctx, int vreg) {
  wasm_alloca_slot_t *slot = find_alloca_slot(ctx, vreg);
  if (slot) return slot->offset;
  return -1;
}

static ir_type_t effective_val_type(wasm_func_ctx_t *ctx, ir_val_t v) {
  if (v.id >= 0 && v.id < ctx->f->next_vreg_id && ctx->vreg_type_seen[v.id]) {
    return ctx->vreg_types[v.id];
  }
  return v.type;
}

static void set_vreg_type(wasm_func_ctx_t *ctx, ir_val_t v, ir_type_t type) {
  if (v.id < 0 || v.id >= ctx->f->next_vreg_id) return;
  if (!wasm_type(type)) return;
  ctx->vreg_type_seen[v.id] = 1;
  ctx->vreg_types[v.id] = type;
}

static int val_is_unsigned(wasm_func_ctx_t *ctx, ir_val_t v) {
  return v.id >= 0 && v.id < ctx->f->next_vreg_id && ctx->vreg_unsigned[v.id];
}

static void set_vreg_func_ref(wasm_func_ctx_t *ctx, int vreg, char *name, int name_len) {
  if (vreg < 0 || vreg >= ctx->f->next_vreg_id) return;
  ctx->vreg_func_ref_names[vreg] = name;
  ctx->vreg_func_ref_name_lens[vreg] = name_len;
}

static char *get_vreg_func_ref(wasm_func_ctx_t *ctx, int vreg, int *out_len) {
  if (out_len) *out_len = 0;
  if (vreg < 0 || vreg >= ctx->f->next_vreg_id) return NULL;
  if (out_len) *out_len = ctx->vreg_func_ref_name_lens[vreg];
  return ctx->vreg_func_ref_names[vreg];
}

static void set_vreg_global_ref(wasm_func_ctx_t *ctx, int vreg, global_var_t *gv, int offset) {
  if (vreg < 0 || vreg >= ctx->f->next_vreg_id) return;
  ctx->vreg_global_refs[vreg] = gv;
  ctx->vreg_global_ref_offsets[vreg] = offset;
}

static global_var_t *get_vreg_global_ref(wasm_func_ctx_t *ctx, int vreg, int *out_offset) {
  if (out_offset) *out_offset = 0;
  if (vreg < 0 || vreg >= ctx->f->next_vreg_id) return NULL;
  if (out_offset) *out_offset = ctx->vreg_global_ref_offsets[vreg];
  return ctx->vreg_global_refs[vreg];
}

static void set_vreg_const(wasm_func_ctx_t *ctx, int vreg, long long value) {
  if (vreg < 0 || vreg >= ctx->f->next_vreg_id) return;
  ctx->vreg_const_known[vreg] = 1;
  ctx->vreg_const_values[vreg] = value;
}

static int get_vreg_const(wasm_func_ctx_t *ctx, ir_val_t v, long long *out_value) {
  if (v.id == IR_VAL_IMM) {
    if (out_value) *out_value = v.imm;
    return 1;
  }
  if (v.id < 0 || v.id >= ctx->f->next_vreg_id || !ctx->vreg_const_known[v.id]) return 0;
  if (out_value) *out_value = ctx->vreg_const_values[v.id];
  return 1;
}

static char *global_scalar_func_ref(global_var_t *gv, int *out_len) {
  if (out_len) *out_len = 0;
  if (!gv || !gv->init_symbol || gv->init_symbol_len <= 0) return NULL;
  if (!psx_ctx_has_function_name(gv->init_symbol, gv->init_symbol_len)) return NULL;
  if (out_len) *out_len = gv->init_symbol_len;
  return gv->init_symbol;
}

static wasm_global_func_state_t *find_global_func_state(wasm_func_ctx_t *ctx, global_var_t *gv,
                                                        int offset) {
  if (!gv) return NULL;
  for (int i = 0; i < ctx->global_func_state_count; i++) {
    if (ctx->global_func_states[i].gv == gv && ctx->global_func_states[i].offset == offset) {
      return &ctx->global_func_states[i];
    }
  }
  return NULL;
}

static void set_global_func_ref(wasm_func_ctx_t *ctx, global_var_t *gv, int offset, char *name,
                                int name_len) {
  if (!gv) return;
  wasm_global_func_state_t *s = find_global_func_state(ctx, gv, offset);
  if (!s) {
    if (ctx->global_func_state_count == ctx->global_func_state_cap) {
      int ncap = ctx->global_func_state_cap ? ctx->global_func_state_cap * 2 : 8;
      wasm_global_func_state_t *n =
          realloc(ctx->global_func_states, (size_t)ncap * sizeof(*n));
      if (!n) diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
      ctx->global_func_states = n;
      ctx->global_func_state_cap = ncap;
    }
    s = &ctx->global_func_states[ctx->global_func_state_count++];
    s->gv = gv;
    s->offset = offset;
  }
  s->func_ref_name = name;
  s->func_ref_name_len = name_len;
  s->is_set = 1;
}

static char *global_member_func_ref(global_var_t *gv, int offset, int *out_len) {
  if (out_len) *out_len = 0;
  if (!gv || gv->tag_kind == TK_EOF || gv->is_tag_pointer || gv->is_array || !gv->init_value_symbols) {
    return NULL;
  }
  int n = psx_ctx_get_tag_member_count(gv->tag_kind, gv->tag_name, gv->tag_len);
  int init_idx = 0;
  for (int m = 0; m < n; m++) {
    tag_member_info_t mi = {0};
    if (!psx_ctx_get_tag_member_info(gv->tag_kind, gv->tag_name, gv->tag_len, m, &mi)) break;
    if (mi.bit_width > 0) {
      init_idx++;
      continue;
    }
    if ((mi.tag_kind == TK_STRUCT || mi.tag_kind == TK_UNION) && !mi.is_tag_pointer) return NULL;
    if (mi.array_len > 0) {
      for (int k = 0; k < mi.array_len && init_idx < gv->init_count; k++, init_idx++) {
        if (mi.offset + k * mi.type_size != offset) continue;
        char *sym = gv->init_value_symbols[init_idx];
        int sym_len = gv->init_value_symbol_lens ? gv->init_value_symbol_lens[init_idx] : 0;
        if (sym && sym_len > 0 && psx_ctx_has_function_name(sym, sym_len)) {
          if (out_len) *out_len = sym_len;
          return sym;
        }
        return NULL;
      }
      continue;
    }
    if (mi.offset == offset && init_idx < gv->init_count) {
      char *sym = gv->init_value_symbols[init_idx];
      int sym_len = gv->init_value_symbol_lens ? gv->init_value_symbol_lens[init_idx] : 0;
      if (sym && sym_len > 0 && psx_ctx_has_function_name(sym, sym_len)) {
        if (out_len) *out_len = sym_len;
        return sym;
      }
      return NULL;
    }
    init_idx++;
  }
  return NULL;
}

static char *current_global_func_ref(wasm_func_ctx_t *ctx, global_var_t *gv, int offset,
                                     int *out_len) {
  if (out_len) *out_len = 0;
  wasm_global_func_state_t *s = find_global_func_state(ctx, gv, offset);
  if (s && s->is_set) {
    if (out_len) *out_len = s->func_ref_name_len;
    return s->func_ref_name;
  }
  if (offset == 0) {
    char *name = global_scalar_func_ref(gv, out_len);
    if (name) return name;
  }
  return global_member_func_ref(gv, offset, out_len);
}

static void infer_alloca_value_types(wasm_func_ctx_t *ctx) {
  for (ir_block_t *b = ctx->f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      if (i->op == IR_LOAD && i->dst.id >= 0 && i->dst.id < ctx->f->next_vreg_id &&
          i->is_unsigned) {
        ctx->vreg_unsigned[i->dst.id] = 1;
      }
      if (i->op == IR_ZEXT && i->dst.id >= 0 && i->dst.id < ctx->f->next_vreg_id) {
        ctx->vreg_unsigned[i->dst.id] = 1;
      }
      if (i->op == IR_F2I && i->is_unsigned &&
          i->dst.id >= 0 && i->dst.id < ctx->f->next_vreg_id) {
        ctx->vreg_unsigned[i->dst.id] = 1;
      }
      if (i->op == IR_STORE) {
        wasm_alloca_slot_t *slot = find_alloca_slot(ctx, i->src1.id);
        ir_type_t val_ty = i->src2.type;
        if (slot && slot->value_type == IR_TY_VOID && wasm_type(val_ty)) {
          slot->value_type = val_ty;
        }
      }
      if (i->op == IR_LOAD && i->dst.type == IR_TY_PTR) {
        wasm_alloca_slot_t *slot = find_alloca_slot(ctx, i->src1.id);
        if (slot && slot->value_type == IR_TY_I64) set_vreg_type(ctx, i->dst, IR_TY_I64);
      }
    }
  }
}

static void analyze_func(wasm_func_ctx_t *ctx) {
  ctx->vreg_type_seen = calloc((size_t)ctx->f->next_vreg_id, sizeof(int));
  ctx->vreg_types = calloc((size_t)ctx->f->next_vreg_id, sizeof(ir_type_t));
  ctx->vreg_unsigned = calloc((size_t)ctx->f->next_vreg_id, sizeof(unsigned char));
  ctx->vreg_func_ref_names = calloc((size_t)ctx->f->next_vreg_id, sizeof(char *));
  ctx->vreg_func_ref_name_lens = calloc((size_t)ctx->f->next_vreg_id, sizeof(int));
  ctx->vreg_global_refs = calloc((size_t)ctx->f->next_vreg_id, sizeof(global_var_t *));
  ctx->vreg_global_ref_offsets = calloc((size_t)ctx->f->next_vreg_id, sizeof(int));
  ctx->vreg_const_known = calloc((size_t)ctx->f->next_vreg_id, sizeof(unsigned char));
  ctx->vreg_const_values = calloc((size_t)ctx->f->next_vreg_id, sizeof(long long));
  if (!ctx->vreg_type_seen || !ctx->vreg_types || !ctx->vreg_unsigned ||
      !ctx->vreg_func_ref_names || !ctx->vreg_func_ref_name_lens || !ctx->vreg_global_refs ||
      !ctx->vreg_global_ref_offsets || !ctx->vreg_const_known || !ctx->vreg_const_values) {
    diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
  }
  for (ir_block_t *b = ctx->f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      collect_inst_vregs(ctx, i);
      if (i->op == IR_ALLOCA) add_alloca_slot(ctx, i);
      if (i->op == IR_LABEL || i->op == IR_BR || i->op == IR_BR_COND) ctx->has_control_flow = 1;
    }
  }
  for (ir_block_t *b = ctx->f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      if (i->op == IR_CALL && i->callee.id != IR_VAL_NONE) {
        set_vreg_type(ctx, i->callee, IR_TY_I32);
      }
    }
  }
  infer_alloca_value_types(ctx);
  ctx->frame_size = align_to(ctx->frame_size, 16);
}

static void emit_val_expr(wasm_func_ctx_t *ctx, ir_val_t v) {
  ir_type_t type = effective_val_type(ctx, v);
  const char *ty = wasm_type(type);
  if (!ty) wasm_unsupported_msg("unsupported Wasm value type");
  if (v.id == IR_VAL_IMM) {
    if (is_fp_type(type)) {
      cg_emitf("(%s.const %.17g)", ty, v.fp_imm);
    } else if (type == IR_TY_I64) {
      cg_emitf("(%s.const %lld)", ty, v.imm);
    } else {
      cg_emitf("(%s.const %d)", ty, (int32_t)v.imm);
    }
  } else if (v.id >= 0) {
    cg_emitf("(local.get $v%d)", v.id);
  } else {
    wasm_unsupported_msg("missing Wasm value");
  }
}

static void emit_val_expr_as(wasm_func_ctx_t *ctx, ir_val_t v, ir_type_t target) {
  if (target == IR_TY_PTR) target = IR_TY_I32;
  ir_type_t src = effective_val_type(ctx, v);
  if (src == IR_TY_PTR) src = IR_TY_I32;
  if (src == target) {
    emit_val_expr(ctx, v);
    return;
  }
  if (target == IR_TY_I64 && src != IR_TY_I64) {
    if (v.id == IR_VAL_IMM && !is_fp_type(src)) {
      cg_emitf("(i64.const %lld)", v.imm);
      return;
    }
    cg_emitf("(%s ", val_is_unsigned(ctx, v) ? "i64.extend_i32_u" : "i64.extend_i32_s");
    emit_val_expr(ctx, v);
    cg_emitf(")");
    return;
  }
  if (target != IR_TY_I64 && src == IR_TY_I64) {
    cg_emitf("(i32.wrap_i64 ");
    emit_val_expr(ctx, v);
    cg_emitf(")");
    return;
  }
  emit_val_expr(ctx, v);
}

static void emit_wasm_type_cast_prefix(ir_type_t from, ir_type_t to, int is_unsigned) {
  if (from == IR_TY_PTR) from = IR_TY_I32;
  if (to == IR_TY_PTR) to = IR_TY_I32;
  if (from == to) return;
  if (to == IR_TY_I64 && from != IR_TY_I64) {
    cg_emitf("(%s ", is_unsigned ? "i64.extend_i32_u" : "i64.extend_i32_s");
  } else if (to != IR_TY_I64 && from == IR_TY_I64) {
    cg_emitf("(i32.wrap_i64 ");
  }
}

static void emit_wasm_type_cast_suffix(ir_type_t from, ir_type_t to) {
  if (from == IR_TY_PTR) from = IR_TY_I32;
  if (to == IR_TY_PTR) to = IR_TY_I32;
  if (from != to && (from == IR_TY_I64 || to == IR_TY_I64)) cg_emitf(")");
}

static void emit_addr_expr(wasm_func_ctx_t *ctx, ir_val_t v) {
  emit_val_expr_as(ctx, v, IR_TY_I32);
}

static void emit_addr_plus_const(wasm_func_ctx_t *ctx, ir_val_t base, int off) {
  if (off == 0) {
    emit_addr_expr(ctx, base);
    return;
  }
  cg_emitf("(i32.add ");
  emit_addr_expr(ctx, base);
  cg_emitf(" (i32.const %d))", off);
}

static const char *wasm_binop(ir_op_t op, ir_type_t t) {
  const char *prefix = wasm_type(t);
  if (!prefix) return NULL;
  switch (op) {
    case IR_ADD: return t == IR_TY_I64 ? "i64.add" : "i32.add";
    case IR_SUB: return t == IR_TY_I64 ? "i64.sub" : "i32.sub";
    case IR_MUL: return t == IR_TY_I64 ? "i64.mul" : "i32.mul";
    case IR_DIV: return t == IR_TY_I64 ? "i64.div_s" : "i32.div_s";
    case IR_UDIV: return t == IR_TY_I64 ? "i64.div_u" : "i32.div_u";
    case IR_MOD: return t == IR_TY_I64 ? "i64.rem_s" : "i32.rem_s";
    case IR_UMOD: return t == IR_TY_I64 ? "i64.rem_u" : "i32.rem_u";
    case IR_AND: return t == IR_TY_I64 ? "i64.and" : "i32.and";
    case IR_OR:  return t == IR_TY_I64 ? "i64.or" : "i32.or";
    case IR_XOR: return t == IR_TY_I64 ? "i64.xor" : "i32.xor";
    case IR_SHL: return t == IR_TY_I64 ? "i64.shl" : "i32.shl";
    case IR_SHR: return t == IR_TY_I64 ? "i64.shr_s" : "i32.shr_s";
    case IR_LSR: return t == IR_TY_I64 ? "i64.shr_u" : "i32.shr_u";
    case IR_EQ:  return t == IR_TY_I64 ? "i64.eq" : "i32.eq";
    case IR_NE:  return t == IR_TY_I64 ? "i64.ne" : "i32.ne";
    case IR_LT:  return t == IR_TY_I64 ? "i64.lt_s" : "i32.lt_s";
    case IR_LE:  return t == IR_TY_I64 ? "i64.le_s" : "i32.le_s";
    case IR_ULT: return t == IR_TY_I64 ? "i64.lt_u" : "i32.lt_u";
    case IR_ULE: return t == IR_TY_I64 ? "i64.le_u" : "i32.le_u";
    default: return NULL;
  }
}

static const char *wasm_fp_binop(ir_op_t op, ir_type_t t) {
  if (!is_fp_type(t)) return NULL;
  switch (op) {
    case IR_FADD: return t == IR_TY_F64 ? "f64.add" : "f32.add";
    case IR_FSUB: return t == IR_TY_F64 ? "f64.sub" : "f32.sub";
    case IR_FMUL: return t == IR_TY_F64 ? "f64.mul" : "f32.mul";
    case IR_FDIV: return t == IR_TY_F64 ? "f64.div" : "f32.div";
    case IR_FEQ:  return t == IR_TY_F64 ? "f64.eq" : "f32.eq";
    case IR_FNE:  return t == IR_TY_F64 ? "f64.ne" : "f32.ne";
    case IR_FLT:  return t == IR_TY_F64 ? "f64.lt" : "f32.lt";
    case IR_FLE:  return t == IR_TY_F64 ? "f64.le" : "f32.le";
    default: return NULL;
  }
}

static ir_type_t effective_load_type(wasm_func_ctx_t *ctx, ir_inst_t *i) {
  ir_type_t dst_ty = effective_val_type(ctx, i->dst);
  if (dst_ty != i->dst.type) return dst_ty;
  if (i->dst.type == IR_TY_PTR) {
    wasm_alloca_slot_t *slot = find_alloca_slot(ctx, i->src1.id);
    if (slot && slot->value_type == IR_TY_I64) return IR_TY_I64;
  }
  return i->dst.type;
}

static void emit_load(wasm_func_ctx_t *ctx, ir_inst_t *i, int indent) {
  wasm_alloca_slot_t *slot = find_alloca_slot(ctx, i->src1.id);
  const char *op = NULL;
  switch (effective_load_type(ctx, i)) {
    case IR_TY_I8:  op = i->is_unsigned ? "i32.load8_u" : "i32.load8_s"; break;
    case IR_TY_I16: op = i->is_unsigned ? "i32.load16_u" : "i32.load16_s"; break;
    case IR_TY_I32:
    case IR_TY_PTR: op = "i32.load"; break;
    case IR_TY_I64: op = "i64.load"; break;
    case IR_TY_F32: op = "f32.load"; break;
    case IR_TY_F64: op = "f64.load"; break;
    default: wasm_unsupported_op(i->op);
  }
  wasm_emitf(indent, "(local.set $v%d (%s ", i->dst.id, op);
  emit_addr_expr(ctx, i->src1);
  cg_emitf("))\n");
  if (slot && slot->func_ref_name) {
    set_vreg_func_ref(ctx, i->dst.id, slot->func_ref_name, slot->func_ref_name_len);
  } else {
    int global_off = 0;
    global_var_t *gv = get_vreg_global_ref(ctx, i->src1.id, &global_off);
    int name_len = 0;
    char *name = current_global_func_ref(ctx, gv, global_off, &name_len);
    if (name) set_vreg_func_ref(ctx, i->dst.id, name, name_len);
  }
}

static void emit_store(wasm_func_ctx_t *ctx, ir_inst_t *i, int indent) {
  wasm_alloca_slot_t *slot = find_alloca_slot(ctx, i->src1.id);
  int global_off = 0;
  global_var_t *gv = get_vreg_global_ref(ctx, i->src1.id, &global_off);
  const char *op = NULL;
  ir_type_t store_ty = i->src2.type;
  switch (store_ty) {
    case IR_TY_I8:  op = "i32.store8"; break;
    case IR_TY_I16: op = "i32.store16"; break;
    case IR_TY_I32:
    case IR_TY_PTR: op = "i32.store"; break;
    case IR_TY_I64: op = "i64.store"; break;
    case IR_TY_F32: op = "f32.store"; break;
    case IR_TY_F64: op = "f64.store"; break;
    default: wasm_unsupported_op(i->op);
  }
  wasm_emitf(indent, "(%s ", op);
  emit_addr_expr(ctx, i->src1);
  cg_emitf(" ");
  emit_val_expr_as(ctx, i->src2, store_ty);
  cg_emitf(")\n");
  if (slot) {
    int name_len = 0;
    char *name = get_vreg_func_ref(ctx, i->src2.id, &name_len);
    slot->func_ref_name = name;
    slot->func_ref_name_len = name_len;
  }
  if (gv) {
    int name_len = 0;
    char *name = get_vreg_func_ref(ctx, i->src2.id, &name_len);
    if (ctx->has_control_flow) {
      set_global_func_ref(ctx, gv, global_off, NULL, 0);
    } else {
      set_global_func_ref(ctx, gv, global_off, name, name_len);
    }
  }
}

static ir_type_t atomic_mem_type(ir_inst_t *i) {
  return i->atomic_width == 8 ? IR_TY_I64 : IR_TY_I32;
}

static const char *atomic_load_op(ir_inst_t *i) {
  switch (i->atomic_width ? i->atomic_width : 4) {
    case 1: return i->is_unsigned ? "i32.load8_u" : "i32.load8_s";
    case 2: return i->is_unsigned ? "i32.load16_u" : "i32.load16_s";
    case 4: return "i32.load";
    case 8: return "i64.load";
    default: return NULL;
  }
}

static const char *atomic_store_op(ir_inst_t *i) {
  switch (i->atomic_width ? i->atomic_width : 4) {
    case 1: return "i32.store8";
    case 2: return "i32.store16";
    case 4: return "i32.store";
    case 8: return "i64.store";
    default: return NULL;
  }
}

static void emit_atomic_load_expr(wasm_func_ctx_t *ctx, ir_inst_t *i) {
  const char *op = atomic_load_op(i);
  if (!op) wasm_unsupported_op(i->op);
  cg_emitf("(%s ", op);
  emit_addr_expr(ctx, i->src1);
  cg_emitf(")");
}

static void emit_atomic_store(wasm_func_ctx_t *ctx, ir_inst_t *i, ir_val_t ptr, ir_val_t val,
                              int indent) {
  const char *op = atomic_store_op(i);
  if (!op) wasm_unsupported_op(i->op);
  wasm_emitf(indent, "(%s ", op);
  emit_addr_expr(ctx, ptr);
  cg_emitf(" ");
  emit_val_expr_as(ctx, val, atomic_mem_type(i));
  cg_emitf(")\n");
}

static const char *atomic_rmw_wasm_op(ir_inst_t *i) {
  int is64 = atomic_mem_type(i) == IR_TY_I64;
  switch (i->atomic_rmw_op) {
    case IR_ARMW_ADD: return is64 ? "i64.add" : "i32.add";
    case IR_ARMW_SUB: return is64 ? "i64.sub" : "i32.sub";
    case IR_ARMW_OR:  return is64 ? "i64.or"  : "i32.or";
    case IR_ARMW_AND: return is64 ? "i64.and" : "i32.and";
    case IR_ARMW_XOR: return is64 ? "i64.xor" : "i32.xor";
    default: return NULL;
  }
}

static void emit_atomic_inst(wasm_func_ctx_t *ctx, ir_inst_t *i, int indent) {
  if (i->atomic_kind == IR_ATOMIC_FENCE) {
    wasm_emitf(indent, "(nop)\n");
    return;
  }

  ir_type_t mem_ty = atomic_mem_type(i);
  if (i->atomic_kind == IR_ATOMIC_LOAD) {
    ir_type_t dst_ty = effective_val_type(ctx, i->dst);
    wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
    emit_wasm_type_cast_prefix(mem_ty, dst_ty, i->is_unsigned);
    emit_atomic_load_expr(ctx, i);
    emit_wasm_type_cast_suffix(mem_ty, dst_ty);
    cg_emitf(")\n");
    return;
  }

  if (i->atomic_kind == IR_ATOMIC_STORE) {
    emit_atomic_store(ctx, i, i->src1, i->src2, indent);
    return;
  }

  if (i->atomic_kind == IR_ATOMIC_RMW) {
    ir_type_t dst_ty = effective_val_type(ctx, i->dst);
    wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
    emit_wasm_type_cast_prefix(mem_ty, dst_ty, i->is_unsigned);
    emit_atomic_load_expr(ctx, i);
    emit_wasm_type_cast_suffix(mem_ty, dst_ty);
    cg_emitf(")\n");

    const char *store_op = atomic_store_op(i);
    if (!store_op) wasm_unsupported_op(i->op);
    wasm_emitf(indent, "(%s ", store_op);
    emit_addr_expr(ctx, i->src1);
    cg_emitf(" ");
    if (i->atomic_rmw_op == IR_ARMW_XCHG) {
      emit_val_expr_as(ctx, i->src2, mem_ty);
    } else {
      const char *op = atomic_rmw_wasm_op(i);
      if (!op) wasm_unsupported_op(i->op);
      cg_emitf("(%s ", op);
      emit_val_expr_as(ctx, i->dst, mem_ty);
      cg_emitf(" ");
      emit_val_expr_as(ctx, i->src2, mem_ty);
      cg_emitf(")");
    }
    cg_emitf(")\n");
    return;
  }

  if (i->atomic_kind == IR_ATOMIC_CAS) {
    const char *load_op = atomic_load_op(i);
    const char *store_op = atomic_store_op(i);
    const char *eq_op = mem_ty == IR_TY_I64 ? "i64.eq" : "i32.eq";
    const char *tmp = mem_ty == IR_TY_I64 ? "$atomic_tmp_i64" : "$atomic_tmp_i32";
    const char *exp = mem_ty == IR_TY_I64 ? "$atomic_exp_i64" : "$atomic_exp_i32";
    if (!load_op || !store_op) wasm_unsupported_op(i->op);

    wasm_emitf(indent, "(local.set %s (%s ", tmp, load_op);
    emit_addr_expr(ctx, i->src1);
    cg_emitf("))\n");
    wasm_emitf(indent, "(local.set %s (%s ", exp, load_op);
    emit_addr_expr(ctx, i->src2);
    cg_emitf("))\n");
    wasm_emitf(indent, "(local.set $v%d (%s (local.get %s) (local.get %s)))\n",
               i->dst.id, eq_op, tmp, exp);
    wasm_emitf(indent, "(if (local.get $v%d)\n", i->dst.id);
    wasm_emitf(indent + 2, "(then\n");
    wasm_emitf(indent + 4, "(%s ", store_op);
    emit_addr_expr(ctx, i->src1);
    cg_emitf(" ");
    emit_val_expr_as(ctx, i->src3, mem_ty);
    cg_emitf(")\n");
    wasm_emitf(indent + 2, ")\n");
    wasm_emitf(indent, ")\n");
    wasm_emitf(indent, "(%s ", store_op);
    emit_addr_expr(ctx, i->src2);
    cg_emitf(" (local.get %s))\n", tmp);
    return;
  }

  wasm_unsupported_op(i->op);
}

static int uses_ptr_value(wasm_func_ctx_t *ctx, ir_inst_t *i) {
  ir_type_t src1 = effective_val_type(ctx, i->src1);
  ir_type_t src2 = effective_val_type(ctx, i->src2);
  return src1 == IR_TY_PTR || src2 == IR_TY_PTR;
}

static int i64_runtime_extension_unsupported(wasm_func_ctx_t *ctx, ir_val_t v) {
  ir_type_t src = effective_val_type(ctx, v);
  if (src == IR_TY_I64) return 0;
  if (v.id >= 0 && (src == IR_TY_I8 || src == IR_TY_I16 || src == IR_TY_I32)) return 0;
  if (v.id == IR_VAL_IMM) return 0;
  return 1;
}

static void emit_memcpy(wasm_func_ctx_t *ctx, ir_inst_t *i, int indent) {
  int n = i->alloca_size;
  if (n < 0) wasm_unsupported_op(i->op);
  int off = 0;
  for (; off + 8 <= n; off += 8) {
    wasm_emitf(indent, "(i64.store ");
    emit_addr_plus_const(ctx, i->src1, off);
    cg_emitf(" (i64.load ");
    emit_addr_plus_const(ctx, i->src2, off);
    cg_emitf("))\n");
  }
  for (; off + 4 <= n; off += 4) {
    wasm_emitf(indent, "(i32.store ");
    emit_addr_plus_const(ctx, i->src1, off);
    cg_emitf(" (i32.load ");
    emit_addr_plus_const(ctx, i->src2, off);
    cg_emitf("))\n");
  }
  for (; off + 2 <= n; off += 2) {
    wasm_emitf(indent, "(i32.store16 ");
    emit_addr_plus_const(ctx, i->src1, off);
    cg_emitf(" (i32.load16_u ");
    emit_addr_plus_const(ctx, i->src2, off);
    cg_emitf("))\n");
  }
  for (; off < n; off++) {
    wasm_emitf(indent, "(i32.store8 ");
    emit_addr_plus_const(ctx, i->src1, off);
    cg_emitf(" (i32.load8_u ");
    emit_addr_plus_const(ctx, i->src2, off);
    cg_emitf("))\n");
  }
}

static int val_uses_vreg(ir_val_t v, int id) {
  return v.id == id;
}

static int inst_uses_vreg(ir_inst_t *i, int id) {
  if (!i) return 0;
  if (val_uses_vreg(i->src1, id) || val_uses_vreg(i->src2, id) ||
      val_uses_vreg(i->src3, id) || val_uses_vreg(i->callee, id) ||
      val_uses_vreg(i->ret_struct_area, id)) {
    return 1;
  }
  for (int a = 0; a < i->nargs; a++) {
    if (val_uses_vreg(i->args[a], id)) return 1;
  }
  return 0;
}

static int vreg_used_after(ir_inst_t *from, int id) {
  if (!from || id < 0) return 0;
  for (ir_inst_t *i = from->next; i; i = i->next) {
    if (inst_uses_vreg(i, id)) return 1;
  }
  return 0;
}

static int emit_variadic_arg_area_prepare(wasm_func_ctx_t *ctx, ir_inst_t *i, int indent) {
  if (!i->is_variadic_call) return 0;
  int nargs_var = i->nargs - i->nargs_fixed;
  if (nargs_var <= 0) return 0;
  int bytes = align_to(nargs_var * 8, 16);
  wasm_emitf(indent, "(local.set $old_va_arg_area (global.get $__ag_va_arg_area))\n");
  wasm_emitf(indent, "(global.set $__stack_pointer (i32.sub (global.get $__stack_pointer) (i32.const %d)))\n",
             bytes);
  wasm_emitf(indent, "(global.set $__ag_va_arg_area (global.get $__stack_pointer))\n");
  for (int a = i->nargs_fixed; a < i->nargs; a++) {
    int off = (a - i->nargs_fixed) * 8;
    ir_type_t arg_ty = effective_val_type(ctx, i->args[a]);
    if (arg_ty == IR_TY_F64) {
      wasm_emitf(indent, "(f64.store (i32.add (global.get $__ag_va_arg_area) (i32.const %d)) ", off);
      emit_val_expr_as(ctx, i->args[a], IR_TY_F64);
      cg_emitf(")\n");
    } else if (arg_ty == IR_TY_F32) {
      wasm_emitf(indent, "(f64.store (i32.add (global.get $__ag_va_arg_area) (i32.const %d)) (f64.promote_f32 ", off);
      emit_val_expr_as(ctx, i->args[a], IR_TY_F32);
      cg_emitf("))\n");
    } else {
      wasm_emitf(indent, "(i64.store (i32.add (global.get $__ag_va_arg_area) (i32.const %d)) ", off);
      emit_val_expr_as(ctx, i->args[a], IR_TY_I64);
      cg_emitf(")\n");
    }
  }
  return bytes;
}

static void emit_variadic_arg_area_restore(int bytes, int indent) {
  if (bytes <= 0) return;
  wasm_emitf(indent, "(global.set $__stack_pointer (i32.add (global.get $__stack_pointer) (i32.const %d)))\n",
             bytes);
  wasm_emitf(indent, "(global.set $__ag_va_arg_area (local.get $old_va_arg_area))\n");
}

static void emit_call(wasm_func_ctx_t *ctx, ir_inst_t *i, int indent) {
  int vararg_area_bytes = emit_variadic_arg_area_prepare(ctx, i, indent);
  if (i->callee.id != IR_VAL_NONE) {
    g_func_table.needs_table = 1;
    if (i->ret_complex_half != 0) {
      wasm_unsupported_op(i->op);
    }
    int callee_name_len = 0;
    char *callee_name = get_vreg_func_ref(ctx, i->callee.id, &callee_name_len);
    int returns_aggregate = i->ret_struct_size > 0 || i->ret_struct_area.id != IR_VAL_NONE;
    if (returns_aggregate && i->ret_struct_area.id == IR_VAL_NONE) {
      wasm_unsupported_msg("indirect aggregate function call without return area in Wasm backend");
    }
    int returns_void = returns_aggregate || i->is_void_call ||
                       (callee_name && psx_ctx_is_function_ret_void(callee_name, callee_name_len)) ||
                       i->dst.id == IR_VAL_NONE || i->dst.type == IR_TY_VOID;
    int result_unused = !returns_void && !vreg_used_after(i, i->dst.id);
    const char *ret_ty = returns_void ? NULL : wasm_any_type_or_unsupported(i->dst.type);
    if (result_unused) wasm_emitf(indent, "(drop ");
    else if (!returns_void) wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
    else wasm_emitf(indent, "");
    cg_emitf("(call_indirect");
    int call_nargs = i->is_variadic_call ? i->nargs_fixed : i->nargs;
    if (returns_aggregate) cg_emitf(" (param i32)");
    for (int a = 0; a < call_nargs; a++) {
      ir_type_t raw_arg_ty = effective_val_type(ctx, i->args[a]);
      ir_type_t arg_ty = raw_arg_ty;
      int from_funcptr_sig = i->has_funcptr_sig ? 1 : 0;
      if (from_funcptr_sig) arg_ty = funcptr_param_type_from_inst(i, a, arg_ty);
      int null_ptr_pair_arg =
          a == 0 && call_nargs >= 2 && i->args[1].type == IR_TY_PTR;
      if (null_ptr_pair_arg) arg_ty = IR_TY_I32;
      if (from_funcptr_sig && !i->is_variadic_funcptr && !i->is_variadic_call &&
          arg_ty == IR_TY_I32 &&
          raw_arg_ty != IR_TY_PTR && !is_fp_type(raw_arg_ty) && !null_ptr_pair_arg) {
        arg_ty = IR_TY_I64;
      }
      if (arg_ty == IR_TY_PTR) arg_ty = IR_TY_I32;
      else if (!from_funcptr_sig && !is_fp_type(arg_ty) && !null_ptr_pair_arg) arg_ty = IR_TY_I64;
      cg_emitf(" (param %s)", wasm_any_type_or_unsupported(arg_ty));
    }
    if (!returns_void) cg_emitf(" (result %s)", ret_ty);
    if (returns_aggregate) {
      cg_emitf(" ");
      emit_val_expr_as(ctx, i->ret_struct_area, IR_TY_PTR);
    }
    for (int a = 0; a < call_nargs; a++) {
      ir_type_t raw_arg_ty = effective_val_type(ctx, i->args[a]);
      ir_type_t arg_ty = raw_arg_ty;
      int from_funcptr_sig = i->has_funcptr_sig ? 1 : 0;
      if (from_funcptr_sig) arg_ty = funcptr_param_type_from_inst(i, a, arg_ty);
      int null_ptr_pair_arg =
          a == 0 && call_nargs >= 2 && i->args[1].type == IR_TY_PTR;
      if (null_ptr_pair_arg) arg_ty = IR_TY_I32;
      if (from_funcptr_sig && !i->is_variadic_funcptr && !i->is_variadic_call &&
          arg_ty == IR_TY_I32 &&
          raw_arg_ty != IR_TY_PTR && !is_fp_type(raw_arg_ty) && !null_ptr_pair_arg) {
        arg_ty = IR_TY_I64;
      }
      if (arg_ty == IR_TY_PTR) arg_ty = IR_TY_I32;
      else if (!from_funcptr_sig && !is_fp_type(arg_ty) && !null_ptr_pair_arg) arg_ty = IR_TY_I64;
      cg_emitf(" ");
      emit_val_expr_as(ctx, i->args[a], arg_ty);
    }
    cg_emitf(" ");
    emit_val_expr_as(ctx, i->callee, IR_TY_I32);
    cg_emitf(")");
    if (result_unused || !returns_void) cg_emitf(")");
    cg_emitf("\n");
    emit_variadic_arg_area_restore(vararg_area_bytes, indent);
    return;
  }
  if (!i->sym) wasm_unsupported_op(i->op);
  if (!psx_ctx_has_function_name(i->sym, i->sym_len)) {
    wasm_unsupported_msg("external or implicitly declared function call in Wasm backend");
  }
  int returns_void = psx_ctx_is_function_ret_void(i->sym, i->sym_len);
  if (i->ret_complex_half > 0) {
    wasm_emitf(indent, "(call $%.*s ", i->sym_len, i->sym);
    emit_val_expr_as(ctx, i->dst, IR_TY_PTR);
  } else if (i->ret_struct_size > 0) {
    wasm_emitf(indent, "(call $%.*s ", i->sym_len, i->sym);
    emit_val_expr_as(ctx, i->ret_struct_area, IR_TY_PTR);
  } else if (!returns_void && i->dst.id >= 0 && i->dst.type != IR_TY_VOID) {
    wasm_emitf(indent, "(local.set $v%d (call $%.*s", i->dst.id, i->sym_len, i->sym);
  } else {
    wasm_emitf(indent, "(call $%.*s", i->sym_len, i->sym);
  }
  int is_minimal_snprintf =
      i->sym_len == 8 && memcmp(i->sym, "snprintf", 8) == 0 &&
      !psx_ctx_is_function_defined(i->sym, i->sym_len);
  int call_nargs = is_minimal_snprintf ? 5 : (i->is_variadic_call ? i->nargs_fixed : i->nargs);
  for (int a = 0; a < call_nargs; a++) {
    if (is_minimal_snprintf && a >= i->nargs) {
      cg_emitf(" (i64.const 0)");
      continue;
    }
    ir_type_t arg_ty = effective_val_type(ctx, i->args[a]);
    int minimal_stub_ptr_arg =
        (i->sym_len == 6 && memcmp(i->sym, "printf", 6) == 0 && a == 0) ||
        (i->sym_len == 4 && memcmp(i->sym, "puts", 4) == 0 && a == 0) ||
        (i->sym_len == 6 && memcmp(i->sym, "strlen", 6) == 0 && a == 0) ||
        (is_minimal_snprintf && (a == 0 || a == 2));
    if (minimal_stub_ptr_arg ||
        psx_ctx_get_function_param_category(i->sym, i->sym_len, a) == PSX_PCAT_PTR) {
      arg_ty = IR_TY_PTR;
    } else if (is_minimal_snprintf) {
      arg_ty = IR_TY_I64;
    } else if (psx_ctx_get_function_param_int_size(i->sym, i->sym_len, a) == 8) {
      arg_ty = IR_TY_I64;
    }
    cg_emitf(" ");
    emit_val_expr_as(ctx, i->args[a], arg_ty);
  }
  cg_emitf(")");
  if (i->ret_struct_size == 0 && i->ret_complex_half == 0 &&
      !returns_void && i->dst.id >= 0 && i->dst.type != IR_TY_VOID) {
    cg_emitf(")");
  }
  cg_emitf("\n");
  emit_variadic_arg_area_restore(vararg_area_bytes, indent);
}

static void emit_complex_ret_copy(wasm_func_ctx_t *ctx, ir_inst_t *i, int indent) {
  int half = i->ret_complex_half;
  const char *ty = half == 4 ? "f32" : "f64";
  wasm_emitf(indent, "(%s.store (local.get $p0) (%s.load ", ty, ty);
  emit_addr_expr(ctx, i->src1);
  cg_emitf("))\n");
  wasm_emitf(indent, "(%s.store (i32.add (local.get $p0) (i32.const %d)) (%s.load ", ty, half, ty);
  emit_addr_plus_const(ctx, i->src1, half);
  cg_emitf("))\n");
}

static void emit_inst(wasm_func_ctx_t *ctx, ir_inst_t *i, int dispatch_mode, int indent) {
  switch (i->op) {
    case IR_NOP:
      return;
    case IR_LABEL:
      if (!dispatch_mode) wasm_unsupported_op(i->op);
      return;
    case IR_PARAM:
      if (i->src1.id != IR_VAL_IMM || i->src1.imm < -1) wasm_unsupported_op(i->op);
      int param_ordinal = func_param_ordinal_for_inst(ctx->f, i);
      if (i->src1.imm >= 0 && param_ordinal < 0) wasm_unsupported_op(i->op);
      wasm_emitf(indent, "(local.set $v%d (local.get $p%d))\n", i->dst.id,
                 i->src1.imm < 0 ? 0 : param_ordinal + func_has_hidden_ret_area(ctx->f));
      return;
    case IR_ALLOCA: {
      int off = find_alloca_offset(ctx, i->dst.id);
      if (off < 0) wasm_unsupported_op(i->op);
      wasm_emitf(indent, "(local.set $v%d (i32.add (local.get $fp) (i32.const %d)))\n", i->dst.id, off);
      return;
    }
    case IR_LOAD_IMM:
      wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
      emit_val_expr(ctx, i->src1);
      cg_emitf(")\n");
      if (!is_fp_type(i->dst.type)) set_vreg_const(ctx, i->dst.id, i->src1.imm);
      return;
    case IR_LOAD_FP_IMM:
      wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
      emit_val_expr(ctx, i->src1);
      cg_emitf(")\n");
      return;
    case IR_LOAD_STR: {
      int addr = data_addr_for_string_label(i->sym);
      if (addr < 0) wasm_unsupported_op(i->op);
      wasm_emitf(indent, "(local.set $v%d (i32.const %d))\n", i->dst.id, addr);
      return;
    }
    case IR_LOAD_SYM: {
      if (psx_ctx_has_function_name(i->sym, i->sym_len)) {
        int func_idx = function_table_index_or_unsupported(i->sym, i->sym_len);
        wasm_emitf(indent, "(local.set $v%d (i32.const %d))\n", i->dst.id, func_idx);
        set_vreg_func_ref(ctx, i->dst.id, i->sym, i->sym_len);
        return;
      }
      int addr = data_addr_for_global(i->sym, i->sym_len);
      wasm_emitf(indent, "(local.set $v%d (i32.const %d))\n", i->dst.id, addr);
      set_vreg_global_ref(ctx, i->dst.id, psx_find_global_var(i->sym, i->sym_len), 0);
      return;
    }
    case IR_LOAD_TLV_ADDR: {
      int addr = data_addr_for_global(i->sym, i->sym_len);
      wasm_emitf(indent, "(local.set $v%d (i32.const %d))\n", i->dst.id, addr);
      set_vreg_global_ref(ctx, i->dst.id, psx_find_global_var(i->sym, i->sym_len), 0);
      return;
    }
    case IR_ZEXT:
    case IR_SEXT:
    case IR_TRUNC:
      wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
      if (i->dst.type == IR_TY_I64 && i->src1.type != IR_TY_I64) {
        cg_emitf("(%s ", i->op == IR_ZEXT ? "i64.extend_i32_u" : "i64.extend_i32_s");
        emit_val_expr(ctx, i->src1);
        cg_emitf(")");
      } else if (i->dst.type != IR_TY_I64 && i->src1.type == IR_TY_I64) {
        cg_emitf("(i32.wrap_i64 ");
        emit_val_expr(ctx, i->src1);
        cg_emitf(")");
      } else {
        emit_val_expr(ctx, i->src1);
      }
      cg_emitf(")\n");
      return;
    case IR_I2F:
      wasm_emitf(indent, "(local.set $v%d (%s.convert_%s_%c ", i->dst.id,
                 wasm_fp_type_or_unsupported(i->dst.type),
                 wasm_int_type_or_unsupported(effective_val_type(ctx, i->src1)),
                 (i->is_unsigned || val_is_unsigned(ctx, i->src1)) ? 'u' : 's');
      emit_val_expr(ctx, i->src1);
      cg_emitf("))\n");
      return;
    case IR_F2I:
      wasm_emitf(indent, "(local.set $v%d (%s.trunc_%s_%c ", i->dst.id,
                 wasm_int_type_or_unsupported(i->dst.type),
                 wasm_fp_type_or_unsupported(effective_val_type(ctx, i->src1)),
                 i->is_unsigned ? 'u' : 's');
      emit_val_expr(ctx, i->src1);
      cg_emitf("))\n");
      return;
    case IR_F2F:
      wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
      if (i->dst.type == IR_TY_F64 && effective_val_type(ctx, i->src1) == IR_TY_F32) {
        cg_emitf("(f64.promote_f32 ");
        emit_val_expr(ctx, i->src1);
        cg_emitf(")");
      } else if (i->dst.type == IR_TY_F32 && effective_val_type(ctx, i->src1) == IR_TY_F64) {
        cg_emitf("(f32.demote_f64 ");
        emit_val_expr(ctx, i->src1);
        cg_emitf(")");
      } else {
        emit_val_expr(ctx, i->src1);
      }
      cg_emitf(")\n");
      return;
    case IR_LOAD:
      emit_load(ctx, i, indent);
      return;
    case IR_STORE:
      emit_store(ctx, i, indent);
      return;
    case IR_ATOMIC:
      emit_atomic_inst(ctx, i, indent);
      return;
    case IR_MEMCPY:
      emit_memcpy(ctx, i, indent);
      return;
    case IR_ALIGN_PTR: {
      int align = i->alloca_align > 0 ? i->alloca_align : 16;
      wasm_emitf(indent, "(local.set $v%d (i32.and (i32.add ", i->dst.id);
      emit_addr_expr(ctx, i->src1);
      cg_emitf(" (i32.const %d)) (i32.const %d)))\n", align - 1, -align);
      return;
    }
    case IR_VLA_ALLOC:
      wasm_emitf(indent, "(local.set $v%d (i32.sub (global.get $__stack_pointer) ", i->dst.id);
      cg_emitf("(i32.and (i32.add ");
      emit_val_expr_as(ctx, i->src1, IR_TY_I32);
      cg_emitf(" (i32.const 15)) (i32.const -16))))\n");
      wasm_emitf(indent, "(global.set $__stack_pointer (local.get $v%d))\n", i->dst.id);
      return;
    case IR_VA_ARG_AREA:
      wasm_emitf(indent, "(local.set $v%d (global.get $__ag_va_arg_area))\n", i->dst.id);
      return;
    case IR_LEA:
      wasm_emitf(indent, "(local.set $v%d (i32.add ", i->dst.id);
      emit_addr_expr(ctx, i->src1);
      cg_emitf(" ");
      emit_addr_expr(ctx, i->src2);
      cg_emitf("))\n");
      return;
    case IR_NEG:
      wasm_emitf(indent, "(local.set $v%d (%s.sub (%s.const 0) ", i->dst.id,
               i->dst.type == IR_TY_I64 ? "i64" : "i32",
               i->dst.type == IR_TY_I64 ? "i64" : "i32");
      emit_val_expr(ctx, i->src1);
      cg_emitf("))\n");
      return;
    case IR_NOT:
      wasm_emitf(indent, "(local.set $v%d (%s.xor ", i->dst.id, i->dst.type == IR_TY_I64 ? "i64" : "i32");
      emit_val_expr(ctx, i->src1);
      cg_emitf(" (%s.const -1)))\n", i->dst.type == IR_TY_I64 ? "i64" : "i32");
      return;
    case IR_FNEG:
      wasm_emitf(indent, "(local.set $v%d (%s.neg ", i->dst.id,
                 wasm_fp_type_or_unsupported(effective_val_type(ctx, i->src1)));
      emit_val_expr(ctx, i->src1);
      cg_emitf("))\n");
      return;
    case IR_FADD: case IR_FSUB: case IR_FMUL: case IR_FDIV:
    case IR_FEQ: case IR_FNE: case IR_FLT: case IR_FLE: {
      int is_cmp = (i->op == IR_FEQ || i->op == IR_FNE || i->op == IR_FLT || i->op == IR_FLE);
      ir_type_t op_ty = is_cmp ? effective_val_type(ctx, i->src1) : effective_val_type(ctx, i->dst);
      const char *op = wasm_fp_binop(i->op, op_ty);
      if (!op) wasm_unsupported_op(i->op);
      wasm_emitf(indent, "(local.set $v%d (%s ", i->dst.id, op);
      emit_val_expr_as(ctx, i->src1, op_ty);
      cg_emitf(" ");
      emit_val_expr_as(ctx, i->src2, op_ty);
      cg_emitf("))\n");
      return;
    }
    case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: case IR_UDIV:
    case IR_MOD: case IR_UMOD: case IR_AND: case IR_OR: case IR_XOR:
    case IR_SHL: case IR_SHR: case IR_LSR:
    case IR_EQ: case IR_NE: case IR_LT: case IR_LE: case IR_ULT: case IR_ULE: {
      int is_cmp = (i->op == IR_EQ || i->op == IR_NE || i->op == IR_LT || i->op == IR_LE ||
                    i->op == IR_ULT || i->op == IR_ULE);
      ir_type_t src1_ty = effective_val_type(ctx, i->src1);
      ir_type_t src2_ty = effective_val_type(ctx, i->src2);
      ir_type_t op_ty = is_cmp ? ((src1_ty == IR_TY_I64 || src2_ty == IR_TY_I64) ? IR_TY_I64 : src1_ty)
                               : effective_val_type(ctx, i->dst);
      if (uses_ptr_value(ctx, i)) op_ty = IR_TY_I32;
      const char *op = wasm_binop(i->op, op_ty);
      if (!op) wasm_unsupported_op(i->op);
      if (op_ty == IR_TY_I64 &&
          src1_ty != IR_TY_PTR && src2_ty != IR_TY_PTR &&
          (i64_runtime_extension_unsupported(ctx, i->src1) ||
           ((i->op != IR_SHL && i->op != IR_SHR && i->op != IR_LSR) &&
            i64_runtime_extension_unsupported(ctx, i->src2)))) {
        wasm_unsupported_msg("runtime i32 to i64 extension in Wasm backend");
      }
      ir_type_t dst_ty = effective_val_type(ctx, i->dst);
      ir_type_t result_ty = is_cmp ? IR_TY_I32 : op_ty;
      int result_unsigned = uses_ptr_value(ctx, i) ||
                            i->op == IR_UDIV || i->op == IR_UMOD || i->op == IR_LSR ||
                            i->op == IR_ULT || i->op == IR_ULE;
      if ((i->op == IR_MOD || i->op == IR_UMOD) &&
          i->src2.id == IR_VAL_IMM && i->src2.imm == 0) {
        wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
        emit_wasm_type_cast_prefix(op_ty, dst_ty, result_unsigned);
        emit_val_expr_as(ctx, i->src1, op_ty);
        emit_wasm_type_cast_suffix(op_ty, dst_ty);
        cg_emitf(")\n");
        return;
      }
      wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
      emit_wasm_type_cast_prefix(result_ty, dst_ty, result_unsigned);
      cg_emitf("(%s ", op);
      emit_val_expr_as(ctx, i->src1, op_ty);
      cg_emitf(" ");
      emit_val_expr_as(ctx, i->src2, op_ty);
      cg_emitf(")");
      emit_wasm_type_cast_suffix(result_ty, dst_ty);
      cg_emitf(")\n");
      if ((i->op == IR_ADD || i->op == IR_SUB) && !is_cmp) {
        int base_off = 0;
        global_var_t *gv = get_vreg_global_ref(ctx, i->src1.id, &base_off);
        long long delta = 0;
        if (gv && get_vreg_const(ctx, i->src2, &delta)) {
          if (i->op == IR_SUB) delta = -delta;
          set_vreg_global_ref(ctx, i->dst.id, gv, base_off + (int)delta);
        } else if (i->op == IR_ADD && get_vreg_const(ctx, i->src1, &delta)) {
          gv = get_vreg_global_ref(ctx, i->src2.id, &base_off);
          if (gv) set_vreg_global_ref(ctx, i->dst.id, gv, base_off + (int)delta);
        }
      }
      return;
    }
    case IR_CALL:
      emit_call(ctx, i, indent);
      return;
    case IR_BR:
      if (!dispatch_mode) wasm_unsupported_op(i->op);
      wasm_emitf(indent, "(local.set $pc (i32.const %d))\n", i->label_id);
      wasm_emitf(indent, "(br $dispatch)\n");
      return;
    case IR_BR_COND:
      if (!dispatch_mode) wasm_unsupported_op(i->op);
      wasm_emitf(indent, "(if ");
      emit_val_expr_as(ctx, i->src1, IR_TY_I32);
      cg_emitf("\n");
      wasm_emitf(indent + 2, "(then (local.set $pc (i32.const %d)))\n", i->label_id);
      wasm_emitf(indent + 2, "(else (local.set $pc (i32.const %d)))\n", i->else_label_id);
      wasm_emitf(indent, ")\n");
      wasm_emitf(indent, "(br $dispatch)\n");
      return;
    case IR_RET:
      if (i->ret_complex_half > 0) {
        emit_complex_ret_copy(ctx, i, indent);
        if (ctx->frame_size > 0) wasm_emitf(indent, "(global.set $__stack_pointer (local.get $old_sp))\n");
        wasm_emitf(indent, "return\n");
        return;
      }
      if (ctx->frame_size > 0) wasm_emitf(indent, "(global.set $__stack_pointer (local.get $old_sp))\n");
      if (i->src1.id != IR_VAL_NONE) {
        wasm_emitf(indent, "(return ");
        emit_val_expr_as(ctx, i->src1, ctx->f->ret_type);
        cg_emitf(")\n");
      } else {
        wasm_emitf(indent, "return\n");
      }
      return;
    default:
      wasm_unsupported_op(i->op);
  }
}

static int func_param_count(ir_func_t *f) {
  int count = 0;
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      if (i->op == IR_PARAM && i->src1.id == IR_VAL_IMM && i->src1.imm >= 0) {
        count++;
      }
    }
  }
  return count + func_has_hidden_ret_area(f);
}

static ir_type_t func_param_type(ir_func_t *f, int idx) {
  if (func_has_hidden_ret_area(f)) {
    if (idx == 0) return IR_TY_PTR;
    idx--;
  }
  ir_inst_t *param = func_param_inst_at_ordinal(f, idx);
  if (param) {
    return func_param_type_from_decl(f, idx, param->dst.type);
  }
  return IR_TY_I32;
}

static int block_has_terminator(ir_block_t *b) {
  ir_inst_t *tail = b ? b->tail : NULL;
  return tail && (tail->op == IR_BR || tail->op == IR_BR_COND || tail->op == IR_RET);
}

static void emit_func(ir_func_t *f) {
  wasm_func_ctx_t ctx = {0};
  ctx.f = f;
  analyze_func(&ctx);

  int nparams = func_param_count(f);
  wasm_emitf(2, "(func $%.*s", f->name_len, f->name);
  for (int p = 0; p < nparams; p++) {
    const char *pt = wasm_type(func_param_type(f, p));
    if (!pt) wasm_unsupported_msg("non-integer Wasm function parameter");
    cg_emitf(" (param $p%d %s)", p, pt);
  }
  if (f->ret_type != IR_TY_VOID && !func_has_hidden_ret_area(f)) {
    const char *rt = wasm_type(f->ret_type);
    if (!rt) wasm_unsupported_msg("non-integer Wasm function return");
    cg_emitf(" (result %s)", rt);
  }
  cg_emitf("\n");
  for (int v = 0; v < f->next_vreg_id; v++) {
    if (!ctx.vreg_type_seen[v]) continue;
    const char *vt = wasm_type(ctx.vreg_types[v]);
    if (vt) wasm_emitf(4, "(local $v%d %s)\n", v, vt);
  }
  wasm_emitf(4, "(local $fp i32)\n");
  wasm_emitf(4, "(local $old_sp i32)\n");
  wasm_emitf(4, "(local $old_va_arg_area i32)\n");
  wasm_emitf(4, "(local $atomic_tmp_i32 i32)\n");
  wasm_emitf(4, "(local $atomic_exp_i32 i32)\n");
  wasm_emitf(4, "(local $atomic_tmp_i64 i64)\n");
  wasm_emitf(4, "(local $atomic_exp_i64 i64)\n");
  if (ctx.has_control_flow) wasm_emitf(4, "(local $pc i32)\n");
  if (ctx.frame_size > 0) {
    wasm_emitf(4, "(local.set $old_sp (global.get $__stack_pointer))\n");
    wasm_emitf(4, "(local.set $fp (i32.sub (global.get $__stack_pointer) (i32.const %d)))\n",
               ctx.frame_size);
    wasm_emitf(4, "(global.set $__stack_pointer (local.get $fp))\n");
  }
  if (ctx.has_control_flow) {
    wasm_emitf(4, "(local.set $pc (i32.const %d))\n", f->entry ? f->entry->id : 0);
    wasm_emitf(4, "(block $exit\n");
    wasm_emitf(6, "(loop $dispatch\n");
    for (ir_block_t *b = f->entry; b; b = b->next) {
      wasm_emitf(8, "(if (i32.eq (local.get $pc) (i32.const %d))\n", b->id);
      wasm_emitf(10, "(then\n");
      for (ir_inst_t *i = b->head; i; i = i->next) emit_inst(&ctx, i, 1, 12);
      if (!block_has_terminator(b)) {
        if (b->next) {
          wasm_emitf(12, "(local.set $pc (i32.const %d))\n", b->next->id);
          wasm_emitf(12, "(br $dispatch)\n");
        } else {
          wasm_emitf(12, "(br $exit)\n");
        }
      }
      wasm_emitf(10, ")\n");
      wasm_emitf(8, ")\n");
    }
    wasm_emitf(8, "(br $exit)\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(4, "unreachable\n");
  } else {
    for (ir_block_t *b = f->entry; b; b = b->next) {
      for (ir_inst_t *i = b->head; i; i = i->next) emit_inst(&ctx, i, 0, 4);
    }
  }
  wasm_emitf(2, ")\n");
  if (f->name_len == 4 && memcmp(f->name, "main", 4) == 0) {
    wasm_emitf(2, "(export \"main\" (func $main))\n");
  }

  free(ctx.allocas);
  free(ctx.global_func_states);
  free(ctx.vreg_type_seen);
  free(ctx.vreg_types);
  free(ctx.vreg_unsigned);
  free(ctx.vreg_func_ref_names);
  free(ctx.vreg_func_ref_name_lens);
  free(ctx.vreg_global_refs);
  free(ctx.vreg_global_ref_offsets);
  free(ctx.vreg_const_known);
  free(ctx.vreg_const_values);
}

void wasm32_module_begin(void) {
  g_data.next_data_off = WASM_STATIC_BASE;
  g_data.symbol_count = 0;
  g_func_table.ref_count = 0;
  g_func_table.needs_table = 0;
  cg_emitf("(module\n");
  wasm_emitf(2, "(memory (export \"memory\") 1)\n");
  wasm_emitf(2, "(global $__stack_pointer (mut i32) (i32.const %d))\n", WASM_STACK_BASE);
  wasm_emitf(2, "(global $__ag_va_arg_area (mut i32) (i32.const 0))\n");
  wasm_emitf(2, "(global $__ag_heap_pointer (mut i32) (i32.const 32768))\n");
}

void wasm32_gen_ir_module(ir_module_t *m) {
  if (!m) return;
  ir_opt_const_fold(m);
  ir_opt_dce(m);
  for (ir_func_t *f = m->funcs; f; f = f->next) emit_func(f);
}

static void emit_wat_escaped_byte(unsigned char c) {
  if (c == '"' || c == '\\') {
    cg_emitf("\\%02x", (unsigned)c);
  } else if (c >= 0x20 && c <= 0x7e) {
    cg_emitf("%c", c);
  } else {
    cg_emitf("\\%02x", (unsigned)c);
  }
}

static void emit_string_literal_data(string_lit_t *lit, void *user) {
  (void)user;
  int addr = data_addr_for_string_label(lit->label);
  if (addr < 0) wasm_unsupported_msg("string literal label in Wasm backend");
  wasm_emitf(2, "(data (i32.const %d) \"", addr);
  int cw = lit->char_width > 0 ? (int)lit->char_width : TK_CHAR_WIDTH_CHAR;
  if (cw != TK_CHAR_WIDTH_CHAR) {
    int sp = 0;
    while (sp < lit->len) {
      uint32_t units[2];
      int nu = tk_next_string_code_units(lit->str, lit->len, &sp, cw, units);
      for (int u = 0; u < nu; u++) {
        for (int b = 0; b < cw; b++) {
          emit_wat_escaped_byte((unsigned char)(units[u] >> (8 * b)));
        }
      }
    }
    for (int b = 0; b < cw; b++) emit_wat_escaped_byte(0);
    cg_emitf("\")\n");
    return;
  }
  int i = 0;
  while (i < lit->len) {
    uint32_t v = 0;
    if (lit->str[i] == '\\') {
      tk_parse_escape_value(lit->str, lit->len, &i, &v);
    } else {
      v = (unsigned char)lit->str[i++];
    }
    if (v < 0x80) {
      emit_wat_escaped_byte((unsigned char)v);
    } else if (v < 0x800) {
      emit_wat_escaped_byte((unsigned char)(0xC0 | (v >> 6)));
      emit_wat_escaped_byte((unsigned char)(0x80 | (v & 0x3F)));
    } else if (v < 0x10000) {
      emit_wat_escaped_byte((unsigned char)(0xE0 | (v >> 12)));
      emit_wat_escaped_byte((unsigned char)(0x80 | ((v >> 6) & 0x3F)));
      emit_wat_escaped_byte((unsigned char)(0x80 | (v & 0x3F)));
    } else {
      emit_wat_escaped_byte((unsigned char)(0xF0 | (v >> 18)));
      emit_wat_escaped_byte((unsigned char)(0x80 | ((v >> 12) & 0x3F)));
      emit_wat_escaped_byte((unsigned char)(0x80 | ((v >> 6) & 0x3F)));
      emit_wat_escaped_byte((unsigned char)(0x80 | (v & 0x3F)));
    }
  }
  cg_emitf("\\00\")\n");
}

static void emit_i32_data_bytes(int addr, long long value, int size) {
  wasm_emitf(2, "(data (i32.const %d) \"", addr);
  for (int i = 0; i < size; i++) emit_wat_escaped_byte((unsigned char)((uint64_t)value >> (8 * i)));
  cg_emitf("\")\n");
}

static void emit_fp_data_bytes(int addr, tk_float_kind_t fp_kind, double value) {
  if (fp_kind == TK_FLOAT_KIND_FLOAT) {
    float f = (float)value;
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    emit_i32_data_bytes(addr, (long long)bits, 4);
    return;
  }
  if (fp_kind >= TK_FLOAT_KIND_DOUBLE) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    emit_i32_data_bytes(addr, (long long)bits, 8);
    return;
  }
  wasm_unsupported_msg("floating global initializer in Wasm backend");
}

static int data_addr_for_init_symbol(char *sym, int sym_len) {
  if (!sym) return -1;
  if (sym_len < 0) return data_addr_for_string_label(sym);
  if (psx_ctx_has_function_name(sym, sym_len)) {
    return function_table_index_or_unsupported(sym, sym_len);
  }
  return data_addr_for_global(sym, sym_len);
}

static void emit_global_init_values_data(global_var_t *gv, int addr, int size) {
  int elem = gv->is_array && gv->deref_size > 0 ? gv->deref_size : size;
  if (elem != 1 && elem != 2 && elem != 4 && elem != 8) wasm_unsupported_msg("global element size in Wasm backend");
  int total_elems = elem > 0 ? (size + elem - 1) / elem : 0;
  int is_fp_array = gv->init_fvalues &&
                    (gv->fp_kind == TK_FLOAT_KIND_FLOAT || gv->fp_kind >= TK_FLOAT_KIND_DOUBLE);
  wasm_emitf(2, "(data (i32.const %d) \"", addr);
  for (int i = 0; i < total_elems; i++) {
    char *sym = (i < gv->init_count && gv->init_value_symbols) ? gv->init_value_symbols[i] : NULL;
    int sym_len = (i < gv->init_count && gv->init_value_symbol_lens)
                      ? gv->init_value_symbol_lens[i] : 0;
    uint64_t value = (uint64_t)((i < gv->init_count && gv->init_values) ? gv->init_values[i] : 0);
    if (is_fp_array && !sym) {
      double fv = (i < gv->init_count) ? gv->init_fvalues[i] : 0.0;
      if (gv->fp_kind == TK_FLOAT_KIND_FLOAT) {
        float f = (float)fv;
        uint32_t bits;
        memcpy(&bits, &f, sizeof(bits));
        value = bits;
      } else {
        uint64_t bits;
        memcpy(&bits, &fv, sizeof(bits));
        value = bits;
      }
    }
    if (sym) {
      int sym_addr = data_addr_for_init_symbol(sym, sym_len);
      if (sym_addr < 0) wasm_unsupported_msg("symbol array initializer in Wasm backend");
      value += (uint64_t)sym_addr;
    }
    int bytes = elem;
    if ((i + 1) * elem > size) bytes = size - i * elem;
    for (int b = 0; b < bytes; b++) emit_wat_escaped_byte((unsigned char)(value >> (8 * b)));
  }
  cg_emitf("\")\n");
}

static void emit_global_symbol_addr_data(global_var_t *gv, int addr, int size) {
  int sym_addr = data_addr_for_init_symbol(gv->init_symbol, gv->init_symbol_len);
  if (sym_addr < 0) wasm_unsupported_msg("global symbol initializer in Wasm backend");
  emit_i32_data_bytes(addr, (long long)sym_addr + gv->init_symbol_offset, size);
}

static void emit_global_init_slot_data(global_var_t *gv, int idx, int addr, int size, int normalize_bool) {
  if (size != 1 && size != 2 && size != 4 && size != 8) wasm_unsupported_msg("global member size in Wasm backend");
  char *sym = (idx < gv->init_count && gv->init_value_symbols) ? gv->init_value_symbols[idx] : NULL;
  int sym_len = (idx < gv->init_count && gv->init_value_symbol_lens)
                    ? gv->init_value_symbol_lens[idx] : 0;
  if (!sym && (sym_len == -2 || sym_len == -3)) {
    wasm_unsupported_msg("floating global struct initializer in Wasm backend");
  }
  long long value = (idx < gv->init_count && gv->init_values) ? gv->init_values[idx] : 0;
  if (normalize_bool) value = value != 0;
  if (sym) {
    int sym_addr = data_addr_for_init_symbol(sym, sym_len);
    if (sym_addr < 0) wasm_unsupported_msg("symbol global struct initializer in Wasm backend");
    value += sym_addr;
  }
  emit_i32_data_bytes(addr, value, size);
}

static void emit_global_init_member_data(global_var_t *gv, int idx, int addr,
                                         const tag_member_info_t *mi) {
  if (!mi) wasm_unsupported_msg("global struct member initializer in Wasm backend");
  char *sym = (idx < gv->init_count && gv->init_value_symbols) ? gv->init_value_symbols[idx] : NULL;
  if (mi->fp_kind != TK_FLOAT_KIND_NONE && !sym) {
    double fv = (idx < gv->init_count && gv->init_fvalues) ? gv->init_fvalues[idx] : 0.0;
    emit_fp_data_bytes(addr, mi->fp_kind, fv);
    return;
  }
  emit_global_init_slot_data(gv, idx, addr, mi->type_size, mi->is_bool);
}

static void emit_global_bitfield_unit_data(token_kind_t tk, char *tn, int tl,
                                           int *member_idx, global_var_t *gv,
                                           int *val_idx, int base_addr) {
  tag_member_info_t first = {0};
  if (!psx_ctx_get_tag_member_info(tk, tn, tl, *member_idx, &first) ||
      first.bit_width <= 0) {
    wasm_unsupported_msg("global bitfield initializer in Wasm backend");
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
    uint64_t value = (uint64_t)gv->init_values[*val_idx];
    packed |= (value & mask) << mi.bit_offset;
    (*val_idx)++;
    m++;
  }
  emit_i32_data_bytes(base_addr + unit_off, (long long)packed, unit_size);
  *member_idx = m - 1;
}

static void emit_global_bitfield_member_data(global_var_t *gv, int idx, int addr,
                                             const tag_member_info_t *mi) {
  if (!mi || mi->bit_width <= 0) {
    wasm_unsupported_msg("global bitfield initializer in Wasm backend");
  }
  uint64_t mask = mi->bit_width >= 64 ? UINT64_MAX : ((UINT64_C(1) << mi->bit_width) - 1);
  uint64_t value = (uint64_t)((idx < gv->init_count && gv->init_values) ? gv->init_values[idx] : 0);
  uint64_t packed = (value & mask) << mi->bit_offset;
  emit_i32_data_bytes(addr + mi->offset, (long long)packed, mi->type_size);
}

static void emit_global_union_member_data(token_kind_t tk, char *tn, int tl,
                                          global_var_t *gv, int *val_idx, int addr);

static int wasm_member_is_unnamed_struct(const tag_member_info_t *mi) {
  return mi->len == 0 && !mi->is_tag_pointer && mi->tag_kind == TK_STRUCT;
}

static int wasm_member_is_unnamed_union(const tag_member_info_t *mi) {
  return mi->len == 0 && !mi->is_tag_pointer && mi->tag_kind == TK_UNION;
}

static int wasm_find_unnamed_union_covering_offset_rec(token_kind_t tk, char *tn, int tl,
                                                       int base_off, int target_off,
                                                       int *out_off, int *out_size) {
  int n = psx_ctx_get_tag_member_count(tk, tn, tl);
  for (int i = 0; i < n; i++) {
    tag_member_info_t mi = {0};
    if (!psx_ctx_get_tag_member_info(tk, tn, tl, i, &mi)) break;
    if (mi.len != 0 || mi.is_tag_pointer) continue;
    int start = base_off + mi.offset;
    int end = start + mi.type_size;
    if (target_off < start || target_off >= end) continue;
    if (mi.tag_kind == TK_UNION) {
      if (out_off) *out_off = start;
      if (out_size) *out_size = mi.type_size;
      return 1;
    }
    if (mi.tag_kind == TK_STRUCT &&
        wasm_find_unnamed_union_covering_offset_rec(mi.tag_kind, mi.tag_name, mi.tag_len,
                                                    start, target_off, out_off, out_size)) {
      return 1;
    }
  }
  return 0;
}

static void emit_global_nested_union_data(token_kind_t tk, char *tn, int tl,
                                          global_var_t *gv, int *val_idx, int addr) {
  if (*val_idx >= gv->init_count) return;
  emit_global_union_member_data(tk, tn, tl, gv, val_idx, addr);
}

static void emit_global_union_scalar_data(global_var_t *gv, int *val_idx, int addr,
                                          const tag_member_info_t *mi) {
  int idx = (*val_idx)++;
  char *sym = (idx < gv->init_count && gv->init_value_symbols) ? gv->init_value_symbols[idx] : NULL;
  int sym_len = (idx < gv->init_count && gv->init_value_symbol_lens)
                    ? gv->init_value_symbol_lens[idx] : 0;
  if (!sym && (sym_len == -2 || sym_len == -3)) {
    double fv = (idx < gv->init_count && gv->init_fvalues) ? gv->init_fvalues[idx] : 0.0;
    emit_fp_data_bytes(addr, sym_len == -2 ? TK_FLOAT_KIND_FLOAT : TK_FLOAT_KIND_DOUBLE, fv);
    return;
  }
  emit_global_init_member_data(gv, idx, addr, mi);
}

static int wasm_flat_slot_count(token_kind_t tk, char *tn, int tl);
static void consume_trailing_zero_union_padding(global_var_t *gv, int start_idx, int *val_idx,
                                                int target_slots);

static void emit_global_struct_members_data_rec(token_kind_t tk, char *tn, int tl,
                                                global_var_t *gv, int *val_idx, int base_addr) {
  int n_members = psx_ctx_get_tag_member_count(tk, tn, tl);
  int covered_union_off = 0;
  int covered_union_size = 0;
  for (int m = 0; m < n_members && *val_idx < gv->init_count; m++) {
    tag_member_info_t mi = {0};
    if (!psx_ctx_get_tag_member_info(tk, tn, tl, m, &mi)) break;
    if (wasm_member_is_unnamed_struct(&mi)) continue;
    if (covered_union_size > 0 &&
        mi.offset >= covered_union_off &&
        mi.offset < covered_union_off + covered_union_size) {
      continue;
    }
    int cover_off = 0;
    int cover_size = 0;
    int has_cover = wasm_find_unnamed_union_covering_offset_rec(tk, tn, tl, 0, mi.offset,
                                                                &cover_off, &cover_size);
    if (mi.bit_width > 0) {
      emit_global_bitfield_unit_data(tk, tn, tl, &m, gv, val_idx, base_addr);
      continue;
    }
    if (mi.array_len > 0) {
      if ((mi.tag_kind == TK_STRUCT || mi.tag_kind == TK_UNION) && !mi.is_tag_pointer) {
        for (int k = 0; k < mi.array_len && *val_idx < gv->init_count; k++) {
          int elem_start_idx = *val_idx;
          if (mi.tag_kind == TK_UNION) {
            emit_global_nested_union_data(mi.tag_kind, mi.tag_name, mi.tag_len, gv, val_idx,
                                          base_addr + mi.offset + k * mi.type_size);
          } else {
            emit_global_struct_members_data_rec(mi.tag_kind, mi.tag_name, mi.tag_len, gv, val_idx,
                                                base_addr + mi.offset + k * mi.type_size);
          }
          consume_trailing_zero_union_padding(gv, elem_start_idx, val_idx,
                                              wasm_flat_slot_count(mi.tag_kind, mi.tag_name,
                                                                   mi.tag_len));
        }
      } else {
        for (int k = 0; k < mi.array_len && *val_idx < gv->init_count; k++) {
          int slot = (*val_idx)++;
          emit_global_init_member_data(gv, slot, base_addr + mi.offset + k * mi.type_size, &mi);
        }
      }
      if (has_cover) {
        covered_union_off = cover_off;
        covered_union_size = cover_size;
      }
      continue;
    }
    if (mi.tag_kind == TK_STRUCT && !mi.is_tag_pointer) {
      int member_start_idx = *val_idx;
      emit_global_struct_members_data_rec(mi.tag_kind, mi.tag_name, mi.tag_len, gv, val_idx,
                                          base_addr + mi.offset);
      consume_trailing_zero_union_padding(gv, member_start_idx, val_idx,
                                          wasm_flat_slot_count(mi.tag_kind, mi.tag_name,
                                                               mi.tag_len));
      if (has_cover) {
        covered_union_off = cover_off;
        covered_union_size = cover_size;
      }
      continue;
    }
    if (mi.tag_kind == TK_UNION && !mi.is_tag_pointer) {
      emit_global_nested_union_data(mi.tag_kind, mi.tag_name, mi.tag_len, gv, val_idx,
                                    base_addr + mi.offset);
      if (wasm_member_is_unnamed_union(&mi)) {
        covered_union_off = mi.offset;
        covered_union_size = mi.type_size;
      } else if (has_cover) {
        covered_union_off = cover_off;
        covered_union_size = cover_size;
      }
      continue;
    }
    int slot = (*val_idx)++;
    emit_global_init_member_data(gv, slot, base_addr + mi.offset, &mi);
    if (has_cover) {
      covered_union_off = cover_off;
      covered_union_size = cover_size;
    }
  }
}

static void emit_global_union_element_data(global_var_t *gv, int *val_idx, int addr) {
  emit_global_union_member_data(gv->tag_kind, gv->tag_name, gv->tag_len, gv, val_idx, addr);
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

static int global_init_slot_is_plain_zero(global_var_t *gv, int idx) {
  if (idx < 0 || idx >= gv->init_count) return 1;
  char *sym = gv->init_value_symbols ? gv->init_value_symbols[idx] : NULL;
  int sym_len = gv->init_value_symbol_lens ? gv->init_value_symbol_lens[idx] : 0;
  long long value = gv->init_values ? gv->init_values[idx] : 0;
  double fv = gv->init_fvalues ? gv->init_fvalues[idx] : 0.0;
  return sym == NULL && sym_len == 0 && value == 0 && fv == 0.0;
}

static int wasm_member_flat_slots(const tag_member_info_t *mi) {
  int per = 1;
  if ((mi->tag_kind == TK_STRUCT || mi->tag_kind == TK_UNION) && !mi->is_tag_pointer) {
    per = wasm_flat_slot_count(mi->tag_kind, mi->tag_name, mi->tag_len);
  }
  return (mi->array_len > 0) ? mi->array_len * per : per;
}

static int wasm_flat_slot_count(token_kind_t tk, char *tn, int tl) {
  int n = psx_ctx_get_tag_member_count(tk, tn, tl);
  int slots = 0;
  int union_max_bytes = 0;
  for (int i = 0; i < n; i++) {
    tag_member_info_t mi = {0};
    if (!psx_ctx_get_tag_member_info(tk, tn, tl, i, &mi)) break;
    if (tk == TK_UNION) {
      int ms = wasm_member_flat_slots(&mi);
      int count = mi.array_len > 0 ? mi.array_len : 1;
      int bytes = mi.type_size * count;
      if (bytes > union_max_bytes || (bytes == union_max_bytes && ms > slots)) {
        union_max_bytes = bytes;
        slots = ms;
      }
    } else {
      slots += wasm_member_flat_slots(&mi);
    }
  }
  return slots > 0 ? slots : 1;
}

static void consume_trailing_zero_union_padding(global_var_t *gv, int start_idx, int *val_idx,
                                                int target_slots) {
  if (!val_idx || target_slots <= 1) return;
  int limit = start_idx + target_slots;
  while (*val_idx < limit && *val_idx < gv->init_count &&
         global_init_slot_is_plain_zero(gv, *val_idx)) {
    (*val_idx)++;
  }
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

static void emit_global_union_member_data(token_kind_t tk, char *tn, int tl,
                                          global_var_t *gv, int *val_idx, int addr) {
  if (*val_idx >= gv->init_count) return;
  int start_idx = *val_idx;
  tag_member_info_t mi = {0};
  int ord = gv->union_init_ordinal;
  if (gv->init_union_ordinals && gv->init_union_ordinals[*val_idx] >= 0)
    ord = gv->init_union_ordinals[*val_idx];
  if (!psx_ctx_get_tag_member_info(tk, tn, tl, ord, &mi)) {
    wasm_unsupported_msg("global union initializer in Wasm backend");
  }
  select_union_member_for_init_slot(tk, tn, tl, gv, *val_idx, &mi);
  if (mi.bit_width > 0) {
    emit_global_bitfield_member_data(gv, (*val_idx)++, addr, &mi);
    consume_trailing_zero_union_padding(gv, start_idx, val_idx,
                                        wasm_flat_slot_count(tk, tn, tl));
    return;
  }
  if (mi.array_len > 0) {
    if ((mi.tag_kind == TK_STRUCT || mi.tag_kind == TK_UNION) && !mi.is_tag_pointer) {
      for (int k = 0; k < mi.array_len && *val_idx < gv->init_count; k++) {
        int elem_addr = addr + mi.offset + k * mi.type_size;
        if (mi.tag_kind == TK_STRUCT) {
          emit_global_struct_members_data_rec(mi.tag_kind, mi.tag_name, mi.tag_len, gv, val_idx,
                                              elem_addr);
        } else {
          emit_global_nested_union_data(mi.tag_kind, mi.tag_name, mi.tag_len, gv, val_idx,
                                        elem_addr);
        }
      }
    } else {
      for (int k = 0; k < mi.array_len && *val_idx < gv->init_count; k++) {
        emit_global_union_scalar_data(gv, val_idx, addr + mi.offset + k * mi.type_size, &mi);
      }
    }
    return;
  }
  if ((mi.tag_kind == TK_STRUCT || mi.tag_kind == TK_UNION) && !mi.is_tag_pointer) {
    if (mi.tag_kind == TK_STRUCT) {
      emit_global_struct_members_data_rec(mi.tag_kind, mi.tag_name, mi.tag_len, gv, val_idx, addr);
    } else {
      emit_global_nested_union_data(mi.tag_kind, mi.tag_name, mi.tag_len, gv, val_idx, addr);
    }
    consume_trailing_zero_union_padding(gv, start_idx, val_idx,
                                        wasm_flat_slot_count(tk, tn, tl));
    return;
  }
  emit_global_union_scalar_data(gv, val_idx, addr, &mi);
  consume_trailing_zero_union_padding(gv, start_idx, val_idx,
                                      wasm_flat_slot_count(tk, tn, tl));
}

static void emit_global_union_data(global_var_t *gv, int addr) {
  if (gv->init_count <= 0) return;
  int val_idx = 0;
  emit_global_union_element_data(gv, &val_idx, addr);
}

static int effective_tag_array_elem_size(token_kind_t tk, char *tn, int tl, int fallback) {
  if (fallback > 0) return fallback;
  int n = psx_ctx_get_tag_member_count(tk, tn, tl);
  int max_end = 0;
  for (int i = 0; i < n; i++) {
    tag_member_info_t mi = {0};
    if (!psx_ctx_get_tag_member_info(tk, tn, tl, i, &mi)) break;
    int count = mi.array_len > 0 ? mi.array_len : 1;
    int end = mi.offset + mi.type_size * count;
    if (end > max_end) max_end = end;
  }
  int align = psx_ctx_get_tag_align(tk, tn, tl);
  if (align > 1 && max_end > 0) max_end = (max_end + align - 1) / align * align;
  return max_end > 0 ? max_end : fallback;
}

static void emit_global_struct_data(global_var_t *gv, int addr) {
  if (gv->is_tag_pointer) {
    wasm_unsupported_msg("global aggregate initializer in Wasm backend");
  }
  if (gv->tag_kind == TK_UNION) {
    if (gv->is_array) {
      int elem_size = gv->deref_size > 0 ? gv->deref_size : 0;
      int total = elem_size > 0 ? (int)gv->type_size / elem_size : 0;
      int val_idx = 0;
      for (int e = 0; e < total && val_idx < gv->init_count; e++) {
        emit_global_union_element_data(gv, &val_idx, addr + e * elem_size);
      }
    } else {
      emit_global_union_data(gv, addr);
    }
    return;
  }
  int val_idx = 0;
  if (gv->is_array) {
    int elem_size = effective_tag_array_elem_size(gv->tag_kind, gv->tag_name, gv->tag_len,
                                                  gv->deref_size > 0 ? gv->deref_size : 0);
    int total = elem_size > 0 ? (int)gv->type_size / elem_size : 0;
    for (int e = 0; e < total && val_idx < gv->init_count; e++) {
      int elem_start_idx = val_idx;
      emit_global_struct_members_data_rec(gv->tag_kind, gv->tag_name, gv->tag_len, gv, &val_idx,
                                          addr + e * elem_size);
      consume_trailing_zero_union_padding(gv, elem_start_idx, &val_idx,
                                          wasm_flat_slot_count(gv->tag_kind, gv->tag_name,
                                                               gv->tag_len));
    }
  } else {
    emit_global_struct_members_data_rec(gv->tag_kind, gv->tag_name, gv->tag_len, gv, &val_idx, addr);
  }
}

static void emit_global_data(global_var_t *gv, void *user) {
  (void)user;
  if (gv->is_extern_decl) return;
  int addr = data_addr_for_global(gv->name, gv->name_len);
  int size = gv->type_size > 0 ? gv->type_size : 4;
  if ((gv->tag_kind == TK_STRUCT || gv->tag_kind == TK_UNION) && !gv->is_tag_pointer) {
    emit_global_struct_data(gv, addr);
  } else if (gv->init_symbol) {
    if (size != 1 && size != 2 && size != 4 && size != 8) wasm_unsupported_msg("global size in Wasm backend");
    emit_global_symbol_addr_data(gv, addr, size);
  } else if (gv->init_count > 0) {
    emit_global_init_values_data(gv, addr, size);
  } else if (gv->fp_kind != TK_FLOAT_KIND_NONE) {
    emit_fp_data_bytes(addr, (tk_float_kind_t)gv->fp_kind, gv->has_init ? gv->fval : 0.0);
  } else {
    if ((!gv->has_init || gv->init_val == 0) &&
        size != 1 && size != 2 && size != 4 && size != 8) return;
    if (size != 1 && size != 2 && size != 4 && size != 8) wasm_unsupported_msg("global size in Wasm backend");
    emit_i32_data_bytes(addr, gv->has_init ? gv->init_val : 0, size);
  }
}

void wasm32_emit_data_segments(void) {
  ps_iter_string_literals(emit_string_literal_data, NULL);
  ps_iter_globals(emit_global_data, NULL);
}

static int has_undefined_function(const char *name, int len) {
  return psx_ctx_has_function_name((char *)name, len) &&
         !psx_ctx_is_function_defined((char *)name, len);
}

static void emit_minimal_locale_data_if_needed(void) {
  if (!has_undefined_function("setlocale", 9) && !has_undefined_function("localeconv", 10)) return;
  wasm_data_symbol_t *c = intern_data_symbol("__ag_stub_locale_c", 18, 2, 1);
  wasm_data_symbol_t *dot = intern_data_symbol("__ag_stub_locale_dot", 20, 2, 1);
  wasm_data_symbol_t *lc = intern_data_symbol("__ag_stub_lconv", 15, 96, 4);
  wasm_emitf(2, "(data (i32.const %d) \"C\\00\")\n", c->addr);
  wasm_emitf(2, "(data (i32.const %d) \".\\00\")\n", dot->addr);
  emit_i32_data_bytes(lc->addr, dot->addr, 4);
}

static ir_type_t wasm_function_result_type_from_decl(char *name, int name_len) {
  if (psx_ctx_get_function_ret_is_pointer(name, name_len) ||
      psx_ctx_get_function_ret_is_funcptr(name, name_len)) {
    return IR_TY_PTR;
  }
  tk_float_kind_t fp = psx_ctx_get_function_ret_fp_kind(name, name_len);
  if (fp == TK_FLOAT_KIND_FLOAT) return IR_TY_F32;
  if (fp >= TK_FLOAT_KIND_DOUBLE) return IR_TY_F64;
  token_kind_t kind = psx_ctx_get_function_ret_token_kind(name, name_len);
  if (kind == TK_LONG) return IR_TY_I64;
  if (kind == TK_VOID) return IR_TY_VOID;
  return IR_TY_I32;
}

static void emit_minimal_libc_stubs(void) {
  emit_minimal_locale_data_if_needed();
  if (has_undefined_function("printf", 6)) {
    wasm_emitf(2, "(func $printf (param i32) (result i32) (i32.const 5))\n");
  }
  if (has_undefined_function("fprintf", 7)) {
    wasm_emitf(2, "(func $fprintf (param i32 i32) (result i32) (i32.const 1))\n");
  }
  if (has_undefined_function("puts", 4)) {
    wasm_emitf(2, "(func $puts (param i32) (result i32) (i32.const 1))\n");
  }
  if (has_undefined_function("fopen", 5)) {
    wasm_emitf(2, "(func $fopen (param i32 i32) (result i32) (i32.const 1))\n");
  }
  if (has_undefined_function("fclose", 6)) {
    wasm_emitf(2, "(func $fclose (param i32) (result i32) (i32.const 0))\n");
  }
  if (has_undefined_function("fread", 5)) {
    wasm_emitf(2, "(func $fread (param i32 i64 i64 i32) (result i64) (local.get 2))\n");
  }
  if (has_undefined_function("fwrite", 6)) {
    wasm_emitf(2, "(func $fwrite (param i32 i64 i64 i32) (result i64) (local.get 2))\n");
  }
  if (has_undefined_function("fgetc", 5)) {
    wasm_emitf(2, "(func $fgetc (param i32) (result i32) (i32.const -1))\n");
  }
  if (has_undefined_function("getc", 4)) {
    wasm_emitf(2, "(func $getc (param i32) (result i32) (i32.const -1))\n");
  }
  if (has_undefined_function("fgets", 5)) {
    wasm_emitf(2, "(func $fgets (param i32 i64 i32) (result i32) (i32.const 0))\n");
  }
  if (has_undefined_function("__assert_rtn", 12)) {
    wasm_emitf(2, "(func $__assert_rtn (param i32 i32 i32 i32) (unreachable))\n");
  }
  if (has_undefined_function("snprintf", 8)) {
    wasm_emitf(2, "(func $__ag_write_u64_dec (param $buf i32) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $div i64)\n");
    wasm_emitf(4, "(local $digit i64)\n");
    wasm_emitf(4, "(local $started i32)\n");
    wasm_emitf(4, "(local $count i32)\n");
    wasm_emitf(4, "(local.set $div (i64.const 1000000000000000000))\n");
    wasm_emitf(4, "(block $done\n");
    wasm_emitf(6, "(loop $loop\n");
    wasm_emitf(8, "(local.set $digit (i64.div_u (local.get $n) (local.get $div)))\n");
    wasm_emitf(8, "(if (i32.or (i32.or (local.get $started) (i64.ne (local.get $digit) (i64.const 0))) (i64.eq (local.get $div) (i64.const 1)))\n");
    wasm_emitf(10, "(then\n");
    wasm_emitf(12, "(i32.store8 (i32.add (local.get $buf) (local.get $count)) (i32.wrap_i64 (i64.add (local.get $digit) (i64.const 48))))\n");
    wasm_emitf(12, "(local.set $count (i32.add (local.get $count) (i32.const 1)))\n");
    wasm_emitf(12, "(local.set $started (i32.const 1))\n");
    wasm_emitf(10, ")\n");
    wasm_emitf(8, ")\n");
    wasm_emitf(8, "(local.set $n (i64.rem_u (local.get $n) (local.get $div)))\n");
    wasm_emitf(8, "(if (i64.eq (local.get $div) (i64.const 1)) (then (br $done)))\n");
    wasm_emitf(8, "(local.set $div (i64.div_u (local.get $div) (i64.const 10)))\n");
    wasm_emitf(8, "(br $loop)\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(4, "(local.get $count)\n");
    wasm_emitf(2, ")\n");
    wasm_emitf(2, "(func $snprintf (param $buf i32) (param $size i64) (param $fmt i32) (param $a i64) (param $b i64) (result i32)\n");
    wasm_emitf(4, "(local $c1 i32)\n");
    wasm_emitf(4, "(local $c2 i32)\n");
    wasm_emitf(4, "(local.set $c1 (call $__ag_write_u64_dec (local.get $buf) (local.get $a)))\n");
    wasm_emitf(4, "(if (i32.eq (i32.load8_u (i32.add (local.get $fmt) (i32.const 2))) (i32.const 45))\n");
    wasm_emitf(6, "(then\n");
    wasm_emitf(8, "(i32.store8 (i32.add (local.get $buf) (local.get $c1)) (i32.const 45))\n");
    wasm_emitf(8, "(local.set $c2 (call $__ag_write_u64_dec (i32.add (i32.add (local.get $buf) (local.get $c1)) (i32.const 1)) (local.get $b)))\n");
    wasm_emitf(8, "(i32.store8 (i32.add (i32.add (i32.add (local.get $buf) (local.get $c1)) (i32.const 1)) (local.get $c2)) (i32.const 0))\n");
    wasm_emitf(8, "(return (i32.add (i32.add (local.get $c1) (i32.const 1)) (local.get $c2)))\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(4, "(i32.store8 (i32.add (local.get $buf) (local.get $c1)) (i32.const 0))\n");
    wasm_emitf(4, "(local.get $c1)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("sprintf", 7)) {
    wasm_emitf(2, "(func $sprintf (param $buf i32) (param $fmt i32) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $n i32)\n");
    wasm_emitf(4, "(local.set $p (local.get $buf))\n");
    wasm_emitf(4, "(local.set $n (i32.wrap_i64 (i64.load (global.get $__ag_va_arg_area))))\n");
    wasm_emitf(4, "(i32.store8 (local.get $p) (i32.const 45))\n");
    wasm_emitf(4, "(i32.store8 (i32.add (local.get $p) (i32.const 1)) (i32.const 62))\n");
    wasm_emitf(4, "(i32.store8 (i32.add (local.get $p) (i32.const 2)) (i32.add (i32.div_u (local.get $n) (i32.const 10)) (i32.const 48)))\n");
    wasm_emitf(4, "(i32.store8 (i32.add (local.get $p) (i32.const 3)) (i32.add (i32.rem_u (local.get $n) (i32.const 10)) (i32.const 48)))\n");
    wasm_emitf(4, "(i32.store8 (i32.add (local.get $p) (i32.const 4)) (i32.const 60))\n");
    wasm_emitf(4, "(i32.store8 (i32.add (local.get $p) (i32.const 5)) (i32.const 45))\n");
    wasm_emitf(4, "(i32.store8 (i32.add (local.get $p) (i32.const 6)) (i32.const 10))\n");
    wasm_emitf(4, "(i32.store8 (i32.add (local.get $p) (i32.const 7)) (i32.const 0))\n");
    wasm_emitf(4, "(i32.const 7)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("imaxabs", 7)) {
    wasm_emitf(2, "(func $imaxabs (param $x i64) (result i64)\n");
    wasm_emitf(4, "(if (result i64) (i64.lt_s (local.get $x) (i64.const 0))\n");
    wasm_emitf(6, "(then (i64.sub (i64.const 0) (local.get $x)))\n");
    wasm_emitf(6, "(else (local.get $x))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("feclearexcept", 13)) {
    wasm_emitf(2, "(func $feclearexcept (param i64) (result i32) (i32.const 0))\n");
  }
  if (has_undefined_function("fetestexcept", 12)) {
    wasm_emitf(2, "(func $fetestexcept (param $mask i64) (result i32) (i32.wrap_i64 (local.get $mask)))\n");
  }
  if (has_undefined_function("isalpha", 7)) {
    wasm_emitf(2, "(func $isalpha (param $c i64) (result i32)\n");
    wasm_emitf(4, "(i32.or\n");
    wasm_emitf(6, "(i32.and (i64.ge_s (local.get $c) (i64.const 65)) (i64.le_s (local.get $c) (i64.const 90)))\n");
    wasm_emitf(6, "(i32.and (i64.ge_s (local.get $c) (i64.const 97)) (i64.le_s (local.get $c) (i64.const 122)))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("isdigit", 7)) {
    wasm_emitf(2, "(func $isdigit (param $c i64) (result i32)\n");
    wasm_emitf(4, "(i32.and (i64.ge_s (local.get $c) (i64.const 48)) (i64.le_s (local.get $c) (i64.const 57)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("toupper", 7)) {
    wasm_emitf(2, "(func $toupper (param $c i64) (result i32)\n");
    wasm_emitf(4, "(if (result i32) (i32.and (i64.ge_s (local.get $c) (i64.const 97)) (i64.le_s (local.get $c) (i64.const 122)))\n");
    wasm_emitf(6, "(then (i32.wrap_i64 (i64.sub (local.get $c) (i64.const 32))))\n");
    wasm_emitf(6, "(else (i32.wrap_i64 (local.get $c)))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("abs", 3)) {
    wasm_emitf(2, "(func $abs (param $x i64) (result i32)\n");
    wasm_emitf(4, "(i32.wrap_i64 (if (result i64) (i64.lt_s (local.get $x) (i64.const 0))\n");
    wasm_emitf(6, "(then (i64.sub (i64.const 0) (local.get $x)))\n");
    wasm_emitf(6, "(else (local.get $x))\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strlen", 6)) {
    ir_type_t ret_ty = wasm_function_result_type_from_decl("strlen", 6);
    const char *ret_wty = ret_ty == IR_TY_I64 ? "i64" : "i32";
    wasm_emitf(2, "(func $strlen (param $s i32) (result %s)\n", ret_wty);
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $n i64)\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.eq (i32.load8_u (local.get $p)) (i32.const 0)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $n (i64.add (local.get $n) (i64.const 1)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    if (ret_ty == IR_TY_I64) {
      wasm_emitf(4, "(local.get $n)\n");
    } else {
      wasm_emitf(4, "(i32.wrap_i64 (local.get $n))\n");
    }
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strcpy", 6)) {
    wasm_emitf(2, "(func $strcpy (param $dst i32) (param $src i32) (result i32)\n");
    wasm_emitf(4, "(local $d i32)\n");
    wasm_emitf(4, "(local $s i32)\n");
    wasm_emitf(4, "(local $c i32)\n");
    wasm_emitf(4, "(local.set $d (local.get $dst))\n");
    wasm_emitf(4, "(local.set $s (local.get $src))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $c (i32.load8_u (local.get $s)))\n");
    wasm_emitf(6, "(i32.store8 (local.get $d) (local.get $c))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $c) (i32.const 0)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $d (i32.add (local.get $d) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $s (i32.add (local.get $s) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $dst)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strncpy", 7)) {
    wasm_emitf(2, "(func $strncpy (param $dst i32) (param $src i32) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $d i32)\n");
    wasm_emitf(4, "(local $s i32)\n");
    wasm_emitf(4, "(local $end i32)\n");
    wasm_emitf(4, "(local $c i32)\n");
    wasm_emitf(4, "(local $zeroing i32)\n");
    wasm_emitf(4, "(local.set $d (local.get $dst))\n");
    wasm_emitf(4, "(local.set $s (local.get $src))\n");
    wasm_emitf(4, "(local.set $end (i32.add (local.get $d) (i32.wrap_i64 (local.get $n))))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $d) (local.get $end)) (then (br $done)))\n");
    wasm_emitf(6, "(if (local.get $zeroing)\n");
    wasm_emitf(8, "(then (local.set $c (i32.const 0)))\n");
    wasm_emitf(8, "(else\n");
    wasm_emitf(10, "(local.set $c (i32.load8_u (local.get $s)))\n");
    wasm_emitf(10, "(local.set $s (i32.add (local.get $s) (i32.const 1)))\n");
    wasm_emitf(10, "(if (i32.eq (local.get $c) (i32.const 0)) (then (local.set $zeroing (i32.const 1))))\n");
    wasm_emitf(8, ")\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(6, "(i32.store8 (local.get $d) (local.get $c))\n");
    wasm_emitf(6, "(local.set $d (i32.add (local.get $d) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $dst)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strcat", 6)) {
    wasm_emitf(2, "(func $strcat (param $dst i32) (param $src i32) (result i32)\n");
    wasm_emitf(4, "(local $d i32)\n");
    wasm_emitf(4, "(local $s i32)\n");
    wasm_emitf(4, "(local $c i32)\n");
    wasm_emitf(4, "(local.set $d (local.get $dst))\n");
    wasm_emitf(4, "(local.set $s (local.get $src))\n");
    wasm_emitf(4, "(block $found (loop $find\n");
    wasm_emitf(6, "(if (i32.eq (i32.load8_u (local.get $d)) (i32.const 0)) (then (br $found)))\n");
    wasm_emitf(6, "(local.set $d (i32.add (local.get $d) (i32.const 1)))\n");
    wasm_emitf(6, "(br $find)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(block $done (loop $copy\n");
    wasm_emitf(6, "(local.set $c (i32.load8_u (local.get $s)))\n");
    wasm_emitf(6, "(i32.store8 (local.get $d) (local.get $c))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $c) (i32.const 0)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $d (i32.add (local.get $d) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $s (i32.add (local.get $s) (i32.const 1)))\n");
    wasm_emitf(6, "(br $copy)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $dst)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strcmp", 6)) {
    wasm_emitf(2, "(func $strcmp (param $a i32) (param $b i32) (result i32)\n");
    wasm_emitf(4, "(local $pa i32)\n");
    wasm_emitf(4, "(local $pb i32)\n");
    wasm_emitf(4, "(local $ca i32)\n");
    wasm_emitf(4, "(local $cb i32)\n");
    wasm_emitf(4, "(local.set $pa (local.get $a))\n");
    wasm_emitf(4, "(local.set $pb (local.get $b))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $ca (i32.load8_u (local.get $pa)))\n");
    wasm_emitf(6, "(local.set $cb (i32.load8_u (local.get $pb)))\n");
    wasm_emitf(6, "(if (i32.ne (local.get $ca) (local.get $cb)) (then (return (i32.sub (local.get $ca) (local.get $cb)))))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $ca) (i32.const 0)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $pa (i32.add (local.get $pa) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $pb (i32.add (local.get $pb) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strncmp", 7)) {
    wasm_emitf(2, "(func $strncmp (param $a i32) (param $b i32) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $pa i32)\n");
    wasm_emitf(4, "(local $pb i32)\n");
    wasm_emitf(4, "(local $end i32)\n");
    wasm_emitf(4, "(local $ca i32)\n");
    wasm_emitf(4, "(local $cb i32)\n");
    wasm_emitf(4, "(local.set $pa (local.get $a))\n");
    wasm_emitf(4, "(local.set $pb (local.get $b))\n");
    wasm_emitf(4, "(local.set $end (i32.add (local.get $pa) (i32.wrap_i64 (local.get $n))))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $pa) (local.get $end)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $ca (i32.load8_u (local.get $pa)))\n");
    wasm_emitf(6, "(local.set $cb (i32.load8_u (local.get $pb)))\n");
    wasm_emitf(6, "(if (i32.ne (local.get $ca) (local.get $cb)) (then (return (i32.sub (local.get $ca) (local.get $cb)))))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $ca) (i32.const 0)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $pa (i32.add (local.get $pa) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $pb (i32.add (local.get $pb) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strchr", 6)) {
    wasm_emitf(2, "(func $strchr (param $s i32) (param $ch i64) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $c i32)\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $c (i32.load8_u (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $c) (i32.wrap_i64 (local.get $ch))) (then (return (local.get $p))))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $c) (i32.const 0)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("strrchr", 7)) {
    wasm_emitf(2, "(func $strrchr (param $s i32) (param $ch i64) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $last i32)\n");
    wasm_emitf(4, "(local $c i32)\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $c (i32.load8_u (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $c) (i32.wrap_i64 (local.get $ch))) (then (local.set $last (local.get $p))))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $c) (i32.const 0)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $last)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("putchar", 7)) {
    wasm_emitf(2, "(func $putchar (param $c i64) (result i32) (i32.wrap_i64 (local.get $c)))\n");
  }
  if (has_undefined_function("memset", 6)) {
    wasm_emitf(2, "(func $memset (param $dst i32) (param $ch i64) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $end i32)\n");
    wasm_emitf(4, "(local.set $p (local.get $dst))\n");
    wasm_emitf(4, "(local.set $end (i32.add (local.get $p) (i32.wrap_i64 (local.get $n))))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $p) (local.get $end)) (then (br $done)))\n");
    wasm_emitf(6, "(i32.store8 (local.get $p) (i32.wrap_i64 (local.get $ch)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $dst)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("memcpy", 6)) {
    wasm_emitf(2, "(func $memcpy (param $dst i32) (param $src i32) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $d i32)\n");
    wasm_emitf(4, "(local $s i32)\n");
    wasm_emitf(4, "(local $end i32)\n");
    wasm_emitf(4, "(local.set $d (local.get $dst))\n");
    wasm_emitf(4, "(local.set $s (local.get $src))\n");
    wasm_emitf(4, "(local.set $end (i32.add (local.get $d) (i32.wrap_i64 (local.get $n))))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $d) (local.get $end)) (then (br $done)))\n");
    wasm_emitf(6, "(i32.store8 (local.get $d) (i32.load8_u (local.get $s)))\n");
    wasm_emitf(6, "(local.set $d (i32.add (local.get $d) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $s (i32.add (local.get $s) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $dst)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("memcmp", 6)) {
    wasm_emitf(2, "(func $memcmp (param $a i32) (param $b i32) (param $n i64) (result i32)\n");
    wasm_emitf(4, "(local $pa i32)\n");
    wasm_emitf(4, "(local $pb i32)\n");
    wasm_emitf(4, "(local $end i32)\n");
    wasm_emitf(4, "(local $ca i32)\n");
    wasm_emitf(4, "(local $cb i32)\n");
    wasm_emitf(4, "(local.set $pa (local.get $a))\n");
    wasm_emitf(4, "(local.set $pb (local.get $b))\n");
    wasm_emitf(4, "(local.set $end (i32.add (local.get $pa) (i32.wrap_i64 (local.get $n))))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $pa) (local.get $end)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $ca (i32.load8_u (local.get $pa)))\n");
    wasm_emitf(6, "(local.set $cb (i32.load8_u (local.get $pb)))\n");
    wasm_emitf(6, "(if (i32.ne (local.get $ca) (local.get $cb)) (then (return (i32.sub (local.get $ca) (local.get $cb)))))\n");
    wasm_emitf(6, "(local.set $pa (i32.add (local.get $pa) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $pb (i32.add (local.get $pb) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("atoi", 4)) {
    wasm_emitf(2, "(func $atoi (param $s i32) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(local $n i32)\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $ch (i32.load8_u (local.get $p)))\n");
    wasm_emitf(6, "(if (i32.or (i32.lt_u (local.get $ch) (i32.const 48)) (i32.gt_u (local.get $ch) (i32.const 57))) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $n (i32.add (i32.mul (local.get $n) (i32.const 10)) (i32.sub (local.get $ch) (i32.const 48))))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $n)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("malloc", 6)) {
    wasm_emitf(2, "(func $malloc (param $size i64) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local.set $p (global.get $__ag_heap_pointer))\n");
    wasm_emitf(4, "(global.set $__ag_heap_pointer (i32.add (global.get $__ag_heap_pointer) (i32.and (i32.add (i32.wrap_i64 (local.get $size)) (i32.const 7)) (i32.const -8))))\n");
    wasm_emitf(4, "(local.get $p)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("calloc", 6)) {
    wasm_emitf(2, "(func $calloc (param $nmemb i64) (param $size i64) (result i32)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $q i32)\n");
    wasm_emitf(4, "(local $end i32)\n");
    wasm_emitf(4, "(local $bytes i32)\n");
    wasm_emitf(4, "(local.set $bytes (i32.wrap_i64 (i64.mul (local.get $nmemb) (local.get $size))))\n");
    wasm_emitf(4, "(local.set $p (global.get $__ag_heap_pointer))\n");
    wasm_emitf(4, "(local.set $q (local.get $p))\n");
    wasm_emitf(4, "(local.set $end (i32.add (local.get $p) (local.get $bytes)))\n");
    wasm_emitf(4, "(global.set $__ag_heap_pointer (i32.add (local.get $p) (i32.and (i32.add (local.get $bytes) (i32.const 7)) (i32.const -8))))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $q) (local.get $end)) (then (br $done)))\n");
    wasm_emitf(6, "(i32.store8 (local.get $q) (i32.const 0))\n");
    wasm_emitf(6, "(local.set $q (i32.add (local.get $q) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $p)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("free", 4)) {
    wasm_emitf(2, "(func $free (param i32))\n");
  }
  if (has_undefined_function("setlocale", 9)) {
    int c_addr = intern_data_symbol("__ag_stub_locale_c", 18, 2, 1)->addr;
    wasm_emitf(2, "(func $setlocale (param i64 i32) (result i32) (i32.const %d))\n", c_addr);
  }
  if (has_undefined_function("localeconv", 10)) {
    int lc_addr = intern_data_symbol("__ag_stub_lconv", 15, 96, 4)->addr;
    wasm_emitf(2, "(func $localeconv (result i32) (i32.const %d))\n", lc_addr);
  }
  if (has_undefined_function("iswalpha", 8)) {
    wasm_emitf(2, "(func $iswalpha (param $c i64) (result i32)\n");
    wasm_emitf(4, "(i32.or\n");
    wasm_emitf(6, "(i32.and (i64.ge_s (local.get $c) (i64.const 65)) (i64.le_s (local.get $c) (i64.const 90)))\n");
    wasm_emitf(6, "(i32.and (i64.ge_s (local.get $c) (i64.const 97)) (i64.le_s (local.get $c) (i64.const 122)))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("iswdigit", 8)) {
    wasm_emitf(2, "(func $iswdigit (param $c i64) (result i32)\n");
    wasm_emitf(4, "(i32.and (i64.ge_s (local.get $c) (i64.const 48)) (i64.le_s (local.get $c) (i64.const 57)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("towupper", 8)) {
    wasm_emitf(2, "(func $towupper (param $c i64) (result i32)\n");
    wasm_emitf(4, "(if (result i32) (i32.and (i64.ge_s (local.get $c) (i64.const 97)) (i64.le_s (local.get $c) (i64.const 122)))\n");
    wasm_emitf(6, "(then (i32.wrap_i64 (i64.sub (local.get $c) (i64.const 32))))\n");
    wasm_emitf(6, "(else (i32.wrap_i64 (local.get $c)))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcslen", 6)) {
    wasm_emitf(2, "(func $wcslen (param $s i32) (result i64)\n");
    wasm_emitf(4, "(local $p i32)\n");
    wasm_emitf(4, "(local $n i64)\n");
    wasm_emitf(4, "(local.set $p (local.get $s))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.eq (i32.load (local.get $p)) (i32.const 0)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $n (i64.add (local.get $n) (i64.const 1)))\n");
    wasm_emitf(6, "(local.set $p (i32.add (local.get $p) (i32.const 4)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $n)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcscpy", 6)) {
    wasm_emitf(2, "(func $wcscpy (param $dst i32) (param $src i32) (result i32)\n");
    wasm_emitf(4, "(local $d i32)\n");
    wasm_emitf(4, "(local $s i32)\n");
    wasm_emitf(4, "(local $ch i32)\n");
    wasm_emitf(4, "(local.set $d (local.get $dst))\n");
    wasm_emitf(4, "(local.set $s (local.get $src))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $ch (i32.load (local.get $s)))\n");
    wasm_emitf(6, "(i32.store (local.get $d) (local.get $ch))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $ch) (i32.const 0)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $d (i32.add (local.get $d) (i32.const 4)))\n");
    wasm_emitf(6, "(local.set $s (i32.add (local.get $s) (i32.const 4)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $dst)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("wcscmp", 6)) {
    wasm_emitf(2, "(func $wcscmp (param $a i32) (param $b i32) (result i32)\n");
    wasm_emitf(4, "(local $pa i32)\n");
    wasm_emitf(4, "(local $pb i32)\n");
    wasm_emitf(4, "(local $ca i32)\n");
    wasm_emitf(4, "(local $cb i32)\n");
    wasm_emitf(4, "(local.set $pa (local.get $a))\n");
    wasm_emitf(4, "(local.set $pb (local.get $b))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(local.set $ca (i32.load (local.get $pa)))\n");
    wasm_emitf(6, "(local.set $cb (i32.load (local.get $pb)))\n");
    wasm_emitf(6, "(if (i32.ne (local.get $ca) (local.get $cb)) (then (return (i32.sub (local.get $ca) (local.get $cb)))))\n");
    wasm_emitf(6, "(if (i32.eq (local.get $ca) (i32.const 0)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $pa (i32.add (local.get $pa) (i32.const 4)))\n");
    wasm_emitf(6, "(local.set $pb (i32.add (local.get $pb) (i32.const 4)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(i32.const 0)\n");
    wasm_emitf(2, ")\n");
  }
  int atan_defined = psx_ctx_has_function_name("atan", 4) &&
                     psx_ctx_is_function_defined("atan", 4);
  int atan2_defined = psx_ctx_has_function_name("atan2", 5) &&
                      psx_ctx_is_function_defined("atan2", 5);
  int need_atan2_stub = has_undefined_function("atan2", 5) ||
                        has_undefined_function("atan2f", 6) ||
                        has_undefined_function("atan2l", 6) ||
                        ((has_undefined_function("asin", 4) ||
                          has_undefined_function("asinf", 5) ||
                          has_undefined_function("asinl", 5) ||
                          has_undefined_function("acosf", 5) ||
                          has_undefined_function("acosl", 5) ||
                          has_undefined_function("acos", 4)) &&
                         !atan2_defined);
  int need_atan_stub = has_undefined_function("atan", 4) ||
                       has_undefined_function("atanf", 5) ||
                       has_undefined_function("atanl", 5) ||
                       (need_atan2_stub && !atan_defined);
  int need_atan = need_atan_stub || need_atan2_stub;
  if (need_atan) {
    wasm_emitf(2, "(func $__ag_atan_core (param $x f64) (result f64)\n");
    wasm_emitf(4, "(local $x2 f64)\n");
    wasm_emitf(4, "(local $term f64)\n");
    wasm_emitf(4, "(local $sum f64)\n");
    wasm_emitf(4, "(local $den f64)\n");
    wasm_emitf(4, "(local $i i32)\n");
    wasm_emitf(4, "(local $neg i32)\n");
    wasm_emitf(4, "(local.set $x2 (f64.mul (local.get $x) (local.get $x)))\n");
    wasm_emitf(4, "(local.set $term (local.get $x))\n");
    wasm_emitf(4, "(local.set $sum (local.get $x))\n");
    wasm_emitf(4, "(local.set $den (f64.const 3))\n");
    wasm_emitf(4, "(local.set $neg (i32.const 1))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $i) (i32.const 80)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $term (f64.mul (local.get $term) (local.get $x2)))\n");
    wasm_emitf(6, "(if (local.get $neg)\n");
    wasm_emitf(8, "(then (local.set $sum (f64.sub (local.get $sum) (f64.div (local.get $term) (local.get $den)))))\n");
    wasm_emitf(8, "(else (local.set $sum (f64.add (local.get $sum) (f64.div (local.get $term) (local.get $den)))))\n");
    wasm_emitf(6, ")\n");
    wasm_emitf(6, "(local.set $neg (i32.xor (local.get $neg) (i32.const 1)))\n");
    wasm_emitf(6, "(local.set $den (f64.add (local.get $den) (f64.const 2)))\n");
    wasm_emitf(6, "(local.set $i (i32.add (local.get $i) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $sum)\n");
    wasm_emitf(2, ")\n");
  }
  if (need_atan_stub) {
    wasm_emitf(2, "(func $atan (param $x f64) (result f64)\n");
    wasm_emitf(4, "(if (result f64) (f64.gt (local.get $x) (f64.const 1))\n");
    wasm_emitf(6, "(then (f64.sub (f64.const 1.5707963267948966) (call $__ag_atan_core (f64.div (f64.const 1) (local.get $x)))))\n");
    wasm_emitf(6, "(else (if (result f64) (f64.lt (local.get $x) (f64.const -1))\n");
    wasm_emitf(8, "(then (f64.sub (f64.const -1.5707963267948966) (call $__ag_atan_core (f64.div (f64.const 1) (local.get $x)))))\n");
    wasm_emitf(8, "(else (if (result f64) (f64.gt (local.get $x) (f64.const 0.5))\n");
    wasm_emitf(10, "(then (f64.add (f64.const 0.7853981633974483) (call $__ag_atan_core (f64.div (f64.sub (local.get $x) (f64.const 1)) (f64.add (local.get $x) (f64.const 1))))))\n");
    wasm_emitf(10, "(else (if (result f64) (f64.lt (local.get $x) (f64.const -0.5))\n");
    wasm_emitf(12, "(then (f64.sub (f64.const -0.7853981633974483) (call $__ag_atan_core (f64.div (f64.add (local.get $x) (f64.const 1)) (f64.sub (f64.const 1) (local.get $x))))))\n");
    wasm_emitf(12, "(else (call $__ag_atan_core (local.get $x)))\n");
    wasm_emitf(10, "))\n");
    wasm_emitf(8, "))\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(2, ")\n");
  }
  if (need_atan2_stub) {
    wasm_emitf(2, "(func $atan2 (param $y f64) (param $x f64) (result f64)\n");
    wasm_emitf(4, "(if (result f64) (f64.gt (local.get $x) (f64.const 0))\n");
    wasm_emitf(6, "(then (call $atan (f64.div (local.get $y) (local.get $x))))\n");
    wasm_emitf(6, "(else (if (result f64) (f64.lt (local.get $x) (f64.const 0))\n");
    wasm_emitf(8, "(then (if (result f64) (f64.ge (local.get $y) (f64.const 0))\n");
    wasm_emitf(10, "(then (f64.add (call $atan (f64.div (local.get $y) (local.get $x))) (f64.const 3.141592653589793)))\n");
    wasm_emitf(10, "(else (f64.sub (call $atan (f64.div (local.get $y) (local.get $x))) (f64.const 3.141592653589793)))\n");
    wasm_emitf(8, "))\n");
    wasm_emitf(8, "(else (if (result f64) (f64.gt (local.get $y) (f64.const 0))\n");
    wasm_emitf(10, "(then (f64.const 1.5707963267948966))\n");
    wasm_emitf(10, "(else (if (result f64) (f64.lt (local.get $y) (f64.const 0)) (then (f64.const -1.5707963267948966)) (else (f64.const 0))))\n");
    wasm_emitf(8, "))\n");
    wasm_emitf(6, "))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(2, ")\n");
  }
  int exp_defined = psx_ctx_has_function_name("exp", 3) &&
                    psx_ctx_is_function_defined("exp", 3);
  int need_exp_stub = has_undefined_function("exp", 3) ||
                      has_undefined_function("expf", 4) ||
                      has_undefined_function("expl", 4) ||
                      ((has_undefined_function("sinh", 4) ||
                        has_undefined_function("cosh", 4) ||
                        has_undefined_function("tanh", 4)) &&
                       !exp_defined);
  if (need_exp_stub) {
    wasm_emitf(2, "(func $exp (param $x f64) (result f64)\n");
    wasm_emitf(4, "(local $r f64)\n");
    wasm_emitf(4, "(local $term f64)\n");
    wasm_emitf(4, "(local $sum f64)\n");
    wasm_emitf(4, "(local $n f64)\n");
    wasm_emitf(4, "(local $k i32)\n");
    wasm_emitf(4, "(local $i i32)\n");
    wasm_emitf(4, "(local.set $r (local.get $x))\n");
    wasm_emitf(4, "(block $done_pos (loop $pos\n");
    wasm_emitf(6, "(if (f64.le (local.get $r) (f64.const 0.5)) (then (br $done_pos)))\n");
    wasm_emitf(6, "(local.set $r (f64.sub (local.get $r) (f64.const 0.6931471805599453)))\n");
    wasm_emitf(6, "(local.set $k (i32.add (local.get $k) (i32.const 1)))\n");
    wasm_emitf(6, "(br $pos)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(block $done_neg (loop $neg\n");
    wasm_emitf(6, "(if (f64.ge (local.get $r) (f64.const -0.5)) (then (br $done_neg)))\n");
    wasm_emitf(6, "(local.set $r (f64.add (local.get $r) (f64.const 0.6931471805599453)))\n");
    wasm_emitf(6, "(local.set $k (i32.sub (local.get $k) (i32.const 1)))\n");
    wasm_emitf(6, "(br $neg)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.set $term (f64.const 1))\n");
    wasm_emitf(4, "(local.set $sum (f64.const 1))\n");
    wasm_emitf(4, "(local.set $n (f64.const 1))\n");
    wasm_emitf(4, "(block $done_series (loop $series\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $i) (i32.const 28)) (then (br $done_series)))\n");
    wasm_emitf(6, "(local.set $term (f64.div (f64.mul (local.get $term) (local.get $r)) (local.get $n)))\n");
    wasm_emitf(6, "(local.set $sum (f64.add (local.get $sum) (local.get $term)))\n");
    wasm_emitf(6, "(local.set $n (f64.add (local.get $n) (f64.const 1)))\n");
    wasm_emitf(6, "(local.set $i (i32.add (local.get $i) (i32.const 1)))\n");
    wasm_emitf(6, "(br $series)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(block $done_scale_pos (loop $scale_pos\n");
    wasm_emitf(6, "(if (i32.le_s (local.get $k) (i32.const 0)) (then (br $done_scale_pos)))\n");
    wasm_emitf(6, "(local.set $sum (f64.mul (local.get $sum) (f64.const 2)))\n");
    wasm_emitf(6, "(local.set $k (i32.sub (local.get $k) (i32.const 1)))\n");
    wasm_emitf(6, "(br $scale_pos)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(block $done_scale_neg (loop $scale_neg\n");
    wasm_emitf(6, "(if (i32.ge_s (local.get $k) (i32.const 0)) (then (br $done_scale_neg)))\n");
    wasm_emitf(6, "(local.set $sum (f64.mul (local.get $sum) (f64.const 0.5)))\n");
    wasm_emitf(6, "(local.set $k (i32.add (local.get $k) (i32.const 1)))\n");
    wasm_emitf(6, "(br $scale_neg)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $sum)\n");
    wasm_emitf(2, ")\n");
  }
  int log_defined = psx_ctx_has_function_name("log", 3) &&
                    psx_ctx_is_function_defined("log", 3);
  int need_log_stub = has_undefined_function("log", 3) ||
                      has_undefined_function("logf", 4) ||
                      has_undefined_function("logl", 4) ||
                      ((has_undefined_function("log2", 4) ||
                        has_undefined_function("log2f", 5) ||
                        has_undefined_function("log2l", 5) ||
                        has_undefined_function("log10f", 6) ||
                        has_undefined_function("log10l", 6) ||
                        has_undefined_function("log10", 5)) &&
                       !log_defined);
  if (need_log_stub) {
    wasm_emitf(2, "(func $log (param $x f64) (result f64)\n");
    wasm_emitf(4, "(local $z f64)\n");
    wasm_emitf(4, "(local $z2 f64)\n");
    wasm_emitf(4, "(local $term f64)\n");
    wasm_emitf(4, "(local $sum f64)\n");
    wasm_emitf(4, "(local $den f64)\n");
    wasm_emitf(4, "(local $k i32)\n");
    wasm_emitf(4, "(local $i i32)\n");
    wasm_emitf(4, "(if (f64.le (local.get $x) (f64.const 0)) (then (return (f64.const -inf))))\n");
    wasm_emitf(4, "(block $done_hi (loop $hi\n");
    wasm_emitf(6, "(if (f64.le (local.get $x) (f64.const 2)) (then (br $done_hi)))\n");
    wasm_emitf(6, "(local.set $x (f64.mul (local.get $x) (f64.const 0.5)))\n");
    wasm_emitf(6, "(local.set $k (i32.add (local.get $k) (i32.const 1)))\n");
    wasm_emitf(6, "(br $hi)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(block $done_lo (loop $lo\n");
    wasm_emitf(6, "(if (f64.ge (local.get $x) (f64.const 0.5)) (then (br $done_lo)))\n");
    wasm_emitf(6, "(local.set $x (f64.mul (local.get $x) (f64.const 2)))\n");
    wasm_emitf(6, "(local.set $k (i32.sub (local.get $k) (i32.const 1)))\n");
    wasm_emitf(6, "(br $lo)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.set $z (f64.div (f64.sub (local.get $x) (f64.const 1)) (f64.add (local.get $x) (f64.const 1))))\n");
    wasm_emitf(4, "(local.set $z2 (f64.mul (local.get $z) (local.get $z)))\n");
    wasm_emitf(4, "(local.set $term (local.get $z))\n");
    wasm_emitf(4, "(local.set $sum (local.get $z))\n");
    wasm_emitf(4, "(local.set $den (f64.const 3))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $i) (i32.const 80)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $term (f64.mul (local.get $term) (local.get $z2)))\n");
    wasm_emitf(6, "(local.set $sum (f64.add (local.get $sum) (f64.div (local.get $term) (local.get $den))))\n");
    wasm_emitf(6, "(local.set $den (f64.add (local.get $den) (f64.const 2)))\n");
    wasm_emitf(6, "(local.set $i (i32.add (local.get $i) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(f64.add (f64.mul (f64.const 2) (local.get $sum)) (f64.mul (f64.convert_i32_s (local.get $k)) (f64.const 0.6931471805599453)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("log2", 4) || has_undefined_function("log2f", 5) ||
      has_undefined_function("log2l", 5)) {
    wasm_emitf(2, "(func $log2 (param $x f64) (result f64)\n");
    wasm_emitf(4, "(f64.div (call $log (local.get $x)) (f64.const 0.6931471805599453))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("log2f", 5)) {
    wasm_emitf(2, "(func $log2f (param $x f32) (result f32) (f32.demote_f64 (call $log2 (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("log2l", 5)) {
    wasm_emitf(2, "(func $log2l (param $x f64) (result f64) (call $log2 (local.get $x)))\n");
  }
  if (has_undefined_function("log10", 5) || has_undefined_function("log10f", 6) ||
      has_undefined_function("log10l", 6)) {
    wasm_emitf(2, "(func $log10 (param $x f64) (result f64)\n");
    wasm_emitf(4, "(f64.div (call $log (local.get $x)) (f64.const 2.302585092994046))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("log10f", 6)) {
    wasm_emitf(2, "(func $log10f (param $x f32) (result f32) (f32.demote_f64 (call $log10 (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("log10l", 6)) {
    wasm_emitf(2, "(func $log10l (param $x f64) (result f64) (call $log10 (local.get $x)))\n");
  }
  int sin_defined = psx_ctx_has_function_name("sin", 3) &&
                    psx_ctx_is_function_defined("sin", 3);
  int need_sin_stub = has_undefined_function("sin", 3) ||
                      has_undefined_function("sinf", 4) ||
                      has_undefined_function("sinl", 4) ||
                      (has_undefined_function("tan", 3) && !sin_defined) ||
                      (has_undefined_function("tanf", 4) && !sin_defined) ||
                      (has_undefined_function("tanl", 4) && !sin_defined);
  if (need_sin_stub) {
    wasm_emitf(2, "(func $sin (param $x f64) (result f64)\n");
    wasm_emitf(4, "(local $x2 f64)\n");
    wasm_emitf(4, "(local $term f64)\n");
    wasm_emitf(4, "(local $sum f64)\n");
    wasm_emitf(4, "(local $den f64)\n");
    wasm_emitf(4, "(local $i i32)\n");
    wasm_emitf(4, "(block $done_hi (loop $hi\n");
    wasm_emitf(6, "(if (f64.le (local.get $x) (f64.const 3.141592653589793)) (then (br $done_hi)))\n");
    wasm_emitf(6, "(local.set $x (f64.sub (local.get $x) (f64.const 6.283185307179586)))\n");
    wasm_emitf(6, "(br $hi)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(block $done_lo (loop $lo\n");
    wasm_emitf(6, "(if (f64.ge (local.get $x) (f64.const -3.141592653589793)) (then (br $done_lo)))\n");
    wasm_emitf(6, "(local.set $x (f64.add (local.get $x) (f64.const 6.283185307179586)))\n");
    wasm_emitf(6, "(br $lo)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (f64.gt (local.get $x) (f64.const 1.5707963267948966)) (then (local.set $x (f64.sub (f64.const 3.141592653589793) (local.get $x)))))\n");
    wasm_emitf(4, "(if (f64.lt (local.get $x) (f64.const -1.5707963267948966)) (then (local.set $x (f64.sub (f64.const -3.141592653589793) (local.get $x)))))\n");
    wasm_emitf(4, "(local.set $x2 (f64.mul (local.get $x) (local.get $x)))\n");
    wasm_emitf(4, "(local.set $term (local.get $x))\n");
    wasm_emitf(4, "(local.set $sum (local.get $x))\n");
    wasm_emitf(4, "(local.set $i (i32.const 1))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $i) (i32.const 14)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $den (f64.convert_i32_s (i32.mul (i32.mul (local.get $i) (i32.const 2)) (i32.add (i32.mul (local.get $i) (i32.const 2)) (i32.const 1)))))\n");
    wasm_emitf(6, "(local.set $term (f64.div (f64.neg (f64.mul (local.get $term) (local.get $x2))) (local.get $den)))\n");
    wasm_emitf(6, "(local.set $sum (f64.add (local.get $sum) (local.get $term)))\n");
    wasm_emitf(6, "(local.set $i (i32.add (local.get $i) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $sum)\n");
    wasm_emitf(2, ")\n");
  }
  int cos_defined = psx_ctx_has_function_name("cos", 3) &&
                    psx_ctx_is_function_defined("cos", 3);
  int need_cos_stub = has_undefined_function("cos", 3) ||
                      has_undefined_function("cosf", 4) ||
                      has_undefined_function("cosl", 4) ||
                      (has_undefined_function("tan", 3) && !cos_defined) ||
                      (has_undefined_function("tanf", 4) && !cos_defined) ||
                      (has_undefined_function("tanl", 4) && !cos_defined);
  if (need_cos_stub) {
    wasm_emitf(2, "(func $cos (param $x f64) (result f64)\n");
    wasm_emitf(4, "(local $x2 f64)\n");
    wasm_emitf(4, "(local $term f64)\n");
    wasm_emitf(4, "(local $sum f64)\n");
    wasm_emitf(4, "(local $den f64)\n");
    wasm_emitf(4, "(local $i i32)\n");
    wasm_emitf(4, "(local $sign f64)\n");
    wasm_emitf(4, "(local.set $sign (f64.const 1))\n");
    wasm_emitf(4, "(block $done_hi (loop $hi\n");
    wasm_emitf(6, "(if (f64.le (local.get $x) (f64.const 3.141592653589793)) (then (br $done_hi)))\n");
    wasm_emitf(6, "(local.set $x (f64.sub (local.get $x) (f64.const 6.283185307179586)))\n");
    wasm_emitf(6, "(br $hi)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(block $done_lo (loop $lo\n");
    wasm_emitf(6, "(if (f64.ge (local.get $x) (f64.const -3.141592653589793)) (then (br $done_lo)))\n");
    wasm_emitf(6, "(local.set $x (f64.add (local.get $x) (f64.const 6.283185307179586)))\n");
    wasm_emitf(6, "(br $lo)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(if (f64.gt (local.get $x) (f64.const 1.5707963267948966)) (then (local.set $x (f64.sub (f64.const 3.141592653589793) (local.get $x))) (local.set $sign (f64.const -1))))\n");
    wasm_emitf(4, "(if (f64.lt (local.get $x) (f64.const -1.5707963267948966)) (then (local.set $x (f64.sub (f64.const -3.141592653589793) (local.get $x))) (local.set $sign (f64.const -1))))\n");
    wasm_emitf(4, "(local.set $x2 (f64.mul (local.get $x) (local.get $x)))\n");
    wasm_emitf(4, "(local.set $term (f64.const 1))\n");
    wasm_emitf(4, "(local.set $sum (f64.const 1))\n");
    wasm_emitf(4, "(local.set $i (i32.const 1))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $i) (i32.const 14)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $den (f64.convert_i32_s (i32.mul (i32.sub (i32.mul (local.get $i) (i32.const 2)) (i32.const 1)) (i32.mul (local.get $i) (i32.const 2)))))\n");
    wasm_emitf(6, "(local.set $term (f64.div (f64.neg (f64.mul (local.get $term) (local.get $x2))) (local.get $den)))\n");
    wasm_emitf(6, "(local.set $sum (f64.add (local.get $sum) (local.get $term)))\n");
    wasm_emitf(6, "(local.set $i (i32.add (local.get $i) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(f64.mul (local.get $sign) (local.get $sum))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("tan", 3) || has_undefined_function("tanf", 4) ||
      has_undefined_function("tanl", 4)) {
    wasm_emitf(2, "(func $tan (param $x f64) (result f64)\n");
    wasm_emitf(4, "(f64.div (call $sin (local.get $x)) (call $cos (local.get $x)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("sinf", 4)) {
    wasm_emitf(2, "(func $sinf (param $x f32) (result f32) (f32.demote_f64 (call $sin (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("sinl", 4)) {
    wasm_emitf(2, "(func $sinl (param $x f64) (result f64) (call $sin (local.get $x)))\n");
  }
  if (has_undefined_function("cosf", 4)) {
    wasm_emitf(2, "(func $cosf (param $x f32) (result f32) (f32.demote_f64 (call $cos (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("cosl", 4)) {
    wasm_emitf(2, "(func $cosl (param $x f64) (result f64) (call $cos (local.get $x)))\n");
  }
  if (has_undefined_function("tanf", 4)) {
    wasm_emitf(2, "(func $tanf (param $x f32) (result f32) (f32.demote_f64 (call $tan (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("tanl", 4)) {
    wasm_emitf(2, "(func $tanl (param $x f64) (result f64) (call $tan (local.get $x)))\n");
  }
  if (has_undefined_function("sinh", 4)) {
    wasm_emitf(2, "(func $sinh (param $x f64) (result f64)\n");
    wasm_emitf(4, "(f64.mul (f64.const 0.5) (f64.sub (call $exp (local.get $x)) (call $exp (f64.neg (local.get $x)))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("cosh", 4)) {
    wasm_emitf(2, "(func $cosh (param $x f64) (result f64)\n");
    wasm_emitf(4, "(f64.mul (f64.const 0.5) (f64.add (call $exp (local.get $x)) (call $exp (f64.neg (local.get $x)))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("tanh", 4)) {
    wasm_emitf(2, "(func $tanh (param $x f64) (result f64)\n");
    wasm_emitf(4, "(f64.div (f64.sub (call $exp (local.get $x)) (call $exp (f64.neg (local.get $x)))) (f64.add (call $exp (local.get $x)) (call $exp (f64.neg (local.get $x)))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("sqrt", 4) || has_undefined_function("sqrtl", 5) ||
      has_undefined_function("hypot", 5) || has_undefined_function("hypotf", 6) ||
      has_undefined_function("hypotl", 6) ||
      has_undefined_function("asin", 4) || has_undefined_function("asinf", 5) ||
      has_undefined_function("asinl", 5) || has_undefined_function("acos", 4) ||
      has_undefined_function("acosf", 5) || has_undefined_function("acosl", 5) ||
      has_undefined_function("pow", 3) || has_undefined_function("powf", 4) ||
      has_undefined_function("powl", 4)) {
    wasm_emitf(2, "(func $sqrt (param $x f64) (result f64) (f64.sqrt (local.get $x)))\n");
  }
  if (has_undefined_function("sqrtf", 5)) {
    wasm_emitf(2, "(func $sqrtf (param $x f32) (result f32) (f32.sqrt (local.get $x)))\n");
  }
  if (has_undefined_function("sqrtl", 5)) {
    wasm_emitf(2, "(func $sqrtl (param $x f64) (result f64) (call $sqrt (local.get $x)))\n");
  }
  if (has_undefined_function("asin", 4) || has_undefined_function("asinf", 5) ||
      has_undefined_function("asinl", 5)) {
    wasm_emitf(2, "(func $asin (param $x f64) (result f64)\n");
    wasm_emitf(4, "(call $atan2 (local.get $x) (call $sqrt (f64.sub (f64.const 1) (f64.mul (local.get $x) (local.get $x)))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("acos", 4) || has_undefined_function("acosf", 5) ||
      has_undefined_function("acosl", 5)) {
    wasm_emitf(2, "(func $acos (param $x f64) (result f64)\n");
    wasm_emitf(4, "(call $atan2 (call $sqrt (f64.sub (f64.const 1) (f64.mul (local.get $x) (local.get $x)))) (local.get $x))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("asinf", 5)) {
    wasm_emitf(2, "(func $asinf (param $x f32) (result f32) (f32.demote_f64 (call $asin (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("asinl", 5)) {
    wasm_emitf(2, "(func $asinl (param $x f64) (result f64) (call $asin (local.get $x)))\n");
  }
  if (has_undefined_function("acosf", 5)) {
    wasm_emitf(2, "(func $acosf (param $x f32) (result f32) (f32.demote_f64 (call $acos (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("acosl", 5)) {
    wasm_emitf(2, "(func $acosl (param $x f64) (result f64) (call $acos (local.get $x)))\n");
  }
  if (has_undefined_function("atanf", 5)) {
    wasm_emitf(2, "(func $atanf (param $x f32) (result f32) (f32.demote_f64 (call $atan (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("atanl", 5)) {
    wasm_emitf(2, "(func $atanl (param $x f64) (result f64) (call $atan (local.get $x)))\n");
  }
  if (has_undefined_function("atan2f", 6)) {
    wasm_emitf(2, "(func $atan2f (param $y f32) (param $x f32) (result f32) (f32.demote_f64 (call $atan2 (f64.promote_f32 (local.get $y)) (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("atan2l", 6)) {
    wasm_emitf(2, "(func $atan2l (param $y f64) (param $x f64) (result f64) (call $atan2 (local.get $y) (local.get $x)))\n");
  }
  if (has_undefined_function("fmod", 4) || has_undefined_function("fmodf", 5) ||
      has_undefined_function("fmodl", 5)) {
    wasm_emitf(2, "(func $fmod (param $x f64) (param $y f64) (result f64)\n");
    wasm_emitf(4, "(f64.sub (local.get $x) (f64.mul (f64.trunc (f64.div (local.get $x) (local.get $y))) (local.get $y)))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fmodf", 5)) {
    wasm_emitf(2, "(func $fmodf (param $x f32) (param $y f32) (result f32)\n");
    wasm_emitf(4, "(f32.demote_f64 (call $fmod (f64.promote_f32 (local.get $x)) (f64.promote_f32 (local.get $y))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("fmodl", 5)) {
    wasm_emitf(2, "(func $fmodl (param $x f64) (param $y f64) (result f64) (call $fmod (local.get $x) (local.get $y)))\n");
  }
  if (has_undefined_function("cbrt", 4) || has_undefined_function("cbrtf", 5) ||
      has_undefined_function("cbrtl", 5)) {
    wasm_emitf(2, "(func $cbrt (param $x f64) (result f64)\n");
    wasm_emitf(4, "(local $g f64)\n");
    wasm_emitf(4, "(local $i i32)\n");
    wasm_emitf(4, "(if (f64.eq (local.get $x) (f64.const 0)) (then (return (f64.const 0))))\n");
    wasm_emitf(4, "(local.set $g (local.get $x))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_u (local.get $i) (i32.const 24)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $g (f64.div (f64.add (f64.mul (f64.const 2) (local.get $g)) (f64.div (local.get $x) (f64.mul (local.get $g) (local.get $g)))) (f64.const 3)))\n");
    wasm_emitf(6, "(local.set $i (i32.add (local.get $i) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $g)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("cbrtf", 5)) {
    wasm_emitf(2, "(func $cbrtf (param $x f32) (result f32) (f32.demote_f64 (call $cbrt (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("cbrtl", 5)) {
    wasm_emitf(2, "(func $cbrtl (param $x f64) (result f64) (call $cbrt (local.get $x)))\n");
  }
  if (has_undefined_function("pow", 3) || has_undefined_function("powf", 4) ||
      has_undefined_function("powl", 4)) {
    wasm_emitf(2, "(func $pow (param $x f64) (param $y f64) (result f64)\n");
    wasm_emitf(4, "(local $n i32)\n");
    wasm_emitf(4, "(local $i i32)\n");
    wasm_emitf(4, "(local $r f64)\n");
    wasm_emitf(4, "(if (f64.eq (local.get $y) (f64.const 0.5)) (then (return (call $sqrt (local.get $x)))))\n");
    wasm_emitf(4, "(local.set $n (i32.trunc_f64_s (local.get $y)))\n");
    wasm_emitf(4, "(local.set $r (f64.const 1))\n");
    wasm_emitf(4, "(block $done (loop $loop\n");
    wasm_emitf(6, "(if (i32.ge_s (local.get $i) (local.get $n)) (then (br $done)))\n");
    wasm_emitf(6, "(local.set $r (f64.mul (local.get $r) (local.get $x)))\n");
    wasm_emitf(6, "(local.set $i (i32.add (local.get $i) (i32.const 1)))\n");
    wasm_emitf(6, "(br $loop)\n");
    wasm_emitf(4, "))\n");
    wasm_emitf(4, "(local.get $r)\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("powf", 4)) {
    wasm_emitf(2, "(func $powf (param $x f32) (param $y f32) (result f32)\n");
    wasm_emitf(4, "(f32.demote_f64 (call $pow (f64.promote_f32 (local.get $x)) (f64.promote_f32 (local.get $y))))\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("powl", 4)) {
    wasm_emitf(2, "(func $powl (param $x f64) (param $y f64) (result f64) (call $pow (local.get $x) (local.get $y)))\n");
  }
  if (has_undefined_function("fabs", 4)) {
    wasm_emitf(2, "(func $fabs (param $x f64) (result f64) (f64.abs (local.get $x)))\n");
  }
  if (has_undefined_function("fabsf", 5)) {
    wasm_emitf(2, "(func $fabsf (param $x f32) (result f32) (f32.abs (local.get $x)))\n");
  }
  if (has_undefined_function("fabsl", 5)) {
    wasm_emitf(2, "(func $fabsl (param $x f64) (result f64) (f64.abs (local.get $x)))\n");
  }
  if (has_undefined_function("floor", 5)) {
    wasm_emitf(2, "(func $floor (param $x f64) (result f64) (f64.floor (local.get $x)))\n");
  }
  if (has_undefined_function("ceil", 4)) {
    wasm_emitf(2, "(func $ceil (param $x f64) (result f64) (f64.ceil (local.get $x)))\n");
  }
  if (has_undefined_function("round", 5) || has_undefined_function("roundl", 6)) {
    wasm_emitf(2, "(func $round (param $x f64) (result f64)\n");
    wasm_emitf(4, "(if (result f64) (f64.lt (local.get $x) (f64.const 0))\n");
    wasm_emitf(6, "(then (f64.ceil (f64.sub (local.get $x) (f64.const 0.5))))\n");
    wasm_emitf(6, "(else (f64.floor (f64.add (local.get $x) (f64.const 0.5))))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("trunc", 5)) {
    wasm_emitf(2, "(func $trunc (param $x f64) (result f64) (f64.trunc (local.get $x)))\n");
  }
  if (has_undefined_function("floorf", 6)) {
    wasm_emitf(2, "(func $floorf (param $x f32) (result f32) (f32.floor (local.get $x)))\n");
  }
  if (has_undefined_function("ceilf", 5)) {
    wasm_emitf(2, "(func $ceilf (param $x f32) (result f32) (f32.ceil (local.get $x)))\n");
  }
  if (has_undefined_function("roundf", 6)) {
    wasm_emitf(2, "(func $roundf (param $x f32) (result f32)\n");
    wasm_emitf(4, "(if (result f32) (f32.lt (local.get $x) (f32.const 0))\n");
    wasm_emitf(6, "(then (f32.ceil (f32.sub (local.get $x) (f32.const 0.5))))\n");
    wasm_emitf(6, "(else (f32.floor (f32.add (local.get $x) (f32.const 0.5))))\n");
    wasm_emitf(4, ")\n");
    wasm_emitf(2, ")\n");
  }
  if (has_undefined_function("floorl", 6)) {
    wasm_emitf(2, "(func $floorl (param $x f64) (result f64) (f64.floor (local.get $x)))\n");
  }
  if (has_undefined_function("ceill", 5)) {
    wasm_emitf(2, "(func $ceill (param $x f64) (result f64) (f64.ceil (local.get $x)))\n");
  }
  if (has_undefined_function("roundl", 6)) {
    wasm_emitf(2, "(func $roundl (param $x f64) (result f64) (call $round (local.get $x)))\n");
  }
  if (has_undefined_function("truncf", 6)) {
    wasm_emitf(2, "(func $truncf (param $x f32) (result f32) (f32.trunc (local.get $x)))\n");
  }
  if (has_undefined_function("truncl", 6)) {
    wasm_emitf(2, "(func $truncl (param $x f64) (result f64) (f64.trunc (local.get $x)))\n");
  }
  if (has_undefined_function("expf", 4)) {
    wasm_emitf(2, "(func $expf (param $x f32) (result f32) (f32.demote_f64 (call $exp (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("expl", 4)) {
    wasm_emitf(2, "(func $expl (param $x f64) (result f64) (call $exp (local.get $x)))\n");
  }
  if (has_undefined_function("logf", 4)) {
    wasm_emitf(2, "(func $logf (param $x f32) (result f32) (f32.demote_f64 (call $log (f64.promote_f32 (local.get $x)))))\n");
  }
  if (has_undefined_function("logl", 4)) {
    wasm_emitf(2, "(func $logl (param $x f64) (result f64) (call $log (local.get $x)))\n");
  }
  if (has_undefined_function("hypot", 5)) {
    wasm_emitf(2, "(func $hypot (param $x f64) (param $y f64) (result f64) (call $sqrt (f64.add (f64.mul (local.get $x) (local.get $x)) (f64.mul (local.get $y) (local.get $y)))))\n");
  }
  if (has_undefined_function("hypotf", 6)) {
    wasm_emitf(2, "(func $hypotf (param $x f32) (param $y f32) (result f32) (f32.demote_f64 (call $hypot (f64.promote_f32 (local.get $x)) (f64.promote_f32 (local.get $y)))))\n");
  }
  if (has_undefined_function("hypotl", 6)) {
    wasm_emitf(2, "(func $hypotl (param $x f64) (param $y f64) (result f64) (call $hypot (local.get $x) (local.get $y)))\n");
  }
  if (has_undefined_function("fmin", 4)) {
    wasm_emitf(2, "(func $fmin (param $x f64) (param $y f64) (result f64) (f64.min (local.get $x) (local.get $y)))\n");
  }
  if (has_undefined_function("fminf", 5)) {
    wasm_emitf(2, "(func $fminf (param $x f32) (param $y f32) (result f32) (f32.min (local.get $x) (local.get $y)))\n");
  }
  if (has_undefined_function("fminl", 5)) {
    wasm_emitf(2, "(func $fminl (param $x f64) (param $y f64) (result f64) (f64.min (local.get $x) (local.get $y)))\n");
  }
  if (has_undefined_function("fmax", 4)) {
    wasm_emitf(2, "(func $fmax (param $x f64) (param $y f64) (result f64) (f64.max (local.get $x) (local.get $y)))\n");
  }
  if (has_undefined_function("fmaxf", 5)) {
    wasm_emitf(2, "(func $fmaxf (param $x f32) (param $y f32) (result f32) (f32.max (local.get $x) (local.get $y)))\n");
  }
  if (has_undefined_function("fmaxl", 5)) {
    wasm_emitf(2, "(func $fmaxl (param $x f64) (param $y f64) (result f64) (f64.max (local.get $x) (local.get $y)))\n");
  }
}

void wasm32_module_end(void) {
  emit_minimal_libc_stubs();
  emit_function_table();
  cg_emitf(")\n");
}
