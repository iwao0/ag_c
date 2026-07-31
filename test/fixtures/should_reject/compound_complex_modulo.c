/* Complex operands are not valid for compound modulo assignment. */
int main(void) {
  double _Complex value = 5.0;
  value %= 2;
  return 0;
}
