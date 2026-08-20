/* Qualifier order does not make atomic plus restrict valid. */
int * restrict _Atomic invalid_pointer;

int main(void) {
  return 0;
}
