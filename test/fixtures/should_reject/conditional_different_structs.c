/* Both structure operands of a conditional expression must have compatible types. */
struct first {
  int member;
};

struct second {
  int member;
};

int main(void) {
  struct first left = {1};
  struct second right = {2};
  (void)(1 ? left : right);
  return 0;
}
