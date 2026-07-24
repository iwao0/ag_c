// A const member reached through an anonymous union is recursive record state.
struct Item {
  union {
    int integer;
    const double real;
  };
};

int main(void) {
  struct Item left = {.integer = 1};
  struct Item right = {.integer = 2};
  left = right;
  return left.integer;
}
