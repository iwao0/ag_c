// The arrow operator preserves the const qualification of its pointed-to record.
struct Item {
  int value;
};

int main(void) {
  const struct Item *pointer = 0;
  pointer->value = 2;
  return 0;
}
