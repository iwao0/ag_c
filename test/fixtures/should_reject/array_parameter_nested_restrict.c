/* 'restrict' is invalid in a nested parameter array derivation. */
int consume(int (*values)[restrict 3]);

int main(void) {
  return 0;
}
