/* Logical negation requires a scalar operand. */
struct value {
  int member;
};

int main(void) {
  struct value object = {1};
  return !object;
}
