/* Return conversion cannot discard the atomic qualifier from a pointee. */
static int *get(_Atomic int *pointer) {
  return pointer;
}

int main(void) {
  _Atomic int value = 1;
  return *get(&value);
}
