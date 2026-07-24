/* Enum constants and object names share the ordinary identifier namespace. */
int main(void) {
  enum { shared = 1 };
  extern int shared;
  return 0;
}
