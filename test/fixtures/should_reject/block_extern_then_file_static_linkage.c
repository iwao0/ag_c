/* A block extern declaration prevents a later file-scope static declaration. */
int main(void) {
  extern int value;
  return 0;
}

static int value;
