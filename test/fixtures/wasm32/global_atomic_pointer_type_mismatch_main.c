extern _Atomic(int *) global_atomic_pointer_value;

int main(void) {
  int *pointer = global_atomic_pointer_value;
  return *pointer;
}
