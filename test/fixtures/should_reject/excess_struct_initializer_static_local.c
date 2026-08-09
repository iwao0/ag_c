/* A static local struct initializer cannot exceed its member count. */
struct Item { int value; };

int main(void) {
  static struct Item item = {1, 2};
  return item.value;
}
