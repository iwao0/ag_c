#include "abi_target_policy.h"

#include "../target_info.h"

static const ir_abi_target_policy_t aapcs64_policy = {
    .complex_result_piece_count = 2,
};

static const ir_abi_target_policy_t wasm32_policy = {
    .complex_result_piece_count = 1,
};

const ir_abi_target_policy_t *ir_abi_target_policy_for(
    const ag_target_info_t *target) {
  switch (ag_target_info_call_abi(target)) {
    case AG_TARGET_CALL_ABI_AAPCS64:
      return &aapcs64_policy;
    case AG_TARGET_CALL_ABI_WASM32:
      return &wasm32_policy;
  }
  return NULL;
}
