/*
 * IR の人間可読フォーマットへのダンプ。
 *
 * フォーマット例:
 *   func @f -> i32 {
 *   .L0:
 *     v0 = load_imm i32 1
 *     v1 = load_imm i32 2
 *     v2 = add i32 v0, v1
 *     ret v2
 *   }
 *
 *   global @g i32 = 42
 *   global @arr i32[3] = {10, 20, 30}
 */

#include "ir.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

typedef struct {
  char *buf;
  size_t buf_size;
  size_t pos;
  FILE *fp;       /* fp != NULL なら fp に出力、それ以外は buf に書き込む */
} ir_print_sink_t;

static void sink_printf(ir_print_sink_t *s, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  if (s->fp) {
    vfprintf(s->fp, fmt, ap);
  } else if (s->buf && s->pos < s->buf_size) {
    int n = vsnprintf(s->buf + s->pos, s->buf_size - s->pos, fmt, ap);
    if (n > 0) {
      size_t add = (size_t)n;
      if (s->pos + add >= s->buf_size) {
        s->pos = s->buf_size - 1;
        s->buf[s->buf_size - 1] = '\0';
      } else {
        s->pos += add;
      }
    }
  }
  va_end(ap);
}

static void print_val(ir_print_sink_t *s, ir_val_t v) {
  if (v.id == IR_VAL_NONE) {
    sink_printf(s, "_");
    return;
  }
  if (v.id == IR_VAL_IMM) {
    if (v.type == IR_TY_F32 || v.type == IR_TY_F64) {
      sink_printf(s, "%g", v.fp_imm);
    } else {
      sink_printf(s, "%lld", v.imm);
    }
    return;
  }
  sink_printf(s, "v%d", v.id);
}

static void print_inst(ir_print_sink_t *s, ir_inst_t *i) {
  const char *opname = ir_op_name(i->op);
  /* ラベル定義は字下げなし */
  if (i->op == IR_LABEL) {
    sink_printf(s, ".L%d:\n", i->label_id);
    return;
  }
  sink_printf(s, "  ");

  /* dst = があるもの */
  int has_dst = (i->dst.id != IR_VAL_NONE);
  if (has_dst) {
    print_val(s, i->dst);
    sink_printf(s, " = %s %s ", opname, ir_type_name(i->dst.type));
  } else {
    sink_printf(s, "%s ", opname);
  }

  switch (i->op) {
    case IR_LOAD_IMM:
    case IR_LOAD_FP_IMM:
      print_val(s, i->src1);
      break;
    case IR_LOAD_STR:
    case IR_LOAD_SYM:
    case IR_LOAD_TLV_ADDR:
      sink_printf(s, "@%.*s", i->sym_len, i->sym ? i->sym : "");
      break;
    case IR_LOAD:
      print_val(s, i->src1);
      break;
    case IR_STORE:
      /* store dst.type src2, [src1]  (src2 を *src1 に書く) */
      sink_printf(s, "%s ", ir_type_name(i->src2.type));
      print_val(s, i->src2);
      sink_printf(s, ", [");
      print_val(s, i->src1);
      sink_printf(s, "]");
      break;
    case IR_ALLOCA:
      sink_printf(s, "size=%d align=%d", i->alloca_size, i->alloca_align);
      break;
    case IR_BR:
      sink_printf(s, ".L%d", i->label_id);
      break;
    case IR_BR_COND:
      print_val(s, i->src1);
      sink_printf(s, ", .L%d, .L%d", i->label_id, i->else_label_id);
      break;
    case IR_RET:
      if (i->src1.id != IR_VAL_NONE) print_val(s, i->src1);
      break;
    case IR_CALL:
      sink_printf(s, "@%.*s(", i->sym_len, i->sym ? i->sym : "");
      for (int k = 0; k < i->nargs; k++) {
        if (k > 0) sink_printf(s, ", ");
        print_val(s, i->args[k]);
      }
      sink_printf(s, ")");
      break;
    case IR_PARAM:
      sink_printf(s, "#%lld", i->src1.imm);
      break;
    case IR_NEG:
    case IR_NOT:
    case IR_FNEG:
    case IR_ZEXT:
    case IR_SEXT:
    case IR_TRUNC:
    case IR_F2I:
    case IR_I2F:
    case IR_F2F:
    case IR_LEA:
      print_val(s, i->src1);
      if (i->src2.id != IR_VAL_NONE) {
        sink_printf(s, ", ");
        print_val(s, i->src2);
      }
      break;
    case IR_VA_ARG_AREA:
      /* no operand */
      break;
    default:
      /* 二項演算 (ADD/SUB/.../FADD/...) と比較 (EQ/.../FEQ/...) */
      print_val(s, i->src1);
      sink_printf(s, ", ");
      print_val(s, i->src2);
      break;
  }
  sink_printf(s, "\n");
}

static void print_block(ir_print_sink_t *s, ir_block_t *b) {
  sink_printf(s, ".L%d:\n", b->id);
  for (ir_inst_t *i = b->head; i; i = i->next) {
    print_inst(s, i);
  }
}

static void print_func(ir_print_sink_t *s, ir_func_t *f) {
  sink_printf(s, "func @%.*s -> %s", f->name_len, f->name ? f->name : "", ir_type_name(f->ret_type));
  if (f->is_variadic) sink_printf(s, " variadic(fixed=%d)", f->nargs_fixed);
  sink_printf(s, " {\n");
  for (ir_block_t *b = f->entry; b; b = b->next) {
    print_block(s, b);
  }
  sink_printf(s, "}\n");
}

static void print_global(ir_print_sink_t *s, ir_global_t *g) {
  sink_printf(s, "global @%.*s", g->name_len, g->name ? g->name : "");
  if (g->is_array) {
    sink_printf(s, " [%d]", g->byte_size / (g->elem_size > 0 ? g->elem_size : 1));
  }
  if (g->init_count > 0) {
    sink_printf(s, " = {");
    for (int i = 0; i < g->init_count; i++) {
      if (i > 0) sink_printf(s, ", ");
      sink_printf(s, "%lld", g->init_values[i]);
    }
    sink_printf(s, "}");
  } else if (g->init_symbol) {
    sink_printf(s, " = @%.*s", g->init_symbol_len, g->init_symbol);
  } else {
    sink_printf(s, " = %lld", g->init_val);
  }
  sink_printf(s, "\n");
}

static void print_module(ir_print_sink_t *s, ir_module_t *m) {
  if (!m) return;
  for (ir_global_t *g = m->globals; g; g = g->next) {
    print_global(s, g);
  }
  if (m->globals) sink_printf(s, "\n");
  for (ir_func_t *f = m->funcs; f; f = f->next) {
    print_func(s, f);
    if (f->next) sink_printf(s, "\n");
  }
}

void ir_print_module(ir_module_t *m) {
  ir_print_sink_t s = { .buf = NULL, .buf_size = 0, .pos = 0, .fp = stdout };
  print_module(&s, m);
}

size_t ir_print_module_to_buf(ir_module_t *m, char *buf, size_t buf_size) {
  if (!buf || buf_size == 0) return 0;
  ir_print_sink_t s = { .buf = buf, .buf_size = buf_size, .pos = 0, .fp = NULL };
  buf[0] = '\0';
  print_module(&s, m);
  return s.pos;
}
