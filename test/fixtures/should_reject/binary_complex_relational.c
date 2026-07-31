/* Relational operators do not accept complex operands. */
int main(void) {
  double _Complex left = 1.0;
  double _Complex right = 2.0;
  return left < right;
}
