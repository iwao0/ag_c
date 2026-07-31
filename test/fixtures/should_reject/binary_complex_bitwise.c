/* Bitwise operators require integer operands. */
int main(void) {
  double _Complex value = 5.0;
  return (int)(value & 2);
}
