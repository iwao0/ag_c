#ifndef SEMANTIC_MEMBER_RESOLUTION_H
#define SEMANTIC_MEMBER_RESOLUTION_H

#include "../parser/semantic_ctx.h"

typedef enum {
  PSX_MEMBER_ACCESS_OK = 0,
  PSX_MEMBER_ACCESS_INVALID_BASE,
  PSX_MEMBER_ACCESS_NOT_FOUND,
} psx_member_access_status_t;

typedef struct {
  psx_member_access_status_t status;
  psx_qual_type_t base_object_qual_type;
  const psx_type_t *base_object_type;
  psx_record_id_t record_id;
  int member_index;
  psx_record_member_decl_t declaration;
  psx_qual_type_t member_qual_type;
} psx_member_access_resolution_t;

void psx_resolve_member_access_qual_type_in(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t base_qual_type,
    const char *member_name,
    int member_name_len,
    int from_pointer,
    psx_member_access_resolution_t *resolution);

#endif
