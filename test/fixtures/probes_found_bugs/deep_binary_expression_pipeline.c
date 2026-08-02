/*
 * A cold Wasm self-host compile must not consume the JavaScript call stack
 * while processing a valid left-associative scalar expression tree.
 */
int main(void) {
  volatile int value = 1;
  return (
      value + value + value + value + value + value + value + value +
      value + value + value + value + value + value + value + value +
      value + value + value + value + value + value + value + value +
      value + value + value + value + value + value + value + value +
      value + value + value + value + value + value + value + value +
      value + value + value + value + value + value + value + value +
      value + value + value + value + value + value + value + value +
      value + value + value + value + value + value + value + value +
      value + value + value + value + value + value + value + value +
      value + value + value + value + value + value + value + value +
      value + value + value + value + value + value + value + value +
      value + value + value + value + value + value + value + value +
      value + value + value + value + value + value + value + value +
      value + value + value + value + value + value + value + value +
      value + value + value + value + value + value + value + value +
      value + value + value + value + value + value + value + value +
      value
  ) - 129;
}
