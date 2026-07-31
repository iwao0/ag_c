static int runtime_value(void) {
  return 7;
}

// An enumerator's integer constant expression cannot contain a function call,
// even in the unselected operand of a conditional expression.
enum invalid_conditional_constant {
  INVALID_CONDITIONAL_CONSTANT = 1 ? 3 : runtime_value()
};

int main(void) {
  return INVALID_CONDITIONAL_CONSTANT;
}
