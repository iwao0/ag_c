/* A block-scope compound literal has automatic storage and is not a static address constant. */
int main(void) {
  static const int *pointer = &(const int){1};
  return *pointer;
}
