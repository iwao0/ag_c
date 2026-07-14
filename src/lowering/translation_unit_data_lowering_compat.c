#include "translation_unit_data_lowering_compat.h"

#include "../compilation_session_compat.h"

ir_data_module_t *lower_ir_translation_unit_data(void) {
  return lower_ir_translation_unit_data_in_session(
      ag_compilation_session_active_compat());
}
