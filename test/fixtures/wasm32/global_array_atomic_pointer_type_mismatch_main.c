extern _Atomic(int *) global_array_atomic_pointer[1];

int main(void) {
  return global_array_atomic_pointer[0] != 0;
}
