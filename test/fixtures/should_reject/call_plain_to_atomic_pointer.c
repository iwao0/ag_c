/* A plain object pointer is incompatible with an atomic-object pointer parameter. */
static int read(_Atomic int *pointer) {
  return *pointer;
}

int main(void) {
  int value = 1;
  return read(&value);
}
