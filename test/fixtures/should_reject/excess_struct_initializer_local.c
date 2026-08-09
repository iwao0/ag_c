/* An automatic struct initializer cannot exceed its member count. */
struct Item { int value; };

int main(void) {
  struct Item item = {1, 2};
  return item.value;
}
