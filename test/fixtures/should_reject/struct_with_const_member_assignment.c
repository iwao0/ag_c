// C11 6.3.2.1p1: an aggregate with a const-qualified member is not modifiable.
struct Item {
  const int value;
};

int main(void) {
  struct Item left = {1};
  struct Item right = {2};
  left = right;
  return left.value;
}
