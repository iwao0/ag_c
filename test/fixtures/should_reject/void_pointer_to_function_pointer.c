/* A void pointer cannot implicitly convert to a function pointer. */
int main(void) {
  void *object_pointer = 0;
  int (*invalid)(void) = object_pointer;
  return invalid == 0;
}
