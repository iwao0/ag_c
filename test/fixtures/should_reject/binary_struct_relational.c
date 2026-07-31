/* Relational operators do not accept structure operands. */
struct value {
  int member;
};

int main(void) {
  struct value left = {1};
  struct value right = {2};
  return left < right;
}
