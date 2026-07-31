/* A switch controlling expression must have integer type. */
int main(void) {
  double _Complex value = 5.0;
  switch (value) {
  default:
    return 0;
  }
}
