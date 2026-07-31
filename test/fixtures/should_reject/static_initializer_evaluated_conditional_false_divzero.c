// Selecting the false branch makes its division by zero part of the initializer value.
static int value = 0 ? 7 : (1 / 0);

int main(void) {
  return value;
}
