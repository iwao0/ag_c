_Atomic(int) function_return_atomic_type(int value);

int main(void) {
  _Atomic(int) result = function_return_atomic_type(42);
  return result;
}
