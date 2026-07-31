/* An atomic-object pointer is incompatible with a plain pointer parameter. */
static int read(int *pointer) {
  return *pointer;
}

int main(void) {
  _Atomic int value = 1;
  return read(&value);
}
