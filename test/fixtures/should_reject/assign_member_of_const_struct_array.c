// Subscripting an array of const records produces a const-qualified record lvalue.
struct Item {
  int value;
};

int main(void) {
  const struct Item items[1] = {{1}};
  items[0].value = 2;
  return items[0].value;
}
