#ifndef AG_IR_ABI_LOWERING_H
#define AG_IR_ABI_LOWERING_H

#include "../ir/ir.h"
#include "../semantic/type_identity.h"

struct ag_target_info_t;
struct psx_record_layout_table_t;

typedef struct {
  const psx_semantic_type_table_t *semantic_types;
  const struct psx_record_layout_table_t *record_layouts;
  const struct ag_target_info_t *target;
} ir_abi_type_context_t;

typedef enum {
  IR_ABI_PARAM_UNKNOWN = 0,
  IR_ABI_PARAM_INTEGER,
  IR_ABI_PARAM_FLOAT,
  IR_ABI_PARAM_POINTER,
  IR_ABI_PARAM_AGGREGATE,
} ir_abi_param_class_t;

typedef struct {
  ir_type_t type;
  ir_abi_param_class_t param_class;
  int source_size;
  int is_unsigned;
} ir_abi_param_info_t;

typedef struct {
  ir_abi_param_info_t result;
  ir_type_t *param_types;
  size_t param_count;
  size_t fixed_param_count;
  int result_size;
  int result_area_vreg;
  ir_val_t result_area;
  unsigned char result_is_indirect;
  unsigned char result_complex_half;
  unsigned char is_variadic;
} ir_abi_signature_t;

typedef struct {
  const ir_func_t *function;
  ir_abi_signature_t signature;
} ir_abi_function_t;

typedef struct {
  const ir_inst_t *call;
  ir_abi_signature_t signature;
} ir_abi_call_t;

typedef struct ir_abi_module_t {
  ir_abi_function_t *functions;
  size_t function_count;
  ir_abi_call_t *calls;
  size_t call_count;
} ir_abi_module_t;

ir_abi_param_info_t ir_abi_classify_builtin_param(
    const ir_abi_type_context_t *context,
    const char *name, int name_len, int param_idx);
ir_abi_param_info_t ir_abi_classify_type_id(
    const ir_abi_type_context_t *context, psx_type_id_t type_id);
int ir_abi_callable_sig_from_type_id(
    const ir_abi_type_context_t *context, psx_type_id_t type_id,
    ir_callable_sig_t *out);
ir_abi_module_t *ir_abi_lower_module(
    const ir_abi_type_context_t *context,
    const ir_module_t *module);
void ir_abi_module_free(ir_abi_module_t *module);
const ir_abi_signature_t *ir_abi_function_signature(
    const ir_abi_module_t *module, const ir_func_t *function);
const ir_abi_signature_t *ir_abi_call_signature(
    const ir_abi_module_t *module, const ir_inst_t *call);

#endif
