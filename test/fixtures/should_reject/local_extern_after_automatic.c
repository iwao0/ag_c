/* A no-linkage object may not be redeclared in the same scope. */
int main(void) {
  int shared;
  extern int shared;
  return 0;
}
