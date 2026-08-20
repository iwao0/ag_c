/* Parameter adjustment does not make an atomic restrict pointer valid. */
int read_value(int * _Atomic restrict pointer);

int main(void) {
  return 0;
}
