/* An automatic scalar initializer list cannot contain an excess element. */
int main(void) {
  int value = {1, 2};
  return value;
}
