/* A for-init static assertion still requires an integer constant expression. */
int main(void) {
  int value = 1;
  for (_Static_assert(value, "not constant"); ; ) return 0;
}
