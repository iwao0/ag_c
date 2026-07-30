extern _Atomic(int) *global_atomic_pointee_value;

int main(void) {
  return *global_atomic_pointee_value;
}
