#ifndef AG_MIR_TYPE_LOWERING_H
#define AG_MIR_TYPE_LOWERING_H

#include "../ir/ir.h"
#include "../semantic/type_identity.h"

struct ag_data_layout_t;
struct psx_record_layout_table_t;

typedef struct {
  const psx_semantic_type_table_t *semantic_types;
  const struct psx_record_layout_table_t *record_layouts;
  const struct ag_data_layout_t *data_layout;
} ir_mir_type_context_t;

typedef enum {
  IR_MIR_TYPE_UNKNOWN = 0,
  IR_MIR_TYPE_INTEGER,
  IR_MIR_TYPE_FLOAT,
  IR_MIR_TYPE_POINTER,
  IR_MIR_TYPE_COMPLEX,
  IR_MIR_TYPE_AGGREGATE,
} ir_mir_type_class_t;

typedef struct {
  ir_type_t type;
  ir_mir_type_class_t type_class;
  int source_size;
  int integer_rank;
  int is_unsigned;
} ir_mir_type_info_t;

ir_mir_type_info_t ir_mir_classify_type_id(
    const ir_mir_type_context_t *context, psx_type_id_t type_id);
int ir_mir_integer_promotion_is_unsigned(
    ir_mir_type_info_t type, const struct ag_data_layout_t *data_layout);
int ir_mir_usual_arithmetic_result_is_unsigned(
    ir_mir_type_info_t left, ir_mir_type_info_t right,
    const struct ag_data_layout_t *data_layout);

#endif
