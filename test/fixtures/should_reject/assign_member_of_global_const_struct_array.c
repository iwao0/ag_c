// File-scope storage does not make a const record element modifiable.
struct Item {
  int value;
};

const struct Item items[1] = {{1}};

int main(void) {
  items[0].value = 2;
  return items[0].value;
}
