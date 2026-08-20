/* Type-name resolution must reject atomic plus restrict as well. */
int main(void) {
  return sizeof(int * _Atomic restrict);
}
