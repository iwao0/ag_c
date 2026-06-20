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
    case IR_VA_ARG_AREA:  return "va_arg_area";
    case IR_VLA_ALLOC:    return "vla_alloc";
    case IR_UDIV:         return "udiv";
    case IR_UMOD:         return "umod";
    case IR_LSR:          return "lsr";
    case IR_ALIGN_PTR:    return "align_ptr";
    default:              return "?";
  }
}

/* ---- ir_val_t ヘルパ ---- */

/* imm と fp_imm は匿名 union (排他)。初期化子では片方だけ指定する
 * (両方書くと後勝ちで union を上書きしてしまう)。non-imm は .imm=0 で union を 0 クリア。 */
ir_val_t ir_val_none(void) {
  ir_val_t v = { .id = IR_VAL_NONE, .type = IR_TY_VOID, .imm = 0 };
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

/* ---- アロケータ ---- */

/* メモリ計測用カウンタ (ir_inst_t / ir_block_t は最も大量に確保される)。 */
static size_t ir_inst_count = 0;
static size_t ir_block_count = 0;
size_t ir_inst_total_count(void) { return ir_inst_count; }
size_t ir_block_total_count(void) { return ir_block_count; }

ir_module_t *ir_module_new(void) {
  ir_module_t *m = calloc(1, sizeof(ir_module_t));
  return m;
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
  f->ret_area_vreg = -1;
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
  ir_block_count++;
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
  ir_inst_count++;
  ir_inst_t *i = calloc(1, sizeof(ir_inst_t));
  i->op = op;
  i->dst = ir_val_none();
  i->src1 = ir_val_none();
  i->src2 = ir_val_none();
  i->label_id = -1;
  i->else_label_id = -1;
  i->ret_struct_area = ir_val_none();
  i->callee = ir_val_none();
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
