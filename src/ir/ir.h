/*
 * ag_c 中間表現 (IR) の型定義。
 *
 * 詳細は docs/ir_intermediate_representation/implementation_plan.md。
 *
 * 設計方針:
 *   - 中レベル IR (chibicc 風): 型情報は保持、構造体メンバ/添字は base+offset に展開
 *   - 非 SSA: 各 vreg は複数回書ける
 *   - 無限仮想レジスタ (レジスタ割り付けフェーズで物理化)
 *   - メモリモデル: ローカル変数は ALLOCA + LOAD/STORE
 *   - 3 番地命令: dst = op src1, src2
 */

#ifndef AG_IR_H
#define AG_IR_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* 型システム                                                          */
/* ------------------------------------------------------------------ */

typedef enum {
  IR_TY_VOID = 0,
  IR_TY_I8,
  IR_TY_I16,
  IR_TY_I32,
  IR_TY_I64,
  IR_TY_F32,
  IR_TY_F64,
  IR_TY_PTR,
} ir_type_t;

int ir_type_size(ir_type_t t);
const char *ir_type_name(ir_type_t t);

/* ------------------------------------------------------------------ */
/* オペコード                                                          */
/* ------------------------------------------------------------------ */

typedef enum {
  IR_NOP = 0,

  /* 整数算術 */
  IR_ADD, IR_SUB, IR_MUL, IR_DIV, IR_MOD,
  IR_AND, IR_OR,  IR_XOR, IR_SHL, IR_SHR,
  IR_NEG, IR_NOT,

  /* 比較 (signed)。unsigned は ULT/ULE で区別。結果は IR_TY_I32 (0/1) */
  IR_EQ, IR_NE, IR_LT, IR_LE, IR_ULT, IR_ULE,

  /* 浮動小数算術 */
  IR_FADD, IR_FSUB, IR_FMUL, IR_FDIV, IR_FNEG,
  IR_FEQ, IR_FNE, IR_FLT, IR_FLE,

  /* 型変換 */
  IR_ZEXT, IR_SEXT, IR_TRUNC,
  IR_F2I, IR_I2F, IR_F2F,

  /* メモリ */
  IR_LOAD,
  IR_STORE,
  IR_ALLOCA,
  IR_LEA,
  /* 構造体コピー (memcpy 相当)。src1=dst ptr, src2=src ptr, alloca_size=バイト数。 */
  IR_MEMCPY,

  /* 即値ロード */
  IR_LOAD_IMM, IR_LOAD_FP_IMM, IR_LOAD_STR, IR_LOAD_SYM,

  /* 制御フロー */
  IR_BR, IR_BR_COND, IR_LABEL, IR_RET,

  /* 関数呼び出し */
  IR_CALL,

  /* 関数 prologue: 第 n 仮引数を vreg に読む */
  IR_PARAM,

  /* Apple ARM64 ABI 用 builtin (現 ND_VA_ARG_AREA 相当) */
  IR_VA_ARG_AREA,

  IR_OP_COUNT,
} ir_op_t;

const char *ir_op_name(ir_op_t op);

/* ------------------------------------------------------------------ */
/* 値 (vreg or immediate)                                              */
/* ------------------------------------------------------------------ */

#define IR_VAL_IMM  (-1)
#define IR_VAL_NONE (-2)

typedef struct ir_val_t {
  int id;            /* >= 0: vreg id、IR_VAL_IMM: imm/fp_imm を見る、IR_VAL_NONE: 未使用 */
  ir_type_t type;
  long long imm;     /* 整数 immediate */
  double fp_imm;     /* 浮動小数 immediate */
} ir_val_t;

ir_val_t ir_val_none(void);
ir_val_t ir_val_imm(ir_type_t t, long long imm);
ir_val_t ir_val_fp_imm(ir_type_t t, double v);
ir_val_t ir_val_vreg(int id, ir_type_t t);

/* ------------------------------------------------------------------ */
/* 命令                                                                */
/* ------------------------------------------------------------------ */

typedef struct ir_inst_t {
  struct ir_inst_t *next;
  ir_op_t op;
  ir_val_t dst, src1, src2;

  /* op に応じて使うサブ情報 */
  int label_id;       /* BR / BR_COND / LABEL */
  int else_label_id;  /* BR_COND の偽分岐先 */
  char *sym;          /* CALL / LOAD_SYM / LOAD_STR のシンボル */
  int sym_len;
  ir_val_t *args;     /* CALL の実引数列 */
  int nargs;
  int alloca_size;    /* ALLOCA / MEMCPY: スロットサイズ (バイト) */
  int alloca_align;   /* ALLOCA: アライメント (バイト) */
  /* IR_CALL で戻り値が struct の場合: x8 に渡すバッファのアドレス vreg。
   * ret_struct_size > 0 のときに ret_struct_area が有効。 */
  int ret_struct_size;
  ir_val_t ret_struct_area;
  /* variadic 呼び出し (Apple ARM64 ABI: 可変部分は全て stack)。
   * is_variadic_call > 0 のとき、args[nargs_fixed..nargs-1] は stack に置く。 */
  int is_variadic_call;
  int nargs_fixed;
} ir_inst_t;

/* ------------------------------------------------------------------ */
/* 基本ブロック                                                        */
/* ------------------------------------------------------------------ */

typedef struct ir_block_t {
  struct ir_block_t *next;
  int id;
  ir_inst_t *head, *tail;
} ir_block_t;

/* ------------------------------------------------------------------ */
/* 関数                                                                */
/* ------------------------------------------------------------------ */

typedef struct ir_func_t {
  struct ir_func_t *next;
  char *name;
  int name_len;
  ir_block_t *entry;
  ir_block_t *cur_block;
  ir_block_t *blocks_tail;
  int next_vreg_id;
  int next_block_id;
  int frame_size;
  int is_variadic;
  int nargs_fixed;
  ir_type_t ret_type;
  /* 戻り値が struct のときのサイズ (Apple ABI で x8 隠し引数を使う条件)。
   * 0 = struct 戻り値ではない。 */
  int ret_struct_size;
  /* 関数 prologue で x8 (= caller の戻り値領域ポインタ) を受け取る vreg。
   * ret_struct_size > 0 のときのみ有効。 */
  int ret_area_vreg;
  /* Phase 5: vreg → 物理レジスタ番号 (-1 = spill / frame に置く)。
   * 物理レジスタ番号は実際の x{n} の n。長さ = next_vreg_id。
   * NULL のとき regalloc 未実行 (codegen は全 vreg を frame に置く既存挙動)。 */
  int *vreg_phys_reg;
} ir_func_t;

/* ------------------------------------------------------------------ */
/* グローバル定義                                                      */
/* ------------------------------------------------------------------ */

typedef struct ir_global_t {
  struct ir_global_t *next;
  char *name;
  int name_len;
  int byte_size;
  int elem_size;
  int is_array;
  long long *init_values;
  int init_count;
  long long init_val;
  char *init_symbol;
  int init_symbol_len;
} ir_global_t;

/* ------------------------------------------------------------------ */
/* IR モジュール                                                       */
/* ------------------------------------------------------------------ */

typedef struct ir_module_t {
  ir_func_t *funcs;
  ir_func_t *funcs_tail;
  ir_global_t *globals;
  ir_global_t *globals_tail;
} ir_module_t;

/* ------------------------------------------------------------------ */
/* アロケータ / ビルダー (詳細は ir_builder.h)                          */
/* ------------------------------------------------------------------ */

ir_module_t *ir_module_new(void);
ir_func_t   *ir_func_new(ir_module_t *m, const char *name, int name_len, ir_type_t ret_type);
ir_block_t  *ir_block_new(ir_func_t *f);
ir_inst_t   *ir_inst_new(ir_op_t op);

/* 関数 f の現在ブロックに inst を末尾追加する */
void ir_func_append_inst(ir_func_t *f, ir_inst_t *inst);

/* 新規 vreg を 1 つ確保する */
int ir_func_new_vreg(ir_func_t *f);

/* 新規 label id を 1 つ確保する */
int ir_func_new_label(ir_func_t *f);

/* 関数 f の cur_block を block に切り替える (block は ir_block_new 済み) */
void ir_func_switch_block(ir_func_t *f, ir_block_t *block);

/* ------------------------------------------------------------------ */
/* プリンタ (詳細は ir_print.h)                                        */
/* ------------------------------------------------------------------ */

/* IR をテキスト形式で stdout にダンプ */
void ir_print_module(ir_module_t *m);

/* バッファに書き出す版 (テスト用)。buf の残量を超えたら切り詰める。 */
size_t ir_print_module_to_buf(ir_module_t *m, char *buf, size_t buf_size);

/* ------------------------------------------------------------------ */
/* レジスタ割り付け                                                    */
/* ------------------------------------------------------------------ */

/* Phase 5: 関数単位の線形スキャンレジスタ割り付け。
 * 関数全体で last use を計算し、x19..x28 (10 個の callee-saved) を割り当てる。
 * ループバック検出で live range を延長する。
 * 実行後、f->vreg_phys_reg[v] が -1 なら spill、>=0 なら物理レジスタ番号 (19..28)。 */
void ir_regalloc_function(ir_func_t *f);

/* Phase 6: モジュール全体の最適化パス。 */
void ir_opt_const_fold(ir_module_t *m);     /* 即値伝播 + 算術畳み込み */
void ir_opt_copy_propagate(ir_module_t *m); /* 現状 const_fold が兼ねる */
void ir_opt_dce(ir_module_t *m);            /* 副作用なしで dst が使われない命令を NOP 化 */

#endif /* AG_IR_H */
