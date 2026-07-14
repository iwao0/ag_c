#ifndef PARSER_TYPE_OWNED_INTERNAL_H
#define PARSER_TYPE_OWNED_INTERNAL_H

#include "type.h"

/* Only type construction and semantic-context registry ownership may recover
 * mutable children from an owned canonical type root. */
psx_type_t *psx_type_owned_base_mut(psx_type_t *owner);
psx_type_t *psx_type_owned_param_mut(psx_type_t *owner, int index);

#endif
