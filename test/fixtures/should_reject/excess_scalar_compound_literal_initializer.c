/* A scalar compound literal initializer cannot contain an excess element. */
int main(void) {
  return (int){1, 2};
}
