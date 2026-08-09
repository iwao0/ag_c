int main(void) {
  static int source[2] = {1, 2};
  static int values[] = source;
  return values[0];
}
