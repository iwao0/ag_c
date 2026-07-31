/* 'static' is invalid in a nested parameter array derivation. */
static int consume(int (*values)[static 3]);

int main(void) {
  return 0;
}
