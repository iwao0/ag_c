#include "static_assert_resolution.h"

void psx_resolve_static_assert(
    const psx_static_assert_request_t *request,
    psx_static_assert_resolution_t *resolution) {
  if (!resolution) return;
  resolution->status = PSX_STATIC_ASSERT_NOT_CONSTANT;
  if (!request || !request->is_constant) return;
  resolution->status = request->value != 0
                           ? PSX_STATIC_ASSERT_OK
                           : PSX_STATIC_ASSERT_FAILED;
}
