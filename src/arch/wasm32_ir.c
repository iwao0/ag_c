#include "wasm32_ir.h"
#include "arm64_apple_emit.h"
#include "../diag/diag.h"
#include "../parser/parser_public.h"
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
} wasm_alloca_slot_t;

typedef struct {
  ir_func_t *f;
  wasm_alloca_slot_t *allocas;
  int alloca_count;
  int alloca_cap;
  int frame_size;
  int *vreg_type_seen;
  ir_type_t *vreg_types;
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

static wasm_data_ctx_t g_data = {WASM_STATIC_BASE, NULL, 0, 0};

static void wasm_emit_indent(int spaces) {
  for (int i = 0; i < spaces; i++) cg_emitf(" ");
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
    default:
      return NULL;
  }
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

static void collect_vreg_type(wasm_func_ctx_t *ctx, ir_val_t v) {
  if (v.id < 0 || v.id >= ctx->f->next_vreg_id) return;
  if (!wasm_type(v.type)) return;
  ctx->vreg_type_seen[v.id] = 1;
  ctx->vreg_types[v.id] = v.type;
}

static void collect_inst_vregs(wasm_func_ctx_t *ctx, ir_inst_t *i) {
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
  if (align > 16) wasm_unsupported_op(i->op);
  ctx->frame_size = align_to(ctx->frame_size, align);
  ctx->allocas[ctx->alloca_count].vreg = i->dst.id;
  ctx->allocas[ctx->alloca_count].offset = ctx->frame_size;
  ctx->allocas[ctx->alloca_count].size = i->alloca_size;
  ctx->alloca_count++;
  ctx->frame_size += i->alloca_size;
}

static int find_alloca_offset(wasm_func_ctx_t *ctx, int vreg) {
  for (int i = 0; i < ctx->alloca_count; i++) {
    if (ctx->allocas[i].vreg == vreg) return ctx->allocas[i].offset;
  }
  return -1;
}

static void analyze_func(wasm_func_ctx_t *ctx) {
  ctx->vreg_type_seen = calloc((size_t)ctx->f->next_vreg_id, sizeof(int));
  ctx->vreg_types = calloc((size_t)ctx->f->next_vreg_id, sizeof(ir_type_t));
  if (!ctx->vreg_type_seen || !ctx->vreg_types) {
    diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
  }
  for (ir_block_t *b = ctx->f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      collect_inst_vregs(ctx, i);
      if (i->op == IR_ALLOCA) add_alloca_slot(ctx, i);
      if (i->op == IR_LABEL || i->op == IR_BR || i->op == IR_BR_COND) ctx->has_control_flow = 1;
    }
  }
  ctx->frame_size = align_to(ctx->frame_size, 16);
}

static void emit_val_expr(ir_val_t v) {
  const char *ty = wasm_type(v.type);
  if (!ty) wasm_unsupported_msg("non-integer Wasm value type");
  if (v.id == IR_VAL_IMM) {
    cg_emitf("(%s.const %lld)", ty, v.imm);
  } else if (v.id >= 0) {
    cg_emitf("(local.get $v%d)", v.id);
  } else {
    wasm_unsupported_msg("missing Wasm value");
  }
}

static void emit_val_expr_as(ir_val_t v, ir_type_t target) {
  if (target == IR_TY_PTR) target = IR_TY_I32;
  ir_type_t src = (v.type == IR_TY_PTR) ? IR_TY_I32 : v.type;
  if (src == target) {
    emit_val_expr(v);
    return;
  }
  if (target == IR_TY_I64 && src != IR_TY_I64) {
    cg_emitf("(i64.extend_i32_s ");
    emit_val_expr(v);
    cg_emitf(")");
    return;
  }
  if (target != IR_TY_I64 && src == IR_TY_I64) {
    cg_emitf("(i32.wrap_i64 ");
    emit_val_expr(v);
    cg_emitf(")");
    return;
  }
  emit_val_expr(v);
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

static void emit_load(ir_inst_t *i, int indent) {
  const char *op = NULL;
  switch (i->dst.type) {
    case IR_TY_I8:  op = i->is_unsigned_load ? "i32.load8_u" : "i32.load8_s"; break;
    case IR_TY_I16: op = i->is_unsigned_load ? "i32.load16_u" : "i32.load16_s"; break;
    case IR_TY_I32:
    case IR_TY_PTR: op = "i32.load"; break;
    case IR_TY_I64: op = "i64.load"; break;
    default: wasm_unsupported_op(i->op);
  }
  wasm_emitf(indent, "(local.set $v%d (%s ", i->dst.id, op);
  emit_val_expr(i->src1);
  cg_emitf("))\n");
}

static void emit_store(ir_inst_t *i, int indent) {
  const char *op = NULL;
  switch (i->src2.type) {
    case IR_TY_I8:  op = "i32.store8"; break;
    case IR_TY_I16: op = "i32.store16"; break;
    case IR_TY_I32:
    case IR_TY_PTR: op = "i32.store"; break;
    case IR_TY_I64: op = "i64.store"; break;
    default: wasm_unsupported_op(i->op);
  }
  wasm_emitf(indent, "(%s ", op);
  emit_val_expr(i->src1);
  cg_emitf(" ");
  emit_val_expr(i->src2);
  cg_emitf(")\n");
}

static void emit_call(ir_inst_t *i, int indent) {
  if (i->callee.id != IR_VAL_NONE || i->ret_struct_size > 0 || i->ret_complex_half != 0 ||
      i->is_variadic_call) {
    wasm_unsupported_op(i->op);
  }
  if (!i->sym) wasm_unsupported_op(i->op);
  if (i->dst.id >= 0 && i->dst.type != IR_TY_VOID) {
    wasm_emitf(indent, "(local.set $v%d (call $%.*s", i->dst.id, i->sym_len, i->sym);
  } else {
    wasm_emitf(indent, "(call $%.*s", i->sym_len, i->sym);
  }
  for (int a = 0; a < i->nargs; a++) {
    cg_emitf(" ");
    emit_val_expr(i->args[a]);
  }
  cg_emitf(")");
  if (i->dst.id >= 0 && i->dst.type != IR_TY_VOID) cg_emitf(")");
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
      if (i->src1.id != IR_VAL_IMM || i->src1.imm < 0) wasm_unsupported_op(i->op);
      wasm_emitf(indent, "(local.set $v%d (local.get $p%d))\n", i->dst.id, (int)i->src1.imm);
      return;
    case IR_ALLOCA: {
      int off = find_alloca_offset(ctx, i->dst.id);
      if (off < 0) wasm_unsupported_op(i->op);
      wasm_emitf(indent, "(local.set $v%d (i32.add (local.get $fp) (i32.const %d)))\n", i->dst.id, off);
      return;
    }
    case IR_LOAD_IMM:
      wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
      emit_val_expr(i->src1);
      cg_emitf(")\n");
      return;
    case IR_LOAD_STR: {
      int addr = data_addr_for_string_label(i->sym);
      if (addr < 0) wasm_unsupported_op(i->op);
      wasm_emitf(indent, "(local.set $v%d (i32.const %d))\n", i->dst.id, addr);
      return;
    }
    case IR_LOAD_SYM: {
      int addr = data_addr_for_global(i->sym, i->sym_len);
      wasm_emitf(indent, "(local.set $v%d (i32.const %d))\n", i->dst.id, addr);
      return;
    }
    case IR_ZEXT:
    case IR_SEXT:
    case IR_TRUNC:
      wasm_emitf(indent, "(local.set $v%d ", i->dst.id);
      if (i->dst.type == IR_TY_I64 && i->src1.type != IR_TY_I64) {
        cg_emitf("(%s ", i->op == IR_ZEXT ? "i64.extend_i32_u" : "i64.extend_i32_s");
        emit_val_expr(i->src1);
        cg_emitf(")");
      } else if (i->dst.type != IR_TY_I64 && i->src1.type == IR_TY_I64) {
        cg_emitf("(i32.wrap_i64 ");
        emit_val_expr(i->src1);
        cg_emitf(")");
      } else {
        emit_val_expr(i->src1);
      }
      cg_emitf(")\n");
      return;
    case IR_LOAD:
      emit_load(i, indent);
      return;
    case IR_STORE:
      emit_store(i, indent);
      return;
    case IR_LEA:
      wasm_emitf(indent, "(local.set $v%d (i32.add ", i->dst.id);
      emit_val_expr(i->src1);
      cg_emitf(" ");
      emit_val_expr(i->src2);
      cg_emitf("))\n");
      return;
    case IR_NEG:
      wasm_emitf(indent, "(local.set $v%d (%s.sub (%s.const 0) ", i->dst.id,
               i->dst.type == IR_TY_I64 ? "i64" : "i32",
               i->dst.type == IR_TY_I64 ? "i64" : "i32");
      emit_val_expr(i->src1);
      cg_emitf("))\n");
      return;
    case IR_NOT:
      wasm_emitf(indent, "(local.set $v%d (%s.xor ", i->dst.id, i->dst.type == IR_TY_I64 ? "i64" : "i32");
      emit_val_expr(i->src1);
      cg_emitf(" (%s.const -1)))\n", i->dst.type == IR_TY_I64 ? "i64" : "i32");
      return;
    case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: case IR_UDIV:
    case IR_MOD: case IR_UMOD: case IR_AND: case IR_OR: case IR_XOR:
    case IR_SHL: case IR_SHR: case IR_LSR:
    case IR_EQ: case IR_NE: case IR_LT: case IR_LE: case IR_ULT: case IR_ULE: {
      int is_cmp = (i->op == IR_EQ || i->op == IR_NE || i->op == IR_LT || i->op == IR_LE ||
                    i->op == IR_ULT || i->op == IR_ULE);
      ir_type_t op_ty = is_cmp ? i->src1.type : i->dst.type;
      const char *op = wasm_binop(i->op, op_ty);
      if (!op) wasm_unsupported_op(i->op);
      wasm_emitf(indent, "(local.set $v%d (%s ", i->dst.id, op);
      emit_val_expr_as(i->src1, op_ty);
      cg_emitf(" ");
      emit_val_expr_as(i->src2, (op_ty == IR_TY_I64 &&
                                 (i->op == IR_SHL || i->op == IR_SHR || i->op == IR_LSR))
                                    ? IR_TY_I32 : op_ty);
      cg_emitf("))\n");
      return;
    }
    case IR_CALL:
      emit_call(i, indent);
      return;
    case IR_BR:
      if (!dispatch_mode) wasm_unsupported_op(i->op);
      wasm_emitf(indent, "(local.set $pc (i32.const %d))\n", i->label_id);
      wasm_emitf(indent, "(br $dispatch)\n");
      return;
    case IR_BR_COND:
      if (!dispatch_mode) wasm_unsupported_op(i->op);
      wasm_emitf(indent, "(if ");
      emit_val_expr_as(i->src1, IR_TY_I32);
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
        emit_val_expr(i->src1);
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
  int max_idx = -1;
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      if (i->op == IR_PARAM && i->src1.id == IR_VAL_IMM && i->src1.imm >= 0 &&
          i->src1.imm > max_idx) {
        max_idx = (int)i->src1.imm;
      }
    }
  }
  return max_idx + 1;
}

static ir_type_t func_param_type(ir_func_t *f, int idx) {
  for (ir_block_t *b = f->entry; b; b = b->next) {
    for (ir_inst_t *i = b->head; i; i = i->next) {
      if (i->op == IR_PARAM && i->src1.id == IR_VAL_IMM && i->src1.imm == idx) return i->dst.type;
    }
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
  if (f->ret_type != IR_TY_VOID) {
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
  free(ctx.vreg_type_seen);
  free(ctx.vreg_types);
}

void wasm32_module_begin(void) {
  g_data.next_data_off = WASM_STATIC_BASE;
  g_data.symbol_count = 0;
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

static void emit_global_data(global_var_t *gv, void *user) {
  (void)user;
  if (gv->is_extern_decl) return;
  if (gv->is_thread_local || gv->fp_kind != TK_FLOAT_KIND_NONE || gv->tag_kind != TK_EOF ||
      gv->init_symbol || gv->init_count > 0) {
    wasm_unsupported_msg("global initializer in Wasm backend");
  }
  int addr = data_addr_for_global(gv->name, gv->name_len);
  int size = gv->type_size > 0 ? gv->type_size : 4;
  if (size != 1 && size != 2 && size != 4 && size != 8) wasm_unsupported_msg("global size in Wasm backend");
  emit_i32_data_bytes(addr, gv->has_init ? gv->init_val : 0, size);
}

void wasm32_emit_data_segments(void) {
  ps_iter_string_literals(emit_string_literal_data, NULL);
  ps_iter_globals(emit_global_data, NULL);
}

void wasm32_module_end(void) {
  cg_emitf(")\n");
}
