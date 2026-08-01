/* Inferred compound-literal length does not relax UTF-16 element compatibility. */
int main(void) {
  short *values = (short[]){u"hi"};
  return values[0];
}
