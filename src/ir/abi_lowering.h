#ifndef AG_IR_ABI_LOWERING_H
#define AG_IR_ABI_LOWERING_H

#include "ir.h"

struct psx_type_t;

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

ir_abi_param_info_t ir_abi_classify_function_param(char *name, int name_len,
                                                    int param_idx);
ir_abi_param_info_t ir_abi_classify_param_type(
    const struct psx_type_t *type);
int ir_abi_callable_sig_from_type(
    const struct psx_type_t *type, ir_callable_sig_t *out);

#endif
