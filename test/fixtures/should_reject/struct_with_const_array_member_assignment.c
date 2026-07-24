// An array member whose element type is const also makes the record non-modifiable.
struct Item {
  const int values[2];
};

int main(void) {
  struct Item left = {{1, 2}};
  struct Item right = {{3, 4}};
  left = right;
  return left.values[0];
}
