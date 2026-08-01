/* A wide string cannot initialize an array of unsigned int on this C11 target. */
unsigned int values[3] = L"hi";

int main(void) {
  return 0;
}
