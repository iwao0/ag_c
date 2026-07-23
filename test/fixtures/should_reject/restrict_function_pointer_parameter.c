// Parameter qualifiers are checked before function-type top-level qualifier
// adjustment; restrict cannot qualify a pointer to a function (C11 6.7.3p2).
static int apply(int (*restrict callback)(void)) {
  return callback();
}

int main(void) {
  return apply(0);
}
