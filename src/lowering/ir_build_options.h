#ifndef LOWERING_IR_BUILD_OPTIONS_H
#define LOWERING_IR_BUILD_OPTIONS_H

#include "../continuation_options.h"
#include "../target_info.h"

typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;
typedef struct psx_semantic_type_table_t psx_semantic_type_table_t;
typedef struct psx_record_decl_table_t psx_record_decl_table_t;
typedef struct psx_record_layout_table_t psx_record_layout_table_t;

typedef struct {
  const ag_target_info_t *target;
  const psx_semantic_type_table_t *semantic_types;
  const psx_record_decl_table_t *record_decls;
  const psx_record_layout_table_t *record_layouts;
  const ag_continuation_options_t *continuation;
  ag_diagnostic_context_t *diagnostic_context;
} ir_build_options_t;

#endif
