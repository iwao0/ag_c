enum global_atomic_enum_matrix_unsigned {
  GLOBAL_ATOMIC_ENUM_MATRIX_ZERO = 0,
  GLOBAL_ATOMIC_ENUM_MATRIX_VALUE = 42
};

extern _Atomic(enum global_atomic_enum_matrix_unsigned)
    global_atomic_enum_matrix[1][2];

int main(void) {
  return global_atomic_enum_matrix[0][1];
}
