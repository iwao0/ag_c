/* Automatic arrays obey the same UTF-16 element type constraint. */
int main(void) {
  short values[3] = u"hi";
  return values[0];
}
