/* The array compound-literal initializer extension requires an identical array type. */
int main(void) {
  int values[2] = (long[2]){1, 2};
  return values[0];
}
