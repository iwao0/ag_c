// A typedef for a pointer to a VLA is variably modified and constrains goto entry.
int f(int n) {
  goto target;
  typedef int (*row_pointer)[n];
target:
  return sizeof(row_pointer);
}

int main(void) {
  return 0;
}
