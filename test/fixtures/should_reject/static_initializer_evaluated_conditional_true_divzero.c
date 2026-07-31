// Selecting the true branch makes its division by zero part of the initializer value.
static int value = 1 ? (1 / 0) : 7;

int main(void) {
  return value;
}
