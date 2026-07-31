/* Return conversion cannot change a plain pointee into an atomic pointee. */
static _Atomic int *get(int *pointer) {
  return pointer;
}

int main(void) {
  int value = 1;
  return *get(&value);
}
