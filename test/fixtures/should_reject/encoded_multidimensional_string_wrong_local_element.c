/* A local multidimensional row of signed short is incompatible with UTF-16. */
int main(void) {
  short rows[1][3] = {u"hi"};
  return rows[0][0];
}
