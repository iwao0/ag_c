int main(void) {
  int integers[2];
  double floating[2];
  return (int)(&integers[1] - &floating[0]);
}
