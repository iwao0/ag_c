/* An atomic-qualified pointer cannot also be restrict-qualified. */
int * _Atomic restrict invalid_pointer;

int main(void) {
  return 0;
}
