/* A file-scope struct initializer cannot exceed its member count. */
struct Item { int value; };
struct Item item = {1, 2};

int main(void) {
  return item.value;
}
