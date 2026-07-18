int main(void) {
  int bound = 3;
  typedef int captured_array_t[bound];
  bound = 1;
  captured_array_t values;
  return sizeof(captured_array_t) == 3 * sizeof(int) &&
                 sizeof(values) == 3 * sizeof(int)
             ? 0
             : 1;
}
