static int inspect_name(void) {
  const char *first = __func__;
  const char *second = __func__;
  const char (*whole)[13] = &__func__;

  _Static_assert(sizeof(__func__) == sizeof("inspect_name"),
                 "__func__ array size");
  if (_Generic(__func__, const char *: 1, default: 0) != 1) return 1;
  if (_Generic(&__func__[0], const char *: 1, default: 0) != 1) return 2;
  if (_Generic(&__func__, const char (*)[13]: 1, default: 0) != 1) return 3;
  if (first != second || first != &(*whole)[0]) return 4;
  if (first[0] != 'i' || first[12] != '\0') return 5;
  return 0;
}

int main(void) {
  return inspect_name();
}
