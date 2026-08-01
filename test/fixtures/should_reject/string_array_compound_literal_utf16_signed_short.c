/* A signed-short compound literal cannot use a UTF-16 string initializer. */
int main(void) {
  short *values = (short[3]){u"hi"};
  return values[0];
}
