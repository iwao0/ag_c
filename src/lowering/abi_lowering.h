#ifndef AG_IR_ABI_LOWERING_H
#define AG_IR_ABI_LOWERING_H

#include "../ir/ir.h"
#include "../ir/ir_data.h"
#include "../semantic/type_identity.h"

struct ag_target_info_t;
struct psx_record_layout_table_t;

typedef struct {
  const psx_semantic_type_table_t *semantic_types;
  const struct psx_record_layout_table_t *record_layouts;
  const struct ag_target_info_t *target;
} ir_abi_type_context_t;

typedef enum {
  IR_ABI_PIECE_DIRECT = 0,
  IR_ABI_PIECE_DIRECT_AGGREGATE,
  IR_ABI_PIECE_INDIRECT,
  IR_ABI_PIECE_COMPLEX_REAL,
  IR_ABI_PIECE_COMPLEX_IMAGINARY,
  IR_ABI_PIECE_VARIADIC,
} ir_abi_piece_kind_t;

typedef struct {
  ir_type_t type;
  size_t source_index;
  int source_size;
  int byte_offset;
  ir_abi_piece_kind_t kind;
} ir_abi_piece_t;

typedef enum {
  IR_ABI_ARGUMENT_DIRECT = 0,
  IR_ABI_ARGUMENT_LOAD,
} ir_abi_argument_access_t;

typedef struct {
  ir_val_t source;
  ir_type_t type;
  int byte_offset;
  ir_abi_argument_access_t access;
} ir_abi_argument_t;

typedef struct {
  ir_abi_piece_t *result_pieces;
  size_t result_count;
  ir_abi_piece_t *param_pieces;
  size_t param_count;
  size_t fixed_param_count;
  ir_val_t result_area;
  unsigned char is_variadic;
} ir_abi_signature_t;

typedef struct {
  const ir_func_t *function;
  ir_abi_signature_t signature;
} ir_abi_function_t;

typedef struct {
  const ir_inst_t *call;
  ir_abi_signature_t signature;
  ir_abi_argument_t *arguments;
  size_t argument_count;
} ir_abi_call_t;

typedef struct {
  const ir_inst_t *reference;
  ir_abi_signature_t signature;
} ir_abi_reference_t;

typedef struct {
  const ir_symbol_func_ref_t *reference;
  ir_abi_signature_t signature;
} ir_abi_symbol_reference_t;

typedef struct ir_abi_module_t {
  ir_abi_function_t *functions;
  size_t function_count;
  ir_abi_call_t *calls;
  size_t call_count;
  ir_abi_reference_t *references;
  size_t reference_count;
  ir_abi_symbol_reference_t *symbol_references;
  size_t symbol_reference_count;
} ir_abi_module_t;

typedef struct {
  const ir_data_reloc_t *relocation;
  ir_abi_signature_t signature;
} ir_abi_data_relocation_t;

typedef struct ir_abi_data_module_t {
  ir_abi_data_relocation_t *relocations;
  size_t relocation_count;
} ir_abi_data_module_t;

ir_abi_module_t *ir_abi_lower_module(
    const ir_abi_type_context_t *context,
    const ir_module_t *module);
void ir_abi_module_free(ir_abi_module_t *module);
const ir_abi_signature_t *ir_abi_function_signature(
    const ir_abi_module_t *module, const ir_func_t *function);
const ir_abi_piece_t *ir_abi_signature_parameter_pieces(
    const ir_abi_signature_t *signature, size_t source_index,
    size_t *piece_count, size_t *physical_index);
const ir_abi_piece_t *ir_abi_signature_result_pieces(
    const ir_abi_signature_t *signature, size_t *piece_count);
int ir_abi_signature_result_is_indirect(
    const ir_abi_signature_t *signature);
int ir_abi_signature_result_is_direct_aggregate(
    const ir_abi_signature_t *signature);
ir_type_t ir_abi_signature_direct_result_type(
    const ir_abi_signature_t *signature);
int ir_abi_signature_result_source_size(
    const ir_abi_signature_t *signature);
const ir_abi_signature_t *ir_abi_call_signature(
    const ir_abi_module_t *module, const ir_inst_t *call);
const ir_abi_argument_t *ir_abi_call_arguments(
    const ir_abi_module_t *module, const ir_inst_t *call,
    size_t *argument_count);
const ir_abi_signature_t *ir_abi_reference_signature(
    const ir_abi_module_t *module, const ir_inst_t *reference);
const ir_abi_signature_t *ir_abi_symbol_reference_signature(
    const ir_abi_module_t *module,
    const ir_symbol_func_ref_t *reference);
ir_abi_data_module_t *ir_abi_lower_data_module(
    const ir_abi_type_context_t *context,
    const ir_data_module_t *module);
void ir_abi_data_module_free(ir_abi_data_module_t *module);
const ir_abi_signature_t *ir_abi_data_relocation_signature(
    const ir_abi_data_module_t *module,
    const ir_data_reloc_t *relocation);

#endif
