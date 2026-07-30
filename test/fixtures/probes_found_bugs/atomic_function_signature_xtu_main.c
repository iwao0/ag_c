_Atomic(int) transform_atomic_value(_Atomic(int) value);

int main(void) {
  _Atomic(int) value = 40;
  _Atomic(int) result = transform_atomic_value(value);
  return result;
}
