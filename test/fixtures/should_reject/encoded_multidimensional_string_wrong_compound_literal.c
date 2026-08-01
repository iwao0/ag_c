/* A multidimensional compound literal preserves UTF-16 element compatibility. */
int main(void) {
  short (*rows)[3] = (short[1][3]){{u"hi"}};
  return rows[0][0];
}
