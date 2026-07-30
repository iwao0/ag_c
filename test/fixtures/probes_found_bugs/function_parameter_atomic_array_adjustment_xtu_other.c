int inspect_atomic_array_parameters(
    int *pointer_atomic, _Atomic(int) *element_atomic) {
  return pointer_atomic[0] + element_atomic[0];
}

int read_old_style_atomic_array(value)
int value[_Atomic 1];
{
  return value[0];
}
