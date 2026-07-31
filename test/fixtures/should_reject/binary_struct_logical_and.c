/* Logical operators require scalar operands, not structure values. */
struct value {
  int member;
};

int main(void) {
  struct value value = {1};
  return value && 1;
}
