extern _Atomic(int) *global_array_atomic_pointee[1];

int main(void) {
  return *global_array_atomic_pointee[0];
}
