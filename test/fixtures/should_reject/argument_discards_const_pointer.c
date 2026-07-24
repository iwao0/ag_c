static int read_mutable(int *value) {
  return *value;
}

int main(void) {
  const int value = 7;
  return read_mutable(&value);
}
