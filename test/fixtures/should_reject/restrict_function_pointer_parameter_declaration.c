/* A function-pointer prototype parameter cannot be restrict-qualified. */
int apply(int (*restrict callback)(void));

int main(void) {
  return 0;
}
