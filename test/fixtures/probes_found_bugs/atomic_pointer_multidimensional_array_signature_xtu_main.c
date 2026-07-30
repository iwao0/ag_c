extern _Atomic(int *) atomic_pointer_matrix[1][2];
extern _Atomic(int) *atomic_pointee_matrix[1][2];

int main(void) {
  return *atomic_pointer_matrix[0][0] +
         *atomic_pointee_matrix[0][1];
}
