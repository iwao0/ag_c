// Canonical data signatures retain enum compatibility through atomic
// pointer-to-array and pointer-to-array-of-atomic-enum positions.
// Expected with the companion TU: exit=42.

enum atomic_enum_array_data_value {
  ATOMIC_ENUM_ARRAY_DATA_ZERO = 0,
  ATOMIC_ENUM_ARRAY_DATA_VALUE = 42
};

extern _Atomic(enum atomic_enum_array_data_value (*)[2])
    shared_atomic_enum_row_pointer;
extern _Atomic(enum atomic_enum_array_data_value)
    (*shared_atomic_enum_element_row)[2];

int main(void) {
  return (*shared_atomic_enum_row_pointer)[0] +
         (*shared_atomic_enum_element_row)[1];
}
