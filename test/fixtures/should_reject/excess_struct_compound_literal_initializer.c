/* A struct compound literal initializer cannot exceed its member count. */
struct Item { int value; };

int main(void) {
  return ((struct Item){1, 2}).value;
}
