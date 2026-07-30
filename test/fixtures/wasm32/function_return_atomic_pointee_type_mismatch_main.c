_Atomic(int) *function_return_atomic_pointee(void);

int main(void) {
  return *function_return_atomic_pointee();
}
