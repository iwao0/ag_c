// A file-scope object still cannot have restrict-qualified
// pointer-to-function type (C11 6.7.3p2).
int (*restrict callback)(void);

int main(void) {
  return callback != 0;
}
