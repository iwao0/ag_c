static int internal_function(void) {
  return 1;
}

inline int call_internal_function(void) {
  return internal_function();
}
