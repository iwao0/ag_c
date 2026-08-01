/* A typedef to _Bool cannot be initialized as a character array by a string literal. */
typedef _Bool flag_t;

flag_t values[3] = "hi";

int main(void) {
  return 0;
}
