/* A function pointer cannot be restrict-qualified in a type name. */
int main(void) {
  return (int (*restrict)(void))0 != 0;
}
