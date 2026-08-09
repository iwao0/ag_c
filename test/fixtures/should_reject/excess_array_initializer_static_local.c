/* A static local array initializer cannot exceed its declared bound. */
int main(void) {
  static int values[1] = {1, 2};
  return values[0];
}
