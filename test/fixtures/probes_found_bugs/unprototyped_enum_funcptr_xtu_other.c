// Paired with unprototyped_enum_funcptr_xtu_main.c.

enum signed_result {
  SIGNED_RESULT_NEGATIVE = -1,
  SIGNED_RESULT_OTHER = 0
};

enum unsigned_result {
  UNSIGNED_RESULT_ZERO = 0,
  UNSIGNED_RESULT_OK = 42
};

int check_signed_enum(enum signed_result value) {
  return value == SIGNED_RESULT_NEGATIVE ? 19 : 1;
}

int check_unsigned_enum(enum unsigned_result value) {
  return value == UNSIGNED_RESULT_OK ? 23 : 1;
}
