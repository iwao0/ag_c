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
} wasm_func_table_ctx_t;

static wasm_data_ctx_t g_data = {WASM_STATIC_BASE, NULL, 0, 0};
static wasm_func_table_ctx_t g_func_table = {NULL, 0, 0};

static void wasm_emit_indent(int spaces) {
  static const char k_spaces[] = "                                ";
  int chunk = (int)sizeof(k_spaces) - 1;
  while (spaces > chunk) {
    cg_emitf("%s", k_spaces);
    spaces -= chunk;
  }
  if (spaces > 0) cg_emitf("%.*s", spaces, k_spaces);
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
  if (lit->char_width != TK_CHAR_WIDTH_CHAR) wasm_unsupported_msg("wide string literal in Wasm backend");
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
  if (!psx_ctx_is_function_defined(name, name_len)) {
    wasm_unsupported_msg("external function pointer in Wasm backend");
  }
  return intern_function_table_ref(name, name_len);
}

static void emit_function_table(void) {
  if (g_func_table.ref_count <= 0) return;
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
  return f && f->ret_struct_size > 0;
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

static void emit_call(wasm_func_ctx_t *ctx, ir_inst_t *i, int indent) {
  if (i->ret_complex_half != 0 || i->is_variadic_call) {
    wasm_unsupported_op(i->op);
  }
  if (i->callee.id != IR_VAL_NONE) {
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
    if (result_unused && !callee_name) {
      wasm_unsupported_msg("indirect non-void unused-result function call in Wasm backend");
    }
    const char *ret_ty = returns_void ? NULL : wasm_any_type_or_unsupported(i->dst.type);
    if (result_unused) wasm_emitf(indent, "(drop ");
    else if (!returns_void) wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
    else wasm_emitf(indent, "");
    cg_emitf("(call_indirect");
    if (returns_aggregate) cg_emitf(" (param i32)");
    for (int a = 0; a < i->nargs; a++) {
      ir_type_t arg_ty = effective_val_type(ctx, i->args[a]);
      if (!is_fp_type(arg_ty)) arg_ty = IR_TY_I64;
      cg_emitf(" (param %s)", wasm_any_type_or_unsupported(arg_ty));
    }
    if (!returns_void) cg_emitf(" (result %s)", ret_ty);
    if (returns_aggregate) {
      cg_emitf(" ");
      emit_val_expr_as(ctx, i->ret_struct_area, IR_TY_PTR);
    }
    for (int a = 0; a < i->nargs; a++) {
      ir_type_t arg_ty = effective_val_type(ctx, i->args[a]);
      if (!is_fp_type(arg_ty)) arg_ty = IR_TY_I64;
      cg_emitf(" ");
      emit_val_expr_as(ctx, i->args[a], arg_ty);
    }
    cg_emitf(" ");
    emit_val_expr_as(ctx, i->callee, IR_TY_I32);
    cg_emitf(")");
    if (result_unused || !returns_void) cg_emitf(")");
    cg_emitf("\n");
    return;
  }
  if (!i->sym) wasm_unsupported_op(i->op);
  if (!psx_ctx_has_function_name(i->sym, i->sym_len)) {
    wasm_unsupported_msg("external or implicitly declared function call in Wasm backend");
  }
  int returns_void = psx_ctx_is_function_ret_void(i->sym, i->sym_len);
  if (i->ret_struct_size > 0) {
    wasm_emitf(indent, "(call $%.*s ", i->sym_len, i->sym);
    emit_val_expr_as(ctx, i->ret_struct_area, IR_TY_PTR);
  } else if (!returns_void && i->dst.id >= 0 && i->dst.type != IR_TY_VOID) {
    wasm_emitf(indent, "(local.set $v%d (call $%.*s", i->dst.id, i->sym_len, i->sym);
  } else {
    wasm_emitf(indent, "(call $%.*s", i->sym_len, i->sym);
  }
  for (int a = 0; a < i->nargs; a++) {
    ir_type_t arg_ty = effective_val_type(ctx, i->args[a]);
    if (psx_ctx_get_function_param_int_size(i->sym, i->sym_len, a) == 8) arg_ty = IR_TY_I64;
    cg_emitf(" ");
    emit_val_expr_as(ctx, i->args[a], arg_ty);
  }
  cg_emitf(")");
  if (i->ret_struct_size == 0 && !returns_void && i->dst.id >= 0 && i->dst.type != IR_TY_VOID) {
    cg_emitf(")");
  }
  cg_emitf("\n");
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
  cg_emitf("(module\n");
  wasm_emitf(2, "(memory (export \"memory\") 1)\n");
  wasm_emitf(2, "(global $__stack_pointer (mut i32) (i32.const %d))\n", WASM_STACK_BASE);
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
  if (lit->char_width != TK_CHAR_WIDTH_CHAR) wasm_unsupported_msg("wide string literal in Wasm backend");
  int addr = data_addr_for_string_label(lit->label);
  if (addr < 0) wasm_unsupported_msg("string literal label in Wasm backend");
  wasm_emitf(2, "(data (i32.const %d) \"", addr);
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

static void emit_global_struct_members_data_rec(token_kind_t tk, char *tn, int tl,
                                                global_var_t *gv, int *val_idx, int base_addr) {
  int n_members = psx_ctx_get_tag_member_count(tk, tn, tl);
  for (int m = 0; m < n_members && *val_idx < gv->init_count; m++) {
    tag_member_info_t mi = {0};
    if (!psx_ctx_get_tag_member_info(tk, tn, tl, m, &mi)) break;
    if (mi.bit_width > 0) {
      emit_global_bitfield_unit_data(tk, tn, tl, &m, gv, val_idx, base_addr);
      continue;
    }
    if (mi.array_len > 0) {
      if ((mi.tag_kind == TK_STRUCT || mi.tag_kind == TK_UNION) && !mi.is_tag_pointer) {
        for (int k = 0; k < mi.array_len && *val_idx < gv->init_count; k++) {
          if (mi.tag_kind == TK_UNION) {
            emit_global_nested_union_data(mi.tag_kind, mi.tag_name, mi.tag_len, gv, val_idx,
                                          base_addr + mi.offset + k * mi.type_size);
          } else {
            emit_global_struct_members_data_rec(mi.tag_kind, mi.tag_name, mi.tag_len, gv, val_idx,
                                                base_addr + mi.offset + k * mi.type_size);
          }
        }
      } else {
        for (int k = 0; k < mi.array_len && *val_idx < gv->init_count; k++) {
          int slot = (*val_idx)++;
          emit_global_init_member_data(gv, slot, base_addr + mi.offset + k * mi.type_size, &mi);
        }
      }
      continue;
    }
    if (mi.tag_kind == TK_STRUCT && !mi.is_tag_pointer) {
      emit_global_struct_members_data_rec(mi.tag_kind, mi.tag_name, mi.tag_len, gv, val_idx,
                                          base_addr + mi.offset);
      continue;
    }
    if (mi.tag_kind == TK_UNION && !mi.is_tag_pointer) {
      emit_global_nested_union_data(mi.tag_kind, mi.tag_name, mi.tag_len, gv, val_idx,
                                    base_addr + mi.offset);
      continue;
    }
    int slot = (*val_idx)++;
    emit_global_init_member_data(gv, slot, base_addr + mi.offset, &mi);
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
    return;
  }
  if ((mi.tag_kind == TK_STRUCT || mi.tag_kind == TK_UNION) && !mi.is_tag_pointer) {
    if (mi.tag_kind == TK_STRUCT) {
      emit_global_struct_members_data_rec(mi.tag_kind, mi.tag_name, mi.tag_len, gv, val_idx, addr);
    } else {
      emit_global_nested_union_data(mi.tag_kind, mi.tag_name, mi.tag_len, gv, val_idx, addr);
    }
    return;
  }
  emit_global_union_scalar_data(gv, val_idx, addr, &mi);
}

static void emit_global_union_data(global_var_t *gv, int addr) {
  if (gv->init_count <= 0) return;
  int val_idx = 0;
  emit_global_union_element_data(gv, &val_idx, addr);
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
    int elem_size = gv->deref_size > 0 ? gv->deref_size : 0;
    int total = elem_size > 0 ? (int)gv->type_size / elem_size : 0;
    for (int e = 0; e < total && val_idx < gv->init_count; e++) {
      emit_global_struct_members_data_rec(gv->tag_kind, gv->tag_name, gv->tag_len, gv, &val_idx,
                                          addr + e * elem_size);
    }
  } else {
    emit_global_struct_members_data_rec(gv->tag_kind, gv->tag_name, gv->tag_len, gv, &val_idx, addr);
  }
}

static void emit_global_data(global_var_t *gv, void *user) {
  (void)user;
  if (gv->is_extern_decl) return;
  if (gv->is_thread_local) {
    wasm_unsupported_msg("global initializer in Wasm backend");
  }
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
    if (!gv->has_init && size > 8) return;
    if (size != 1 && size != 2 && size != 4 && size != 8) wasm_unsupported_msg("global size in Wasm backend");
    emit_i32_data_bytes(addr, gv->has_init ? gv->init_val : 0, size);
  }
}

void wasm32_emit_data_segments(void) {
  ps_iter_string_literals(emit_string_literal_data, NULL);
  ps_iter_globals(emit_global_data, NULL);
}

void wasm32_module_end(void) {
  emit_function_table();
  cg_emitf(")\n");
}
