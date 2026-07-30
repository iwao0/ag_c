// Paired with atomic_enum_container_signature_xtu_main.c.

#ifndef AG_C_ATOMIC_ENUM_CONTAINER_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_CONTAINER_SIGNATURE_XTU_TYPES
struct atomic_enum_container {
  _Atomic(unsigned int) value;
  _Atomic(unsigned int *) pointer;
};
#endif

static unsigned int atomic_enum_payload = 20U;

struct atomic_enum_container shared_atomic_enum_container = {
    19U, &atomic_enum_payload};

_Atomic(unsigned int) shared_atomic_enum_values[2] = {
    1U, 2U};
