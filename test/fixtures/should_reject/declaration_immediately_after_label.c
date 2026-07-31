// A C11 label must be followed by a statement, not a declaration.
int main(void) {
label:
  _Static_assert(1, "declaration");
  return 0;
}
