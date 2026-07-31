/* Bitwise complement does not accept a complex operand. */
int main(void) {
  double _Complex value = 5.0;
  return (int)~value;
}
