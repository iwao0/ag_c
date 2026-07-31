/* 'volatile' is invalid in a nested parameter array derivation. */
int consume(int (*values)[volatile 3]);

int main(void) {
  return 0;
}
