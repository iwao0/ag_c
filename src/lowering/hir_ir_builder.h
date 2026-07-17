#ifndef LOWERING_HIR_IR_BUILDER_H
#define LOWERING_HIR_IR_BUILDER_H

#include "../hir/hir.h"
#include "../ir/ir.h"
#include "ir_build_options.h"

typedef enum {
  IR_HIR_BUILD_OK = 0,
  IR_HIR_BUILD_UNSUPPORTED,
  IR_HIR_BUILD_INVALID,
  IR_HIR_BUILD_OUT_OF_MEMORY,
} ir_hir_build_status_t;

/* Transitional direct HIR backend. Unsupported HIR shapes are reported
 * separately so callers may retain the compatibility backend while node
 * coverage is migrated. No parser AST pointer enters this API. */
ir_module_t *ir_build_function_module_from_hir(
    const psx_hir_module_t *hir, psx_hir_node_id_t function_root,
    const ir_build_options_t *options, ir_hir_build_status_t *status);

#endif
