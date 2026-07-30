// Paired with enum_compatible_function_signature_xtu_main.c.

int make_signed_result(void) {
  return 19;
}

unsigned int make_unsigned_result(void) {
  return 23U;
}

int accept_signed(int value) {
  return value == 19 ? 0 : 1;
}

int accept_unsigned_pointer(unsigned int *value) {
  return *value == 23U ? 0 : 1;
}

typedef int integer_callback_t(void);

int call_signed(integer_callback_t *callback) {
  return callback() == 19 ? 0 : 1;
}
