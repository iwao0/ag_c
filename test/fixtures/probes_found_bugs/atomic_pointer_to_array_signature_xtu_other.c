#ifndef AG_C_ATOMIC_POINTER_TO_ARRAY_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_POINTER_TO_ARRAY_SIGNATURE_XTU_TYPES
typedef int plain_atomic_row_t[2];
typedef _Atomic(int) atomic_element_row_t[2];
#endif

int read_atomic_pointer_to_array(
    _Atomic(plain_atomic_row_t *) row) {
  return (*row)[0];
}

int read_pointer_to_atomic_array(
    atomic_element_row_t *row) {
  return (*row)[1];
}
