/* An array compound literal initializer cannot exceed its declared bound. */
int main(void) {
  return (int[1]){1, 2}[0];
}
