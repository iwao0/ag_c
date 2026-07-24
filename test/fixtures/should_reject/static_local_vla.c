/* A VLA object must have automatic or allocated storage duration. */
int function(int count) {
  static int values[count];
  return values[0];
}
int main(void) { return 0; }
