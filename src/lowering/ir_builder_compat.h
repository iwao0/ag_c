#ifndef AG_IR_BUILDER_COMPAT_H
#define AG_IR_BUILDER_COMPAT_H

#include "ir_builder.h"

ir_module_t *ir_build_module(struct node_t **code);
int ir_build_each_and_emit(
    struct node_t **code, void (*emit_module)(ir_module_t *));
int ir_build_emit_function(
    struct node_t *fn, void (*emit_module)(ir_module_t *));
ir_module_t *ir_build_function_module(struct node_t *fn);

#endif
