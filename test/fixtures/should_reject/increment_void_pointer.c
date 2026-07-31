/* Pointer increment requires a pointer to a complete object type. */
int main(void) {
  void *pointer = 0;
  ++pointer;
  return 0;
}
