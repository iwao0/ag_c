int main(void) {
  static int source[2] = {1, 2};
  static int values[2] = source;
  return values[0];
}
