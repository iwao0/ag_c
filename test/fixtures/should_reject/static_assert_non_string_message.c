/* A static assertion message must be a string literal, not an integer. */
_Static_assert(1, 123);

int main(void) {
  return 0;
}
