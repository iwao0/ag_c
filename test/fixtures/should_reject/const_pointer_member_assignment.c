// The pointer object itself is const even though the pointed-to int is mutable.
struct Item {
  int *const pointer;
};

int main(void) {
  int left_value = 1;
  int right_value = 2;
  struct Item left = {&left_value};
  struct Item right = {&right_value};
  left = right;
  return *left.pointer;
}
