/* '_Atomic' is invalid in a nested parameter array derivation. */
int consume(int (*values)[_Atomic 3]);

int main(void) {
  return 0;
}
