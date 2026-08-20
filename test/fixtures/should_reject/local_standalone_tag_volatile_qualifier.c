/* The block-scope tag-only path must reject an unused qualifier too. */
int main(void) {
  volatile struct Local { int value; };
  return 0;
}
