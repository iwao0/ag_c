int main(void) {
  int values[2] = {0};
  _Atomic(int *) pointer = values;
  pointer += 1;
  return pointer == values + 1;
}
