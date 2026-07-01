int __agc_runtime_abs(int x) {
  return x < 0 ? -x : x;
}

long __agc_runtime_imaxabs(long x) {
  return x < 0 ? -x : x;
}

int __agc_runtime_isdigit(int c) {
  return c >= '0' && c <= '9';
}

int __agc_runtime_islower(int c) {
  return c >= 'a' && c <= 'z';
}

int __agc_runtime_isupper(int c) {
  return c >= 'A' && c <= 'Z';
}

int __agc_runtime_isalpha(int c) {
  return __agc_runtime_islower(c) || __agc_runtime_isupper(c);
}

int __agc_runtime_isalnum(int c) {
  return __agc_runtime_isalpha(c) || __agc_runtime_isdigit(c);
}

int __agc_runtime_isblank(int c) {
  return c == ' ' || c == '\t';
}

int __agc_runtime_iscntrl(int c) {
  return (c >= 0 && c < 32) || c == 127;
}

int __agc_runtime_isgraph(int c) {
  return c >= 33 && c <= 126;
}

int __agc_runtime_isprint(int c) {
  return c >= 32 && c <= 126;
}

int __agc_runtime_ispunct(int c) {
  return __agc_runtime_isgraph(c) && !__agc_runtime_isalnum(c);
}

int __agc_runtime_isspace(int c) {
  return c == ' ' || c == '\f' || c == '\n' || c == '\r' || c == '\t' || c == '\v';
}

int __agc_runtime_isxdigit(int c) {
  return __agc_runtime_isdigit(c) ||
         (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

int __agc_runtime_tolower(int c) {
  return __agc_runtime_isupper(c) ? c + 32 : c;
}

int __agc_runtime_toupper(int c) {
  return __agc_runtime_islower(c) ? c - 32 : c;
}

int __agc_runtime_wctype(long property_addr) {
  char *p = ag_rt_ptr(property_addr);
  if (__agc_runtime_strcmp((long)p, (long)"alnum") == 0) return 1;
  if (__agc_runtime_strcmp((long)p, (long)"alpha") == 0) return 2;
  if (__agc_runtime_strcmp((long)p, (long)"blank") == 0) return 3;
  if (__agc_runtime_strcmp((long)p, (long)"digit") == 0) return 4;
  if (__agc_runtime_strcmp((long)p, (long)"space") == 0) return 5;
  if (__agc_runtime_strcmp((long)p, (long)"xdigit") == 0) return 6;
  return 0;
}

int __agc_runtime_iswctype(int wc, int desc) {
  if (desc == 1) return __agc_runtime_isalnum(wc);
  if (desc == 2) return __agc_runtime_isalpha(wc);
  if (desc == 3) return __agc_runtime_isblank(wc);
  if (desc == 4) return __agc_runtime_isdigit(wc);
  if (desc == 5) return __agc_runtime_isspace(wc);
  if (desc == 6) return __agc_runtime_isxdigit(wc);
  return 0;
}

int __agc_runtime_wctrans(long property_addr) {
  char *p = ag_rt_ptr(property_addr);
  if (__agc_runtime_strcmp((long)p, (long)"tolower") == 0) return 1;
  if (__agc_runtime_strcmp((long)p, (long)"toupper") == 0) return 2;
  return 0;
}

int __agc_runtime_towctrans(int wc, int desc) {
  if (desc == 1) return __agc_runtime_tolower(wc);
  if (desc == 2) return __agc_runtime_toupper(wc);
  return wc;
}
