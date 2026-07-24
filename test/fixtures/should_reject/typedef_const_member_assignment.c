// A typedef does not hide the const qualification of a record member.
typedef const int ConstantInt;
struct Item {
  ConstantInt value;
};

int main(void) {
  struct Item left = {1};
  struct Item right = {2};
  left = right;
  return left.value;
}
