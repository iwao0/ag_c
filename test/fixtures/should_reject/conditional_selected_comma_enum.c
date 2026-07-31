// An evaluated comma operator is not permitted in an integer constant expression.
enum invalid_conditional_constant {
  INVALID_CONDITIONAL_CONSTANT = 1 ? (2, 3) : 4
};

int main(void) {
  return INVALID_CONDITIONAL_CONSTANT;
}
