// Dereferencing a pointer to an array of const records preserves element constness.
struct Item {
  int value;
};

int main(void) {
  const struct Item items[1] = {{1}};
  const struct Item (*pointer)[1] = &items;
  (*pointer)[0].value = 2;
  return (*pointer)[0].value;
}
