/* A pointer-to-VLA typedef is also variably modified. */
int function(int count) {
  typedef int (*Rows)[count];
  typedef int (*Rows)[count];
  return 0;
}
