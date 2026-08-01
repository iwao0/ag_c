/* A UTF-32 string cannot initialize an array of signed int. */
int values[3] = U"hi";

int main(void) {
  return 0;
}
