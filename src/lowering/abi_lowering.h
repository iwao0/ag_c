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

ir_abi_param_info_t ir_abi_classify_builtin_param(
    const ir_abi_type_context_t *context,
    const char *name, int name_len, int param_idx);
ir_abi_param_info_t ir_abi_classify_type_id(
    const ir_abi_type_context_t *context, psx_type_id_t type_id);
int ir_abi_callable_sig_from_type_id(
    const ir_abi_type_context_t *context, psx_type_id_t type_id,
    ir_callable_sig_t *out);

#endif
