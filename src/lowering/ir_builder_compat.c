#include "ir_builder_compat.h"

#include "../compilation_session_compat.h"

static ir_build_options_t active_session_options(
    const ag_target_info_t *target) {
  return (ir_build_options_t){
      .target = target,
      .continuation = ag_compilation_session_continuation(
          ag_compilation_session_active_compat()),
  };
}

ir_module_t *ir_build_module(struct node_t **code) {
  ag_target_info_t target =
      ag_compilation_session_effective_target_compat();
  ir_build_options_t options = active_session_options(&target);
  return ir_build_module_with_options(code, &options);
}

int ir_build_emit_function(
    struct node_t *fn, void (*emit_module)(ir_module_t *)) {
  ag_target_info_t target =
      ag_compilation_session_effective_target_compat();
  ir_build_options_t options = active_session_options(&target);
  return ir_build_emit_function_with_options(fn, &options, emit_module);
}

ir_module_t *ir_build_function_module(struct node_t *fn) {
  ag_target_info_t target =
      ag_compilation_session_effective_target_compat();
  ir_build_options_t options = active_session_options(&target);
  return ir_build_function_module_with_options(fn, &options);
}

int ir_build_each_and_emit(
    struct node_t **code, void (*emit_module)(ir_module_t *)) {
  ag_target_info_t target =
      ag_compilation_session_effective_target_compat();
  ir_build_options_t options = active_session_options(&target);
  return ir_build_each_and_emit_with_options(code, &options, emit_module);
}
