/* A variably modified object declaration cannot have extern linkage. */
int consume(int count) {
  extern int values[count];
  return values[0];
}

int main(void) {
  return 0;
}
