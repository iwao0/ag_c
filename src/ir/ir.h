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

#include "ir_allocation_stats.h"
#include "../type_system/type_ids.h"

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

/* Target-independent C function type retained by MIR. TypeId/QualType are
 * semantic identities; target register classes, sizes and ABI pieces belong
 * to AbiLowering and are intentionally absent here. */
typedef struct {
  /* Compiler-generated functions may not have an interned source TypeId;
   * their result/params still form a complete target-independent type. */
  psx_type_id_t type_id;
  psx_qual_type_t result;
  psx_qual_type_t *params;
  size_t param_count;
  unsigned char is_variadic;
  unsigned char has_prototype;
} ir_function_type_t;

int ir_function_type_set(
    ir_function_type_t *type, psx_type_id_t type_id,
    psx_qual_type_t result, const psx_qual_type_t *params,
    size_t param_count, int is_variadic, int has_prototype);
int ir_function_type_copy(
    ir_function_type_t *destination,
    const ir_function_type_t *source);
void ir_function_type_dispose(ir_function_type_t *type);

/* ------------------------------------------------------------------ */
/* オペコード                                                          */
/* ------------------------------------------------------------------ */

typedef enum {
  IR_NOP = 0,

  /* 整数算術。SHR は算術シフト (signed)。LSR は論理シフト (unsigned)。
   * UDIV/UMOD は unsigned 除算/剰余。 */
  IR_ADD, IR_SUB, IR_MUL, IR_DIV, IR_MOD,
  IR_UDIV, IR_UMOD,
  IR_AND, IR_OR,  IR_XOR, IR_SHL, IR_SHR, IR_LSR,
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
  /* Apple ARM64 TLS: _<sym>@TLVPPAGE 経由で thread-local 変数のアドレスを解決。
   * 内部で __tlv_bootstrap (関数ポインタ) を blr するため、IR_CALL と同様に
   * caller-saved レジスタを clobber する。dst は PTR (TLS 変数のアドレス)。 */
  IR_LOAD_TLV_ADDR,

  /* 制御フロー */
  IR_BR, IR_BR_COND, IR_LABEL, IR_RET,
  /* Resumable Wasm entryのframe条件境界。label_id/else_label_idは
   * hostから渡された条件値のtrue/false継続先。 */
  IR_CONTINUATION_SUSPEND,

  /* 関数呼び出し */
  IR_CALL,

  /* Source-level parameter binding. src1 is the destination object address and
   * parameter_index names one parameter in ir_func_t.function_type. The MIR
   * instruction contains no register class, physical parameter index, or ABI
   * piece count; AbiLowering expands it for the selected target. */
  IR_PARAM_BIND,

  /* Current C variadic cursor. The selected backend supplies its ABI-specific
   * representation (frame address, global cursor, or another target form). */
  IR_VARARG_CURSOR,
  /* VLA 動的スタック確保: src1 = 必要バイト数 (i32/i64)、dst = 確保した領域の
   * 先頭アドレス (PTR)。codegen は内部で 16-byte アライン → sub sp, sp → mov dst, sp。
   * SP を変更するため副作用ありで、regalloc/DCE は副作用扱いにする。 */
  IR_VLA_ALLOC,

  /* C11 アトミック操作 (Apple ARM64 LSE 命令 / バリアにマップ)。
   * atomic_width = 1/2/4/8 (操作幅)。atomic_kind = 下記 IR_ATOMIC_* のいずれか。
   *  - IR_ATOMIC_LOAD:  dst = *src1 (LDAR)。
   *  - IR_ATOMIC_STORE: *src1 = src2 (STLR)。
   *  - IR_ATOMIC_RMW:   dst = old(*src1); *src1 op= src2 (LDADDAL/LDSETAL/...)。
   *                     op は atomic_rmw_op (ADD/SUB/OR/AND/XOR/XCHG)。
   *  - IR_ATOMIC_CAS:   src1=ptr, src2=expected-ptr, src3=desired; dst = 成否 (0/1)。
   *  - IR_ATOMIC_FENCE: DMB ISH。 */
  IR_ATOMIC,

  /* 過剰整列ローカル (_Alignas(>16)) のアドレスを実行時に丸める。
   * dst = (src1 + (alloca_align-1)) & ~(alloca_align-1)。副作用なし。 */
  IR_ALIGN_PTR,

  IR_OP_COUNT,
} ir_op_t;

/* IR_ATOMIC の種別。 */
typedef enum {
  IR_ATOMIC_LOAD = 0,
  IR_ATOMIC_STORE,
  IR_ATOMIC_RMW,
  IR_ATOMIC_CAS,
  IR_ATOMIC_FENCE,
} ir_atomic_kind_t;

/* IR_ATOMIC_RMW の演算。 */
typedef enum {
  IR_ARMW_ADD = 0,
  IR_ARMW_SUB,
  IR_ARMW_OR,
  IR_ARMW_AND,
  IR_ARMW_XOR,
  IR_ARMW_XCHG,
} ir_atomic_rmw_op_t;

const char *ir_op_name(ir_op_t op);

/* ------------------------------------------------------------------ */
/* 値 (vreg or immediate)                                              */
/* ------------------------------------------------------------------ */

#define IR_VAL_IMM  (-1)
#define IR_VAL_NONE (-2)

typedef struct ir_val_t {
  int id;            /* >= 0: vreg id、IR_VAL_IMM: imm/fp_imm を見る、IR_VAL_NONE: 未使用 */
  ir_type_t type;
  /* imm と fp_imm は排他 (値は整数即値か浮動小数即値のどちらか一方)。匿名 union で
   * 共有し ir_val_t を 24→16B に縮小する。read は常に type / op でゲートされる
   * (F32/F64 または IR_LOAD_FP_IMM のときだけ fp_imm、それ以外は imm)。 */
  union {
    long long imm;   /* 整数 immediate (IR_VAL_IMM かつ非 fp 型) */
    double fp_imm;   /* 浮動小数 immediate (IR_VAL_IMM かつ F32/F64) */
  };
} ir_val_t;

ir_val_t ir_val_none(void);
ir_val_t ir_val_imm(ir_type_t t, long long imm);
ir_val_t ir_val_fp_imm(ir_type_t t, double v);
ir_val_t ir_val_vreg(int id, ir_type_t t);

typedef enum {
  IR_CALL_ARGUMENT_VALUE = 0,
  IR_CALL_ARGUMENT_ADDRESS,
} ir_call_argument_representation_t;

/* One source-level C argument. Aggregate and complex values are represented
 * by an address; AbiLowering expands that value into target-specific pieces. */
typedef struct {
  ir_val_t value;
  psx_qual_type_t type;
  ir_call_argument_representation_t representation;
} ir_call_argument_t;

/* ------------------------------------------------------------------ */
/* 命令                                                                */
/* ------------------------------------------------------------------ */

/*
 * フィールドはアライメント降順に寄せ、パディングを抑えている。codegen / regalloc が
 * 毎命令で読むホットフィールド (op / dst / src1 / src2) は先頭側に置き、op のあとの
 * 4 バイト穴を nargs で埋めている。
 *
 * 匿名 union (C11 標準・GNU 拡張ではない) には op ごとに排他的にしか使われないスカラ系
 * メタフィールドだけを入れている。ir_opt / ir_regalloc の汎用オペランド走査が op を
 * 問わず全命令で読む args / nargs / callee / result_storage / src3 は union に入れない
 * (同一メモリを別 op の値と共有すると誤読する)。並べ替えはレイアウトのみの変更で、
 * ir_inst_new が calloc + フィールド代入のため挙動には影響しない。
 */
typedef struct ir_inst_t {
  struct ir_inst_t *next;
  ir_op_t op;
  int nargs;          /* CALL の実引数個数 (op 直後の 4 バイト穴を埋める。汎用走査される) */
  ir_val_t dst, src1, src2;

  /* --- 8 バイト (ポインタ / ir_val_t)。汎用オペランド走査が触るため union 外 --- */
  char *sym;          /* CALL / LOAD_SYM / LOAD_STR のシンボル */
  ir_call_argument_t *args; /* CALL のsource-level実引数列 */
  /* IR_CALL のsource-level object結果を受け取る保存先。直接/間接returnや
   * register pieceへの分解はAbiLowering sidecarが決定する。 */
  ir_val_t result_storage;
  /* 間接呼び出し時の callee 値 (関数ポインタ)。id != IR_VAL_NONE のとき
   * sym ではなく callee vreg を blr する。 */
  ir_val_t callee;
  ir_val_t src3;               /* IR_ATOMIC_CAS の desired 値 */

  /* --- 4 バイト --- */
  int sym_len;        /* CALL / LOAD_* のシンボル長 (sym と対) */
  int object_size;    /* IR_LOAD_STR: 終端を含むlower済みデータサイズ */

  /* --- 1 バイト (複数 op 族で共有するため union 外) --- */
  /* IR_LOAD / IR_ATOMIC: unsigned (zero-extend) なら 1。signed (sign-extend) なら 0。
   * IR_I2F / IR_F2I: unsigned 変換なら 1。
   * 32bit unsigned 値を 64bit reg で扱うとき、上位 32bit を 0 にしないと
   * 後段の LSR/UDIV/ULT が誤動作するので必須。 */
  unsigned char is_unsigned;
  ir_function_type_t function_type;
  /* IR_LOAD_SYM: 関数シンボルのアドレス参照 (関数ポインタ値)。外部関数 (libc 等) の
   * アドレスは Apple ARM64 では GOT 経由 (@GOTPAGE/@GOTPAGEOFF + ldr) が必須で、直接
   * adrp @PAGE だと「does not have address」リンクエラーになる。GOT はローカル定義にも
   * 有効なので関数アドレスは常に GOT 経由にする。 */
  unsigned char is_got_funcref;
  unsigned char is_function_symbol;
  unsigned char has_function_type;

  /* --- op ごとに排他なスカラ系メタ (匿名 union で同一メモリを共有) ---
   * 各命令は単一 op で対応アームのみを読み書きする。読み出しは全て op で
   * ゲートされている (switch(op) / if(op==...))。ここに汎用走査される
   * args/nargs/callee/result_storage/src3 を入れてはならない。 */
  union {
    struct {            /* IR_BR / IR_BR_COND / IR_LABEL */
      int label_id;       /* 分岐先 / ラベル id */
      int else_label_id;  /* BR_COND の偽分岐先 */
    };
    struct {            /* IR_ALLOCA / IR_MEMCPY / IR_ALIGN_PTR */
      int alloca_size;    /* スロットサイズ (バイト) */
      int alloca_align;   /* アライメント (バイト) */
    };
    struct {            /* IR_CALL */
      unsigned char is_void_call;
      unsigned char is_implicit_call;
    };
    struct {            /* IR_PARAM_BIND */
      size_t parameter_index;
    };
    struct {            /* IR_ATOMIC */
      unsigned char atomic_kind;   /* ir_atomic_kind_t */
      unsigned char atomic_rmw_op; /* ir_atomic_rmw_op_t (RMW のとき) */
      unsigned char atomic_width;  /* 1/2/4/8 バイト */
    };
  };
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

/* フィールドはアライメント降順 (8→4 バイト) に並べる。 */
typedef struct ir_func_t {
  struct ir_func_t *next;
  ir_allocation_stats_t *allocation_stats;
  size_t tracked_instruction_count;
  size_t tracked_block_count;
  char *name;
  char *c_signature; /* semantic pass由来のcanonical C関数型。backendはparserを再参照しない。 */
  char *continuation_entry_name;
  char *continuation_condition_name;
  char *continuation_start_export;
  char *continuation_resume_export;
  char *continuation_status_export;
  char *continuation_result_export;
  ir_function_type_t function_type;
  ir_block_t *entry;
  ir_block_t *cur_block;
  ir_block_t *blocks_tail;
  /* Phase 5: vreg → 物理レジスタ番号 (-1 = spill / frame に置く)。
   * 物理レジスタ番号は実際の x{n} の n。長さ = next_vreg_id。
   * NULL のとき regalloc 未実行 (codegen は全 vreg を frame に置く既存挙動)。 */
  int *vreg_phys_reg;
  int name_len;
  int c_signature_len;
  int next_vreg_id;
  int next_block_id;
  int frame_size;
  int is_static;      /* 1: static 関数 (内部リンケージ)。codegen で .global を抑制する。 */
  int continuation_condition_block_id;
  unsigned char is_continuation_entry;
  unsigned char continuation_has_suspend;
} ir_func_t;

/* ------------------------------------------------------------------ */
/* グローバル定義                                                      */
/* ------------------------------------------------------------------ */

/* フィールドはアライメント降順 (8→4 バイト) に並べてパディングを除いている
 * (sizeof=64B、並べ替え前は 72B)。 */
typedef struct ir_global_t {
  struct ir_global_t *next;
  char *name;
  long long *init_values;
  long long init_val;
  char *init_symbol;
  int name_len;
  int byte_size;
  int elem_size;
  int is_array;
  int init_count;
  int init_symbol_len;
} ir_global_t;

/* semantic/loweringで解決済みのシンボル情報。backendはparser registryへ
 * 戻らず、この表からobject layoutと初期関数ポインタ値を読む。 */
typedef struct ir_symbol_func_ref_t {
  struct ir_symbol_func_ref_t *next;
  char *name;
  int name_len;
  int offset;
  ir_function_type_t function_type;
  unsigned char has_function_type;
} ir_symbol_func_ref_t;

typedef struct ir_symbol_t {
  struct ir_symbol_t *next;
  char *name;
  int name_len;
  int byte_size;
  int alignment;
  unsigned char is_extern;
  unsigned char is_static;
  unsigned char is_thread_local;
  ir_symbol_func_ref_t *func_refs;
  ir_symbol_func_ref_t *func_refs_tail;
} ir_symbol_t;

/* ------------------------------------------------------------------ */
/* IR モジュール                                                       */
/* ------------------------------------------------------------------ */

typedef struct ir_module_t {
  ir_allocation_stats_t *allocation_stats;
  ir_func_t *funcs;
  ir_func_t *funcs_tail;
  ir_global_t *globals;
  ir_global_t *globals_tail;
  ir_symbol_t *symbols;
  ir_symbol_t *symbols_tail;
} ir_module_t;

/* ------------------------------------------------------------------ */
/* IR model allocation helpers                                          */
/* ------------------------------------------------------------------ */

ir_module_t *ir_module_new(void);
ir_module_t *ir_module_new_with_allocation_stats(
    ir_allocation_stats_t *stats);
ir_symbol_t *ir_module_find_symbol(const ir_module_t *m,
                                   const char *name, int name_len);
ir_symbol_t *ir_module_add_symbol(ir_module_t *m,
                                  const char *name, int name_len);
ir_symbol_func_ref_t *ir_symbol_add_func_ref(
    ir_symbol_t *symbol, int offset, const char *name, int name_len,
    const ir_function_type_t *function_type);
const ir_symbol_func_ref_t *ir_symbol_find_func_ref(
    const ir_symbol_t *symbol, int offset);
ir_func_t   *ir_func_new(ir_module_t *m, const char *name, int name_len);
ir_block_t  *ir_block_new(ir_func_t *f);
ir_inst_t   *ir_inst_new(ir_op_t op);

/* 関数 1 つ / モジュール全体の IR を解放する (関数ごとストリーミング codegen 用)。 */
void ir_func_free(ir_func_t *f);
void ir_module_free(ir_module_t *m);

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
