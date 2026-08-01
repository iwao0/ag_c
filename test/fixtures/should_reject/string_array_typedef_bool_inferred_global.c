/* Inferred length does not make a _Bool array compatible with a string literal. */
typedef _Bool flag_t;

flag_t values[] = "hi";

int main(void) {
  return 0;
}
