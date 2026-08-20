/* A typedef does not hide an atomic pointer from restrict validation. */
typedef int * _Atomic atomic_pointer;
restrict atomic_pointer invalid_pointer;

int main(void) {
  return 0;
}
