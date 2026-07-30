int transform_atomic_pointer(
    _Atomic(int) *value, _Atomic(int *) pointer) {
  return *value + *pointer;
}
