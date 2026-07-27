/*
 * A generic selection that selects a const lvalue remains const-qualified
 * and cannot be assigned to. Expect ag_c E3077.
 */
int main(void) {
  const int constant = 1;
  int mutable = 2;
  _Generic(0, int: constant, default: mutable) = 3;
  return 0;
}
