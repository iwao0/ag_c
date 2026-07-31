// Atomic and const both apply to the pointer produced by array parameter adjustment.
int update(int values[const _Atomic 1]) {
  values = 0;
  return 0;
}

int main(void) {
  return 0;
}
