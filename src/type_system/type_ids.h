#ifndef TYPE_SYSTEM_TYPE_IDS_H
#define TYPE_SYSTEM_TYPE_IDS_H

#include <stdint.h>

typedef unsigned int psx_type_id_t;
typedef unsigned int psx_record_id_t;
typedef unsigned int psx_type_qualifiers_t;
typedef uint32_t psx_decl_id_t;

#define PSX_TYPE_ID_INVALID ((psx_type_id_t)0)
#define PSX_RECORD_ID_INVALID ((psx_record_id_t)0)
#define PSX_DECL_ID_INVALID ((psx_decl_id_t)0)

enum {
  PSX_TYPE_QUALIFIER_NONE = 0,
  PSX_TYPE_QUALIFIER_CONST = 1u << 0,
  PSX_TYPE_QUALIFIER_VOLATILE = 1u << 1,
  PSX_TYPE_QUALIFIER_ATOMIC = 1u << 2,
  PSX_TYPE_QUALIFIER_RESTRICT = 1u << 3,
};

typedef struct {
  psx_type_id_t type_id;
  psx_type_qualifiers_t qualifiers;
} psx_qual_type_t;

#endif
