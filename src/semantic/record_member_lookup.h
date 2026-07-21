#ifndef SEMANTIC_RECORD_MEMBER_LOOKUP_H
#define SEMANTIC_RECORD_MEMBER_LOOKUP_H

#include "../type_system/type_ids.h"

typedef int (*psx_record_member_name_lookup_t)(
    void *context, psx_record_id_t record_id,
    const char *member_name, int member_name_len,
    int *out_member_index);

#endif
