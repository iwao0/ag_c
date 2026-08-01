/* A UTF-16 string cannot initialize an array of signed short. */
short values[3] = u"hi";

int main(void) {
  return 0;
}
