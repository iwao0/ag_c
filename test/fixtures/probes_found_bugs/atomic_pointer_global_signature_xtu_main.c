extern _Atomic(int *) shared_atomic_pointer;
extern _Atomic(int) *shared_atomic_pointee_pointer;

int main(void) {
  int *pointer = shared_atomic_pointer;
  return *pointer + *shared_atomic_pointee_pointer;
}
