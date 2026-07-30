// Distinct enumeration return types remain incompatible even when both
// select unsigned int and lower to the same Wasm i32 result.

enum expected_result {
  EXPECTED_RESULT_ZERO = 0,
  EXPECTED_RESULT_VALUE = 42
};

enum expected_result distinct_result(void);

int main(void) {
  return distinct_result();
}
