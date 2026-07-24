int main(void) {
  int plain_values[2] = {1, 2};
  _Atomic int atomic_values[2] = {1, 2};
  int *plain = plain_values;
  _Atomic int *atomic_pointer = atomic_values;
  return sizeof(atomic_pointer - plain);
}
