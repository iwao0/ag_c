/* Equality cannot compare pointers to atomic and plain object types. */
int main(void) {
  int left = 1;
  _Atomic int right = 1;
  return &left == &right;
}
