#include "../codegen_backend.h"
#include "../diag/diag.h"
#include "../parser/internal/arena.h"
#include "../parser/parser.h"
#include "../tokenizer/escape.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>

// ラベルの一意番号を生成するカウンタ
static int label_count = 0;
// TCO: 現在コード生成中の関数情報
static char *cg_current_funcname = NULL;
static int cg_current_funcname_len = 0;
static int cg_current_nargs = 0;
static int *break_labels;
static int *continue_labels;
static int control_cap = 0;
static int control_depth = 0;
static gen_output_line_fn gen_output_cb;
static void *gen_output_user_data;
// 浮動小数点定数用ラベルカウンタ

// ピープホール最適化: 直前の出力行をバッファリングし、
// 冗長な str/ldr ペアを除去する
#define PEEPHOLE_BUF_SIZE 128
static char peephole_buf[PEEPHOLE_BUF_SIZE];
static size_t peephole_len = 0;
static int peephole_has_line = 0;

static const char STR_X0_PUSH[] = "  str x0, [sp, #-16]!\n";
static const char LDR_X0_POP[]  = "  ldr x0, [sp], #16\n";
static const char LDR_X1_POP[]  = "  ldr x1, [sp], #16\n";
static const char MOV_X1_X0[]   = "  mov x1, x0\n";


static void cg_raw_emit(const char *line, size_t len) {
  if (gen_output_cb) {
    gen_output_cb(line, len, gen_output_user_data);
  } else {
    fwrite(line, 1, len, stdout);
  }
}

static void cg_flush_peephole(void) {
  if (!peephole_has_line) return;
  cg_raw_emit(peephole_buf, peephole_len);
  peephole_has_line = 0;
  peephole_len = 0;
}

static void cg_emit_line(const char *line, size_t len) {
  if (peephole_has_line
      && peephole_len == sizeof(STR_X0_PUSH) - 1
      && memcmp(peephole_buf, STR_X0_PUSH, peephole_len) == 0) {
    // str x0, [sp, #-16]! の直後に ldr x0, [sp], #16 → 両方除去
    if (len == sizeof(LDR_X0_POP) - 1
        && memcmp(line, LDR_X0_POP, len) == 0) {
      peephole_has_line = 0;
      peephole_len = 0;
      return;
    }
    // str x0, [sp, #-16]! の直後に ldr x1, [sp], #16 → mov x1, x0 に置換
    if (len == sizeof(LDR_X1_POP) - 1
        && memcmp(line, LDR_X1_POP, len) == 0) {
      peephole_has_line = 0;
      peephole_len = 0;
      cg_raw_emit(MOV_X1_X0, sizeof(MOV_X1_X0) - 1);
      return;
    }
  }
  // バッファにある前の行をフラッシュ
  cg_flush_peephole();
  // 現在の行をバッファに保存
  if (len < PEEPHOLE_BUF_SIZE) {
    memcpy(peephole_buf, line, len);
    peephole_len = len;
    peephole_has_line = 1;
  } else {
    cg_raw_emit(line, len);
  }
}

static void cg_emitf(const char *fmt, ...) {
  char stack_buf[256];
  va_list ap;
  va_start(ap, fmt);
  va_list ap2;
  va_copy(ap2, ap);
  int need_i = vsnprintf(stack_buf, sizeof(stack_buf), fmt, ap2);
  va_end(ap2);
  if (need_i < 0) {
    va_end(ap);
    diag_emit_internalf(DIAG_ERR_CODEGEN_OUTPUT_FAILED, "%s",
                        diag_message_for(DIAG_ERR_CODEGEN_OUTPUT_FAILED));
  }
  size_t need = (size_t)need_i;
  char *buf = stack_buf;
  char *heap_buf = NULL;
  if (need >= sizeof(stack_buf)) {
    heap_buf = malloc(need + 1);
    if (!heap_buf) {
      va_end(ap);
      diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
    }
    vsnprintf(heap_buf, need + 1, fmt, ap);
    buf = heap_buf;
  }
  va_end(ap);
  size_t start = 0;
  for (size_t i = 0; i < need; i++) {
    if (buf[i] == '\n') {
      cg_emit_line(buf + start, i - start + 1);
      start = i + 1;
    }
  }
  if (start < need) {
    cg_emit_line(buf + start, need - start);
  }
  free(heap_buf);
}


// AArch64 即値ロード: 16bit に収まらない値は movz/movk シーケンスで生成する
static void cg_emit_mov_imm(const char *reg, long long val) {
  uint64_t uval = (uint64_t)val;
  // 16bit に収まる場合（符号付き -65536..65535 もカバー）
  if (val >= 0 && val <= 0xFFFF) {
    cg_emitf("  mov %s, #%lld\n", reg, val);
    return;
  }
  // 負の値で mov で扱える範囲（movn 相当）
  if (val < 0 && val >= -0x10000) {
    cg_emitf("  mov %s, #%lld\n", reg, val);
    return;
  }
  // 一般ケース: movz + movk シーケンス
  int first = 1;
  for (int shift = 0; shift < 64; shift += 16) {
    uint64_t chunk = (uval >> shift) & 0xFFFF;
    if (chunk == 0 && !first) continue;
    if (first) {
      cg_emitf("  movz %s, #%llu, lsl #%d\n", reg, (unsigned long long)chunk, shift);
      first = 0;
    } else {
      cg_emitf("  movk %s, #%llu, lsl #%d\n", reg, (unsigned long long)chunk, shift);
    }
  }
}

void gen_set_output_callback(gen_output_line_fn cb, void *user_data) {
  // コールバック切り替え前にバッファをフラッシュ
  cg_flush_peephole();
  gen_output_cb = cb;
  gen_output_user_data = user_data;
}

// Apple Silicon (ARM64) 向けのアセンブリコード生成

// 26個の変数(a-z) * 8バイト = 208バイト + フレームポインタ/リンクレジスタ用16バイト = 224
// 16バイトアラインメントに合わせる → 224
#define STACK_SIZE 1024
// >16B 構造体戻り値: x8 (indirect result pointer) の退避スロット
// フレーム末尾の固定位置 [x29 + STACK_SIZE - 16]
#define X8_SAVE_OFFSET (STACK_SIZE - 16)

static node_mem_t *as_mem(node_t *node) { return (node_mem_t *)node; }
static node_lvar_t *as_lvar(node_t *node) { return (node_lvar_t *)node; }
static node_num_t *as_num(node_t *node) { return (node_num_t *)node; }
static node_block_t *as_block(node_t *node) { return (node_block_t *)node; }
static node_func_t *as_func(node_t *node) { return (node_func_t *)node; }
static node_ctrl_t *as_ctrl(node_t *node) { return (node_ctrl_t *)node; }
static node_string_t *as_string(node_t *node) { return (node_string_t *)node; }
static node_funcref_t *as_funcref(node_t *node) { return (node_funcref_t *)node; }
static node_case_t *as_case(node_t *node) { return (node_case_t *)node; }
static node_default_t *as_default(node_t *node) { return (node_default_t *)node; }
static node_jump_t *as_jump(node_t *node) { return (node_jump_t *)node; }

static void ensure_control_capacity(int need) {
  if (control_cap >= need) return;
  int new_cap = control_cap ? control_cap : 16;
  while (new_cap < need) new_cap *= 2;
  int *new_break_labels = malloc(sizeof(int) * (size_t)new_cap);
  int *new_continue_labels = malloc(sizeof(int) * (size_t)new_cap);
  if (!new_break_labels || !new_continue_labels) {
    free(new_break_labels);
    free(new_continue_labels);
    diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
  }
  if (control_depth > 0) {
    memcpy(new_break_labels, break_labels, sizeof(int) * (size_t)control_depth);
    memcpy(new_continue_labels, continue_labels, sizeof(int) * (size_t)control_depth);
  }
  free(break_labels);
  free(continue_labels);
  break_labels = new_break_labels;
  continue_labels = new_continue_labels;
  control_cap = new_cap;
}

static void push_control_labels(int break_lbl, int continue_lbl) {
  ensure_control_capacity(control_depth + 1);
  break_labels[control_depth] = break_lbl;
  continue_labels[control_depth] = continue_lbl;
  control_depth++;
}

static void pop_control_labels(void) {
  if (control_depth > 0) control_depth--;
}

static int current_break_label(void) {
  if (control_depth == 0) return -1;
  return break_labels[control_depth - 1];
}

static int current_continue_label(void) {
  for (int i = control_depth - 1; i >= 0; i--) {
    if (continue_labels[i] >= 0) return continue_labels[i];
  }
  return -1;
}

typedef struct {
  node_case_t **cases;
  int case_count;
  int case_cap;
  node_default_t *default_node;
} switch_collect_t;

typedef struct label_map_t label_map_t;
struct label_map_t {
  label_map_t *next;
  char *name;
  int len;
  int id;
};

static label_map_t *label_map_head = NULL;

static void clear_label_map(void) { label_map_head = NULL; }

static void add_label_map(char *name, int len, int id) {
  label_map_t *m = arena_alloc(sizeof(label_map_t));
  m->name = name;
  m->len = len;
  m->id = id;
  m->next = label_map_head;
  label_map_head = m;
}

static int find_label_id(char *name, int len) {
  for (label_map_t *m = label_map_head; m; m = m->next) {
    if (m->len == len && strncmp(m->name, name, (size_t)len) == 0) return m->id;
  }
  return -1;
}

static void switch_collect_add_case(switch_collect_t *sc, node_case_t *c) {
  if (sc->case_count >= sc->case_cap) {
    sc->case_cap = sc->case_cap ? sc->case_cap * 2 : 8;
    node_case_t **new_cases = realloc(sc->cases, sizeof(node_case_t *) * (size_t)sc->case_cap);
    if (!new_cases) {
      diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
    }
    sc->cases = new_cases;
  }
  sc->cases[sc->case_count++] = c;
}

static void collect_switch_labels(node_t *node, switch_collect_t *sc) {
  if (!node) return;
  if (node->kind == ND_SWITCH) return; // ネストしたswitchは別スコープ

  if (node->kind == ND_CASE) {
    node_case_t *c = as_case(node);
    c->label_id = label_count++;
    switch_collect_add_case(sc, c);
    collect_switch_labels(c->base.rhs, sc);
    return;
  }
  if (node->kind == ND_DEFAULT) {
    if (sc->default_node) {
      diag_emit_internalf(DIAG_ERR_CODEGEN_INVALID_CONTROL_FLOW, "%s (switch default)",
                          diag_message_for(DIAG_ERR_CODEGEN_INVALID_CONTROL_FLOW));
    }
    node_default_t *d = as_default(node);
    d->label_id = label_count++;
    sc->default_node = d;
    collect_switch_labels(d->base.rhs, sc);
    return;
  }
  if (node->kind == ND_BLOCK) {
    node_block_t *b = as_block(node);
    for (int i = 0; b->body[i]; i++) {
      collect_switch_labels(b->body[i], sc);
    }
    return;
  }
  if (node->kind == ND_IF) {
    node_ctrl_t *c = as_ctrl(node);
    collect_switch_labels(c->base.rhs, sc);
    collect_switch_labels(c->els, sc);
    return;
  }
  if (node->kind == ND_WHILE || node->kind == ND_DO_WHILE || node->kind == ND_FOR) {
    collect_switch_labels(node->rhs, sc);
    return;
  }
  if (node->kind == ND_LABEL) {
    collect_switch_labels(node->rhs, sc);
    return;
  }
}

static void collect_goto_labels(node_t *node) {
  if (!node) return;
  if (node->kind == ND_LABEL) {
    node_jump_t *j = as_jump(node);
    if (j->label_id <= 0) {
      j->label_id = label_count++;
      add_label_map(j->name, j->name_len, j->label_id);
    }
    collect_goto_labels(j->base.rhs);
    return;
  }
  if (node->kind == ND_BLOCK) {
    node_block_t *b = as_block(node);
    for (int i = 0; b->body[i]; i++) collect_goto_labels(b->body[i]);
    return;
  }
  if (node->kind == ND_IF) {
    node_ctrl_t *c = as_ctrl(node);
    collect_goto_labels(c->base.rhs);
    collect_goto_labels(c->els);
    return;
  }
  if (node->kind == ND_WHILE || node->kind == ND_DO_WHILE || node->kind == ND_FOR || node->kind == ND_SWITCH) {
    collect_goto_labels(node->rhs);
    return;
  }
  if (node->kind == ND_CASE || node->kind == ND_DEFAULT) {
    collect_goto_labels(node->rhs);
    return;
  }
}

static void gen_stmt(node_t *node);

static void gen_switch_body(node_t *node) {
  if (!node) return;
  if (node->kind == ND_BLOCK) {
    node_block_t *b = as_block(node);
    for (int i = 0; b->body[i]; i++) {
      gen_switch_body(b->body[i]);
    }
    return;
  }
  if (node->kind == ND_CASE) {
    node_case_t *c = as_case(node);
    cg_emitf(".Lcase%d:\n", c->label_id);
    gen_stmt(c->base.rhs);
    return;
  }
  if (node->kind == ND_DEFAULT) {
    node_default_t *d = as_default(node);
    cg_emitf(".Ldefault%d:\n", d->label_id);
    gen_stmt(d->base.rhs);
    return;
  }
  gen_stmt(node);
}

static int is_main_func(const node_func_t *fn) {
  return fn->funcname_len == 4 && strncmp(fn->funcname, "main", 4) == 0;
}

static int is_printf_func(const node_func_t *fn) {
  return fn->funcname_len == 6 && strncmp(fn->funcname, "printf", 6) == 0;
}

void gen_main_prologue(void) {
  // 関数定義で生成するため空にする（互換性維持）
}

void gen_main_epilogue(void) {
  // 関数定義で生成するため空にする（互換性維持）
}

static void gen_expr(node_t *node);
static void gen_stmt(node_t *node);

static int cg_can_use_fp_immediate_zero(double fval, tk_float_kind_t fp_kind) {
  if (fp_kind == TK_FLOAT_KIND_FLOAT) {
    float v = (float)fval;
    return v == 0.0f && !signbit(v);
  }
  if (fp_kind >= TK_FLOAT_KIND_DOUBLE) {
    double v = fval;
    return v == 0.0 && !signbit(v);
  }
  return 0;
}

static int cg_emit_fp_literal_immediate_if_possible(const node_num_t *num, tk_float_kind_t fp_kind) {
  if (!cg_can_use_fp_immediate_zero(num->fval, fp_kind)) return 0;
  if (fp_kind == TK_FLOAT_KIND_FLOAT) {
    cg_emitf("  fmov s0, #0.0\n");
    cg_emitf("  str s0, [sp, #-16]!\n");
  } else {
    cg_emitf("  fmov d0, #0.0\n");
    cg_emitf("  str d0, [sp, #-16]!\n");
  }
  return 1;
}

static void gen_load_x0_from_addr(int type_size) {
  if (type_size == 1)
    cg_emitf("  ldrb w0, [x1]\n");
  else if (type_size == 2)
    cg_emitf("  ldrh w0, [x1]\n");
  else if (type_size == 4)
    cg_emitf("  ldr w0, [x1]\n");
  else
    cg_emitf("  ldr x0, [x1]\n");
}

static void gen_store_x0_to_addr(int type_size) {
  if (type_size == 1)
    cg_emitf("  strb w0, [x1]\n");
  else if (type_size == 2)
    cg_emitf("  strh w0, [x1]\n");
  else if (type_size == 4)
    cg_emitf("  str w0, [x1]\n");
  else
    cg_emitf("  str x0, [x1]\n");
}

// レジスタ名取得ヘルパー
static const char *freg(int fp_kind, int reg_idx) {
  if (fp_kind == TK_FLOAT_KIND_FLOAT) return (reg_idx == 0) ? "s0" : "s1";
  return (reg_idx == 0) ? "d0" : "d1";
}

// child_type の値をスタックからポップし、target_type にキャストして FPU レジスタ (s0/d0 または s1/d1) にロードする
static void gen_pop_fpu(int target_type, int child_type, int reg_idx) {
  const char *s = (reg_idx == 0) ? "s0" : "s1";
  const char *d = (reg_idx == 0) ? "d0" : "d1";
  
  if (child_type == TK_FLOAT_KIND_FLOAT) { // float がプッシュされている
    cg_emitf("  ldr %s, [sp], #16\n", s);
    if (target_type >= TK_FLOAT_KIND_DOUBLE) {
      cg_emitf("  fcvt %s, %s\n", d, s); // float -> double
    }
  } else if (child_type >= TK_FLOAT_KIND_DOUBLE) { // double/long double(現状lowering) がプッシュされている
    cg_emitf("  ldr %s, [sp], #16\n", d);
    if (target_type == TK_FLOAT_KIND_FLOAT) {
      cg_emitf("  fcvt %s, %s\n", s, d); // double -> float
    }
  } else {                      // int がプッシュされている
    cg_emitf("  ldr x%d, [sp], #16\n", reg_idx);
    if (target_type == TK_FLOAT_KIND_FLOAT) {
      cg_emitf("  scvtf %s, x%d\n", s, reg_idx); // int -> float
    } else {
      cg_emitf("  scvtf %s, x%d\n", d, reg_idx); // int -> double
    }
  }
}

// 左辺値（変数のアドレス）をスタックへプッシュする
static void gen_lval(node_t *node) {
  if (node->kind == ND_DEREF) {
    // *p = x の左辺値: p の値（アドレス）をプッシュ
    gen_expr(node->lhs);
    return;
  }
  if (node->kind == ND_GVAR) {
    node_gvar_t *gv = (node_gvar_t *)node;
    if (gv->is_thread_local) {
      cg_emitf("  adrp x0, _%.*s@TLVPPAGE\n", gv->name_len, gv->name);
      cg_emitf("  ldr x0, [x0, _%.*s@TLVPPAGEOFF]\n", gv->name_len, gv->name);
      cg_emitf("  ldr x8, [x0]\n");
      cg_emitf("  blr x8\n");
      // resolver returns variable address in x0
    } else {
      cg_emitf("  adrp x0, _%.*s@PAGE\n", gv->name_len, gv->name);
      cg_emitf("  add x0, x0, _%.*s@PAGEOFF\n", gv->name_len, gv->name);
    }
    cg_emitf("  str x0, [sp, #-16]!\n");
    return;
  }
  if (node->kind != ND_LVAR) {
    diag_emit_internalf(DIAG_ERR_CODEGEN_INVALID_LVALUE, "%s (assignment target)",
                        diag_message_for(DIAG_ERR_CODEGEN_INVALID_LVALUE));
  }
  cg_emitf("  add x0, x29, #%d\n", 16 + as_lvar(node)->offset);
  cg_emitf("  str x0, [sp, #-16]!\n");
}

// レジスタ割り付け最適化のネスト防止フラグ
// gen_expr_to_reg 実行中に gen_expr がフォールバック経由で再入した場合、
// gen_expr 内部の register allocation が x9-x15 を上書きするのを防ぐ
static int cg_inside_regalloc = 0;

// レジスタ割り付け最適化: 式の部分木に関数呼び出しが含まれるか判定
// (関数呼び出しは x9-x15 を破壊するため、含まれる場合はスタックモードにフォールバック)
static int cg_has_funcall(node_t *node) {
  if (!node) return 0;
  if (node->kind == ND_FUNCALL) return 1;
  if (node->kind == ND_TERNARY) {
    node_ctrl_t *c = as_ctrl(node);
    return cg_has_funcall(c->base.lhs) || cg_has_funcall(c->base.rhs) || cg_has_funcall(c->els);
  }
  return cg_has_funcall(node->lhs) || cg_has_funcall(node->rhs);
}

// レジスタ割り付け最適化: 式の結果を x(9+depth) に直接生成する
// 利用可能レジスタ: x9-x15 (7本)。depth+1 >= 7 の場合はスタックにフォールバック。
// 整数式専用 (FPU 演算は対象外)。部分木に funcall がないことが前提。
#define CG_REGALLOC_DEPTH_MAX 7

static void gen_expr_to_reg(node_t *node, int depth) {
  int reg = 9 + depth;

  switch (node->kind) {
  case ND_NUM: {
    char regbuf[8];
    snprintf(regbuf, sizeof(regbuf), "x%d", reg);
    cg_emit_mov_imm(regbuf, as_num(node)->val);
    return;
  }

  case ND_LVAR: {
    if (node->fp_kind) break; // FPU はフォールバック
    if (node->is_atomic) break; // atomic は ldar 必要 → フォールバック
    int off = 16 + as_lvar(node)->offset;
    int ts = as_lvar(node)->mem.type_size;
    int uns = as_lvar(node)->mem.is_unsigned;
    if (ts == 1)      cg_emitf(uns ? "  ldrb w%d, [x29, #%d]\n" : "  ldrsb x%d, [x29, #%d]\n", reg, off);
    else if (ts == 2) cg_emitf(uns ? "  ldrh w%d, [x29, #%d]\n" : "  ldrsh x%d, [x29, #%d]\n", reg, off);
    else if (ts == 4) cg_emitf(uns ? "  ldr w%d, [x29, #%d]\n" : "  ldrsw x%d, [x29, #%d]\n", reg, off);
    else              cg_emitf("  ldr x%d, [x29, #%d]\n", reg, off);
    return;
  }

  case ND_GVAR: {
    node_gvar_t *gv = (node_gvar_t *)node;
    if (gv->is_thread_local) break; // TLS uses blr -> fallback to stack mode
    int ts = gv->mem.type_size;
    int uns = gv->mem.is_unsigned;
    cg_emitf("  adrp x%d, _%.*s@PAGE\n", reg, gv->name_len, gv->name);
    cg_emitf("  add x%d, x%d, _%.*s@PAGEOFF\n", reg, reg, gv->name_len, gv->name);
    if (ts == 1)      cg_emitf(uns ? "  ldrb w%d, [x%d]\n" : "  ldrsb x%d, [x%d]\n", reg, reg);
    else if (ts == 2) cg_emitf(uns ? "  ldrh w%d, [x%d]\n" : "  ldrsh x%d, [x%d]\n", reg, reg);
    else if (ts == 4) cg_emitf(uns ? "  ldr w%d, [x%d]\n" : "  ldrsw x%d, [x%d]\n", reg, reg);
    else              cg_emitf("  ldr x%d, [x%d]\n", reg, reg);
    return;
  }

  case ND_DEREF: {
    if (as_mem(node)->bit_width > 0) break; // ビットフィールドはフォールバック
    gen_expr_to_reg(node->lhs, depth);
    int ts = as_mem(node)->type_size;
    int uns = as_mem(node)->is_unsigned;
    if (ts == 1)      cg_emitf(uns ? "  ldrb w%d, [x%d]\n" : "  ldrsb x%d, [x%d]\n", reg, reg);
    else if (ts == 2) cg_emitf(uns ? "  ldrh w%d, [x%d]\n" : "  ldrsh x%d, [x%d]\n", reg, reg);
    else if (ts == 4) cg_emitf("  ldr w%d, [x%d]\n", reg, reg);
    else              cg_emitf("  ldr x%d, [x%d]\n", reg, reg);
    return;
  }

  case ND_STRING: {
    node_string_t *s = as_string(node);
    cg_emitf("  adrp x%d, %s@PAGE\n", reg, s->string_label);
    cg_emitf("  add x%d, x%d, %s@PAGEOFF\n", reg, reg, s->string_label);
    return;
  }

  case ND_ADDR:
    if (node->lhs && node->lhs->kind == ND_LVAR) {
      cg_emitf("  add x%d, x29, #%d\n", reg, 16 + as_lvar(node->lhs)->offset);
      return;
    }
    if (node->lhs && node->lhs->kind == ND_GVAR) {
      node_gvar_t *gv = (node_gvar_t *)node->lhs;
      if (gv->is_thread_local) break; // TLS uses blr -> fallback
      cg_emitf("  adrp x%d, _%.*s@PAGE\n", reg, gv->name_len, gv->name);
      cg_emitf("  add x%d, x%d, _%.*s@PAGEOFF\n", reg, reg, gv->name_len, gv->name);
      return;
    }
    break; // 複雑なケースはフォールバック

  case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD:
  case ND_EQ: case ND_NE: case ND_LT: case ND_LE:
  case ND_BITAND: case ND_BITXOR: case ND_BITOR:
  case ND_SHL: case ND_SHR: {
    if (node->fp_kind) break; // FPU はフォールバック
    // 比較演算の場合、オペランドが浮動小数点なら FPU パスへフォールバック
    if (node->kind == ND_EQ || node->kind == ND_NE || node->kind == ND_LT || node->kind == ND_LE) {
      if ((node->lhs && node->lhs->fp_kind) || (node->rhs && node->rhs->fp_kind)) break;
    }
    if (depth + 1 >= CG_REGALLOC_DEPTH_MAX) break; // レジスタ不足

    gen_expr_to_reg(node->lhs, depth);
    gen_expr_to_reg(node->rhs, depth + 1);
    int r0 = reg, r1 = reg + 1;

    switch (node->kind) {
    case ND_ADD: cg_emitf("  add x%d, x%d, x%d\n", r0, r0, r1); break;
    case ND_SUB: cg_emitf("  sub x%d, x%d, x%d\n", r0, r0, r1); break;
    case ND_MUL: cg_emitf("  mul x%d, x%d, x%d\n", r0, r0, r1); break;
    case ND_DIV: cg_emitf(node->is_unsigned ? "  udiv x%d, x%d, x%d\n" : "  sdiv x%d, x%d, x%d\n", r0, r0, r1); break;
    case ND_MOD:
      cg_emitf(node->is_unsigned ? "  udiv x0, x%d, x%d\n" : "  sdiv x0, x%d, x%d\n", r0, r1);
      cg_emitf("  msub x%d, x0, x%d, x%d\n", r0, r1, r0);
      break;
    case ND_EQ:  cg_emitf("  cmp x%d, x%d\n", r0, r1); cg_emitf("  cset x%d, eq\n", r0); break;
    case ND_NE:  cg_emitf("  cmp x%d, x%d\n", r0, r1); cg_emitf("  cset x%d, ne\n", r0); break;
    case ND_LT:  cg_emitf("  cmp x%d, x%d\n", r0, r1); cg_emitf(node->is_unsigned ? "  cset x%d, lo\n" : "  cset x%d, lt\n", r0); break;
    case ND_LE:  cg_emitf("  cmp x%d, x%d\n", r0, r1); cg_emitf(node->is_unsigned ? "  cset x%d, ls\n" : "  cset x%d, le\n", r0); break;
    case ND_BITAND: cg_emitf("  and x%d, x%d, x%d\n", r0, r0, r1); break;
    case ND_BITXOR: cg_emitf("  eor x%d, x%d, x%d\n", r0, r0, r1); break;
    case ND_BITOR:  cg_emitf("  orr x%d, x%d, x%d\n", r0, r0, r1); break;
    case ND_SHL: cg_emitf("  lsl x%d, x%d, x%d\n", r0, r0, r1); break;
    case ND_SHR: cg_emitf(node->is_unsigned ? "  lsr x%d, x%d, x%d\n" : "  asr x%d, x%d, x%d\n", r0, r0, r1); break;
    default: break;
    }
    return;
  }

  default:
    break;
  }

  // フォールバック: スタックモード経由でレジスタにロード
  gen_expr(node);
  cg_emitf("  ldr x%d, [sp], #16\n", reg);
}

static void gen_expr(node_t *node) {
  switch (node->kind) {
  case ND_NUM:
    if (node->fp_kind) {
      node_num_t *num = as_num(node);
      if (cg_emit_fp_literal_immediate_if_possible(num, node->fp_kind)) return;
      // 浮動小数点リテラルをデータセクションからロード
      cg_emitf("  adrp x0, .LCF%d@PAGE\n", num->fval_id);
      cg_emitf("  add x0, x0, .LCF%d@PAGEOFF\n", num->fval_id);
      if (node->fp_kind == TK_FLOAT_KIND_FLOAT) {
        cg_emitf("  ldr s0, [x0]\n");
        cg_emitf("  str s0, [sp, #-16]!\n");
      } else {
        cg_emitf("  ldr d0, [x0]\n");
        cg_emitf("  str d0, [sp, #-16]!\n");
      }
    } else {
      cg_emit_mov_imm("x0", as_num(node)->val);
      cg_emitf("  str x0, [sp, #-16]!\n");
    }
    return;
  case ND_LVAR:
    if (node->is_complex) {
      // _Complex: 実部+虚部を16Bスロットとしてプッシュ
      int coff = 16 + as_lvar(node)->offset;
      if (node->fp_kind == TK_FLOAT_KIND_FLOAT) {
        // _Complex float: 4B実部 + 4B虚部 → 8Bにパック
        cg_emitf("  ldr s0, [x29, #%d]\n", coff);     // 実部
        cg_emitf("  ldr s1, [x29, #%d]\n", coff + 4); // 虚部
        cg_emitf("  stp s0, s1, [sp, #-16]!\n");
      } else {
        // _Complex double: 8B実部 + 8B虚部 → 16B
        cg_emitf("  ldr d0, [x29, #%d]\n", coff);
        cg_emitf("  ldr d1, [x29, #%d]\n", coff + 8);
        cg_emitf("  stp d0, d1, [sp, #-16]!\n");
      }
      return;
    }
    gen_lval(node);
    cg_emitf("  ldr x0, [sp], #16\n");
    if (node->fp_kind == TK_FLOAT_KIND_FLOAT) {
      cg_emitf("  ldr s0, [x0]\n");
      cg_emitf("  str s0, [sp, #-16]!\n");
    } else if (node->fp_kind >= TK_FLOAT_KIND_DOUBLE) {
      cg_emitf("  ldr d0, [x0]\n");
      cg_emitf("  str d0, [sp, #-16]!\n");
    } else if (node->is_atomic) {
      // _Atomic: load-acquire
      if (as_lvar(node)->mem.type_size == 1)
        cg_emitf("  ldarb w0, [x0]\n");
      else if (as_lvar(node)->mem.type_size == 2)
        cg_emitf("  ldarh w0, [x0]\n");
      else if (as_lvar(node)->mem.type_size == 4)
        cg_emitf("  ldar w0, [x0]\n");
      else
        cg_emitf("  ldar x0, [x0]\n");
      cg_emitf("  str x0, [sp, #-16]!\n");
    } else {
      if (as_lvar(node)->mem.type_size == 1)
        cg_emitf(as_lvar(node)->mem.is_unsigned ? "  ldrb w0, [x0]\n" : "  ldrsb x0, [x0]\n");
      else if (as_lvar(node)->mem.type_size == 2)
        cg_emitf(as_lvar(node)->mem.is_unsigned ? "  ldrh w0, [x0]\n" : "  ldrsh x0, [x0]\n");
      else if (as_lvar(node)->mem.type_size == 4)
        cg_emitf(as_lvar(node)->mem.is_unsigned ? "  ldr w0, [x0]\n" : "  ldrsw x0, [x0]\n");
      else
        cg_emitf("  ldr x0, [x0]\n");
      cg_emitf("  str x0, [sp, #-16]!\n");
    }
    return;
  case ND_GVAR: {
    node_gvar_t *gv_node = (node_gvar_t *)node;
    if (gv_node->is_thread_local) {
      // TLS access: resolve address via TLV descriptor
      cg_emitf("  adrp x0, _%.*s@TLVPPAGE\n", gv_node->name_len, gv_node->name);
      cg_emitf("  ldr x0, [x0, _%.*s@TLVPPAGEOFF]\n", gv_node->name_len, gv_node->name);
      cg_emitf("  ldr x8, [x0]\n");
      cg_emitf("  blr x8\n");
    } else {
      gen_lval(node);
      cg_emitf("  ldr x0, [sp], #16\n");
    }
    int ts = gv_node->mem.type_size;
    int is_uns = gv_node->mem.is_unsigned;
    if (ts == 1)
      cg_emitf(is_uns ? "  ldrb w0, [x0]\n" : "  ldrsb x0, [x0]\n");
    else if (ts == 2)
      cg_emitf(is_uns ? "  ldrh w0, [x0]\n" : "  ldrsh x0, [x0]\n");
    else if (ts == 4)
      cg_emitf(is_uns ? "  ldr w0, [x0]\n" : "  ldrsw x0, [x0]\n");
    else
      cg_emitf("  ldr x0, [x0]\n");
    cg_emitf("  str x0, [sp, #-16]!\n");
    return;
  }
  case ND_DEREF:
    gen_expr(node->lhs);
    cg_emitf("  ldr x0, [sp], #16\n");
    if (as_mem(node)->type_size == 1)
      cg_emitf(as_mem(node)->is_unsigned ? "  ldrb w0, [x0]\n" : "  ldrsb x0, [x0]\n");
    else if (as_mem(node)->type_size == 2)
      cg_emitf(as_mem(node)->is_unsigned ? "  ldrh w0, [x0]\n" : "  ldrsh x0, [x0]\n");
    else if (as_mem(node)->type_size == 4)
      cg_emitf(as_mem(node)->is_unsigned ? "  ldr w0, [x0]\n" : "  ldrsw x0, [x0]\n");
    else
      cg_emitf("  ldr x0, [x0]\n");
    // ビットフィールド抽出
    if (as_mem(node)->bit_width > 0) {
      int lsb = as_mem(node)->bit_offset;
      int width = as_mem(node)->bit_width;
      if (as_mem(node)->bit_is_signed)
        cg_emitf("  sbfx x0, x0, #%d, #%d\n", lsb, width);
      else
        cg_emitf("  ubfx x0, x0, #%d, #%d\n", lsb, width);
    }
    cg_emitf("  str x0, [sp, #-16]!\n");
    return;
  case ND_ADDR:
    gen_lval(node->lhs);
    return;
  case ND_STRING:
    // 文字列ラベルのアドレスをロード
    cg_emitf("  adrp x0, %s@PAGE\n", as_string(node)->string_label);
    cg_emitf("  add x0, x0, %s@PAGEOFF\n", as_string(node)->string_label);
    cg_emitf("  str x0, [sp, #-16]!\n");
    return;
  case ND_VLA_ALLOC: {
    // VLA動的スタック確保
    // フレームレイアウト: [x29+16+off]=baseptr, [x29+16+off+8]=bytesize
    // 2D runtime: [x29+16+rsf]=row_stride (rsf = vla_row_stride_frame_off != 0)
    int off = as_mem(node)->type_size; // ベースポインタのフレームオフセット
    int rsf = as_mem(node)->vla_row_stride_frame_off; // 行ストライドのフレームオフセット (0=なし)
    if (rsf) {
      // 2D VLA runtime inner: rhs=row_stride_expr(m*elem), lhs=outer_count(n)
      gen_expr(node->rhs);                            // x0 = row_stride = m * elem_size
      cg_emitf("  ldr x0, [sp], #16\n");
      cg_emitf("  str x0, [x29, #%d]\n", 16 + rsf);  // row_stride を保存
      gen_expr(node->lhs);                            // x0 = outer_count = n
      cg_emitf("  ldr x0, [sp], #16\n");
      cg_emitf("  ldr x1, [x29, #%d]\n", 16 + rsf);  // x1 = row_stride
      cg_emitf("  mul x0, x0, x1\n");                // x0 = n * row_stride = total byte_size
    } else {
      gen_expr(node->lhs);                            // x0 = total byte_size
      cg_emitf("  ldr x0, [sp], #16\n");
    }
    cg_emitf("  str x0, [x29, #%d]\n", 16 + off + 8); // バイトサイズを保存 (sizeof用)
    cg_emitf("  add x0, x0, #15\n");  // 16バイトアライン
    cg_emitf("  bic x0, x0, #15\n");  // 下位4ビットをクリア (= & ~15)
    cg_emitf("  sub sp, sp, x0\n");   // alloca
    cg_emitf("  mov x0, sp\n");       // spはstr源オペランドに使えないため一時レジスタ経由
    cg_emitf("  str x0, [x29, #%d]\n", 16 + off); // ベースポインタを保存
    cg_emitf("  mov x0, #0\n");
    cg_emitf("  str x0, [sp, #-16]!\n");
    return;
  }
  case ND_FUNCREF:
    cg_emitf("  adrp x0, _%.*s@PAGE\n", as_funcref(node)->funcname_len, as_funcref(node)->funcname);
    cg_emitf("  add x0, x0, _%.*s@PAGEOFF\n", as_funcref(node)->funcname_len, as_funcref(node)->funcname);
    cg_emitf("  str x0, [sp, #-16]!\n");
    return;
  case ND_ASSIGN:
    if (node->rhs && node->rhs->ret_struct_size > 16 && node->rhs->kind == ND_FUNCALL) {
      // >16B 構造体戻り値: lhs アドレスを x8 にセットしてから funcall
      // callee が x8 の指す先に直接書き込む
      gen_lval(node->lhs);
      cg_emitf("  ldr x8, [sp], #16\n");  // lhs アドレスを x8 に
      gen_expr(node->rhs);                // funcall 実行（x8 は callee 内で使われる）
      cg_emitf("  add sp, sp, #16\n");    // funcall がプッシュした dummy x0 を破棄
      // 式の結果としてアドレスをプッシュ（使われないが整合性のため）
      cg_emitf("  str x8, [sp, #-16]!\n");
      return;
    }
    gen_lval(node->lhs);
    gen_expr(node->rhs);
    if (node->rhs && node->rhs->ret_struct_size > 8 && node->rhs->ret_struct_size <= 16) {
      // 9-16B 構造体戻り値: スタックに x0(低),x1(高) の2スロットが積まれている
      cg_emitf("  ldr x1, [sp], #16\n");  // x0: 低8B
      cg_emitf("  ldr x2, [sp], #16\n");  // x1: 高8B
      cg_emitf("  ldr x0, [sp], #16\n");  // lhs アドレス
      cg_emitf("  str x1, [x0]\n");       // 低8B をストア
      cg_emitf("  str x2, [x0, #8]\n");   // 高8B をストア
      cg_emitf("  str x1, [sp, #-16]!\n"); // 式の結果値（低8B）
      return;
    }
    if (node->is_complex && node->fp_kind >= TK_FLOAT_KIND_DOUBLE) {
      // _Complex double 代入
      if (node->rhs && node->rhs->is_complex) {
        // _Complex = _Complex: rhs は16Bスロット(実部d0+虚部d1)
        cg_emitf("  ldp d0, d1, [sp], #16\n"); // rhs 実部,虚部
        cg_emitf("  ldr x0, [sp], #16\n");     // lhs アドレス
        cg_emitf("  str d0, [x0]\n");
        cg_emitf("  str d1, [x0, #8]\n");
      } else {
        // スカラ → _Complex: 実部に代入、虚部は0
        gen_pop_fpu(TK_FLOAT_KIND_DOUBLE, node->rhs->fp_kind, 0);
        cg_emitf("  ldr x0, [sp], #16\n");
        cg_emitf("  str d0, [x0]\n");
        cg_emitf("  movi d1, #0\n");
        cg_emitf("  str d1, [x0, #8]\n");
      }
      cg_emitf("  stp d0, d1, [sp, #-16]!\n"); // 式の結果（16B _Complex）
      return;
    }
    if (node->is_complex && node->fp_kind == TK_FLOAT_KIND_FLOAT) {
      // _Complex float 代入
      if (node->rhs && node->rhs->is_complex) {
        cg_emitf("  ldp s0, s1, [sp], #16\n");
        cg_emitf("  ldr x0, [sp], #16\n");
        cg_emitf("  str s0, [x0]\n");
        cg_emitf("  str s1, [x0, #4]\n");
      } else {
        gen_pop_fpu(TK_FLOAT_KIND_FLOAT, node->rhs->fp_kind, 0);
        cg_emitf("  ldr x0, [sp], #16\n");
        cg_emitf("  str s0, [x0]\n");
        cg_emitf("  movi d1, #0\n");
        cg_emitf("  str s1, [x0, #4]\n");
      }
      cg_emitf("  stp s0, s1, [sp, #-16]!\n");
      return;
    }
    if (node->fp_kind == TK_FLOAT_KIND_FLOAT) {
      // float 代入
      gen_pop_fpu(TK_FLOAT_KIND_FLOAT, node->rhs->fp_kind, 0); // s0 に rhs をロード
      cg_emitf("  ldr x0, [sp], #16\n"); // lhs アドレス
      cg_emitf("  str s0, [x0]\n");
      cg_emitf("  str s0, [sp, #-16]!\n");
    } else if (node->fp_kind >= TK_FLOAT_KIND_DOUBLE) {
      // double/long double(現状lowering) 代入
      gen_pop_fpu(TK_FLOAT_KIND_DOUBLE, node->rhs->fp_kind, 0); // d0 に rhs をロード
      cg_emitf("  ldr x0, [sp], #16\n"); // lhs アドレス
      cg_emitf("  str d0, [x0]\n");
      cg_emitf("  str d0, [sp, #-16]!\n");
    } else {
      cg_emitf("  ldr x1, [sp], #16\n");
      cg_emitf("  ldr x0, [sp], #16\n");
      // ビットフィールド代入: read-modify-write with bfi
      if (node->lhs && node->lhs->kind == ND_DEREF && as_mem(node->lhs)->bit_width > 0) {
        int lsb = as_mem(node->lhs)->bit_offset;
        int width = as_mem(node->lhs)->bit_width;
        int sz = as_mem(node->lhs)->type_size;
        if (sz == 1)
          cg_emitf("  ldrb w2, [x0]\n");
        else if (sz == 2)
          cg_emitf("  ldrh w2, [x0]\n");
        else
          cg_emitf("  ldr w2, [x0]\n");
        cg_emitf("  bfi w2, w1, #%d, #%d\n", lsb, width);
        if (sz == 1)
          cg_emitf("  strb w2, [x0]\n");
        else if (sz == 2)
          cg_emitf("  strh w2, [x0]\n");
        else
          cg_emitf("  str w2, [x0]\n");
      } else if (node->is_atomic) {
        // _Atomic: store-release
        if (as_mem(node)->type_size == 1)
          cg_emitf("  stlrb w1, [x0]\n");
        else if (as_mem(node)->type_size == 2)
          cg_emitf("  stlrh w1, [x0]\n");
        else if (as_mem(node)->type_size == 4)
          cg_emitf("  stlr w1, [x0]\n");
        else
          cg_emitf("  stlr x1, [x0]\n");
      } else if (as_mem(node)->type_size == 1)
        cg_emitf("  strb w1, [x0]\n");
      else if (as_mem(node)->type_size == 2)
        cg_emitf("  strh w1, [x0]\n");
      else if (as_mem(node)->type_size == 4)
        cg_emitf("  str w1, [x0]\n");
      else
        cg_emitf("  str x1, [x0]\n");
      cg_emitf("  str x1, [sp, #-16]!\n");
    }
    return;
  case ND_COMMA:
    gen_expr(node->lhs);
    // lhs の値は式値としては不要
    cg_emitf("  add sp, sp, #16\n");
    gen_expr(node->rhs);
    return;
  case ND_PRE_INC:
  case ND_PRE_DEC:
  case ND_POST_INC:
  case ND_POST_DEC: {
    node_t *target = node->lhs;
    int type_size = 8;
    if (target->kind == ND_LVAR) type_size = as_lvar(target)->mem.type_size;
    else if (target->kind == ND_GVAR) type_size = ((node_gvar_t *)target)->mem.type_size ? ((node_gvar_t *)target)->mem.type_size : 8;
    else if (target->kind == ND_DEREF) type_size = as_mem(target)->type_size ? as_mem(target)->type_size : 8;

    gen_lval(target);
    cg_emitf("  ldr x1, [sp], #16\n"); // x1: addr
    gen_load_x0_from_addr(type_size);   // x0: old value

    if (node->kind == ND_POST_INC || node->kind == ND_POST_DEC) {
      cg_emitf("  mov x2, x0\n");         // x2: return old value
    }

    if (node->kind == ND_PRE_INC || node->kind == ND_POST_INC)
      cg_emitf("  add x0, x0, #1\n");
    else
      cg_emitf("  sub x0, x0, #1\n");

    gen_store_x0_to_addr(type_size);    // store updated value

    if (node->kind == ND_POST_INC || node->kind == ND_POST_DEC)
      cg_emitf("  str x2, [sp, #-16]!\n");
    else
      cg_emitf("  str x0, [sp, #-16]!\n");
    return;
  }
  case ND_LOGAND: {
    int false_lbl = label_count++;
    int end_lbl = label_count++;
    gen_expr(node->lhs);
    cg_emitf("  ldr x0, [sp], #16\n");
    cg_emitf("  cbz x0, .Lfalse%d\n", false_lbl);
    gen_expr(node->rhs);
    cg_emitf("  ldr x0, [sp], #16\n");
    cg_emitf("  cbz x0, .Lfalse%d\n", false_lbl);
    cg_emitf("  mov x0, #1\n");
    cg_emitf("  b .Lend%d\n", end_lbl);
    cg_emitf(".Lfalse%d:\n", false_lbl);
    cg_emitf("  mov x0, #0\n");
    cg_emitf(".Lend%d:\n", end_lbl);
    cg_emitf("  str x0, [sp, #-16]!\n");
    return;
  }
  case ND_LOGOR: {
    int true_lbl = label_count++;
    int end_lbl = label_count++;
    gen_expr(node->lhs);
    cg_emitf("  ldr x0, [sp], #16\n");
    cg_emitf("  cbnz x0, .Ltrue%d\n", true_lbl);
    gen_expr(node->rhs);
    cg_emitf("  ldr x0, [sp], #16\n");
    cg_emitf("  cbnz x0, .Ltrue%d\n", true_lbl);
    cg_emitf("  mov x0, #0\n");
    cg_emitf("  b .Lend%d\n", end_lbl);
    cg_emitf(".Ltrue%d:\n", true_lbl);
    cg_emitf("  mov x0, #1\n");
    cg_emitf(".Lend%d:\n", end_lbl);
    cg_emitf("  str x0, [sp, #-16]!\n");
    return;
  }
  case ND_TERNARY: {
    node_ctrl_t *ctrl = as_ctrl(node);
    int else_lbl = label_count++;
    int end_lbl = label_count++;
    gen_expr(ctrl->base.lhs);
    cg_emitf("  ldr x0, [sp], #16\n");
    cg_emitf("  cbz x0, .Lelse%d\n", else_lbl);
    gen_expr(ctrl->base.rhs);
    cg_emitf("  b .Lend%d\n", end_lbl);
    cg_emitf(".Lelse%d:\n", else_lbl);
    gen_expr(ctrl->els);
    cg_emitf(".Lend%d:\n", end_lbl);
    return;
  }
  case ND_FUNCALL: {
    node_func_t *fn = as_func(node);
    if (!fn->callee && is_printf_func(fn) && fn->nargs >= 1) {
      // Darwin/ARM64: variadic 引数はレジスタではなくスタックで受け渡す。
      gen_expr(fn->args[0]);
      cg_emitf("  ldr x19, [sp], #16\n");

      int var_count = fn->nargs - 1;
      int stack_bytes = ((var_count + 1) / 2) * 16; // call時の16byte alignment維持
      if (stack_bytes > 0) {
        cg_emitf("  sub sp, sp, #%d\n", stack_bytes);
      }
      for (int i = 1; i < fn->nargs; i++) {
        gen_expr(fn->args[i]);
        cg_emitf("  ldr x9, [sp], #16\n");
        cg_emitf("  str x9, [sp, #%d]\n", (i - 1) * 8);
      }

      cg_emitf("  mov x0, x19\n");
      cg_emitf("  bl _printf\n");
      if (stack_bytes > 0) {
        cg_emitf("  add sp, sp, #%d\n", stack_bytes);
      }
      cg_emitf("  str x0, [sp, #-16]!\n");
      return;
    }

    // 間接呼び出し: calleeアドレスを引数評価前に x16 へ退避
    if (fn->callee) {
      gen_expr(fn->callee);
      cg_emitf("  ldr x16, [sp], #16\n");
    }

    // 引数の ABI サイズを計算してレジスタ数を決定
    // - ND_LVAR かつ type_size > 16: byref → 1レジスタ(アドレス)
    // - ND_LVAR かつ type_size 9-16: 2レジスタ(low/highの2スロット)
    // - その他: 1レジスタ(スカラー扱い)
    int *arg_regs = fn->nargs > 0 ? calloc((size_t)fn->nargs, sizeof(int)) : NULL;
    int total_regs = 0;
    for (int i = 0; i < fn->nargs; i++) {
      node_t *a = fn->args[i];
      int abi_sz = (a->kind == ND_LVAR) ? as_lvar(a)->mem.type_size : 0;
      if (abi_sz > 16) {
        arg_regs[i] = 1;  // byref: アドレス
      } else if (abi_sz > 8) {
        arg_regs[i] = 2;  // 2レジスタ
      } else {
        arg_regs[i] = 1;  // 1レジスタ
      }
      total_regs += arg_regs[i];
    }

    // 引数をソフトウェアスタックへプッシュ（順方向）
    for (int i = 0; i < fn->nargs; i++) {
      node_t *a = fn->args[i];
      if (a->kind == ND_LVAR) {
        int abi_sz = as_lvar(a)->mem.type_size;
        int frame_off = 16 + as_lvar(a)->offset;
        if (abi_sz > 16) {
          // byref: フレームスロットのアドレスをプッシュ
          cg_emitf("  add x0, x29, #%d\n", frame_off);
          cg_emitf("  str x0, [sp, #-16]!\n");
        } else if (abi_sz > 8) {
          // 2レジスタ: highを先にプッシュ、lowを後 (pop時: low→x_reg, high→x_{reg+1})
          cg_emitf("  ldr x0, [x29, #%d]\n", frame_off + 8);
          cg_emitf("  str x0, [sp, #-16]!\n");
          cg_emitf("  ldr x0, [x29, #%d]\n", frame_off);
          cg_emitf("  str x0, [sp, #-16]!\n");
        } else {
          gen_expr(a);
        }
      } else {
        gen_expr(a);
      }
    }

    // ソフトウェアスタックからレジスタへ逆順にポップ
    {
      int reg = total_regs;
      for (int i = fn->nargs - 1; i >= 0; i--) {
        reg -= arg_regs[i];
        if (arg_regs[i] == 2) {
          cg_emitf("  ldr x%d, [sp], #16\n", reg);       // low → x_reg
          cg_emitf("  ldr x%d, [sp], #16\n", reg + 1);   // high → x_{reg+1}
        } else {
          cg_emitf("  ldr x%d, [sp], #16\n", reg);
        }
      }
    }

    if (fn->callee) {
      cg_emitf("  blr x16\n");
    } else {
      cg_emitf("  bl _%.*s\n", fn->funcname_len, fn->funcname);
    }
    free(arg_regs);
    // 戻り値をスタックにプッシュ
    if (node->ret_struct_size > 8 && node->ret_struct_size <= 16) {
      // 9-16B 構造体戻り値: x1(高8B)を先にプッシュ、x0(低8B)を後にプッシュ
      cg_emitf("  str x1, [sp, #-16]!\n");
      cg_emitf("  str x0, [sp, #-16]!\n");
    } else {
      cg_emitf("  str x0, [sp, #-16]!\n");
    }
    return;
  }
  default:
    break;
  }

  // 二項演算
  tk_float_kind_t fpu_op_type = node->fp_kind; // ADD/SUB/MUL/DIV の場合は結果型
  if (node->kind == ND_EQ || node->kind == ND_NE || node->kind == ND_LT || node->kind == ND_LE) {
    // 比較演算の場合はオペランドの型を見る
    if (node->lhs && node->lhs->fp_kind > fpu_op_type) fpu_op_type = node->lhs->fp_kind;
    if (node->rhs && node->rhs->fp_kind > fpu_op_type) fpu_op_type = node->rhs->fp_kind;
  }

  // _Complex 演算
  if (node->is_complex && fpu_op_type) {
    gen_expr(node->lhs);
    if (!node->lhs->is_complex) {
      // スカラ→complex昇格: 虚部=0をスタックに追加
      cg_emitf("  ldr d0, [sp], #16\n");
      cg_emitf("  movi d1, #0\n");
      cg_emitf("  stp d0, d1, [sp, #-16]!\n");
    }
    gen_expr(node->rhs);
    if (!node->rhs->is_complex) {
      cg_emitf("  ldr d0, [sp], #16\n");
      cg_emitf("  movi d1, #0\n");
      cg_emitf("  stp d0, d1, [sp, #-16]!\n");
    }
    if (fpu_op_type >= TK_FLOAT_KIND_DOUBLE) {
      // _Complex double
      cg_emitf("  ldp d2, d3, [sp], #16\n"); // rhs: d2=実部, d3=虚部
      cg_emitf("  ldp d0, d1, [sp], #16\n"); // lhs: d0=実部, d1=虚部
      switch (node->kind) {
      case ND_ADD:
        cg_emitf("  fadd d0, d0, d2\n");
        cg_emitf("  fadd d1, d1, d3\n");
        break;
      case ND_SUB:
        cg_emitf("  fsub d0, d0, d2\n");
        cg_emitf("  fsub d1, d1, d3\n");
        break;
      case ND_MUL:
        // (a+bi)(c+di) = (ac-bd) + (ad+bc)i
        cg_emitf("  fmul d4, d0, d2\n"); // ac
        cg_emitf("  fmul d5, d1, d3\n"); // bd
        cg_emitf("  fmul d6, d0, d3\n"); // ad
        cg_emitf("  fmul d7, d1, d2\n"); // bc
        cg_emitf("  fsub d0, d4, d5\n"); // ac-bd
        cg_emitf("  fadd d1, d6, d7\n"); // ad+bc
        break;
      case ND_DIV: {
        // (a+bi)/(c+di) = ((ac+bd)/(c²+d²)) + ((bc-ad)/(c²+d²))i
        cg_emitf("  fmul d4, d2, d2\n"); // c²
        cg_emitf("  fmul d5, d3, d3\n"); // d²
        cg_emitf("  fadd d4, d4, d5\n"); // c²+d²
        cg_emitf("  fmul d5, d0, d2\n"); // ac
        cg_emitf("  fmul d6, d1, d3\n"); // bd
        cg_emitf("  fadd d5, d5, d6\n"); // ac+bd
        cg_emitf("  fdiv d5, d5, d4\n"); // (ac+bd)/(c²+d²)
        cg_emitf("  fmul d6, d1, d2\n"); // bc
        cg_emitf("  fmul d7, d0, d3\n"); // ad
        cg_emitf("  fsub d6, d6, d7\n"); // bc-ad
        cg_emitf("  fdiv d1, d6, d4\n"); // (bc-ad)/(c²+d²)
        cg_emitf("  fmov d0, d5\n");     // 実部
        break;
      }
      case ND_EQ:
        cg_emitf("  fcmp d0, d2\n");
        cg_emitf("  cset x0, eq\n");
        cg_emitf("  fcmp d1, d3\n");
        cg_emitf("  cset x1, eq\n");
        cg_emitf("  and x0, x0, x1\n");
        cg_emitf("  str x0, [sp, #-16]!\n");
        return;
      case ND_NE:
        cg_emitf("  fcmp d0, d2\n");
        cg_emitf("  cset x0, ne\n");
        cg_emitf("  fcmp d1, d3\n");
        cg_emitf("  cset x1, ne\n");
        cg_emitf("  orr x0, x0, x1\n");
        cg_emitf("  str x0, [sp, #-16]!\n");
        return;
      default:
        break;
      }
    } else {
      // _Complex float
      cg_emitf("  ldp s2, s3, [sp], #16\n");
      cg_emitf("  ldp s0, s1, [sp], #16\n");
      switch (node->kind) {
      case ND_ADD:
        cg_emitf("  fadd s0, s0, s2\n");
        cg_emitf("  fadd s1, s1, s3\n");
        break;
      case ND_SUB:
        cg_emitf("  fsub s0, s0, s2\n");
        cg_emitf("  fsub s1, s1, s3\n");
        break;
      case ND_MUL:
        cg_emitf("  fmul s4, s0, s2\n");
        cg_emitf("  fmul s5, s1, s3\n");
        cg_emitf("  fmul s6, s0, s3\n");
        cg_emitf("  fmul s7, s1, s2\n");
        cg_emitf("  fsub s0, s4, s5\n");
        cg_emitf("  fadd s1, s6, s7\n");
        break;
      case ND_DIV:
        cg_emitf("  fmul s4, s2, s2\n");
        cg_emitf("  fmul s5, s3, s3\n");
        cg_emitf("  fadd s4, s4, s5\n");
        cg_emitf("  fmul s5, s0, s2\n");
        cg_emitf("  fmul s6, s1, s3\n");
        cg_emitf("  fadd s5, s5, s6\n");
        cg_emitf("  fdiv s5, s5, s4\n");
        cg_emitf("  fmul s6, s1, s2\n");
        cg_emitf("  fmul s7, s0, s3\n");
        cg_emitf("  fsub s6, s6, s7\n");
        cg_emitf("  fdiv s1, s6, s4\n");
        cg_emitf("  fmov s0, s5\n");
        break;
      case ND_EQ:
        cg_emitf("  fcmp s0, s2\n");
        cg_emitf("  cset x0, eq\n");
        cg_emitf("  fcmp s1, s3\n");
        cg_emitf("  cset x1, eq\n");
        cg_emitf("  and x0, x0, x1\n");
        cg_emitf("  str x0, [sp, #-16]!\n");
        return;
      case ND_NE:
        cg_emitf("  fcmp s0, s2\n");
        cg_emitf("  cset x0, ne\n");
        cg_emitf("  fcmp s1, s3\n");
        cg_emitf("  cset x1, ne\n");
        cg_emitf("  orr x0, x0, x1\n");
        cg_emitf("  str x0, [sp, #-16]!\n");
        return;
      default:
        break;
      }
    }
    // _Complex 結果を16Bスロットとしてプッシュ
    if (fpu_op_type >= TK_FLOAT_KIND_DOUBLE) {
      cg_emitf("  stp d0, d1, [sp, #-16]!\n");
    } else {
      cg_emitf("  stp s0, s1, [sp, #-16]!\n");
    }
    return;
  }

  if (fpu_op_type) {
    // FPU 演算
    gen_expr(node->lhs);
    gen_expr(node->rhs);
    const char *r0 = freg(fpu_op_type, 0);
    const char *r1 = freg(fpu_op_type, 1);
    
    gen_pop_fpu(fpu_op_type, node->rhs->fp_kind, 1); // rhs を r1 に
    gen_pop_fpu(fpu_op_type, node->lhs->fp_kind, 0); // lhs を r0 に

    switch (node->kind) {
    case ND_ADD:
      cg_emitf("  fadd %s, %s, %s\n", r0, r0, r1);
      break;
    case ND_SUB:
      cg_emitf("  fsub %s, %s, %s\n", r0, r0, r1);
      break;
    case ND_MUL:
      cg_emitf("  fmul %s, %s, %s\n", r0, r0, r1);
      break;
    case ND_DIV:
      cg_emitf("  fdiv %s, %s, %s\n", r0, r0, r1);
      break;
    case ND_EQ:
      cg_emitf("  fcmp %s, %s\n", r0, r1);
      cg_emitf("  cset x0, eq\n");
      cg_emitf("  str x0, [sp, #-16]!\n");
      return;
    case ND_NE:
      cg_emitf("  fcmp %s, %s\n", r0, r1);
      cg_emitf("  cset x0, ne\n");
      cg_emitf("  str x0, [sp, #-16]!\n");
      return;
    case ND_LT:
      cg_emitf("  fcmp %s, %s\n", r0, r1);
      cg_emitf("  cset x0, lo\n");
      cg_emitf("  str x0, [sp, #-16]!\n");
      return;
    case ND_LE:
      cg_emitf("  fcmp %s, %s\n", r0, r1);
      cg_emitf("  cset x0, ls\n");
      cg_emitf("  str x0, [sp, #-16]!\n");
      return;
    default:
      break;
    }
    cg_emitf("  str %s, [sp, #-16]!\n", r0);
    return;
  }

  // 整数二項演算
  if (!cg_inside_regalloc && !cg_has_funcall(node)) {
    // レジスタ割り付け最適化: 子ノードの結果を x9/x10 に直接生成
    // cg_inside_regalloc を設定してフォールバック時の再入を防止
    cg_inside_regalloc = 1;
    gen_expr_to_reg(node->lhs, 0);
    gen_expr_to_reg(node->rhs, 1);

    switch (node->kind) {
    case ND_ADD:    cg_emitf("  add x0, x9, x10\n"); break;
    case ND_SUB:    cg_emitf("  sub x0, x9, x10\n"); break;
    case ND_MUL:    cg_emitf("  mul x0, x9, x10\n"); break;
    case ND_DIV:    cg_emitf(node->is_unsigned ? "  udiv x0, x9, x10\n" : "  sdiv x0, x9, x10\n"); break;
    case ND_MOD:
      cg_emitf(node->is_unsigned ? "  udiv x0, x9, x10\n" : "  sdiv x0, x9, x10\n");
      cg_emitf("  msub x0, x0, x10, x9\n");
      break;
    case ND_EQ:     cg_emitf("  cmp x9, x10\n"); cg_emitf("  cset x0, eq\n"); break;
    case ND_NE:     cg_emitf("  cmp x9, x10\n"); cg_emitf("  cset x0, ne\n"); break;
    case ND_LT:     cg_emitf("  cmp x9, x10\n"); cg_emitf(node->is_unsigned ? "  cset x0, lo\n" : "  cset x0, lt\n"); break;
    case ND_LE:     cg_emitf("  cmp x9, x10\n"); cg_emitf(node->is_unsigned ? "  cset x0, ls\n" : "  cset x0, le\n"); break;
    case ND_BITAND: cg_emitf("  and x0, x9, x10\n"); break;
    case ND_BITXOR: cg_emitf("  eor x0, x9, x10\n"); break;
    case ND_BITOR:  cg_emitf("  orr x0, x9, x10\n"); break;
    case ND_SHL:    cg_emitf("  lsl x0, x9, x10\n"); break;
    case ND_SHR:    cg_emitf(node->is_unsigned ? "  lsr x0, x9, x10\n" : "  asr x0, x9, x10\n"); break;
    default: break;
    }
    cg_inside_regalloc = 0;
    cg_emitf("  str x0, [sp, #-16]!\n");
    return;
  }

  // フォールバック: funcall を含む場合またはレジスタ割り付け中はスタックモード
  gen_expr(node->lhs);
  gen_expr(node->rhs);

  cg_emitf("  ldr x1, [sp], #16\n");
  cg_emitf("  ldr x0, [sp], #16\n");

  switch (node->kind) {
  case ND_ADD:    cg_emitf("  add x0, x0, x1\n"); break;
  case ND_SUB:    cg_emitf("  sub x0, x0, x1\n"); break;
  case ND_MUL:    cg_emitf("  mul x0, x0, x1\n"); break;
  case ND_DIV:    cg_emitf(node->is_unsigned ? "  udiv x0, x0, x1\n" : "  sdiv x0, x0, x1\n"); break;
  case ND_MOD:
    cg_emitf(node->is_unsigned ? "  udiv x2, x0, x1\n" : "  sdiv x2, x0, x1\n");
    cg_emitf("  msub x0, x2, x1, x0\n");
    break;
  case ND_EQ:     cg_emitf("  cmp x0, x1\n"); cg_emitf("  cset x0, eq\n"); break;
  case ND_NE:     cg_emitf("  cmp x0, x1\n"); cg_emitf("  cset x0, ne\n"); break;
  case ND_LT:     cg_emitf("  cmp x0, x1\n"); cg_emitf(node->is_unsigned ? "  cset x0, lo\n" : "  cset x0, lt\n"); break;
  case ND_LE:     cg_emitf("  cmp x0, x1\n"); cg_emitf(node->is_unsigned ? "  cset x0, ls\n" : "  cset x0, le\n"); break;
  case ND_BITAND: cg_emitf("  and x0, x0, x1\n"); break;
  case ND_BITXOR: cg_emitf("  eor x0, x0, x1\n"); break;
  case ND_BITOR:  cg_emitf("  orr x0, x0, x1\n"); break;
  case ND_SHL:    cg_emitf("  lsl x0, x0, x1\n"); break;
  case ND_SHR:    cg_emitf(node->is_unsigned ? "  lsr x0, x0, x1\n" : "  asr x0, x0, x1\n"); break;
  default: break;
  }

  cg_emitf("  str x0, [sp, #-16]!\n");
}

static void gen_stmt(node_t *node) {
  switch (node->kind) {
  case ND_BLOCK:
    for (int i = 0; as_block(node)->body[i]; i++) {
      gen_stmt(as_block(node)->body[i]);
    }
    return;
  case ND_RETURN:
    // TCO: return self(args...) → 引数を再設定して関数先頭へジャンプ
    if (node->lhs && node->lhs->kind == ND_FUNCALL && node->ret_struct_size == 0 &&
        node->fp_kind == 0) {
      node_func_t *call = as_func(node->lhs);
      if (call->callee == NULL &&
          call->funcname_len == cg_current_funcname_len &&
          strncmp(call->funcname, cg_current_funcname, cg_current_funcname_len) == 0 &&
          call->nargs == cg_current_nargs) {
        // 引数を評価してスタックに積む
        for (int i = 0; i < call->nargs; i++) {
          gen_expr(call->args[i]);
        }
        // スタックから引数レジスタにポップ（逆順）
        for (int i = call->nargs - 1; i >= 0; i--) {
          cg_emitf("  ldr x%d, [sp], #16\n", i);
        }
        // 引数保存ラベルへジャンプ（プロローグをスキップ）
        cg_emitf("  b .L_tco_%.*s\n", cg_current_funcname_len, cg_current_funcname);
        return;
      }
    }
    if (node->lhs) {
      if (node->ret_struct_size > 16) {
        // >16B 構造体戻り値: x8 が指すバッファにローカル変数の内容をコピー
        if (node->lhs->kind == ND_LVAR) {
          node_lvar_t *lvar = (node_lvar_t *)node->lhs;
          int frame_off = 16 + lvar->offset;
          int size = node->ret_struct_size;
          cg_emitf("  ldr x8, [x29, #%d]\n", X8_SAVE_OFFSET); // 退避した x8 を復元
          int off = 0;
          // 8バイト単位でコピー
          for (; off + 8 <= size; off += 8) {
            cg_emitf("  ldr x9, [x29, #%d]\n", frame_off + off);
            cg_emitf("  str x9, [x8, #%d]\n", off);
          }
          // 残りの4バイト
          if (off + 4 <= size) {
            cg_emitf("  ldr w9, [x29, #%d]\n", frame_off + off);
            cg_emitf("  str w9, [x8, #%d]\n", off);
            off += 4;
          }
          // 残りの1バイト単位
          for (; off < size; off++) {
            cg_emitf("  ldrb w9, [x29, #%d]\n", frame_off + off);
            cg_emitf("  strb w9, [x8, #%d]\n", off);
          }
        }
      } else if (node->ret_struct_size > 8 && node->ret_struct_size <= 16) {
        // 9-16B 構造体戻り値: フレームから x0/x1 ペアにロード
        if (node->lhs->kind == ND_LVAR) {
          node_lvar_t *lvar = (node_lvar_t *)node->lhs;
          int frame_off = 16 + lvar->offset;
          cg_emitf("  ldr x0, [x29, #%d]\n", frame_off);
          cg_emitf("  ldr x1, [x29, #%d]\n", frame_off + 8);
        } else {
          // 式の結果（アドレス）からロード
          gen_expr(node->lhs);
          cg_emitf("  ldr x9, [sp], #16\n");
          cg_emitf("  ldr x0, [x9]\n");
          cg_emitf("  ldr x1, [x9, #8]\n");
        }
      } else if (node->fp_kind == TK_FLOAT_KIND_FLOAT) { // 関数の戻り値が float
        gen_expr(node->lhs);
        gen_pop_fpu(TK_FLOAT_KIND_FLOAT, node->lhs->fp_kind, 0); // s0 にロード
      } else if (node->fp_kind >= TK_FLOAT_KIND_DOUBLE) { // 関数の戻り値が double/long double(現状lowering)
        gen_expr(node->lhs);
        gen_pop_fpu(TK_FLOAT_KIND_DOUBLE, node->lhs->fp_kind, 0); // d0 にロード
      } else {                               // 関数の戻り値が 整数
        gen_expr(node->lhs);
        if (node->lhs->fp_kind == TK_FLOAT_KIND_FLOAT) {
          cg_emitf("  ldr s0, [sp], #16\n");
          cg_emitf("  fcvtzs x0, s0\n");       // float->int
        } else if (node->lhs->fp_kind >= TK_FLOAT_KIND_DOUBLE) {
          cg_emitf("  ldr d0, [sp], #16\n");
          cg_emitf("  fcvtzs x0, d0\n");       // double->int
        } else {
          cg_emitf("  ldr x0, [sp], #16\n");   // そのまま int
        }
      }
    } else {
      // void 関数の return; は戻り値レジスタを0で統一
      cg_emitf("  mov x0, #0\n");
    }
    cg_emitf("  mov sp, x29\n");       // VLA等でspが動いた場合に固定フレーム先頭へ戻す
    cg_emitf("  ldp x29, x30, [sp]\n");
    cg_emitf("  add sp, sp, #%d\n", STACK_SIZE);
    cg_emitf("  ret\n");
    return;
  case ND_FUNCDEF: {
    node_func_t *fn = as_func(node);
    cg_current_funcname = fn->funcname;
    cg_current_funcname_len = fn->funcname_len;
    cg_current_nargs = fn->nargs;
    clear_label_map();
    collect_goto_labels(fn->base.rhs);
    // 関数ラベルの出力
    cg_emitf(".global _%.*s\n", fn->funcname_len, fn->funcname);
    cg_emitf(".align 2\n");
    cg_emitf("_%.*s:\n", fn->funcname_len, fn->funcname);
    // プロローグ
    cg_emitf("  sub sp, sp, #%d\n", STACK_SIZE);
    cg_emitf("  stp x29, x30, [sp]\n");
    cg_emitf("  mov x29, sp\n");
    // >16B 構造体戻り値: x8 (indirect result pointer) をフレームに退避
    if (fn->base.ret_struct_size > 16) {
      cg_emitf("  str x8, [x29, #%d]\n", X8_SAVE_OFFSET);
    }
    // TCO用ラベル: 末尾再帰時はここへジャンプして引数を再保存
    cg_emitf(".L_tco_%.*s:\n", fn->funcname_len, fn->funcname);
    // 仮引数をレジスタからローカル変数スロットへ保存
    // type_size > 16: byref (>16B 構造体の値渡し) → 1レジスタ (ポインタ)
    // type_size 9-16: 2レジスタ構造体 → stp
    // type_size ≤ 8: 1レジスタ (スカラー / ≤8B 構造体)
    {
      int reg = 0;
      for (int i = 0; i < fn->nargs; i++) {
        int abi_sz = as_lvar(fn->args[i])->mem.type_size;
        int frame_off = 16 + as_lvar(fn->args[i])->offset;
        if (abi_sz > 16) {
          // >16B構造体: byref (1レジスタ, ポインタを保存)
          cg_emitf("  str x%d, [x29, #%d]\n", reg, frame_off);
          reg++;
        } else if (abi_sz > 8) {
          // 9-16B構造体: 2レジスタ → stp x_reg, x_{reg+1}
          cg_emitf("  stp x%d, x%d, [x29, #%d]\n", reg, reg + 1, frame_off);
          reg += 2;
        } else {
          // スカラー / ≤8B構造体: 1レジスタ
          cg_emitf("  str x%d, [x29, #%d]\n", reg, frame_off);
          reg++;
        }
      }
      // 可変長引数関数: 残りの引数レジスタ (x{reg}..x7) を次のスロットに保存
      if (fn->is_variadic) {
        for (int i = reg; i < 8; i++) {
          cg_emitf("  str x%d, [x29, #%d]\n", i, 16 + i * 8);
        }
      }
    }
    // 関数本体
    gen_stmt(fn->base.rhs);
    // main の return が無い場合は 0 を返す
    if (is_main_func(fn)) {
      cg_emitf("  mov x0, #0\n");
    }
    // エピローグ
    cg_emitf("  mov sp, x29\n");       // VLA等でspが動いた場合に固定フレーム先頭へ戻す
    cg_emitf("  ldp x29, x30, [sp]\n");
    cg_emitf("  add sp, sp, #%d\n", STACK_SIZE);
    cg_emitf("  ret\n");
    return;
  }
  case ND_IF: {
    node_ctrl_t *ctrl = as_ctrl(node);
    int lbl = label_count++;
    gen_expr(ctrl->base.lhs);
    cg_emitf("  ldr x0, [sp], #16\n");
    cg_emitf("  cbz x0, .Lelse%d\n", lbl);
    gen_stmt(ctrl->base.rhs);
    if (ctrl->els) {
      cg_emitf("  b .Lend%d\n", lbl);
      cg_emitf(".Lelse%d:\n", lbl);
      gen_stmt(ctrl->els);
      cg_emitf(".Lend%d:\n", lbl);
    } else {
      cg_emitf("  b .Lend%d\n", lbl);
      cg_emitf(".Lelse%d:\n", lbl);
      cg_emitf(".Lend%d:\n", lbl);
    }
    return;
  }
  case ND_WHILE: {
    node_ctrl_t *ctrl = as_ctrl(node);
    int begin_lbl = label_count++;
    int cont_lbl = label_count++;
    int end_lbl = label_count++;
    push_control_labels(end_lbl, cont_lbl);
    cg_emitf(".Lbegin%d:\n", begin_lbl);
    cg_emitf(".Lcont%d:\n", cont_lbl);
    gen_expr(ctrl->base.lhs);
    cg_emitf("  ldr x0, [sp], #16\n");
    cg_emitf("  cbz x0, .Lend%d\n", end_lbl);
    gen_stmt(ctrl->base.rhs);
    cg_emitf("  b .Lbegin%d\n", begin_lbl);
    cg_emitf(".Lend%d:\n", end_lbl);
    pop_control_labels();
    return;
  }
  case ND_DO_WHILE: {
    node_ctrl_t *ctrl = as_ctrl(node);
    int begin_lbl = label_count++;
    int cont_lbl = label_count++;
    int end_lbl = label_count++;
    push_control_labels(end_lbl, cont_lbl);
    cg_emitf(".Lbegin%d:\n", begin_lbl);
    gen_stmt(ctrl->base.rhs);
    cg_emitf(".Lcont%d:\n", cont_lbl);
    gen_expr(ctrl->base.lhs);
    cg_emitf("  ldr x0, [sp], #16\n");
    cg_emitf("  cbnz x0, .Lbegin%d\n", begin_lbl);
    cg_emitf(".Lend%d:\n", end_lbl);
    pop_control_labels();
    return;
  }
  case ND_FOR: {
    node_ctrl_t *ctrl = as_ctrl(node);
    int begin_lbl = label_count++;
    int cont_lbl = label_count++;
    int end_lbl = label_count++;
    push_control_labels(end_lbl, cont_lbl);
    if (ctrl->init) {
      gen_expr(ctrl->init);
      cg_emitf("  add sp, sp, #16\n");
    }
    cg_emitf(".Lbegin%d:\n", begin_lbl);
    if (ctrl->base.lhs) {
      gen_expr(ctrl->base.lhs);
      cg_emitf("  ldr x0, [sp], #16\n");
      cg_emitf("  cbz x0, .Lend%d\n", end_lbl);
    }
    gen_stmt(ctrl->base.rhs);
    cg_emitf(".Lcont%d:\n", cont_lbl);
    if (ctrl->inc) {
      gen_expr(ctrl->inc);
      cg_emitf("  add sp, sp, #16\n");
    }
    cg_emitf("  b .Lbegin%d\n", begin_lbl);
    cg_emitf(".Lend%d:\n", end_lbl);
    pop_control_labels();
    return;
  }
  case ND_SWITCH: {
    node_ctrl_t *ctrl = as_ctrl(node);
    int end_lbl = label_count++;

    gen_expr(ctrl->base.lhs);
    cg_emitf("  ldr x0, [sp], #16\n");

    switch_collect_t sc = {0};
    collect_switch_labels(ctrl->base.rhs, &sc);

    for (int i = 0; i < sc.case_count; i++) {
      node_case_t *c = sc.cases[i];
      cg_emit_mov_imm("x1", c->val);
      cg_emitf("  cmp x0, x1\n");
      cg_emitf("  beq .Lcase%d\n", c->label_id);
    }
    if (sc.default_node) {
      cg_emitf("  b .Ldefault%d\n", sc.default_node->label_id);
    } else {
      cg_emitf("  b .Lend%d\n", end_lbl);
    }

    push_control_labels(end_lbl, -1); // switch は continue 対象外
    gen_switch_body(ctrl->base.rhs);
    pop_control_labels();

    cg_emitf(".Lend%d:\n", end_lbl);
    free(sc.cases);
    return;
  }
  case ND_BREAK:
    if (current_break_label() < 0) {
      diag_emit_internalf(DIAG_ERR_CODEGEN_BREAK_OUTSIDE_LOOP_OR_SWITCH, "%s",
                          diag_message_for(DIAG_ERR_CODEGEN_BREAK_OUTSIDE_LOOP_OR_SWITCH));
    }
    cg_emitf("  b .Lend%d\n", current_break_label());
    return;
  case ND_CONTINUE:
    if (current_continue_label() < 0) {
      diag_emit_internalf(DIAG_ERR_CODEGEN_CONTINUE_OUTSIDE_LOOP, "%s",
                          diag_message_for(DIAG_ERR_CODEGEN_CONTINUE_OUTSIDE_LOOP));
    }
    cg_emitf("  b .Lcont%d\n", current_continue_label());
    return;
  case ND_GOTO: {
    node_jump_t *j = as_jump(node);
    int id = find_label_id(j->name, j->name_len);
    if (id < 0) {
      diag_emit_internalf(DIAG_ERR_CODEGEN_GOTO_LABEL_UNDEFINED,
                          diag_message_for(DIAG_ERR_CODEGEN_GOTO_LABEL_UNDEFINED),
                          j->name_len, j->name);
    }
    cg_emitf("  b .Luser%d\n", id);
    return;
  }
  case ND_LABEL: {
    node_jump_t *j = as_jump(node);
    cg_emitf(".Luser%d:\n", j->label_id);
    gen_stmt(j->base.rhs);
    return;
  }
  case ND_CASE: {
    node_case_t *c = as_case(node);
    cg_emitf(".Lcase%d:\n", c->label_id);
    gen_stmt(c->base.rhs);
    return;
  }
  case ND_DEFAULT: {
    node_default_t *d = as_default(node);
    cg_emitf(".Ldefault%d:\n", d->label_id);
    gen_stmt(d->base.rhs);
    return;
  }
  default:
    gen_expr(node);
    cg_emitf("  add sp, sp, #16\n");
    return;
  }
}

void gen(node_t *node) {
  gen_stmt(node);
}

void gen_string_literals(void) {
  if (!string_literals) return;
  cg_emitf(".section __TEXT,__cstring\n");
  for (string_lit_t *lit = string_literals; lit; lit = lit->next) {
    cg_emitf("%s:\n", lit->label);
    int i = 0;
    while (i < lit->len) {
      uint32_t v = 0;
      if (lit->str[i] == '\\') {
        tk_parse_escape_value(lit->str, lit->len, &i, &v);
      } else {
        v = (unsigned char)lit->str[i];
        i++;
      }
      if (lit->char_width == TK_CHAR_WIDTH_CHAR) {
        cg_emitf("  .byte %u\n", (unsigned)(v & 0xFF));
      } else if (lit->char_width == TK_CHAR_WIDTH_CHAR16) {
        cg_emitf("  .hword %u\n", (unsigned)(v & 0xFFFF));
      } else {
        cg_emitf("  .word %u\n", (unsigned)v);
      }
    }
    if (lit->char_width == TK_CHAR_WIDTH_CHAR) cg_emitf("  .byte 0\n");
    else if (lit->char_width == TK_CHAR_WIDTH_CHAR16) cg_emitf("  .hword 0\n");
    else cg_emitf("  .word 0\n");
  }
  cg_emitf(".text\n");
}

void gen_float_literals(void) {
  if (!float_literals) return;
  cg_emitf(".section __DATA,__data\n");
  cg_emitf(".align 3\n");
  for (float_lit_t *lit = float_literals; lit; lit = lit->next) {
    if (cg_can_use_fp_immediate_zero(lit->fval, lit->fp_kind)) continue;
    cg_emitf(".LCF%d:\n", lit->id);
    if (lit->fp_kind == TK_FLOAT_KIND_FLOAT) {
      // float (32bit) 定数出力: IEEE754 format
      union { float f; uint32_t i; } u = { .f = (float)lit->fval };
      cg_emitf("  .word %u\n", u.i);
    } else {
      // double (64bit) 定数出力。
      // note: long double is currently lowered to double in codegen.
      union { double d; uint64_t i; } u = { .d = lit->fval };
      cg_emitf("  .quad %llu\n", (unsigned long long)u.i);
    }
  }
  cg_emitf(".text\n");
}

void gen_global_vars(void) {
  for (global_var_t *gv = global_vars; gv; gv = gv->next) {
    if (gv->is_extern_decl) continue; // extern宣言のみ: emit不要
    if (gv->is_thread_local) {
      // _Thread_local: TLV descriptor + thread data/bss
      if (gv->has_init) {
        cg_emitf(".section __DATA,__thread_data\n");
        cg_emitf("_%.*s$tlv$init:\n", gv->name_len, gv->name);
        if (gv->type_size == 1) cg_emitf("  .byte %lld\n", gv->init_val);
        else if (gv->type_size == 2) cg_emitf("  .short %lld\n", gv->init_val);
        else if (gv->type_size == 4) cg_emitf("  .long %lld\n", gv->init_val);
        else cg_emitf("  .quad %lld\n", gv->init_val);
      } else {
        cg_emitf(".section __DATA,__thread_bss\n");
        cg_emitf("_%.*s$tlv$init:\n", gv->name_len, gv->name);
        cg_emitf("  .space %d\n", gv->type_size);
      }
      cg_emitf(".section __DATA,__thread_vars,thread_local_variables\n");
      cg_emitf(".global _%.*s\n", gv->name_len, gv->name);
      cg_emitf("_%.*s:\n", gv->name_len, gv->name);
      cg_emitf("  .quad __tlv_bootstrap\n");
      cg_emitf("  .quad 0\n");
      cg_emitf("  .quad _%.*s$tlv$init\n", gv->name_len, gv->name);
    } else if (gv->has_init) {
      // 初期化済みグローバル変数: .data セクション
      cg_emitf(".section __DATA,__data\n");
      cg_emitf(".global _%.*s\n", gv->name_len, gv->name);
      int align = (gv->type_size >= 8) ? 3 : (gv->type_size >= 4) ? 2 : (gv->type_size >= 2) ? 1 : 0;
      cg_emitf(".align %d\n", align);
      cg_emitf("_%.*s:\n", gv->name_len, gv->name);
      if (gv->init_symbol) {
        cg_emitf("  .quad _%.*s\n", gv->init_symbol_len, gv->init_symbol);
      } else if (gv->type_size == 1) cg_emitf("  .byte %lld\n", gv->init_val);
      else if (gv->type_size == 2) cg_emitf("  .short %lld\n", gv->init_val);
      else if (gv->type_size == 4) cg_emitf("  .long %lld\n", gv->init_val);
      else cg_emitf("  .quad %lld\n", gv->init_val);
    } else {
      // 暫定定義: .comm
      // .comm _name,size,log2align
      int log2align = (gv->type_size >= 8) ? 3 : (gv->type_size >= 4) ? 2 : (gv->type_size >= 2) ? 1 : 0;
      cg_emitf(".comm _%.*s,%d,%d\n", gv->name_len, gv->name, gv->type_size, log2align);
    }
  }
}
