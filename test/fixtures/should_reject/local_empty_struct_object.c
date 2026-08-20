/* The block-scope aggregate parser must reject an empty body too. */
int main(void) {
  struct Local {} value;
  return 0;
}
