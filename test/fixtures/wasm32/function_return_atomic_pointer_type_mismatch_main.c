_Atomic(int *) function_return_atomic_pointer(void);

int main(void) {
  _Atomic(int *) pointer = function_return_atomic_pointer();
  return pointer != 0;
}
