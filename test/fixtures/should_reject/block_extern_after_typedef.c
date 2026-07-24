/* Typedef and object names share C's ordinary identifier namespace. */
int main(void) {
  typedef int shared;
  extern int shared;
  return 0;
}
