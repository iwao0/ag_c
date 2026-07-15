#ifndef AGC_LOWERING_CAST_LOWERING_H
#define AGC_LOWERING_CAST_LOWERING_H

#include "../parser/ast.h"
#include "../compilation_options.h"

node_t *lower_implicit_value_conversion(node_t *operand,
                                        const psx_type_t *target_type,
                                        token_t *fallback_diag_tok,
                                        const ag_compilation_options_t *options);
node_t *lower_source_cast_expression(node_t *node,
                                     token_t *fallback_diag_tok,
                                     const ag_compilation_options_t *options);
node_t *lower_aggregate_address_expression(node_t *node);

#endif
