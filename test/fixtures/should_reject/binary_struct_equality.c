/* Equality operators require scalar operands, not structure values. */
struct value {
  int member;
};

int main(void) {
  struct value left = {1};
  struct value right = {1};
  return left == right;
}
