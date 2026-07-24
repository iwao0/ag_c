static int read(register int value) {
  int *pointer = &value;
  return *pointer;
}

int main(void) {
  return read(1);
}
