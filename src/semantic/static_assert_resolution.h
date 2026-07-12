#ifndef SEMANTIC_STATIC_ASSERT_RESOLUTION_H
#define SEMANTIC_STATIC_ASSERT_RESOLUTION_H

typedef enum {
  PSX_STATIC_ASSERT_OK = 0,
  PSX_STATIC_ASSERT_NOT_CONSTANT,
  PSX_STATIC_ASSERT_FAILED,
} psx_static_assert_status_t;

typedef struct {
  int is_constant;
  long long value;
} psx_static_assert_request_t;

typedef struct {
  psx_static_assert_status_t status;
} psx_static_assert_resolution_t;

void psx_resolve_static_assert(
    const psx_static_assert_request_t *request,
    psx_static_assert_resolution_t *resolution);

#endif
