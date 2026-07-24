/* A variably modified identifier cannot have linkage. */
int function(int count) {
  extern int (*pointer)[count];
  return pointer != 0;
}
int main(void) { return 0; }
