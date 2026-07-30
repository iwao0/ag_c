// Paired with enum_distinct_return_signature_mismatch_main.c.

enum actual_result {
  ACTUAL_RESULT_ZERO = 0,
  ACTUAL_RESULT_VALUE = 42
};

enum actual_result distinct_result(void) {
  return ACTUAL_RESULT_VALUE;
}
