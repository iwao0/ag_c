#ifndef ARCH_WASM32_MACHINE_ABI_H
#define ARCH_WASM32_MACHINE_ABI_H

#include "../../lowering/abi_lowering.h"

typedef struct {
  ir_type_t *params;
  int nparams;
  ir_type_t result;
  unsigned char has_hidden_result;
  unsigned char has_direct_aggregate_result;
} wasm32_machine_signature_t;

int wasm32_machine_signature_from_abi(
    const ir_abi_signature_t *abi,
    int include_hidden_result_parameter,
    wasm32_machine_signature_t *signature);
int wasm32_machine_call_signature(
    const ir_inst_t *call,
    const ir_abi_signature_t *abi,
    wasm32_machine_signature_t *signature);
void wasm32_machine_signature_dispose(
    wasm32_machine_signature_t *signature);

#endif
