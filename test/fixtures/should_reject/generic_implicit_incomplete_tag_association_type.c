/* An implicitly declared incomplete tag cannot be a generic association type. */
int main(void) {
  return _Generic(1, struct missing: 1, default: 0);
}
