// The right operand is evaluated because the left operand of || is false.
static int value = 0 || (1 / 0);

int main(void) {
  return value;
}
