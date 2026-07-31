/* A false static assertion also fails in a for-init declaration position. */
int main(void) {
  for (_Static_assert(0, "failure"); ; ) return 0;
}
