/* Array qualifiers are permitted only in the outermost parameter array derivation. */
static int consume(int (*values)[const 3]);

int main(void) {
  return 0;
}
