/* An array designator cannot be applied to a scalar initializer. */
int value = {[0] = 7};

int main(void) {
  return value;
}
