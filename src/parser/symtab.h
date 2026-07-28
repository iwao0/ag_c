#ifndef SYMTAB_H
#define SYMTAB_H

/* Parser symbol payload and literal-table definitions.
 * The scope graph owns global_var_t identity and enumeration order.
 * AST node definitions remain in ast.h. */

#include "../tokenizer/token.h"
#include "gvar_public.h"
#include "literal_public.h"

typedef struct psx_semantic_type_table_t psx_semantic_type_table_t;

// Global-variable declaration payload.
//
// Fields are ordered by decreasing alignment (8, 4, 2, 1 bytes), and Boolean
// flags are combined into bit-fields to reduce padding.
struct global_var_t {
  // --- 8 bytes (pointer / long long / double) ---
  char *name;
  char *init_symbol;  // Symbol name in an address initializer (&g -> "g").
  // Array `{...}` initializer: retain a flattened value sequence.
  // Multidimensional `{{1,2,3},{4,5,6}}` is flattened in row-major order.
  // When init_count > 0, codegen emits init_values[] in element-size units.
  long long *init_values;
  // Floating-point arrays only (non-NULL only for float/double elements).
  // fvalues[i] is authoritative and codegen emits its bit pattern;
  // init_values[i] is unused.
  double *init_fvalues;
  /* Per-initializer-slot symbol reference (function or global-variable name);
   * NULL denotes a numeric value.  Used for function-pointer member
   * initializers such as `struct Op { int (*f)(int); } gop = {sq};`. */
  char **init_value_symbols;
  int *init_value_symbol_lens;
  int *init_union_ordinals;  // Active union-member ordinal per slot (-1 = unspecified).
  int *init_offsets;  // Object-relative byte offset of a static aggregate scalar leaf.
  psx_gvar_union_activation_t *init_union_activations;
  long long init_val; // Initial integer constant for a scalar.
  long long init_symbol_offset;  // Byte offset from the symbol in `&a[1]` / `a+1`.
  double fval;        // Initial floating scalar value (valid when fp_kind != NONE).

  // --- 4 bytes (int / enum) ---
  int name_len;
  int init_symbol_len;
  int union_init_ordinal;  // Active-member ordinal for union `{.m=v}` (default 0 = first).
  int init_count;
  int init_union_activation_count;
  int init_union_activation_capacity;
  int requested_alignment;
  // Bit flags (one 4-byte unsigned-int container).  Boolean flags are grouped here.
  unsigned int is_extern_decl : 1; // 1: extern declaration only (no .comm).
  unsigned int is_static : 1;      // 1: static (internal linkage); emit .zerofill, not .global/.comm.
  unsigned int has_init : 1;       // 1: has an initializer.
  unsigned int is_thread_local : 1; // 1: _Thread_local
  unsigned int is_compiler_generated : 1;
  unsigned int is_compound_literal : 1;
  unsigned int has_alignment_specifier : 1;
  const psx_semantic_type_table_t *decl_type_table;
  psx_qual_type_t decl_qual_type;
};
// String-literal table (linked list).
struct string_lit_t {
  string_lit_t *next;
  char *label;
  char *str;
  int len;
  tk_char_width_t char_width;
  tk_string_prefix_kind_t str_prefix_kind;
};

// Floating-point literal table (linked list).
// Fields are ordered by decreasing alignment (8, 4) to remove internal padding.
struct float_lit_t {
  float_lit_t *next;
  double fval;
  int id;
  tk_float_kind_t fp_kind;
  tk_float_suffix_kind_t float_suffix_kind;
};

#endif
