static int read(register _Atomic int value) {
  int *pointer = &value;
  return *pointer;
}

int main(void) {
  return read(1);
}
