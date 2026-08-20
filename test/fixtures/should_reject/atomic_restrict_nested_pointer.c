/* The invalid qualifier pair must be found on an inner pointer level. */
int * _Atomic restrict *invalid_pointer;

int main(void) {
  return 0;
}
