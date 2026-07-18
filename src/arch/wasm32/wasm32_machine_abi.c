#include "wasm32_machine_abi.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "wasm32_machine_ir.h"

static int result_is_indirect(const ir_abi_signature_t *abi) {
  return abi && abi->result_count == 1 && abi->result_pieces &&
         abi->result_pieces[0].kind == IR_ABI_PIECE_INDIRECT;
}

static int result_is_direct_aggregate(const ir_abi_signature_t *abi) {
  return abi && abi->result_count == 1 && abi->result_pieces &&
         abi->result_pieces[0].kind == IR_ABI_PIECE_DIRECT_AGGREGATE;
}

static ir_type_t direct_result_type(const ir_abi_signature_t *abi) {
  if (!abi || abi->result_count != 1 || !abi->result_pieces ||
      result_is_indirect(abi))
    return IR_TY_VOID;
  return wasm32_machine_value_type(abi->result_pieces[0].type);
}

static int allocate_parameters(
    wasm32_machine_signature_t *signature, int count) {
  if (count < 0) return 0;
  signature->nparams = count;
  if (count == 0) return 1;
  signature->params = calloc((size_t)count, sizeof(*signature->params));
  return signature->params != NULL;
}

void wasm32_machine_signature_dispose(
    wasm32_machine_signature_t *signature) {
  if (!signature) return;
  free(signature->params);
  memset(signature, 0, sizeof(*signature));
}

int wasm32_machine_signature_from_abi(
    const ir_abi_signature_t *abi,
    int include_hidden_result_parameter,
    wasm32_machine_signature_t *signature) {
  if (!abi || !signature || abi->param_count > INT_MAX ||
      (abi->param_count > 0 && !abi->param_pieces))
    return 0;
  memset(signature, 0, sizeof(*signature));
  int has_hidden_result = result_is_indirect(abi);
  int hidden_parameter =
      include_hidden_result_parameter && has_hidden_result;
  if (!allocate_parameters(
          signature, (int)abi->param_count + hidden_parameter))
    return 0;
  signature->has_hidden_result = has_hidden_result ? 1 : 0;
  signature->has_direct_aggregate_result =
      result_is_direct_aggregate(abi) ? 1 : 0;
  signature->result = has_hidden_result
                          ? IR_TY_VOID
                          : direct_result_type(abi);
  if (hidden_parameter) signature->params[0] = IR_TY_I32;
  for (size_t i = 0; i < abi->param_count; i++) {
    signature->params[i + (size_t)hidden_parameter] =
        wasm32_machine_value_type(abi->param_pieces[i].type);
  }
  return 1;
}

int wasm32_machine_call_signature(
    const ir_inst_t *call,
    const ir_abi_signature_t *abi,
    wasm32_machine_signature_t *signature) {
  if (!call || !abi || !signature ||
      (abi->param_count > 0 && !abi->param_pieces) ||
      abi->fixed_param_count > abi->param_count ||
      abi->fixed_param_count > INT_MAX)
    return 0;
  memset(signature, 0, sizeof(*signature));
  int has_hidden_result = result_is_indirect(abi);
  int direct_aggregate = result_is_direct_aggregate(abi);
  int parameter_count =
      (int)abi->fixed_param_count + (has_hidden_result ? 1 : 0);
  if (!allocate_parameters(signature, parameter_count)) return 0;
  signature->has_hidden_result = has_hidden_result ? 1 : 0;
  signature->has_direct_aggregate_result = direct_aggregate ? 1 : 0;
  if (has_hidden_result) signature->params[0] = IR_TY_I32;
  for (size_t i = 0; i < abi->fixed_param_count; i++) {
    signature->params[i + (size_t)has_hidden_result] =
        wasm32_machine_value_type(abi->param_pieces[i].type);
  }
  if (has_hidden_result || call->is_void_call ||
      (!direct_aggregate &&
       (call->dst.id == IR_VAL_NONE || call->dst.type == IR_TY_VOID))) {
    signature->result = IR_TY_VOID;
  } else if (direct_aggregate) {
    signature->result = direct_result_type(abi);
  } else {
    signature->result = wasm32_machine_value_type(call->dst.type);
  }
  return 1;
}
