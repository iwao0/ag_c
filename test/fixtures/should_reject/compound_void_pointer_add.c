/* Compound pointer addition requires a pointer to a complete object type. */
int main(void) {
  void *pointer = 0;
  pointer += 1;
  return pointer != 0;
}
