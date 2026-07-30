int transform_atomic_pointer(
    _Atomic(int) *value, _Atomic(int *) pointer);

int main(void) {
  _Atomic(int) value = 40;
  int values[2] = {0, 2};
  _Atomic(int *) pointer = &values[1];
  return transform_atomic_pointer(&value, pointer);
}
