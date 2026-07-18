/*
 * IR データ構造のアロケータ・ビルダー基盤 (Phase 1)。
 *
 * 既存 codegen には触らない。本ファイルは main からはまだ呼ばれない。
 */

#include "ir.h"
#include <stdlib.h>
#include <string.h>

int ir_type_size(ir_type_t t) {
  switch (t) {
    case IR_TY_I8:  return 1;
    case IR_TY_I16: return 2;
    case IR_TY_I32: return 4;
    case IR_TY_I64: return 8;
    case IR_TY_F32: return 4;
    case IR_TY_F64: return 8;
    case IR_TY_PTR: return 8;
    default:        return 0;
  }
}

const char *ir_type_name(ir_type_t t) {
  switch (t) {
    case IR_TY_VOID: return "void";
    case IR_TY_I8:   return "i8";
    case IR_TY_I16:  return "i16";
    case IR_TY_I32:  return "i32";
    case IR_TY_I64:  return "i64";
    case IR_TY_F32:  return "f32";
    case IR_TY_F64:  return "f64";
    case IR_TY_PTR:  return "ptr";
    default:         return "?";
  }
}

const char *ir_op_name(ir_op_t op) {
  switch (op) {
    case IR_NOP:          return "nop";
    case IR_ADD:          return "add";
    case IR_SUB:          return "sub";
    case IR_MUL:          return "mul";
    case IR_DIV:          return "div";
    case IR_MOD:          return "mod";
    case IR_AND:          return "and";
    case IR_OR:           return "or";
    case IR_XOR:          return "xor";
    case IR_SHL:          return "shl";
    case IR_SHR:          return "shr";
    case IR_NEG:          return "neg";
    case IR_NOT:          return "not";
    case IR_EQ:           return "eq";
    case IR_NE:           return "ne";
    case IR_LT:           return "lt";
    case IR_LE:           return "le";
    case IR_ULT:          return "ult";
    case IR_ULE:          return "ule";
    case IR_FADD:         return "fadd";
    case IR_FSUB:         return "fsub";
    case IR_FMUL:         return "fmul";
    case IR_FDIV:         return "fdiv";
    case IR_FNEG:         return "fneg";
    case IR_FEQ:          return "feq";
    case IR_FNE:          return "fne";
    case IR_FLT:          return "flt";
    case IR_FLE:          return "fle";
    case IR_ZEXT:         return "zext";
    case IR_SEXT:         return "sext";
    case IR_TRUNC:        return "trunc";
    case IR_F2I:          return "f2i";
    case IR_I2F:          return "i2f";
    case IR_F2F:          return "f2f";
    case IR_LOAD:         return "load";
    case IR_STORE:        return "store";
    case IR_ALLOCA:       return "alloca";
    case IR_LEA:          return "lea";
    case IR_MEMCPY:       return "memcpy";
    case IR_LOAD_IMM:     return "load_imm";
    case IR_LOAD_FP_IMM:  return "load_fp_imm";
    case IR_LOAD_STR:     return "load_str";
    case IR_LOAD_SYM:     return "load_sym";
    case IR_LOAD_TLV_ADDR:return "load_tlv_addr";
    case IR_BR:           return "br";
    case IR_BR_COND:      return "br_cond";
    case IR_LABEL:        return "label";
    case IR_RET:          return "ret";
    case IR_CALL:         return "call";
    case IR_PARAM:        return "param";
    case IR_RESULT_AREA:  return "result_area";
    case IR_VA_ARG_AREA:  return "va_arg_area";
    case IR_VLA_ALLOC:    return "vla_alloc";
    case IR_ATOMIC:       return "atomic";
    case IR_UDIV:         return "udiv";
    case IR_UMOD:         return "umod";
    case IR_LSR:          return "lsr";
    case IR_ALIGN_PTR:    return "align_ptr";
    case IR_CONTINUATION_SUSPEND: return "continuation_suspend";
    default:              return "?";
  }
}

/* ---- ir_val_t ヘルパ ---- */

/* imm と fp_imm は匿名 union (排他)。none は値を持たないので union を明示的に 0 クリアする。
 * 現状 imm(long long) と fp_imm(double) はどちらも 8B なので `.imm=0` でも fp_imm は 0 に
 * なるが、将来 union メンバのサイズが食い違っても (あるいは 32bit 移植時も) 確実に全体が 0 に
 * なるよう、構造体全体を {0} で初期化してから id/type を設定する。 */
ir_val_t ir_val_none(void) {
  ir_val_t v = {0};
  v.id = IR_VAL_NONE;
  v.type = IR_TY_VOID;
  return v;
}

ir_val_t ir_val_imm(ir_type_t t, long long imm) {
  ir_val_t v = { .id = IR_VAL_IMM, .type = t, .imm = imm };
  return v;
}

ir_val_t ir_val_fp_imm(ir_type_t t, double v) {
  ir_val_t r = { .id = IR_VAL_IMM, .type = t, .fp_imm = v };
  return r;
}

ir_val_t ir_val_vreg(int id, ir_type_t t) {
  ir_val_t v = { .id = id, .type = t, .imm = 0 };
  return v;
}

void ir_function_type_dispose(ir_function_type_t *type) {
  if (!type) return;
  free(type->params);
  memset(type, 0, sizeof(*type));
}

int ir_function_type_set(
    ir_function_type_t *type, psx_type_id_t type_id,
    psx_qual_type_t result, const psx_qual_type_t *params,
    size_t param_count, int is_variadic, int has_prototype) {
  if (!type || result.type_id == PSX_TYPE_ID_INVALID ||
      (param_count > 0 && !params))
    return 0;
  psx_qual_type_t *copy = NULL;
  if (param_count > 0) {
    if (param_count > SIZE_MAX / sizeof(*copy)) return 0;
    copy = malloc(param_count * sizeof(*copy));
    if (!copy) return 0;
    memcpy(copy, params, param_count * sizeof(*copy));
  }
  free(type->params);
  type->type_id = type_id;
  type->result = result;
  type->params = copy;
  type->param_count = param_count;
  type->is_variadic = is_variadic ? 1 : 0;
  type->has_prototype = has_prototype ? 1 : 0;
  return 1;
}

int ir_function_type_copy(
    ir_function_type_t *destination,
    const ir_function_type_t *source) {
  if (!destination || !source) return 0;
  return ir_function_type_set(
      destination, source->type_id, source->result,
      source->params, source->param_count,
      source->is_variadic, source->has_prototype);
}

/* ---- アロケータ ---- */

/* メモリ計測用カウンタ。関数ごとに IR を解放するため、現在 resident と、同時 resident の
 * 最大 (= 最大の 1 関数) を別に追跡し、getter はピークを返す。 */
static size_t ir_inst_live = 0, ir_inst_peak = 0;
static size_t ir_block_live = 0, ir_block_peak = 0;
size_t ir_inst_total_count(void) { return ir_inst_peak; }
size_t ir_block_total_count(void) { return ir_block_peak; }

ir_module_t *ir_module_new(void) {
  ir_module_t *m = calloc(1, sizeof(ir_module_t));
  return m;
}

ir_symbol_t *ir_module_find_symbol(const ir_module_t *m,
                                   const char *name, int name_len) {
  if (!m || !name || name_len <= 0) return NULL;
  for (ir_symbol_t *symbol = m->symbols; symbol; symbol = symbol->next) {
    if (symbol->name_len == name_len &&
        memcmp(symbol->name, name, (size_t)name_len) == 0) {
      return symbol;
    }
  }
  return NULL;
}

ir_symbol_t *ir_module_add_symbol(ir_module_t *m,
                                  const char *name, int name_len) {
  if (!m || !name || name_len <= 0) return NULL;
  ir_symbol_t *existing = ir_module_find_symbol(m, name, name_len);
  if (existing) return existing;
  ir_symbol_t *symbol = calloc(1, sizeof(*symbol));
  if (!symbol) return NULL;
  symbol->name = malloc((size_t)name_len + 1);
  if (!symbol->name) {
    free(symbol);
    return NULL;
  }
  memcpy(symbol->name, name, (size_t)name_len);
  symbol->name[name_len] = '\0';
  symbol->name_len = name_len;
  if (!m->symbols) m->symbols = symbol;
  else m->symbols_tail->next = symbol;
  m->symbols_tail = symbol;
  return symbol;
}

ir_symbol_func_ref_t *ir_symbol_add_func_ref(
    ir_symbol_t *symbol, int offset, const char *name, int name_len,
    const ir_function_type_t *function_type) {
  if (!symbol || !name || name_len <= 0) return NULL;
  for (ir_symbol_func_ref_t *ref = symbol->func_refs; ref; ref = ref->next) {
    if (ref->offset == offset) return ref;
  }
  ir_symbol_func_ref_t *ref = calloc(1, sizeof(*ref));
  if (!ref) return NULL;
  ref->name = malloc((size_t)name_len + 1);
  if (!ref->name) {
    free(ref);
    return NULL;
  }
  memcpy(ref->name, name, (size_t)name_len);
  ref->name[name_len] = '\0';
  ref->name_len = name_len;
  ref->offset = offset;
  if (function_type) {
    if (!ir_function_type_copy(&ref->function_type, function_type)) {
      free(ref->name);
      free(ref);
      return NULL;
    }
    ref->has_function_type = 1;
  }
  if (!symbol->func_refs) symbol->func_refs = ref;
  else symbol->func_refs_tail->next = ref;
  symbol->func_refs_tail = ref;
  return ref;
}

const ir_symbol_func_ref_t *ir_symbol_find_func_ref(
    const ir_symbol_t *symbol, int offset) {
  if (!symbol) return NULL;
  for (const ir_symbol_func_ref_t *ref = symbol->func_refs;
       ref; ref = ref->next) {
    if (ref->offset == offset) return ref;
  }
  return NULL;
}

/* 関数 1 つ分の IR を解放する (関数ごとストリーミング codegen 用)。
 * 所有しているのは block / inst / inst->args / f->name / f->c_signature /
 * f->vreg_phys_reg のみ。
 * inst->sym は gv->name / 文字列ラベル / AST funcname の alias なので解放しない。 */
void ir_func_free(ir_func_t *f) {
  if (!f) return;
  for (ir_block_t *b = f->entry; b; ) {
    ir_block_t *bnext = b->next;
    for (ir_inst_t *i = b->head; i; ) {
      ir_inst_t *inext = i->next;
      free(i->args);   /* IR_CALL の実引数列 (calloc)。NULL なら no-op */
      ir_function_type_dispose(&i->function_type);
      free(i);
      if (ir_inst_live) ir_inst_live--;
      i = inext;
    }
    free(b);
    if (ir_block_live) ir_block_live--;
    b = bnext;
  }
  free(f->vreg_phys_reg);
  ir_function_type_dispose(&f->function_type);
  free(f->c_signature);
  free(f->continuation_entry_name);
  free(f->continuation_condition_name);
  free(f->continuation_start_export);
  free(f->continuation_resume_export);
  free(f->continuation_status_export);
  free(f->continuation_result_export);
  free(f->name);
  free(f);
}

/* モジュール全体を解放する。ストリーミング経路では 1 関数モジュールに使う。 */
void ir_module_free(ir_module_t *m) {
  if (!m) return;
  for (ir_func_t *f = m->funcs; f; ) {
    ir_func_t *fnext = f->next;
    ir_func_free(f);
    f = fnext;
  }
  for (ir_global_t *g = m->globals; g; ) {
    ir_global_t *gnext = g->next;
    free(g->init_values);  /* init_symbol は alias なので解放しない */
    free(g);
    g = gnext;
  }
  for (ir_symbol_t *symbol = m->symbols; symbol; ) {
    ir_symbol_t *next = symbol->next;
    for (ir_symbol_func_ref_t *ref = symbol->func_refs; ref; ) {
      ir_symbol_func_ref_t *ref_next = ref->next;
      ir_function_type_dispose(&ref->function_type);
      free(ref->name);
      free(ref);
      ref = ref_next;
    }
    free(symbol->name);
    free(symbol);
    symbol = next;
  }
  free(m);
}

ir_func_t *ir_func_new(ir_module_t *m, const char *name, int name_len, ir_type_t ret_type) {
  ir_func_t *f = calloc(1, sizeof(ir_func_t));
  if (name && name_len > 0) {
    f->name = malloc((size_t)name_len + 1);
    memcpy(f->name, name, (size_t)name_len);
    f->name[name_len] = '\0';
    f->name_len = name_len;
  }
  f->ret_type = ret_type;
  f->next_vreg_id = 0;
  f->next_block_id = 0;
  /* entry ブロックを最初から確保しておく */
  ir_block_t *entry = ir_block_new(f);
  f->entry = entry;
  f->cur_block = entry;
  /* モジュールに append */
  if (m) {
    if (!m->funcs) m->funcs = f;
    else m->funcs_tail->next = f;
    m->funcs_tail = f;
  }
  return f;
}

ir_block_t *ir_block_new(ir_func_t *f) {
  if (++ir_block_live > ir_block_peak) ir_block_peak = ir_block_live;
  ir_block_t *b = calloc(1, sizeof(ir_block_t));
  b->id = f ? f->next_block_id++ : 0;
  if (f) {
    if (!f->entry) {
      /* 通常 ir_func_new で entry 設定済みだが、防御的に */
      f->entry = b;
    } else if (f->blocks_tail) {
      f->blocks_tail->next = b;
    }
    f->blocks_tail = b;
  }
  return b;
}

ir_inst_t *ir_inst_new(ir_op_t op) {
  if (++ir_inst_live > ir_inst_peak) ir_inst_peak = ir_inst_live;
  ir_inst_t *i = calloc(1, sizeof(ir_inst_t));
  i->op = op;
  i->dst = ir_val_none();
  i->src1 = ir_val_none();
  i->src2 = ir_val_none();
  /* label_id / else_label_id は分岐 op の匿名 union アーム。-1 既定は分岐 op に限定する
   * (非分岐 op で書くと他アーム alloca_size 等を破壊する。読み出しは分岐 op のみ)。 */
  if (op == IR_BR || op == IR_BR_COND || op == IR_LABEL) {
    i->label_id = -1;
    i->else_label_id = -1;
  }
  i->result_area = ir_val_none();
  i->callee = ir_val_none();
  i->src3 = ir_val_none();  /* 未使用時に汎用オペランド走査 (ir_opt/ir_regalloc) の対象外にする */
  return i;
}

void ir_func_append_inst(ir_func_t *f, ir_inst_t *inst) {
  if (!f || !f->cur_block || !inst) return;
  ir_block_t *b = f->cur_block;
  if (!b->head) b->head = inst;
  if (b->tail) b->tail->next = inst;
  b->tail = inst;
}

int ir_func_new_vreg(ir_func_t *f) {
  return f ? f->next_vreg_id++ : -1;
}

int ir_func_new_label(ir_func_t *f) {
  /* ブロック id を label id として共有する。LABEL 命令は label_id だけ持つ。 */
  return f ? f->next_block_id++ : -1;
}

void ir_func_switch_block(ir_func_t *f, ir_block_t *block) {
  if (f && block) f->cur_block = block;
}
