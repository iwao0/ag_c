// The right operand is evaluated because the left operand of && is true.
static int value = 1 && (1 / 0);

int main(void) {
  return value;
}
