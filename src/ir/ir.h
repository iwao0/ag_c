/*
 * Type definitions for the ag_c intermediate representation (IR).
 *
 * See docs/ir_intermediate_representation/implementation_plan.md for details.
 *
 * Design:
 *   - Mid-level IR (in the style of chibicc): retain type information and
 *     lower structure members/subscripts to base + offset.
 *   - Non-SSA: each vreg may be written more than once.
 *   - Unlimited virtual registers, made physical during register allocation.
 *   - Memory model: local variables use ALLOCA + LOAD/STORE.
 *   - Three-address instructions: dst = op src1, src2.
 */

#ifndef AG_IR_H
#define AG_IR_H

#include <stdint.h>
#include <stddef.h>

#include "ir_allocation_stats.h"
#include "../type_system/type_ids.h"

/* ------------------------------------------------------------------ */
/* Type system                                                         */
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

typedef struct ag_data_layout_t ag_data_layout_t;

/* Integer and floating MIR widths are intrinsic. Pointer width is deliberately
 * excluded because it belongs to the selected target DataLayout. */
int ir_type_fixed_size(ir_type_t type);
int ir_type_size_for_layout(
    ir_type_t type, const ag_data_layout_t *data_layout);
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
/* Opcodes                                                             */
/* ------------------------------------------------------------------ */

typedef enum {
  IR_NOP = 0,

  /* Integer arithmetic.  SHR is an arithmetic (signed) shift; LSR is a
   * logical (unsigned) shift.  UDIV/UMOD are unsigned division/remainder. */
  IR_ADD, IR_SUB, IR_MUL, IR_DIV, IR_MOD,
  IR_UDIV, IR_UMOD,
  IR_AND, IR_OR,  IR_XOR, IR_SHL, IR_SHR, IR_LSR,
  IR_NEG, IR_NOT,

  /* Signed comparisons; ULT/ULE distinguish unsigned comparisons.
   * The result is IR_TY_I32 (0/1). */
  IR_EQ, IR_NE, IR_LT, IR_LE, IR_ULT, IR_ULE,

  /* Floating-point arithmetic. */
  IR_FADD, IR_FSUB, IR_FMUL, IR_FDIV, IR_FNEG,
  IR_FEQ, IR_FNE, IR_FLT, IR_FLE,

  /* Type conversions. */
  IR_ZEXT, IR_SEXT, IR_TRUNC,
  IR_F2I, IR_I2F, IR_F2F,

  /* Memory. */
  IR_LOAD,
  IR_STORE,
  IR_ALLOCA,
  IR_LEA,
  /* Structure copy (equivalent to memcpy): src1 = destination pointer,
   * src2 = source pointer, alloca_size = byte count. */
  IR_MEMCPY,

  /* Immediate loads. */
  IR_LOAD_IMM, IR_LOAD_FP_IMM, IR_LOAD_STR, IR_LOAD_SYM,
  /* Address reference to a C thread-local object.  The target selects the
   * concrete TLS model and calling convention.  dst is PTR (the TLS variable's
   * address). */
  IR_LOAD_TLS_SYM,

  /* Control flow. */
  IR_BR, IR_BR_COND, IR_LABEL, IR_RET,
  /* Frame-condition boundary for a resumable Wasm entry.
   * label_id/else_label_id are the true/false continuations selected by the
   * condition value supplied by the host. */
  IR_CONTINUATION_SUSPEND,

  /* Function calls. */
  IR_CALL,

  /* Source-level parameter binding. src1 is the destination object address and
   * parameter_index names one parameter in ir_func_t.function_type. The MIR
   * instruction contains no register class, physical parameter index, or ABI
   * piece count; AbiLowering expands it for the selected target. */
  IR_PARAM_BIND,

  /* Current C variadic cursor. The selected backend supplies its ABI-specific
   * representation (frame address, global cursor, or another target form). */
  IR_VARARG_CURSOR,
  /* Dynamic VLA stack allocation: src1 = required byte count (i32/i64),
   * dst = start address of allocated storage (PTR).  Codegen internally aligns
   * to 16 bytes, subtracts from sp, and moves sp to dst.  Because this changes
   * SP, regalloc and DCE treat it as having side effects. */
  IR_VLA_ALLOC,

  /* C11 atomic operations (mapped to Apple ARM64 LSE instructions/barriers).
   * atomic_width = 1/2/4/8/16 (operation width), and atomic_kind is one of the
   * IR_ATOMIC_* values below.  Because 16-byte loads/stores have no scalar IR
   * type, src2 is respectively the output/input 16-byte storage pointer.
   *  - IR_ATOMIC_LOAD:  dst = *src1 (LDAR).
   *  - IR_ATOMIC_STORE: *src1 = src2 (STLR).
   *  - IR_ATOMIC_RMW:   dst = old(*src1); *src1 op= src2 (LDADDAL/LDSETAL/...).
   *                     op is atomic_rmw_op (ADD/SUB/OR/AND/XOR/XCHG).
   *  - IR_ATOMIC_CAS:   src1=ptr, src2=expected-ptr, src3=desired;
   *                     dst = success (0/1).
   *    For 16-byte CAS, src3 is also a desired-value storage pointer.
   *  - IR_ATOMIC_FENCE: DMB ISH. */
  IR_ATOMIC,

  /* Round an over-aligned local (_Alignas(>16)) address at runtime:
   * dst = (src1 + (alloca_align-1)) & ~(alloca_align-1).  No side effects. */
  IR_ALIGN_PTR,

  IR_OP_COUNT,
} ir_op_t;

/* IR_ATOMIC kinds. */
typedef enum {
  IR_ATOMIC_LOAD = 0,
  IR_ATOMIC_STORE,
  IR_ATOMIC_RMW,
  IR_ATOMIC_CAS,
  IR_ATOMIC_FENCE,
} ir_atomic_kind_t;

/* IR_ATOMIC_RMW operations. */
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
/* Values (vreg or immediate)                                          */
/* ------------------------------------------------------------------ */

#define IR_VAL_IMM  (-1)
#define IR_VAL_NONE (-2)

typedef struct ir_val_t {
  int id;            /* >= 0: vreg ID; IR_VAL_IMM: use imm/fp_imm; IR_VAL_NONE: unused. */
  ir_type_t type;
  /* imm and fp_imm are mutually exclusive (a value is either an integer or a
   * floating immediate).  Sharing them in an anonymous union reduces ir_val_t
   * from 24B to 16B.  Reads are always gated by type/op: fp_imm only for
   * F32/F64 or IR_LOAD_FP_IMM, and imm otherwise. */
  union {
    long long imm;   /* Integer immediate (IR_VAL_IMM and non-FP type). */
    double fp_imm;   /* Floating immediate (IR_VAL_IMM and F32/F64). */
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
/* Instructions                                                        */
/* ------------------------------------------------------------------ */

/*
 * Fields are ordered by decreasing alignment to reduce padding.  Hot fields
 * read by codegen/regalloc for every instruction (op/dst/src1/src2) are near
 * the front, and nargs fills the 4-byte gap after op.
 *
 * The anonymous union (standard C11, not a GNU extension) contains only scalar
 * metadata used exclusively by a particular op.  args/nargs/callee/
 * result_storage/src3 remain outside it because generic operand traversal in
 * ir_opt/ir_regalloc reads them for all instructions regardless of op; sharing
 * their storage with another op's values would cause misreads.  Reordering
 * changes layout only and not behavior because ir_inst_new uses calloc and
 * assigns fields individually.
 */
typedef struct ir_inst_t {
  struct ir_inst_t *next;
  ir_op_t op;
  int nargs;          /* CALL argument count; fills the 4-byte gap after op and is generically traversed. */
  ir_val_t dst, src1, src2;

  /* --- 8 bytes (pointer / ir_val_t), outside the union for generic traversal. --- */
  char *sym;          /* Symbol for CALL / LOAD_SYM / LOAD_STR. */
  /* Canonical C function type for CALL/function LOAD_SYM references, resolved
   * by the semantic pass.  The backend carries it directly into object
   * metadata without consulting the semantic type table. */
  char *reference_c_signature;
  ir_call_argument_t *args; /* Source-level argument sequence for CALL. */
  /* Storage receiving an IR_CALL source-level object result.  The AbiLowering
   * sidecar decides direct/indirect return and decomposition into register pieces. */
  ir_val_t result_storage;
  /* Callee value (function pointer) for indirect calls.  When
   * id != IR_VAL_NONE, blr the callee vreg instead of sym. */
  ir_val_t callee;
  ir_val_t src3;               /* Desired value for IR_ATOMIC_CAS. */

  /* --- 4 bytes --- */
  int sym_len;        /* Symbol length for CALL / LOAD_*, paired with sym. */
  int reference_c_signature_len;
  int object_size;    /* IR_LOAD_STR: lowered data size including terminator. */

  /* --- 1 byte (outside the union because multiple op families share it) --- */
  /* IR_LOAD / IR_ATOMIC: 1 for unsigned (zero extension), 0 for signed (sign
   * extension).  IR_I2F / IR_F2I: 1 for unsigned conversion.  When a 32-bit
   * unsigned value is held in a 64-bit register, its upper 32 bits must be
   * cleared or later LSR/UDIV/ULT operations will behave incorrectly. */
  unsigned char is_unsigned;
  ir_function_type_t function_type;
  /* IR_LOAD_SYM linkage and symbol category. The selected backend decides the
   * concrete address materialization strategy. */
  unsigned char is_external_symbol;
  unsigned char is_function_symbol;
  unsigned char has_function_type;

  /* --- Mutually exclusive per-op scalar metadata sharing an anonymous union. ---
   * Each instruction has one op and reads/writes only the corresponding arm.
   * Every read is gated by op (switch(op) / if(op==...)).  Do not place
   * generically traversed args/nargs/callee/result_storage/src3 here. */
  union {
    struct {            /* IR_BR / IR_BR_COND / IR_LABEL */
      int label_id;       /* Branch target / label ID. */
      int else_label_id;  /* False target of BR_COND. */
    };
    struct {            /* IR_ALLOCA / IR_MEMCPY / IR_ALIGN_PTR */
      int alloca_size;    /* Slot size in bytes. */
      int alloca_align;   /* Alignment in bytes. */
    };
    struct {            /* IR_CALL */
      unsigned char is_void_call;
      unsigned char is_implicit_call;
      unsigned char is_noreturn_call;
    };
    struct {            /* IR_PARAM_BIND */
      size_t parameter_index;
    };
    struct {            /* IR_ATOMIC */
      unsigned char atomic_kind;   /* ir_atomic_kind_t */
      unsigned char atomic_rmw_op; /* ir_atomic_rmw_op_t for RMW. */
      unsigned char atomic_width;  /* 1/2/4/8/16 bytes. */
    };
  };
} ir_inst_t;

/* ------------------------------------------------------------------ */
/* Basic blocks                                                        */
/* ------------------------------------------------------------------ */

typedef struct ir_block_t {
  struct ir_block_t *next;
  int id;
  ir_inst_t *head, *tail;
} ir_block_t;

/* ------------------------------------------------------------------ */
/* Functions                                                           */
/* ------------------------------------------------------------------ */

/* Fields are ordered by decreasing alignment (8 to 4 bytes). */
typedef struct ir_func_t {
  struct ir_func_t *next;
  ir_allocation_stats_t *allocation_stats;
  size_t tracked_instruction_count;
  size_t tracked_block_count;
  char *name;
  char *c_signature; /* Canonical C function type from semantics; the backend does not revisit the parser. */
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
  /* Phase 5: vreg to physical register number (-1 = spill/place in frame).
   * A physical register number is n in the actual x{n}; length = next_vreg_id.
   * NULL means regalloc has not run, preserving the existing codegen behavior
   * of placing every vreg in the frame. */
  int *vreg_phys_reg;
  int name_len;
  int c_signature_len;
  int next_vreg_id;
  int next_block_id;
  int frame_size;
  int is_static;      /* 1: static function (internal linkage); suppress .global in codegen. */
  int continuation_condition_block_id;
  unsigned char is_continuation_entry;
  unsigned char continuation_has_suspend;
} ir_func_t;

/* ------------------------------------------------------------------ */
/* Global definitions                                                  */
/* ------------------------------------------------------------------ */

/* Fields are ordered by decreasing alignment (8 to 4 bytes) to remove padding
 * (sizeof = 64B, versus 72B before reordering). */
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

/* Symbol information resolved by semantics/lowering.  The backend reads object
 * layout and initial function-pointer values from this table without returning
 * to the parser registry. */
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
/* IR module                                                           */
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

/* Free one function or an entire IR module (for per-function streaming codegen). */
void ir_func_free(ir_func_t *f);
void ir_module_free(ir_module_t *m);

/* Append inst to the current block of function f. */
void ir_func_append_inst(ir_func_t *f, ir_inst_t *inst);

/* Allocate one new vreg. */
int ir_func_new_vreg(ir_func_t *f);

/* Allocate one new label ID. */
int ir_func_new_label(ir_func_t *f);

/* Switch function f's cur_block to block, which has already been created by ir_block_new. */
void ir_func_switch_block(ir_func_t *f, ir_block_t *block);

/* ------------------------------------------------------------------ */
/* Printer (see ir_print.h for details)                                */
/* ------------------------------------------------------------------ */

/* Dump IR to stdout in text form. */
void ir_print_module(ir_module_t *m);

/* Buffer-output variant for tests; truncate when buf lacks remaining space. */
size_t ir_print_module_to_buf(ir_module_t *m, char *buf, size_t buf_size);

/* ------------------------------------------------------------------ */
/* Register allocation                                                 */
/* ------------------------------------------------------------------ */

/* Phase 5: per-function linear-scan register allocation.
 * Compute last use across the function and allocate x19..x28 (10 callee-saved
 * registers).  Loop-back detection extends live ranges.  After execution,
 * f->vreg_phys_reg[v] is -1 for a spill or >= 0 for physical register 19..28. */
void ir_regalloc_function(ir_func_t *f);

/* Phase 6: whole-module optimization passes. */
void ir_opt_const_fold(
    ir_module_t *m, const ag_data_layout_t *data_layout);
                                             /* Immediate propagation + arithmetic folding. */
void ir_opt_copy_propagate(ir_module_t *m); /* Currently covered by const_fold. */
void ir_opt_dce(ir_module_t *m);            /* NOP side-effect-free instructions with unused dst. */

#endif /* AG_IR_H */
