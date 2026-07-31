/* A conditional expression cannot add qualifiers within pointed-to array elements. */
int main(void) {
  int (*left)[3] = 0;
  const int (*right)[3] = 0;
  return (1 ? left : right) != 0;
}
