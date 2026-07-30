// Canonical data signatures preserve enum compatibility through atomic array
// elements and atomic-qualified record members.
// Expected with the companion TU: exit=42.

enum atomic_container_value {
  ATOMIC_CONTAINER_ZERO = 0,
  ATOMIC_CONTAINER_VALUE = 42
};

#ifndef AG_C_ATOMIC_ENUM_CONTAINER_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_CONTAINER_SIGNATURE_XTU_TYPES
struct atomic_enum_container {
  _Atomic(enum atomic_container_value) value;
  _Atomic(enum atomic_container_value *) pointer;
};
#endif

extern struct atomic_enum_container shared_atomic_enum_container;
extern _Atomic(enum atomic_container_value)
    shared_atomic_enum_values[2];

int main(void) {
  return shared_atomic_enum_container.value +
         *shared_atomic_enum_container.pointer +
         shared_atomic_enum_values[0] +
         shared_atomic_enum_values[1];
}
