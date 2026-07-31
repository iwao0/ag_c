/* Assignment cannot convert a plain object pointer to an atomic-object pointer. */
int main(void) {
  int value = 1;
  _Atomic int *pointer = 0;
  pointer = &value;
  return *pointer;
}
