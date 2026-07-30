#ifndef AG_C_ATOMIC_POINTER_TO_ARRAY_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_POINTER_TO_ARRAY_SIGNATURE_XTU_TYPES
typedef int plain_atomic_row_t[2];
typedef _Atomic(int) atomic_element_row_t[2];
#endif

int read_atomic_pointer_to_array(
    _Atomic(plain_atomic_row_t *) row);
int read_pointer_to_atomic_array(
    atomic_element_row_t *row);

int main(void) {
  plain_atomic_row_t plain_row = {20, 0};
  atomic_element_row_t atomic_row = {0, 22};
  _Atomic(plain_atomic_row_t *) pointer = &plain_row;
  return read_atomic_pointer_to_array(pointer) +
         read_pointer_to_atomic_array(&atomic_row);
}
