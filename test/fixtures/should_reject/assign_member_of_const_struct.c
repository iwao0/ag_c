// Member access preserves the top-level const qualification of the record.
struct Item {
  int value;
};

int main(void) {
  const struct Item item = {1};
  item.value = 2;
  return item.value;
}
