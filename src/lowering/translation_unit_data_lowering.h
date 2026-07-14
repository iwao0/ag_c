#ifndef AG_TRANSLATION_UNIT_DATA_LOWERING_H
#define AG_TRANSLATION_UNIT_DATA_LOWERING_H

#include "../compilation_session.h"
#include "../ir/ir_data.h"

ir_data_module_t *lower_ir_translation_unit_data_in_session(
    const ag_compilation_session_t *session);

#endif
