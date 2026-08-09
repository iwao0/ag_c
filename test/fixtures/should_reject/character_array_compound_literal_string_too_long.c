/* A character-array compound literal enforces its explicit bound. */
int main(void) {
  return ((char[2]){"abc"})[0];
}
