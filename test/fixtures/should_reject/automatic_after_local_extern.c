/* A block extern declaration and an automatic object cannot share a scope. */
int main(void) {
  extern int shared;
  int shared;
  return 0;
}
